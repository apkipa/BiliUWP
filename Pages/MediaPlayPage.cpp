#include "pch.h"
#include "MediaPlayPage.h"
#if __has_include("MediaPlayPage.g.cpp")
#include "MediaPlayPage.g.cpp"
#endif
#include "MediaPlayPage_UpItem.g.cpp"
#include "MediaPlayPage_PartItem.g.cpp"
#include "HttpRandomAccessStream.h"
#include "App.h"
#include <deque>
#include <ranges>
#include <regex>

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::UI;
using namespace Windows::UI::Text;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Data;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Shapes;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Media::Imaging;
using namespace Windows::Media;
using namespace Windows::Media::Core;
using namespace Windows::Media::Playback;
using namespace Windows::Media::Streaming::Adaptive;
using namespace Windows::Storage::Streams;
using namespace Windows::Security::Cryptography;

using ::BiliUWP::App::res_str;

// TODO: Maybe implement GridSplitter to ease sidebar resizing

// TODO: Add support for App_AlwaysSyncPlayingCfg

// TODO: Implement custom video presenter with Win2D

namespace winrt::BiliUWP::implementation {
    struct DetailedStatsContext {
        DetailedStatsContext(MediaPlayPage* page) :
            m_grid(page->MediaDetailedStatsOverlay()),
            m_grid_rowdefs(m_grid.RowDefinitions()), m_grid_children(m_grid.Children()) {}
        void ClearElements(void) {
            m_grid_rowdefs.Clear();
            m_grid_children.Clear();
        }
        void AddElement(hstring const& name, FrameworkElement const& elem) {
            auto idx = static_cast<int32_t>(m_grid_rowdefs.Size());
            auto rowdef = RowDefinition();
            rowdef.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Auto));
            m_grid_rowdefs.Append(rowdef);
            TextBlock tb;
            tb.HorizontalAlignment(HorizontalAlignment::Right);
            tb.FontWeight(FontWeights::Bold());
            tb.Text(name);
            Grid::SetColumn(tb, 0);
            Grid::SetRow(tb, idx);
            m_grid_children.Append(tb);
            if (elem) {
                Grid::SetColumn(elem, 1);
                Grid::SetRow(elem, idx);
                m_grid_children.Append(elem);
            }
        }
    private:
        Grid m_grid;
        RowDefinitionCollection m_grid_rowdefs;
        UIElementCollection m_grid_children;
    };
    struct DetailedStatsProvider : std::enable_shared_from_this<DetailedStatsProvider> {
        virtual void InitStats(DetailedStatsContext* ctx) {
            ctx->AddElement(L"Error", util::winrt::make_text_block(L"Detailed statistics not supported"));
        }
        // NOTE: Returning std::nullopt indicates periodic updating is not required
        virtual std::optional<TimeSpan> DesiredUpdateInterval(void) {
            return std::nullopt;
        }
        virtual void TimerStarted(void) {}
        virtual void TimerStopped(void) {}
        virtual void UpdateStats(DetailedStatsContext* ctx) { (void)ctx; }
    };

    // Extended items for DetailedStatsContext
    struct PolylineWithTextElemForDetailedStats {
        PolylineWithTextElemForDetailedStats() {
            m_polyline.Stroke(SolidColorBrush(Colors::White()));
            m_polyline.StrokeThickness(1);
            m_polyline.HorizontalAlignment(HorizontalAlignment::Left);
            m_polyline.Width(180);
            m_text_block.SizeChanged(
                [weak_polyline = make_weak(m_polyline)](IInspectable const&, SizeChangedEventArgs const& e) {
                    if (auto polyline = weak_polyline.get()) {
                        polyline.Height(e.NewSize().Height);
                    }
                }
            );
            m_text_block.Text(L"N/A");
            m_sp.Orientation(Orientation::Horizontal);
            m_sp.Spacing(4);
            m_sp.Children().ReplaceAll({ m_polyline, m_text_block });
        }
        operator FrameworkElement() { return m_sp; }
        // NOTE: text_fn(current_value, highest_value)
        template<typename Functor, typename Container>
        void update(Functor&& text_fn, Container&& container, uint64_t min_points) {
            if (container.size() == 0) {
                m_polyline.Points().Clear();
                m_text_block.Text(L"N/A");
                return;
            }
            auto& real_max_value = *std::max_element(container.begin(), container.end());
            const auto max_value = real_max_value < DBL_EPSILON ? 1 : real_max_value;
            const auto region_width = m_polyline.Width();
            const auto region_height = m_polyline.Height();
            const auto points_count = std::max(min_points, container.size());
            auto pl_points = m_polyline.Points();
            pl_points.Clear();
            for (size_t i = 0; i < container.size(); i++) {
                auto x = static_cast<double>(i) / points_count * region_width;
                auto y = (1 - static_cast<double>(container[i]) / max_value) * region_height;
                // Add 1px vertical padding
                y = y / region_height * (region_height - 2) + 1;
                pl_points.Append(PointHelper::FromCoordinates(
                    static_cast<float>(x), static_cast<float>(y)
                ));
            }
            m_text_block.Text(text_fn(container.back(), real_max_value));
        }
    private:
        StackPanel m_sp;
        Polyline m_polyline;
        TextBlock m_text_block;
    };
}

namespace winrt::BiliUWP::implementation {
    MediaPlayPage::MediaPlayPage() :
        m_cfg_model(::BiliUWP::App::get()->cfg_model()),
        m_http_client(nullptr), m_http_client_m(nullptr),
        m_bili_res_is_ready(false),
        m_up_list(single_threaded_observable_vector<IInspectable>()),
        m_parts_list(single_threaded_observable_vector<IInspectable>()),
        m_detailed_stats_update_timer(nullptr),
        m_use_1p_data(false)
    {
        using namespace Windows::Web::Http;
        using namespace Windows::Web::Http::Filters;

        auto make_http_client_fn = [] {
            auto http_filter = HttpBaseProtocolFilter();
            auto cache_control = http_filter.CacheControl();
            cache_control.ReadBehavior(HttpCacheReadBehavior::NoCache);
            cache_control.WriteBehavior(HttpCacheWriteBehavior::NoCache);
            http_filter.AllowAutoRedirect(false);
            auto http_client = HttpClient(http_filter);
            auto http_client_drh = http_client.DefaultRequestHeaders();
            auto user_agent = http_client_drh.UserAgent();
            user_agent.Clear();
            user_agent.ParseAdd(L"Mozilla/5.0 BiliDroid/6.4.0 (bbcallen@gmail.com)");
            http_client_drh.Referer(Uri(L"https://www.bilibili.com"));
            return http_client;
        };

        m_http_client = make_http_client_fn();
        m_http_client_m = make_http_client_fn();
    }
    void MediaPlayPage::InitializeComponent() {
        MediaPlayPageT::InitializeComponent();

        MediaDetailedStatsToggleMenuItem().RegisterPropertyChangedCallback(
            ToggleMenuFlyoutItem::IsCheckedProperty(),
            [this](DependencyObject const& sender, DependencyProperty const&) {
                auto media_detailed_stats_overlay = MediaDetailedStatsOverlay();
                if (sender.as<ToggleMenuFlyoutItem>().IsChecked()) {
                    media_detailed_stats_overlay.Visibility(Visibility::Visible);
                    if (m_detailed_stats_update_timer) {
                        m_detailed_stats_update_timer.Start();
                        m_detailed_stats_provider->TimerStarted();
                    }
                }
                else {
                    media_detailed_stats_overlay.Visibility(Visibility::Collapsed);
                    if (m_detailed_stats_update_timer) {
                        m_detailed_stats_update_timer.Stop();
                        m_detailed_stats_provider->TimerStopped();
                    }
                }
            }
        );
        MediaDetailedStatsToggleMenuItem().IsChecked(m_cfg_model.App_ShowDetailedStats());

        this->SubmitMediaPlaybackSourceToNativePlayer(nullptr);

        Loaded([this](auto&&, auto&&) {
            util::winrt::force_focus_element(*this, FocusState::Programmatic);
        });
        MediaPlayerElem().RegisterPropertyChangedCallback(
            MediaPlayerElement::IsFullWindowProperty(),
            [this](DependencyObject const& sender, DependencyProperty const&) {
                util::winrt::force_focus_element(*this, FocusState::Programmatic);
            }
        );
    }
    fire_forget_except MediaPlayPage::final_release(std::unique_ptr<MediaPlayPage> ptr) noexcept {
        // Gracefully release MediaPlayer and prevent UI from freezing
        using namespace std::chrono_literals;
        ptr->m_cur_async.cancel_running();
        util::debug::log_trace(L"Final release");
        auto dispatcher = ptr->Dispatcher();
        MediaPlayer player{ nullptr };
        auto cleanup_mediaplayer_fn = [&] {
            auto player_elem = ptr->MediaPlayerElem();
            player_elem.AreTransportControlsEnabled(false);
            player = player_elem.MediaPlayer();
            player_elem.SetMediaPlayer(nullptr);
            if (player && player.Source()) {
                auto session = player.PlaybackSession();
                if (session && session.CanPause()) {
                    player.Pause();
                }
            }
        };
        // Ensure we are on the UI thread (MediaPlayerElem may hold a reference
        // in a non-UI thread during media opening)
        co_await dispatcher;
        // Clean up UpListView
        ptr->UpListView().ItemsSource(nullptr);
        ptr->PartsListView().ItemsSource(nullptr);
        // Clean up MediaPlayer
        cleanup_mediaplayer_fn();
        // Clean up DetailedStatsTimer
        if (ptr->m_detailed_stats_update_timer) {
            ptr->m_detailed_stats_update_timer.Stop();
            ptr->m_detailed_stats_provider->TimerStopped();
        }
    }
    void MediaPlayPage::OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs const& e) {
        auto tab = ::BiliUWP::App::get()->tab_from_page(*this);
        tab->set_icon(Symbol::Play);
        tab->set_title(res_str(L"App/Page/MediaPlayPage/Title"));

        auto illegal_nav_fn = [this] {
            MediaSidebar().Visibility(Visibility::Collapsed);
            MediaPlayerStateOverlay().SwitchToFailed(res_str(L"App/Common/IllegalNavParam"));
        };

        if (auto opt = e.Parameter().try_as<MediaPlayPageNavParam>()) {
            switch (opt->type) {
            case MediaPlayPage_MediaType::Video:
                m_cur_async.cancel_and_run(&MediaPlayPage::NavHandleVideoPlay, this, opt->nid, opt->sid);
                break;
            case MediaPlayPage_MediaType::Audio:
                m_cur_async.cancel_and_run(&MediaPlayPage::NavHandleAudioPlay, this, opt->nid);
                break;
            default:
                util::debug::log_error(L"Media type not recognized");
                illegal_nav_fn();
                break;
            }
        }
        else {
            util::debug::log_error(L"OnNavigatedTo only expects MediaPlayPageNavParam");
            illegal_nav_fn();
        }
    }
    void MediaPlayPage::OnKeyDown(Windows::UI::Xaml::Input::KeyRoutedEventArgs const& e) {
        using Windows::System::VirtualKey;
        auto key = e.Key();
        if (key == VirtualKey::Space) {
            auto player = MediaPlayerElem().MediaPlayer();
            if (auto session = util::winrt::try_get_media_playback_session(player)) {
                switch (session.PlaybackState()) {
                case MediaPlaybackState::Paused:
                    player.Play();
                    break;
                case MediaPlaybackState::Playing:
                    if (session.CanPause()) {
                        player.Pause();
                    }
                    break;
                }
            }
            e.Handled(true);
        }
        else if (key == VirtualKey::F11) {
            MediaPlayerElem().IsFullWindow(true);
            e.Handled(true);
        }
    }
    void MediaPlayPage::OnPointerReleased(Windows::UI::Xaml::Input::PointerRoutedEventArgs const& e) {
        util::winrt::force_focus_element(*this, FocusState::Programmatic);
        e.Handled(true);
    }
    void MediaPlayPage::UpListView_ItemClick(IInspectable const&, ItemClickEventArgs const& e) {
        auto vi = e.ClickedItem().as<BiliUWP::MediaPlayPage_UpItem>();
        auto tab = ::BiliUWP::make<::BiliUWP::AppTab>();
        tab->navigate(
            xaml_typename<winrt::BiliUWP::UserPage>(),
            box_value(UserPageNavParam{ vi.UpMid(), UserPage_TargetPart::PublishedVideos })
        );
        ::BiliUWP::App::get()->add_tab(tab);
        tab->activate();
    }
    void MediaPlayPage::PartsListView_SelectionChanged(
        Windows::Foundation::IInspectable const&,
        Windows::UI::Xaml::Controls::SelectionChangedEventArgs const& e
    ) {
        auto added_items = e.AddedItems();
        if (added_items.Size() == 0) { return; }
        auto cid = added_items.GetAt(0).as<BiliUWP::MediaPlayPage_PartItem>().PartCid();
        m_cur_async.cancel_and_run(&MediaPlayPage::PlayVideoWithCid, this, cid);
    }
    void MediaPlayPage::PartsListView_RightTapped(
        IInspectable const& sender, RightTappedRoutedEventArgs const& e
    ) {
        auto vi = e.OriginalSource().as<FrameworkElement>()
            .DataContext().try_as<BiliUWP::MediaPlayPage_PartItem>();
        if (!vi) { return; }
        e.Handled(true);
        auto items_view = sender.as<ListView>();
        auto mf = MenuFlyout();
        auto mfi_copy_part_title = MenuFlyoutItem();
        mfi_copy_part_title.Text(::BiliUWP::App::res_str(L"App/Page/MediaPlayPage/Sidebar/CopyPartTitle"));
        mfi_copy_part_title.Click([part_title = vi.PartName()](IInspectable const&, RoutedEventArgs const&) {
            util::winrt::set_clipboard_text(part_title, true);
            });
        mf.Items().Append(mfi_copy_part_title);
        mf.ShowAt(items_view, e.GetPosition(items_view));
    }
    IAsyncAction MediaPlayPage::NavHandleVideoPlay(uint64_t avid, hstring bvid) {
        auto cancellation_token = co_await get_cancellation_token();
        cancellation_token.enable_propagation();

        auto weak_store = util::winrt::make_weak_storage(*this);

        // Phase 1: Load media information
        auto media_player_state_overlay = MediaPlayerStateOverlay();
        media_player_state_overlay.SwitchToLoading(res_str(L"App/Common/Loading"));
        try {
            if (bvid != L"") {
                util::debug::log_trace(std::format(L"NavHandleVideoPlay with video {}...", bvid));
                co_await this->UpdateVideoInfo(bvid);
            }
            else {
                util::debug::log_trace(std::format(L"NavHandleVideoPlay with video av{}...", avid));
                co_await this->UpdateVideoInfo(avid);
            }
        }
        catch (::BiliUWP::BiliApiException const& e) {
            // TODO: We may transform some specific exceptions into user-friendly errors
            util::winrt::log_current_exception();
            media_player_state_overlay.SwitchToFailed(res_str(L"App/Page/MediaPlayPage/Error/ServerAPIFailed"));
            throw;
        }
        catch (hresult_canceled const&) { throw; }
        catch (...) {
            util::winrt::log_current_exception();
            media_player_state_overlay.SwitchToFailed(res_str(L"App/Page/MediaPlayPage/Error/Unknown"));
            throw;
        }

        // Phase 2: Play media
        if (!weak_store.lock()) { co_return; }
        /*
        try {
            auto& video_vinfo = std::get<::BiliUWP::VideoViewInfoResult>(m_media_info);
            co_await weak_store.ual(this->PlayVideoWithCidInner(video_vinfo.cid_1p));
        }
        catch (::BiliUWP::BiliApiException const& e) {
            // TODO: We may transform some specific exceptions into user-friendly errors
            util::winrt::log_current_exception();
            media_player_state_overlay.SwitchToFailed(res_str(L"App/Page/MediaPlayPage/Error/ServerAPIFailed"));
            throw;
        }
        catch (hresult_canceled const&) { throw; }
        catch (...) {
            util::winrt::log_current_exception();
            media_player_state_overlay.SwitchToFailed(res_str(L"App/Page/MediaPlayPage/Error/Unknown"));
            throw;
        }
        media_player_state_overlay.SwitchToHidden();
        */
        // Simply select the corresponding part item in list
        Dispatcher().RunAsync(Windows::UI::Core::CoreDispatcherPriority::High,
            [that = get_strong()] {
                that->PartsListView().SelectedIndex(0);
            }
        );
    }
    IAsyncAction MediaPlayPage::NavHandleAudioPlay(uint64_t auid) {
        auto cancellation_token = co_await get_cancellation_token();
        cancellation_token.enable_propagation();

        auto weak_store = util::winrt::make_weak_storage(*this);

        // Phase 1: Load media information
        auto media_player_state_overlay = MediaPlayerStateOverlay();
        media_player_state_overlay.SwitchToLoading(res_str(L"App/Common/Loading"));
        try {
            util::debug::log_trace(std::format(L"NavHandleAudioPlay with audio au{}...", auid));
            co_await this->UpdateAudioInfo(auid);
        }
        catch (::BiliUWP::BiliApiException const& e) {
            // TODO: We may transform some specific exceptions into user-friendly errors
            util::winrt::log_current_exception();
            media_player_state_overlay.SwitchToFailed(res_str(L"App/Page/MediaPlayPage/Error/ServerAPIFailed"));
            throw;
        }
        catch (hresult_canceled const&) { throw; }
        catch (...) {
            util::winrt::log_current_exception();
            media_player_state_overlay.SwitchToFailed(res_str(L"App/Page/MediaPlayPage/Error/Unknown"));
            throw;
        }

        // Phase 2: Play media
        if (!weak_store.lock()) { co_return; }
        auto task = this->PlayAudio();
        weak_store.unlock();
        co_await std::move(task);
    }
    // Updates both UI and local data
    util::winrt::task<> MediaPlayPage::UpdateVideoInfoInner(std::variant<uint64_t, hstring> vid) {
        auto cancellation_token = co_await get_cancellation_token();
        cancellation_token.enable_propagation();

        auto weak_store = util::winrt::make_weak_storage(*this);

        m_bili_res_is_ready = false;
        deferred([&weak_store] {
            if (!weak_store.lock()) { return; }
        weak_store->m_bili_res_is_ready = true;
            });
        m_media_info = std::monostate{};

        // Body
        auto client = ::BiliUWP::App::get()->bili_client();
        m_media_info = std::move(co_await weak_store.ual(client->video_view_info(std::move(vid))));
        auto& video_vinfo = std::get<::BiliUWP::VideoViewInfoResult>(m_media_info);
        m_bili_res_is_ready = true;
        m_use_1p_data = true;
        m_up_list.Clear();
        m_up_list.Append(make<MediaPlayPage_UpItem>(
            video_vinfo.owner.name, video_vinfo.owner.face_url, video_vinfo.owner.mid));
        for (uint64_t idx = 0; auto const& page : video_vinfo.pages) {
            m_parts_list.Append(make<MediaPlayPage_PartItem>(
                idx + 1, page.part_title, page.cid));
            idx++;
        }

        Bindings->Update();
    }
    util::winrt::task<> MediaPlayPage::UpdateVideoInfo(std::variant<uint64_t, hstring> vid) {
        auto cancellation_token = co_await get_cancellation_token();
        cancellation_token.enable_propagation();

        auto media_details_overlay = MediaDetailsOverlay();
        media_details_overlay.SwitchToLoading(res_str(L"App/Common/Loading"));
        try { co_await this->UpdateVideoInfoInner(std::move(vid)); }
        catch (hresult_canceled const&) { throw; }
        catch (...) {
            util::winrt::log_current_exception();
            media_details_overlay.SwitchToFailed(res_str(L"App/Common/LoadFailed"));
            throw;
        }
        media_details_overlay.SwitchToHidden();
    }
    // NOTE: VideoType+PlayerType+FetchingBackendType
    IRandomAccessStream MediaPlayPage::PlayVideoWithCidInner_DashNative_MakeDashMpdStream(
        ::BiliUWP::VideoPlayUrl_Dash const& dash_info,
        ::BiliUWP::VideoPlayUrl_Dash_Stream const& vstream,
        ::BiliUWP::VideoPlayUrl_Dash_Stream const* pastream
    ) {
        // Generate Dash MPD from parsed dash info on the fly
        // TODO: Add support for backup url
        // Dash MPD specification: refer to ISO IEC 23009-1
        // NOTE: minBufferTime type: xs:duration ("PT<Time description>")
        std::wstring dash_mpd_str;
        if (pastream) {
            // Normal (video + audio)
            dash_mpd_str = std::format(LR"(<?xml version="1.0" encoding="utf-8"?>)"
                R"(<MPD xmlns="urn:mpeg:dash:schema:mpd:2011" profiles="urn:mpeg:dash:profile:isoff-on-demand:2011" minBufferTime="PT{}S" type="static">)"
                R"(<Period start="PT0S">)"
                R"(<AdaptationSet contentType="video">)"
                R"(<Representation id="{}" bandwidth="{}" mimeType="{}" codecs="{}" startWithSAP="{}" sar="{}" frame_rate="{}" width="{}" height="{}">)"
                R"(<SegmentBase indexRange="{}"><Initialization range="{}"/></SegmentBase>)"
                R"(</Representation>)"
                R"(</AdaptationSet>)"
                R"(<AdaptationSet contentType="audio">)"
                R"(<Representation id="{}" bandwidth="{}" mimeType="{}" codecs="{}" startWithSAP="{}">)"
                R"(<SegmentBase indexRange="{}"><Initialization range="{}"/></SegmentBase>)"
                R"(</Representation>)"
                R"(</AdaptationSet>)"
                R"(</Period>)"
                R"(</MPD>)",
                dash_info.min_buffer_time,
                vstream.id, vstream.bandwidth, vstream.mime_type, vstream.codecs,
                vstream.start_with_sap, vstream.sar, vstream.frame_rate,
                vstream.width, vstream.height,
                vstream.segment_base.index_range, vstream.segment_base.initialization,
                pastream->id, pastream->bandwidth, pastream->mime_type, pastream->codecs,
                pastream->start_with_sap,
                pastream->segment_base.index_range, pastream->segment_base.initialization
            );
        }
        else {
            // Video only
            dash_mpd_str = std::format(LR"(<?xml version="1.0" encoding="utf-8"?>)"
                R"(<MPD xmlns="urn:mpeg:dash:schema:mpd:2011" profiles="urn:mpeg:dash:profile:isoff-on-demand:2011" minBufferTime="PT{}S" type="static">)"
                R"(<Period start="PT0S">)"
                R"(<AdaptationSet contentType="video">)"
                R"(<Representation id="{}" bandwidth="{}" mimeType="{}" codecs="{}" startWithSAP="{}" sar="{}" frame_rate="{}" width="{}" height="{}">)"
                R"(<SegmentBase indexRange="{}"><Initialization range="{}"/></SegmentBase>)"
                R"(</Representation>)"
                R"(</AdaptationSet>)"
                R"(</Period>)"
                R"(</MPD>)",
                dash_info.min_buffer_time,
                vstream.id, vstream.bandwidth, vstream.mime_type, vstream.codecs,
                vstream.start_with_sap, vstream.sar, vstream.frame_rate,
                vstream.width, vstream.height,
                vstream.segment_base.index_range, vstream.segment_base.initialization
            );
        }
        util::debug::log_trace(std::format(L"Generated dash: {}", dash_mpd_str));
        return util::winrt::string_to_utf8_stream(hstring(dash_mpd_str));
    }
    util::winrt::task<MediaPlayPage::MediaSrcDetailedStatsPair> MediaPlayPage::PlayVideoWithCidInner_DashNativeNative(
        ::BiliUWP::VideoPlayUrl_Dash const& dash_info,
        ::BiliUWP::VideoPlayUrl_Dash_Stream const& vstream,
        ::BiliUWP::VideoPlayUrl_Dash_Stream const* pastream
    ) {
        auto cancellation_token = co_await get_cancellation_token();
        cancellation_token.enable_propagation();
        auto weak_store = util::winrt::make_weak_storage(*this);

        auto adaptive_media_src_result = co_await weak_store.ual(AdaptiveMediaSource::CreateFromStreamAsync(
            PlayVideoWithCidInner_DashNative_MakeDashMpdStream(dash_info, vstream, pastream),
            Uri(L"about:blank"),
            L"application/dash+xml",
            m_http_client_m
        ));
        if (adaptive_media_src_result.Status() != AdaptiveMediaSourceCreationStatus::Success) {
            auto hresult = adaptive_media_src_result.ExtendedError();
            throw hresult_error(E_FAIL, std::format(L"Dash parse failed: {} (0x{:08x}: {})",
                std::to_underlying(adaptive_media_src_result.Status()),
                static_cast<uint32_t>(hresult),
                hresult_error(hresult).message()
            ));
        }
        auto adaptive_media_src = adaptive_media_src_result.MediaSource();

        // Post setup (detailed stats & DownloadRequested event)
        static constexpr auto UPDATE_INTERVAL = std::chrono::milliseconds(500);
        struct shared_stats_data : std::enable_shared_from_this<shared_stats_data> {
            void reset(void) {
                last_start_ts = std::chrono::high_resolution_clock::now();
                connection_duration = {};
                requests_delta = 0;
                bytes_delta = 0;
            }
            shared_stats_data() : is_monitoring(false), active_connections(0) { reset(); }
            std::atomic_bool is_monitoring;
            std::mutex rw_mutex;
            // NOTE: Mutex-protected data below
            uint64_t active_connections;
            std::chrono::high_resolution_clock::time_point last_start_ts;
            std::chrono::high_resolution_clock::duration connection_duration;
            uint64_t requests_delta;
            uint64_t bytes_delta;
        };
        std::shared_ptr<shared_stats_data> shared_data = std::make_shared<shared_stats_data>();
        struct DetailedStatsProvider_AdaptiveMediaSource : DetailedStatsProvider {
            DetailedStatsProvider_AdaptiveMediaSource(
                hstring mime_type,
                hstring resolution,
                hstring video_host, hstring audio_host,
                std::shared_ptr<shared_stats_data> shared_data
            ) : m_mime_type(std::move(mime_type)),
                m_resolution(std::move(resolution)),
                m_video_host(std::move(video_host)), m_audio_host(std::move(audio_host)),
                m_shared_data(std::move(shared_data)) {}
            void InitStats(DetailedStatsContext* ctx) {
                using util::winrt::make_text_block;
                ctx->AddElement(L"Mime Type", make_text_block(m_mime_type));
                ctx->AddElement(L"Player Type", make_text_block(L"NativeDashPlayer <- NativeBuffering"));
                ctx->AddElement(L"Resolution", make_text_block(m_resolution));
                ctx->AddElement(L"Video Host", make_text_block(m_video_host));
                ctx->AddElement(L"Audio Host", make_text_block(m_audio_host));
                ctx->AddElement(L"Connection Speed", m_conn_speed);
                ctx->AddElement(L"Network Activity", m_net_activity);
                ctx->AddElement(L"Mystery Text", m_mystery_text_tb);
            }
            std::optional<TimeSpan> DesiredUpdateInterval(void) {
                return UPDATE_INTERVAL;
            }
            void TimerStarted(void) {
                m_shared_data->is_monitoring.store(true);
                std::scoped_lock guard(m_shared_data->rw_mutex);
                m_shared_data->reset();
            }
            void TimerStopped(void) {
                m_shared_data->is_monitoring.store(false);
            }
            void UpdateStats(DetailedStatsContext* ctx) {
                using util::str::byte_size_to_str;
                using util::str::bit_size_to_str;
                static constexpr size_t MAX_POINTS = 60;
                auto add_point_fn = [](auto& container, std::decay_t<decltype(container)>::value_type value) {
                    if (container.size() >= MAX_POINTS) {
                        container.pop_front();
                    }
                    container.push_back(std::move(value));
                };
                uint64_t cur_downloaded_bytes_delta, cur_active_connections_count;
                uint64_t cur_sent_requests_delta;
                uint64_t cur_inbound_bits_per_second;
                {   // Get & clear metrics
                    std::scoped_lock guard(m_shared_data->rw_mutex);
                    cur_active_connections_count = m_shared_data->active_connections;
                    cur_downloaded_bytes_delta = m_shared_data->bytes_delta;
                    auto actual_duration = m_shared_data->connection_duration;
                    if (m_shared_data->active_connections > 0) {
                        actual_duration +=
                            std::chrono::high_resolution_clock::now() - m_shared_data->last_start_ts;
                    }
                    auto actual_dur_secs = std::chrono::duration<double>(actual_duration).count();
                    auto inbound_bytes_per_sec = actual_dur_secs == 0 ? 0 :
                        m_shared_data->bytes_delta / actual_dur_secs;
                    cur_inbound_bits_per_second =
                        static_cast<uint64_t>(std::llround(inbound_bytes_per_sec * 8));
                    cur_sent_requests_delta = m_shared_data->requests_delta;
                    m_shared_data->reset();
                }
                if (cur_active_connections_count == 0 && cur_downloaded_bytes_delta == 0) {
                    add_point_fn(
                        m_conn_speed_points,
                        m_conn_speed_points.empty() ? 0 : m_conn_speed_points.back()
                    );
                }
                else {
                    add_point_fn(m_conn_speed_points, cur_inbound_bits_per_second);
                }
                m_conn_speed.update([](uint64_t cur_val, uint64_t max_val) -> hstring {
                    return hstring(std::format(
                        L"{}ps / {}ps",
                        bit_size_to_str(cur_val),
                        bit_size_to_str(max_val)
                    ));
                }, m_conn_speed_points, MAX_POINTS);
                add_point_fn(m_net_activity_points, cur_downloaded_bytes_delta);
                m_net_activity.update([](uint64_t cur_val, uint64_t max_val) -> hstring {
                    return hstring(
                        byte_size_to_str(cur_val, 1e2) + L" / " +
                        byte_size_to_str(max_val, 1e2)
                    );
                }, m_net_activity_points, MAX_POINTS);
                // Update mystery text
                m_mystery_text_tb.Text(hstring(std::format(
                    L"c:{} rd:{}", cur_active_connections_count, cur_sent_requests_delta
                )));
            }
        private:
            hstring m_mime_type;
            hstring m_resolution;
            hstring m_video_host, m_audio_host;

            std::shared_ptr<shared_stats_data> m_shared_data;
            PolylineWithTextElemForDetailedStats m_conn_speed;
            std::deque<uint64_t> m_conn_speed_points;
            PolylineWithTextElemForDetailedStats m_net_activity;
            std::deque<uint64_t> m_net_activity_points;
            TextBlock m_mystery_text_tb;
        };

        auto vuri = Uri(vstream.base_url);
        auto auri = pastream ? Uri(pastream->base_url) : nullptr;
        auto ds_provider = std::make_shared<DetailedStatsProvider_AdaptiveMediaSource>(
            hstring(pastream ? std::format(
                L"{};codecs=\"{}\" | {};codecs=\"{}\"",
                vstream.mime_type, vstream.codecs,
                pastream->mime_type, pastream->codecs) : std::format(
                L"{};codecs=\"{}\"",
                vstream.mime_type, vstream.codecs
            )),
            hstring(std::format(L"{}x{}@{}", vstream.width, vstream.height, vstream.frame_rate)),
            vuri.Host(), auri ? auri.Host() : L"N/A",
            shared_data
        );

        // UNLIKELY TODO: Add support for DownloadFailed event (refresh urls)

        // Use buffer replacing (may waste some memory)
        adaptive_media_src.DownloadRequested(
            [http_client = m_http_client_m, vuri = std::move(vuri), auri = std::move(auri),
            shared_data = std::move(shared_data)]
        (AdaptiveMediaSource const&, AdaptiveMediaSourceDownloadRequestedEventArgs const& e) -> fire_forget_except {
                co_safe_capture(shared_data);
                Uri target_uri{ nullptr };
                if (e.ResourceContentType().starts_with(L"video")) { target_uri = vuri; }
                else if (e.ResourceContentType().starts_with(L"audio")) { target_uri = auri; }
                else {
                    util::debug::log_error(L"DownloadRequested: Suspicious: Hacked nothing");
                    co_return;
                }
                if (!target_uri) {
                    util::debug::log_error(L"DownloadRequested: Cannot fetch a non-existent uri");
                    co_return;
                }
                auto result = e.Result();
                if (!result) {
                    util::debug::log_error(L"DownloadRequested: Result is nullptr");
                    co_return;
                }
                auto o_content_start = e.ResourceByteRangeOffset().try_as<uint64_t>();
                auto o_content_size = e.ResourceByteRangeLength().try_as<uint64_t>();
                if (!(o_content_start && o_content_size)) {
                    util::debug::log_warn(L"DownloadRequested: No span info provided, falling back to uri");
                    result.ResourceUri(target_uri);
                    co_return;
                }
                auto content_start = *o_content_start;
                auto content_size = *o_content_size;
                {
                    std::scoped_lock guard(shared_data->rw_mutex);
                    if (shared_data->is_monitoring.load()) {
                        shared_data->requests_delta++;
                    }
                    if (shared_data->active_connections++ == 0) {
                        shared_data->last_start_ts = std::chrono::high_resolution_clock::now();
                    }
                }
                deferred([&] {
                    std::scoped_lock guard(shared_data->rw_mutex);
                    if (--shared_data->active_connections == 0) {
                        if (shared_data->is_monitoring.load()) {
                            shared_data->connection_duration +=
                                std::chrono::high_resolution_clock::now() - shared_data->last_start_ts;
                        }
                    }
                });
                util::debug::log_trace(std::format(L"Fetching partial http: {}+{}",
                    content_start, content_size));
                auto read_op = util::winrt::fetch_partial_http_as_buffer(
                    target_uri, http_client,
                    content_start, content_size
                );
                uint64_t cur_progress = 0;
                read_op.Progress([&](auto const&, uint64_t progress) {
                    if (shared_data->is_monitoring.load()) {
                        std::scoped_lock guard(shared_data->rw_mutex);
                        shared_data->bytes_delta += progress - cur_progress;
                    }
                    cur_progress = progress;
                });
                auto deferral = e.GetDeferral();
                deferred([&] { deferral.Complete(); });
                result.Buffer(co_await read_op);
            }
        );

        co_return{ MediaSource::CreateFromAdaptiveMediaSource(adaptive_media_src), std::move(ds_provider) };
    }
    util::winrt::task<MediaPlayPage::MediaSrcDetailedStatsPair> MediaPlayPage::PlayVideoWithCidInner_DashNativeHras(
        ::BiliUWP::VideoPlayUrl_Dash const& dash_info,
        ::BiliUWP::VideoPlayUrl_Dash_Stream const& vstream,
        ::BiliUWP::VideoPlayUrl_Dash_Stream const& astream,
        std::function<util::winrt::task<PlayUrlDashStreamPair>(void)> get_new_stream_fn
    ) {
        auto cancellation_token = co_await get_cancellation_token();
        cancellation_token.enable_propagation();
        auto weak_store = util::winrt::make_weak_storage(*this);

        auto adaptive_media_src_result = co_await weak_store.ual(AdaptiveMediaSource::CreateFromStreamAsync(
            PlayVideoWithCidInner_DashNative_MakeDashMpdStream(dash_info, vstream, &astream),
            Uri(L"about:blank"),
            L"application/dash+xml",
            m_http_client_m
        ));
        if (adaptive_media_src_result.Status() != AdaptiveMediaSourceCreationStatus::Success) {
            auto hresult = adaptive_media_src_result.ExtendedError();
            throw hresult_error(E_FAIL, std::format(L"Dash parse failed: {} (0x{:08x}: {})",
                std::to_underlying(adaptive_media_src_result.Status()),
                static_cast<uint32_t>(hresult),
                hresult_error(hresult).message()
            ));
        }
        auto adaptive_media_src = adaptive_media_src_result.MediaSource();

        // Post setup (detailed stats & DownloadRequested event)
        auto vuri = Uri(vstream.base_url);
        auto auri = Uri(astream.base_url);

        /*auto vhras = co_await weak_store.ual(HttpRandomAccessStream::CreateAsync(
            vuri,
            m_http_client_m,
            HttpRandomAccessStreamBufferOptions::Full,
            0, false
        ));
        auto ahras = co_await weak_store.ual(HttpRandomAccessStream::CreateAsync(
            auri,
            m_http_client_m,
            HttpRandomAccessStreamBufferOptions::Full,
            0, false
        ));*/
        BiliUWP::HttpRandomAccessStream vhras{ nullptr }, ahras{ nullptr };
        {
            auto vhrasop = BiliUWP::HttpRandomAccessStream::CreateAsync(
                vuri,
                m_http_client_m,
                HttpRandomAccessStreamBufferOptions::Full,
                0, false
            );
            auto ahrasop = BiliUWP::HttpRandomAccessStream::CreateAsync(
                auri,
                m_http_client_m,
                HttpRandomAccessStreamBufferOptions::Full,
                0, false
            );
            vhras = co_await weak_store.ual(std::move(vhrasop));
            ahras = co_await weak_store.ual(std::move(ahrasop));
        }

        // TODO: Improve new uri supplying logic
        auto add_backup_uris_fn = [](BiliUWP::HttpRandomAccessStream const& hras, auto const& urls) {
            std::vector<Uri> supply_uris;
            for (auto const& i : urls) { supply_uris.emplace_back(i); }
            hras.SupplyNewUri(supply_uris);
        };
        add_backup_uris_fn(vhras, vstream.backup_url);
        add_backup_uris_fn(ahras, astream.backup_url);
        auto new_uri_requested_inner_fn = [get_new_stream_fn = std::move(get_new_stream_fn),
            weak_vhras = make_weak(vhras), weak_ahras = make_weak(ahras)
        ](void) -> util::winrt::task<>
        {
            auto vhras = weak_vhras.get();
            if (!vhras) { co_return; }
            auto ahras = weak_ahras.get();
            if (!ahras) { co_return; }
            util::debug::log_debug(L"NewUriRequested: Getting new links");
            // SAFETY: get_new_stream_fn is no longer used after any suspension points
            auto [vstream, astream] = std::move(co_await get_new_stream_fn());
            std::vector<Uri> supply_uris;
            supply_uris.emplace_back(vstream.base_url);
            for (auto const& i : vstream.backup_url) { supply_uris.emplace_back(i); }
            vhras.SupplyNewUri(supply_uris);
            supply_uris.clear();
            supply_uris.emplace_back(astream.base_url);
            for (auto const& i : astream.backup_url) { supply_uris.emplace_back(i); }
            ahras.SupplyNewUri(supply_uris);
            util::debug::log_debug(L"NewUriRequested: Done getting new links");
        };
        struct new_uri_requested_shared_data {
            std::mutex mutex;
            util::winrt::task<> op;
        };
        auto new_uri_requested_fn = [=, shared_data = std::make_shared<new_uri_requested_shared_data>()]
        (BiliUWP::HttpRandomAccessStream const&, BiliUWP::NewUriRequestedEventArgs const& e) -> fire_forget_except
        {
            co_safe_capture(shared_data);
            auto deferral = e.GetDeferral();
            deferred([&] { deferral.Complete(); });
            bool owns_op = false;
            util::winrt::task<> op = nullptr;
            auto correlation_id = static_cast<uint32_t>(util::num::gen_global_seqid());
            {
                std::scoped_lock guard(shared_data->mutex);
                op = shared_data->op;
                if (!op) {
                    util::debug::log_trace(std::format(L"Establishing inner NRU async operation (CorrelationId: {:08x})", correlation_id));
                    op = shared_data->op = new_uri_requested_inner_fn();
                    owns_op = true;
                }
            }
            deferred([&] {
                if (owns_op) {
                    util::debug::log_trace(std::format(L"Releasing inner NRU async operation (CorrelationId: {:08x})", correlation_id));
                    std::scoped_lock guard(shared_data->mutex);
                    shared_data->op = nullptr;
                }
            });
            util::debug::log_trace(std::format(L"Waiting for inner NRU async operation to complete (CorrelationId: {:08x})", correlation_id));
            co_await op;
            util::debug::log_trace(std::format(L"Done awaiting inner NRU async operation (CorrelationId: {:08x})", correlation_id));
        };
        vhras.NewUriRequested(new_uri_requested_fn);
        ahras.NewUriRequested(new_uri_requested_fn);

        // Fetch ahead, so we can make sure opening will succeed
        {
            constexpr uint32_t bufsize = 4;
            auto vop = vhras.ReadAsync(Buffer(bufsize), bufsize, InputStreamOptions::None);
            auto aop = ahras.ReadAsync(Buffer(bufsize), bufsize, InputStreamOptions::None);
            co_await std::move(vop);
            co_await std::move(aop);
            vhras.Seek(0);
            ahras.Seek(0);
        }

        static constexpr auto UPDATE_INTERVAL = std::chrono::milliseconds(500);
        struct shared_stats_data : std::enable_shared_from_this<shared_stats_data> {
            shared_stats_data(
                BiliUWP::HttpRandomAccessStream in_vhras, BiliUWP::HttpRandomAccessStream in_ahras
            ) : vhras(in_vhras), ahras(in_ahras), last_failed(false) {}
            BiliUWP::HttpRandomAccessStream vhras, ahras;
            std::atomic_bool last_failed;
        };
        std::shared_ptr<shared_stats_data> shared_data = std::make_shared<shared_stats_data>(
            vhras, ahras);
        struct DetailedStatsProvider_AdaptiveMediaSource : DetailedStatsProvider {
            DetailedStatsProvider_AdaptiveMediaSource(
                hstring mime_type,
                hstring resolution,
                std::shared_ptr<shared_stats_data> shared_data
            ) : m_mime_type(std::move(mime_type)),
                m_resolution(std::move(resolution)),
                m_shared_data(std::move(shared_data)) {}
            void InitStats(DetailedStatsContext* ctx) {
                using util::winrt::make_text_block;
                ctx->AddElement(L"Mime Type", make_text_block(m_mime_type));
                ctx->AddElement(L"Player Type", make_text_block(
                    L"NativeDashPlayer <- NativeBuffering <- HRAS(Full)"
                ));
                ctx->AddElement(L"Resolution", make_text_block(m_resolution));
                ctx->AddElement(L"Video Host", m_video_host_tb);
                ctx->AddElement(L"Audio Host", m_audio_host_tb);
                ctx->AddElement(L"Video Speed", m_video_conn_speed);
                ctx->AddElement(L"Audio Speed", m_audio_conn_speed);
                ctx->AddElement(L"Network Activity", m_net_activity);
                ctx->AddElement(L"Mystery Text", m_mystery_text_tb);
            }
            std::optional<TimeSpan> DesiredUpdateInterval(void) {
                return UPDATE_INTERVAL;
            }
            void TimerStarted(void) {
                m_shared_data->vhras.EnableMetricsCollection(true, 0);
                m_shared_data->vhras.GetMetrics(true);
                m_shared_data->ahras.EnableMetricsCollection(true, 0);
                m_shared_data->ahras.GetMetrics(true);
            }
            void TimerStopped(void) {
                m_shared_data->vhras.EnableMetricsCollection(false, 0);
                m_shared_data->ahras.EnableMetricsCollection(false, 0);
            }
            void UpdateStats(DetailedStatsContext* ctx) {
                using util::str::byte_size_to_str;
                using util::str::bit_size_to_str;
                static constexpr size_t MAX_POINTS = 60;
                auto vmetrics = m_shared_data->vhras.GetMetrics(true);
                auto ametrics = m_shared_data->ahras.GetMetrics(true);
                auto add_point_fn = [](auto& container, std::decay_t<decltype(container)>::value_type value) {
                    if (container.size() >= MAX_POINTS) {
                        container.pop_front();
                    }
                    container.push_back(std::move(value));
                };
                auto update_conn_speed_fn = [&](auto& metrics, auto& elem, auto& points) {
                    if (metrics.ActiveConnectionsCount == 0 && metrics.DownloadedBytesDelta == 0) {
                        add_point_fn(points, points.empty() ? 0 : points.back());
                    }
                    else {
                        add_point_fn(points, metrics.InboundBitsPerSecond);
                    }
                    elem.update([](uint64_t cur_val, uint64_t max_val) -> hstring {
                        return hstring(std::format(
                            L"{}ps / {}ps",
                            bit_size_to_str(cur_val),
                            bit_size_to_str(max_val)
                        ));
                    }, points, MAX_POINTS);
                };
                auto update_host_fn = [](auto const& hras, auto const& tb) {
                    auto uris = hras.GetActiveUris();
                    switch (uris.size()) {
                    case 0:
                        tb.Text(L"N/A");
                        break;
                    case 1:
                        tb.Text(uris[0].Host());
                        break;
                    default:
                        tb.Text(std::format(L"{} (+{})", uris[0].Host(), uris.size() - 1));
                        break;
                    }
                };
                update_conn_speed_fn(vmetrics, m_video_conn_speed, m_video_conn_speed_points);
                update_host_fn(m_shared_data->vhras, m_video_host_tb);
                update_conn_speed_fn(ametrics, m_audio_conn_speed, m_audio_conn_speed_points);
                update_host_fn(m_shared_data->ahras, m_audio_host_tb);
                add_point_fn(m_net_activity_points,
                    vmetrics.DownloadedBytesDelta + ametrics.DownloadedBytesDelta);
                m_net_activity.update([](uint64_t cur_val, uint64_t max_val) -> hstring {
                    return hstring(
                        byte_size_to_str(cur_val, 1e2) + L" / " +
                        byte_size_to_str(max_val, 1e2)
                    );
                }, m_net_activity_points, MAX_POINTS);
                // Update mystery text
                m_mystery_text_tb.Text(hstring(std::format(
                    L"c:{} rd:{} hrasb:{}/{}",
                    vmetrics.ActiveConnectionsCount + ametrics.ActiveConnectionsCount,
                    vmetrics.SentRequestsDelta + ametrics.SentRequestsDelta,
                    vmetrics.UsedBufferSize + ametrics.UsedBufferSize,
                    vmetrics.AllocatedBufferSize + ametrics.AllocatedBufferSize
                )));
            }
        private:
            hstring m_mime_type;
            hstring m_resolution;

            std::shared_ptr<shared_stats_data> m_shared_data;
            TextBlock m_video_host_tb;
            TextBlock m_audio_host_tb;
            PolylineWithTextElemForDetailedStats m_video_conn_speed;
            std::deque<uint64_t> m_video_conn_speed_points;
            PolylineWithTextElemForDetailedStats m_audio_conn_speed;
            std::deque<uint64_t> m_audio_conn_speed_points;
            PolylineWithTextElemForDetailedStats m_net_activity;
            std::deque<uint64_t> m_net_activity_points;
            TextBlock m_mystery_text_tb;
        };

        auto ds_provider = std::make_shared<DetailedStatsProvider_AdaptiveMediaSource>(
            hstring(std::format(
                L"{};codecs=\"{}\" | {};codecs=\"{}\"",
                vstream.mime_type, vstream.codecs,
                astream.mime_type, astream.codecs)),
            hstring(std::format(L"{}x{}@{}", vstream.width, vstream.height, vstream.frame_rate)),
            shared_data
        );

        // Use buffer replacing (may waste some memory)
        adaptive_media_src.DownloadRequested(
            [media_player_state_overlay = MediaPlayerStateOverlay(), shared_data = std::move(shared_data)]
        (AdaptiveMediaSource const&, AdaptiveMediaSourceDownloadRequestedEventArgs const& e) -> fire_forget_except {
                co_safe_capture(shared_data);
                const BiliUWP::HttpRandomAccessStream* target_hras;
                if (e.ResourceContentType().starts_with(L"video")) { target_hras = &shared_data->vhras; }
                else if (e.ResourceContentType().starts_with(L"audio")) { target_hras = &shared_data->ahras; }
                else {
                    util::debug::log_error(L"DownloadRequested: Suspicious: Hacked nothing");
                    co_return;
                }
                if (!*target_hras) {
                    util::debug::log_error(L"DownloadRequested: Cannot fetch a non-existent stream");
                    co_return;
                }
                auto result = e.Result();
                if (!result) {
                    util::debug::log_error(L"DownloadRequested: Result is nullptr");
                    co_return;
                }
                auto o_content_start = e.ResourceByteRangeOffset().try_as<uint64_t>();
                auto o_content_size = e.ResourceByteRangeLength().try_as<uint64_t>();
                if (!(o_content_start && o_content_size)) {
                    util::debug::log_warn(L"DownloadRequested: No span info provided, falling back");
                    result.InputStream(target_hras->CloneStream());
                    co_return;
                }
                auto content_start = *o_content_start;
                auto content_size_u32 = static_cast<uint32_t>(*o_content_size);
                auto deferral = e.GetDeferral();
                deferred([&] { deferral.Complete(); });
                try {
                    result.Buffer(co_await target_hras->GetInputStreamAt(content_start).ReadAsync(
                        Buffer(content_size_u32), content_size_u32, InputStreamOptions::None));
                }
                catch (hresult_error const& err) {
                    // Present failure to the user because normally fetching should never fail
                    // (as we can supply new uris on demand)
                    // Unfortunately, we don't have the ability to halt playback
                    result.ExtendedStatus(err.code());
                    if (!shared_data->last_failed) {
                        shared_data->last_failed = true;
                        media_player_state_overlay.Dispatcher().RunAsync(
                            Windows::UI::Core::CoreDispatcherPriority::Normal,
                            [=] { media_player_state_overlay.SwitchToFailed(res_str(L"App/Common/LoadFailed")); }
                        );
                    }
                    throw;
                }
                if (shared_data->last_failed) {
                    shared_data->last_failed = false;
                    media_player_state_overlay.Dispatcher().RunAsync(
                        Windows::UI::Core::CoreDispatcherPriority::Normal,
                        [=] { media_player_state_overlay.SwitchToHidden(); }
                    );
                }
                /*e.Result().InputStream(target_hras->GetInputStreamAt(
                    e.ResourceByteRangeOffset().as<uint64_t>()));*/
            }
        );

        co_return{ MediaSource::CreateFromAdaptiveMediaSource(adaptive_media_src), std::move(ds_provider) };
    }
    util::winrt::task<MediaPlayPage::MediaSrcDetailedStatsPair> MediaPlayPage::PlayVideoWithCidInner_DashNativeHrasNoAudio(
        ::BiliUWP::VideoPlayUrl_Dash const& dash_info,
        ::BiliUWP::VideoPlayUrl_Dash_Stream const& vstream,
        std::function<util::winrt::task<::BiliUWP::VideoPlayUrl_Dash_Stream>(void)> get_new_stream_fn
    ) {
        auto cancellation_token = co_await get_cancellation_token();
        cancellation_token.enable_propagation();
        auto weak_store = util::winrt::make_weak_storage(*this);

        auto adaptive_media_src_result = co_await weak_store.ual(AdaptiveMediaSource::CreateFromStreamAsync(
            PlayVideoWithCidInner_DashNative_MakeDashMpdStream(dash_info, vstream, nullptr),
            Uri(L"about:blank"),
            L"application/dash+xml",
            m_http_client_m
        ));
        if (adaptive_media_src_result.Status() != AdaptiveMediaSourceCreationStatus::Success) {
            auto hresult = adaptive_media_src_result.ExtendedError();
            throw hresult_error(E_FAIL, std::format(L"Dash parse failed: {} (0x{:08x}: {})",
                std::to_underlying(adaptive_media_src_result.Status()),
                static_cast<uint32_t>(hresult),
                hresult_error(hresult).message()
            ));
        }
        auto adaptive_media_src = adaptive_media_src_result.MediaSource();

        // Post setup (detailed stats & DownloadRequested event)
        auto vuri = Uri(vstream.base_url);

        auto vhras = co_await weak_store.ual(BiliUWP::HttpRandomAccessStream::CreateAsync(
            vuri,
            m_http_client_m,
            HttpRandomAccessStreamBufferOptions::Full,
            0, false
        ));

        // TODO: Improve new uri supplying logic
        auto add_backup_uris_fn = [](BiliUWP::HttpRandomAccessStream const& hras, auto const& urls) {
            std::vector<Uri> supply_uris;
            for (auto const& i : urls) { supply_uris.emplace_back(i); }
            hras.SupplyNewUri(supply_uris);
        };
        add_backup_uris_fn(vhras, vstream.backup_url);
        auto new_uri_requested_fn = [get_new_stream_fn = std::move(get_new_stream_fn),
            weak_vhras = make_weak(vhras)
        ](BiliUWP::HttpRandomAccessStream const&, BiliUWP::NewUriRequestedEventArgs const& e) -> fire_forget_except
        {
            auto vhras = weak_vhras.get();
            if (!vhras) { co_return; }

            auto deferral = e.GetDeferral();
            deferred([&] { deferral.Complete(); });

            util::debug::log_debug(L"NewUriRequested: Getting new links");
            // SAFETY: get_new_stream_fn is no longer used after any suspension points
            auto&& vstream = std::move(co_await get_new_stream_fn());
            std::vector<Uri> supply_uris;
            supply_uris.emplace_back(vstream.base_url);
            for (auto const& i : vstream.backup_url) { supply_uris.emplace_back(i); }
            vhras.SupplyNewUri(supply_uris);
            util::debug::log_debug(L"NewUriRequested: Done getting new links");
        };
        vhras.NewUriRequested(std::move(new_uri_requested_fn));

        // Fetch ahead, so we can make sure opening will succeed
        {
            constexpr uint32_t bufsize = 4;
            auto vop = vhras.ReadAsync(Buffer(bufsize), bufsize, InputStreamOptions::None);
            co_await std::move(vop);
            vhras.Seek(0);
        }

        static constexpr auto UPDATE_INTERVAL = std::chrono::milliseconds(500);
        struct shared_stats_data : std::enable_shared_from_this<shared_stats_data> {
            shared_stats_data(BiliUWP::HttpRandomAccessStream in_vhras) :
                vhras(in_vhras), last_failed(false) {}
            BiliUWP::HttpRandomAccessStream vhras;
            std::atomic_bool last_failed;
        };
        std::shared_ptr<shared_stats_data> shared_data = std::make_shared<shared_stats_data>(vhras);
        struct DetailedStatsProvider_AdaptiveMediaSource : DetailedStatsProvider {
            DetailedStatsProvider_AdaptiveMediaSource(
                hstring mime_type,
                hstring resolution,
                std::shared_ptr<shared_stats_data> shared_data
            ) : m_mime_type(std::move(mime_type)),
                m_resolution(std::move(resolution)),
                m_shared_data(std::move(shared_data)) {}
            void InitStats(DetailedStatsContext* ctx) {
                using util::winrt::make_text_block;
                ctx->AddElement(L"Mime Type", make_text_block(m_mime_type));
                ctx->AddElement(L"Player Type", make_text_block(
                    L"NativeDashPlayer <- NativeBuffering <- HRAS(Full)"
                ));
                ctx->AddElement(L"Resolution", make_text_block(m_resolution));
                ctx->AddElement(L"Video Host", m_video_host_tb);
                ctx->AddElement(L"Audio Host", make_text_block(L"N/A"));
                ctx->AddElement(L"Video Speed", m_video_conn_speed);
                ctx->AddElement(L"Network Activity", m_net_activity);
                ctx->AddElement(L"Mystery Text", m_mystery_text_tb);
            }
            std::optional<TimeSpan> DesiredUpdateInterval(void) {
                return UPDATE_INTERVAL;
            }
            void TimerStarted(void) {
                m_shared_data->vhras.EnableMetricsCollection(true, 0);
                m_shared_data->vhras.GetMetrics(true);
            }
            void TimerStopped(void) {
                m_shared_data->vhras.EnableMetricsCollection(false, 0);
            }
            void UpdateStats(DetailedStatsContext* ctx) {
                using util::str::byte_size_to_str;
                using util::str::bit_size_to_str;
                static constexpr size_t MAX_POINTS = 60;
                auto vmetrics = m_shared_data->vhras.GetMetrics(true);
                auto add_point_fn = [](auto& container, std::decay_t<decltype(container)>::value_type value) {
                    if (container.size() >= MAX_POINTS) {
                        container.pop_front();
                    }
                    container.push_back(std::move(value));
                };
                auto update_conn_speed_fn = [&](auto& metrics, auto& elem, auto& points) {
                    if (metrics.ActiveConnectionsCount == 0 && metrics.DownloadedBytesDelta == 0) {
                        add_point_fn(points, points.empty() ? 0 : points.back());
                    }
                    else {
                        add_point_fn(points, metrics.InboundBitsPerSecond);
                    }
                    elem.update([](uint64_t cur_val, uint64_t max_val) -> hstring {
                        return hstring(std::format(
                            L"{}ps / {}ps",
                            bit_size_to_str(cur_val),
                            bit_size_to_str(max_val)
                        ));
                    }, points, MAX_POINTS);
                };
                auto update_host_fn = [](auto const& hras, auto const& tb) {
                    auto uris = hras.GetActiveUris();
                    switch (uris.size()) {
                    case 0:
                        tb.Text(L"N/A");
                        break;
                    case 1:
                        tb.Text(uris[0].Host());
                        break;
                    default:
                        tb.Text(std::format(L"{} (+{})", uris[0].Host(), uris.size() - 1));
                        break;
                    }
                };
                update_conn_speed_fn(vmetrics, m_video_conn_speed, m_video_conn_speed_points);
                update_host_fn(m_shared_data->vhras, m_video_host_tb);
                add_point_fn(m_net_activity_points, vmetrics.DownloadedBytesDelta);
                m_net_activity.update([](uint64_t cur_val, uint64_t max_val) -> hstring {
                    return hstring(
                        byte_size_to_str(cur_val, 1e2) + L" / " +
                        byte_size_to_str(max_val, 1e2)
                    );
                }, m_net_activity_points, MAX_POINTS);
                // Update mystery text
                m_mystery_text_tb.Text(hstring(std::format(
                    L"c:{} rd:{} hrasb:{}/{}",
                    vmetrics.ActiveConnectionsCount,
                    vmetrics.SentRequestsDelta,
                    vmetrics.UsedBufferSize,
                    vmetrics.AllocatedBufferSize
                )));
            }
        private:
            hstring m_mime_type;
            hstring m_resolution;

            std::shared_ptr<shared_stats_data> m_shared_data;
            TextBlock m_video_host_tb;
            PolylineWithTextElemForDetailedStats m_video_conn_speed;
            std::deque<uint64_t> m_video_conn_speed_points;
            PolylineWithTextElemForDetailedStats m_net_activity;
            std::deque<uint64_t> m_net_activity_points;
            TextBlock m_mystery_text_tb;
        };

        auto ds_provider = std::make_shared<DetailedStatsProvider_AdaptiveMediaSource>(
            hstring(std::format(L"{};codecs=\"{}\"", vstream.mime_type, vstream.codecs)),
            hstring(std::format(L"{}x{}@{}", vstream.width, vstream.height, vstream.frame_rate)),
            shared_data
        );

        // Use buffer replacing (may waste some memory)
        adaptive_media_src.DownloadRequested(
            [media_player_state_overlay = MediaPlayerStateOverlay(), shared_data = std::move(shared_data)]
        (AdaptiveMediaSource const&, AdaptiveMediaSourceDownloadRequestedEventArgs const& e) -> fire_forget_except {
                co_safe_capture(shared_data);
                const BiliUWP::HttpRandomAccessStream* target_hras;
                if (e.ResourceContentType().starts_with(L"video")) { target_hras = &shared_data->vhras; }
                else {
                    util::debug::log_error(L"DownloadRequested: Suspicious: Hacked nothing");
                    co_return;
                }
                if (!*target_hras) {
                    util::debug::log_error(L"DownloadRequested: Cannot fetch a non-existent stream");
                    co_return;
                }
                auto result = e.Result();
                if (!result) {
                    util::debug::log_error(L"DownloadRequested: Result is nullptr");
                    co_return;
                }
                auto o_content_start = e.ResourceByteRangeOffset().try_as<uint64_t>();
                auto o_content_size = e.ResourceByteRangeLength().try_as<uint64_t>();
                if (!(o_content_start && o_content_size)) {
                    util::debug::log_warn(L"DownloadRequested: No span info provided, falling back");
                    result.InputStream(target_hras->CloneStream());
                    co_return;
                }
                auto content_start = *o_content_start;
                auto content_size_u32 = static_cast<uint32_t>(*o_content_size);
                auto deferral = e.GetDeferral();
                deferred([&] { deferral.Complete(); });
                try {
                    result.Buffer(co_await target_hras->GetInputStreamAt(content_start).ReadAsync(
                        Buffer(content_size_u32), content_size_u32, InputStreamOptions::None));
                }
                catch (hresult_error const& err) {
                    // Present failure to the user because normally fetching should never fail
                    // (as we can supply new uris on demand)
                    // Unfortunately, we don't have the ability to halt playback
                    result.ExtendedStatus(err.code());
                    if (!shared_data->last_failed) {
                        shared_data->last_failed = true;
                        media_player_state_overlay.Dispatcher().RunAsync(
                            Windows::UI::Core::CoreDispatcherPriority::Normal,
                            [=] { media_player_state_overlay.SwitchToFailed(res_str(L"App/Common/LoadFailed")); }
                        );
                    }
                    throw;
                }
                if (shared_data->last_failed) {
                    shared_data->last_failed = false;
                    media_player_state_overlay.Dispatcher().RunAsync(
                        Windows::UI::Core::CoreDispatcherPriority::Normal,
                        [=] { media_player_state_overlay.SwitchToHidden(); }
                    );
                }
                /*e.Result().InputStream(target_hras->GetInputStreamAt(
                    e.ResourceByteRangeOffset().as<uint64_t>()));*/
            }
        );

        co_return{ MediaSource::CreateFromAdaptiveMediaSource(adaptive_media_src), std::move(ds_provider) };
    }
    //util::winrt::task<MediaSource> MediaPlayPage::PlayVideoWithCidInner_FlvHrasUnknown(...) {
    //    auto cancellation_token = co_await get_cancellation_token();
    //    cancellation_token.enable_propagation();
    //    // TODO...
    //    throw hresult_not_implemented();
    //}
    util::winrt::task<> MediaPlayPage::PlayVideoWithCidInner(uint64_t cid) {
        auto cancellation_token = co_await get_cancellation_token();
        cancellation_token.enable_propagation();
        auto weak_store = util::winrt::make_weak_storage(*this);

        // Body
        // WARN: Never access video_vinfo after any suspension point,
        //       as the media info may have changed type
        auto& video_vinfo = std::get<::BiliUWP::VideoViewInfoResult>(m_media_info);
        auto client = ::BiliUWP::App::get()->bili_client();

        this->SubmitMediaPlaybackSourceToNativePlayer(nullptr);

        auto video_bvid = video_vinfo.bvid;

        struct VideoPartMetadata {
            uint64_t online_count;
            std::vector<TimedTextSource> tts_vec;
        };
        auto fetch_vpart_meta_fn = [&]() -> util::winrt::task<VideoPartMetadata> {
            std::vector<TimedTextSource> tts_vec;
            auto bvid = video_bvid;
            co_safe_capture(cid);
            auto weak_store = util::winrt::make_weak_storage(*this);
            util::debug::log_trace(L"Start fetching video metadata");
            weak_store.lock();
            co_await resume_background();
            auto client = ::BiliUWP::App::get()->bili_client();
            auto vinfo2 = co_await weak_store.ual(client->video_info_v2(bvid, cid));
            for (auto const& st : vinfo2.subtitle.list) {
                // Add BOM header so that TimedTextSource can load subtitles correctly
                std::wstring srt_str{ 0xfeff };
                auto jo = Windows::Data::Json::JsonObject::Parse(
                    co_await weak_store.ual(weak_store->m_http_client.GetStringAsync(Uri(st.subtitle_url)))
                );
                for (size_t idx = 0; auto && i : jo.GetNamedArray(L"body")) {
                    auto secs_to_srt_time_str_fn = [](double t) {
                        double hrs = std::floor(t / 60 / 60);
                        t -= hrs * 60 * 60;
                        double mins = std::floor(t / 60);
                        t -= mins * 60;
                        double secs = std::floor(t);
                        t -= secs;
                        t = std::floor(t * 1000);
                        return std::format(L"{:02}:{:02}:{:02},{:03}", hrs, mins, secs, t);
                    };
                    auto jo = i.GetObject();
                    std::wstring content{ jo.GetNamedString(L"content") };
                    content = std::regex_replace(content, std::wregex(L"<", std::regex::optimize), L"&lt;");
                    content = std::regex_replace(content, std::wregex(L">", std::regex::optimize), L"&gt;");
                    srt_str += std::format(L"{}\n{} --> {}\n{}\n\n",
                        idx + 1,
                        secs_to_srt_time_str_fn(jo.GetNamedNumber(L"from")),
                        secs_to_srt_time_str_fn(jo.GetNamedNumber(L"to")),
                        content
                    );
                    idx++;
                }
                auto srt_stream = util::winrt::string_to_utf8_stream(hstring(srt_str));
                tts_vec.push_back(TimedTextSource::CreateFromStream(srt_stream, st.language_doc));
            }
            util::debug::log_trace(L"Done fetching video metadata");
            co_return{ vinfo2.online_count, std::move(tts_vec) };
        };

        ::BiliUWP::VideoPlayUrlPreferenceParam param{
            .prefer_dash = true, .prefer_hdr = true, .prefer_4k = true,
            .prefer_dolby = true, .prefer_8k = true, .prefer_av1 = false
        };
        auto video_pinfo = std::move(co_await weak_store.ual(client->video_play_url(
            video_bvid, cid, param
        )));

        std::shared_ptr<DetailedStatsProvider> ds_provider;
        MediaSource media_src{ nullptr };

        util::winrt::task<MediaPlayPage::MediaSrcDetailedStatsPair> video_task;

        if (video_pinfo.dash) {
            auto& video_dash = *video_pinfo.dash;
            auto* pvideo_stream = &video_dash.video.at(0);
            for (auto& i : video_dash.video) {
                // TODO: Remove this hack, and let user choose
                // Skip avc codec
                if (i.codecid == 7) { continue; }
                pvideo_stream = &i;
                break;
            }
            auto& video_stream = *pvideo_stream;
            util::debug::log_trace(std::format(L"Selecting video stream {}", video_stream.id));
            const bool video_only_res = video_dash.audio.empty();
            constexpr double BACKOFF_INITIAL_SECS = 10;
            constexpr double BACKOFF_FACTOR = 1.2;
            if (video_dash.audio.empty()) {
                // No audio
                if (m_cfg_model.App_UseHRASForVideo()) {
                    video_task = this->PlayVideoWithCidInner_DashNativeHrasNoAudio(video_dash, video_stream,
                        [client, bvid = video_bvid, cid, param,
                        backoff_secs = std::make_shared<double>(0),
                        vid = video_stream.id, vcid = video_stream.codecid
                        ]() -> util::winrt::task<::BiliUWP::VideoPlayUrl_Dash_Stream> {
                            co_safe_capture(bvid);
                            co_safe_capture(cid);
                            co_safe_capture(param);
                            co_safe_capture(vid);
                            co_safe_capture(vcid);
                            co_safe_capture(backoff_secs);
                            util::debug::log_debug(L"NewUriRequested: Getting new links");
                            if (*backoff_secs > 0) {
                                co_await std::chrono::seconds(std::lround(*backoff_secs));
                            }
                            try {
                                auto video_pinfo = std::move(co_await client->video_play_url(bvid, cid, param));
                                auto& dash = *video_pinfo.dash;
                                auto vit = std::find_if(dash.video.begin(), dash.video.end(), [&](auto const& v) {
                                    return v.id == vid && v.codecid == vcid;
                                });
                                if (vit == dash.video.end()) {
                                    throw std::runtime_error("Video stream could not be found for refreshing");
                                }
                                *backoff_secs = 0;
                                co_return std::move(*vit);
                            }
                            catch (...) {
                                if (*backoff_secs == 0) { *backoff_secs = BACKOFF_INITIAL_SECS; }
                                else { *backoff_secs *= BACKOFF_FACTOR; }
                                throw;
                            }
                        }
                    );
                }
                else {
                    video_task = this->PlayVideoWithCidInner_DashNativeNative(video_dash, video_stream, nullptr);
                }
            }
            else {
                // Video + audio
                auto& audio_stream = video_dash.audio[0];
                if (m_cfg_model.App_UseHRASForVideo()) {
                    video_task = this->PlayVideoWithCidInner_DashNativeHras(video_dash, video_stream, audio_stream,
                        [client, bvid = video_bvid, cid, param,
                        backoff_secs = std::make_shared<double>(0),
                        vid = video_stream.id, vcid = video_stream.codecid,
                        aid = audio_stream.id, acid = audio_stream.codecid
                        ]() -> util::winrt::task<PlayUrlDashStreamPair> {
                            co_safe_capture(bvid);
                            co_safe_capture(cid);
                            co_safe_capture(param);
                            co_safe_capture(vid);
                            co_safe_capture(vcid);
                            co_safe_capture(aid);
                            co_safe_capture(acid);
                            co_safe_capture(backoff_secs);
                            util::debug::log_debug(L"NewUriRequested: Getting new links");
                            if (*backoff_secs > 0) {
                                co_await std::chrono::seconds(std::lround(*backoff_secs));
                            }
                            try {
                                auto video_pinfo = std::move(co_await client->video_play_url(bvid, cid, param));
                                auto& dash = *video_pinfo.dash;
                                auto vit = std::find_if(dash.video.begin(), dash.video.end(), [&](auto const& v) {
                                    return v.id == vid && v.codecid == vcid;
                                });
                                if (vit == dash.video.end()) {
                                    throw std::runtime_error("Video stream could not be found for refreshing");
                                }
                                auto ait = std::find_if(dash.audio.begin(), dash.audio.end(), [&](auto const& v) {
                                    return v.id == aid && v.codecid == acid;
                                });
                                if (ait == dash.audio.end()) {
                                    throw std::runtime_error("Audio stream could not be found for refreshing");
                                }
                                *backoff_secs = 0;
                                co_return{ std::move(*vit), std::move(*ait) };
                            }
                            catch (...) {
                                if (*backoff_secs == 0) { *backoff_secs = BACKOFF_INITIAL_SECS; }
                                else { *backoff_secs *= BACKOFF_FACTOR; }
                                throw;
                            }
                        }
                    );
                }
                else {
                    video_task = this->PlayVideoWithCidInner_DashNativeNative(video_dash, video_stream, &audio_stream);
                }
            }
        }
        else if (video_pinfo.durl) {
            if (auto tab = ::BiliUWP::App::get()->tab_from_page(*this)) {
                BiliUWP::SimpleContentDialog cd;
                cd.Title(box_value(L"Not Implemented"));
                cd.Content(box_value(L"FLV demuxing not implemented"));
                cd.CloseButtonText(L"Close");
                tab->show_dialog(cd);
            }
            throw hresult_not_implemented(L"FLV demuxing not implemented");
        }
        else {
            throw hresult_error(E_FAIL, L"Invalid video play url info");
        }

        auto fetch_vpart_meta_task = fetch_vpart_meta_fn();

        cancellation_token.callback([&] {
            video_task.cancel();
            fetch_vpart_meta_task.cancel();
        });

        std::tie(media_src, ds_provider) = co_await weak_store.ual(video_task);

        // TODO: Fix SMTC loop button control (ModernFlyouts has this functionality)

        // TODO: Add configurable support for heartbeat packages

        // TODO: Improve subtitle handling
        MediaPlayerStateOverlay().SwitchToLoading(res_str(L"App/Page/MediaPlayPage/LoadingSubtitles"));
        auto vpart_meta = std::move(co_await fetch_vpart_meta_task);
        media_src.ExternalTimedTextSources().ReplaceAll(vpart_meta.tts_vec);

        auto media_playback_item = MediaPlaybackItem(media_src);
        auto display_props = media_playback_item.GetDisplayProperties();
        display_props.Type(MediaPlaybackType::Video);
        display_props.Thumbnail(RandomAccessStreamReference::CreateFromUri(Uri(video_vinfo.cover_url)));
        {
            bool found = false;
            auto display_props_video_props = display_props.VideoProperties();
            display_props_video_props.Title(video_vinfo.title);
            for (auto const& i : video_vinfo.pages) {
                if (cid == i.cid) {
                    display_props_video_props.Subtitle(i.part_title);
                    found = true;
                    break;
                }
            }
            if (!found) {
                util::debug::log_error(std::format(L"Failed to find part title with given cid {}", cid));
            }
        }
        media_playback_item.ApplyDisplayProperties(display_props);
        this->SubmitMediaPlaybackSourceToNativePlayer(media_playback_item, nullptr, ds_provider);
        util::debug::log_trace(std::format(L"Video pic url: {}", video_vinfo.cover_url));
        util::debug::log_trace(std::format(L"Video title: {}", video_vinfo.title));
    }
    util::winrt::task<> MediaPlayPage::PlayVideoWithCid(uint64_t cid) {
        auto cancellation_token = co_await get_cancellation_token();
        cancellation_token.enable_propagation();

        auto media_player_state_overlay = MediaPlayerStateOverlay();
        media_player_state_overlay.SwitchToLoading(res_str(L"App/Common/Loading"));
        try { co_await this->PlayVideoWithCidInner(cid); }
        catch (::BiliUWP::BiliApiException const& e) {
            // TODO: We may transform some specific exceptions into user-friendly errors
            util::winrt::log_current_exception();
            media_player_state_overlay.SwitchToFailed(res_str(L"App/Page/MediaPlayPage/Error/ServerAPIFailed"));
            throw;
        }
        catch (hresult_canceled const&) { throw; }
        catch (...) {
            util::winrt::log_current_exception();
            media_player_state_overlay.SwitchToFailed(res_str(L"App/Page/MediaPlayPage/Error/Unknown"));
            throw;
        }
        media_player_state_overlay.SwitchToHidden();
    }
    util::winrt::task<> MediaPlayPage::UpdateAudioInfoInner(uint64_t auid) {
        auto cancellation_token = co_await get_cancellation_token();
        cancellation_token.enable_propagation();

        auto weak_store = util::winrt::make_weak_storage(*this);

        m_bili_res_is_ready = false;
        deferred([&weak_store] {
            if (!weak_store.lock()) { return; }
            weak_store->m_bili_res_is_ready = true;
        });
        m_media_info = std::monostate{};

        // Body
        auto client = ::BiliUWP::App::get()->bili_client();
        m_media_info = std::move(co_await weak_store.ual(client->audio_basic_info(auid)));
        auto& audio_vinfo = std::get<::BiliUWP::AudioBasicInfoResult>(m_media_info);
        m_bili_res_is_ready = true;
        auto up_card_info = std::move(co_await weak_store.ual(client->user_card_info(audio_vinfo.uid)));
        m_up_list.Clear();
        // NOTE: audio_vinfo.author is sometimes empty, so we use audio_vinfo.uname instead
        m_up_list.Append(make<MediaPlayPage_UpItem>(
            audio_vinfo.uname, up_card_info.card.face_url, audio_vinfo.uid));

        Bindings->Update();
    }
    util::winrt::task<> MediaPlayPage::UpdateAudioInfo(uint64_t auid) {
        auto cancellation_token = co_await get_cancellation_token();
        cancellation_token.enable_propagation();

        auto media_details_overlay = MediaDetailsOverlay();
        media_details_overlay.SwitchToLoading(res_str(L"App/Common/Loading"));
        try { co_await this->UpdateAudioInfoInner(auid); }
        catch (hresult_canceled const&) { throw; }
        catch (...) {
            util::winrt::log_current_exception();
            media_details_overlay.SwitchToFailed(res_str(L"App/Common/LoadFailed"));
            throw;
        }
        media_details_overlay.SwitchToHidden();
    }
    util::winrt::task<> MediaPlayPage::PlayAudioInner() {
        auto cancellation_token = co_await get_cancellation_token();
        cancellation_token.enable_propagation();

        auto weak_store = util::winrt::make_weak_storage(*this);

        // Body
        auto& audio_vinfo = std::get<::BiliUWP::AudioBasicInfoResult>(m_media_info);
        auto client = ::BiliUWP::App::get()->bili_client();

        std::shared_ptr<DetailedStatsProvider> ds_provider;

        this->SubmitMediaPlaybackSourceToNativePlayer(nullptr);

        auto audio_pinfo = std::move(co_await weak_store.ual(client->audio_play_url(
            audio_vinfo.auid, ::BiliUWP::AudioQualityParam::Lossless
        )));
        hstring audio_bps_str = L"<undefined>";
        for (auto const& i : audio_pinfo.qualities) {
            if (audio_pinfo.type == i.type) {
                audio_bps_str = i.bps;
                break;
            }
        }
        auto audio_uri = Uri(audio_pinfo.urls.at(0));
        auto http_stream = co_await weak_store.ual(BiliUWP::HttpRandomAccessStream::CreateAsync(
            audio_uri,
            m_http_client_m,
            HttpRandomAccessStreamBufferOptions::Full,
            0, false
        ));
        auto media_src = MediaSource::CreateFromStream(http_stream, http_stream.ContentType());

        // Make DetailedStatsProvider
        struct DetailedStatsProvider_AudioHRAS : DetailedStatsProvider {
            DetailedStatsProvider_AudioHRAS(
                BiliUWP::HttpRandomAccessStream hras, hstring audio_host,
                ::BiliUWP::AudioQuality audio_type, hstring audio_bps_str
            ) : m_hras(std::move(hras)), m_audio_host(std::move(audio_host)),
                m_audio_type(audio_type), m_audio_bps_str(std::move(audio_bps_str)) {}
            void InitStats(DetailedStatsContext* ctx) {
                using util::winrt::make_text_block;
                ctx->AddElement(L"Player Type", make_text_block(
                    L"NativePlayer <- NativeBuffering <- HRAS(Full)"
                ));
                ctx->AddElement(L"Audio Host", make_text_block(m_audio_host));
                ctx->AddElement(L"Connection Speed", m_conn_speed);
                ctx->AddElement(L"Network Activity", m_net_activity);
                ctx->AddElement(L"Mystery Text", m_mystery_text_tb);
            }
            std::optional<TimeSpan> DesiredUpdateInterval(void) {
                return std::chrono::milliseconds(500);
            }
            void TimerStarted(void) {
                m_hras.EnableMetricsCollection(true, 0);
                m_hras.GetMetrics(true);
            }
            void TimerStopped(void) {
                m_hras.EnableMetricsCollection(false, 0);
            }
            void UpdateStats(DetailedStatsContext* ctx) {
                using util::str::byte_size_to_str;
                using util::str::bit_size_to_str;
                static constexpr size_t MAX_POINTS = 60;
                auto metrics = m_hras.GetMetrics(true);
                auto add_point_fn = [](auto& container, std::decay_t<decltype(container)>::value_type value) {
                    if (container.size() >= MAX_POINTS) {
                        container.pop_front();
                    }
                    container.push_back(std::move(value));
                };
                if (metrics.ActiveConnectionsCount == 0 && metrics.DownloadedBytesDelta == 0) {
                    add_point_fn(
                        m_conn_speed_points,
                        m_conn_speed_points.empty() ? 0 : m_conn_speed_points.back()
                    );
                }
                else {
                    add_point_fn(m_conn_speed_points, metrics.InboundBitsPerSecond);
                }
                m_conn_speed.update([](uint64_t cur_val, uint64_t max_val) -> hstring {
                    return hstring(std::format(
                        L"{}ps / {}ps",
                        bit_size_to_str(cur_val),
                        bit_size_to_str(max_val)
                    ));
                }, m_conn_speed_points, MAX_POINTS);
                add_point_fn(m_net_activity_points, metrics.DownloadedBytesDelta);
                m_net_activity.update([](uint64_t cur_val, uint64_t max_val) -> hstring {
                    return hstring(
                        byte_size_to_str(cur_val, 1e2) + L" / " +
                        byte_size_to_str(max_val, 1e2)
                    );
                }, m_net_activity_points, MAX_POINTS);
                // Update mystery text
                m_mystery_text_tb.Text(hstring(std::format(
                    L"type:{}({}) c:{} rd:{} hrasb:{}/{}",
                    std::to_underlying(m_audio_type), m_audio_bps_str,
                    metrics.ActiveConnectionsCount, metrics.SentRequestsDelta,
                    metrics.UsedBufferSize, metrics.AllocatedBufferSize
                )));
            }
        private:
            BiliUWP::HttpRandomAccessStream m_hras;
            hstring m_audio_host;
            ::BiliUWP::AudioQuality m_audio_type;
            hstring m_audio_bps_str;
            PolylineWithTextElemForDetailedStats m_conn_speed;
            std::deque<uint64_t> m_conn_speed_points;
            PolylineWithTextElemForDetailedStats m_net_activity;
            std::deque<uint64_t> m_net_activity_points;
            TextBlock m_mystery_text_tb;
        };
        ds_provider = std::make_shared<DetailedStatsProvider_AudioHRAS>(
            std::move(http_stream), audio_uri.Host(), audio_pinfo.type, std::move(audio_bps_str));

        auto media_playback_item = MediaPlaybackItem(media_src);
        auto display_props = media_playback_item.GetDisplayProperties();
        display_props.Type(MediaPlaybackType::Music);
        display_props.Thumbnail(RandomAccessStreamReference::CreateFromUri(Uri(audio_vinfo.cover_url)));
        display_props.MusicProperties().Title(audio_vinfo.audio_title);
        media_playback_item.ApplyDisplayProperties(display_props);
        this->SubmitMediaPlaybackSourceToNativePlayer(
            media_playback_item, BitmapImage(Uri(audio_vinfo.cover_url)), ds_provider
        );
        util::debug::log_trace(std::format(L"Audio pic url: {}", audio_vinfo.cover_url));
        util::debug::log_trace(std::format(L"Audio title: {}", audio_vinfo.audio_title));
    }
    util::winrt::task<> MediaPlayPage::PlayAudio() {
        auto cancellation_token = co_await get_cancellation_token();
        cancellation_token.enable_propagation();

        auto media_player_state_overlay = MediaPlayerStateOverlay();
        media_player_state_overlay.SwitchToLoading(res_str(L"App/Common/Loading"));
        try { co_await this->PlayAudioInner(); }
        catch (::BiliUWP::BiliApiException const& e) {
            // TODO: We may transform some specific exceptions into user-friendly errors
            util::winrt::log_current_exception();
            media_player_state_overlay.SwitchToFailed(res_str(L"App/Page/MediaPlayPage/Error/ServerAPIFailed"));
            throw;
        }
        catch (hresult_canceled const&) { throw; }
        catch (...) {
            util::winrt::log_current_exception();
            media_player_state_overlay.SwitchToFailed(res_str(L"App/Page/MediaPlayPage/Error/Unknown"));
            throw;
        }
        media_player_state_overlay.SwitchToHidden();
    }

    void MediaPlayPage::SubmitMediaPlaybackSourceToNativePlayer(
        IMediaPlaybackSource const& source,
        ImageSource const& poster_source,
        std::shared_ptr<DetailedStatsProvider> ds_provider
    ) {
        auto media_player_elem = MediaPlayerElem();

        if (source == nullptr) {
            // Release MediaPlayer and related resources
            media_player_elem.AreTransportControlsEnabled(false);
            auto player = media_player_elem.MediaPlayer();
            media_player_elem.SetMediaPlayer(nullptr);
            if (player && player.Source()) {
                auto session = player.PlaybackSession();
                if (session && session.CanPause()) {
                    player.Pause();
                }
            }
            media_player_elem.PosterSource(nullptr);
            // Release detailed stats provider
            if (m_detailed_stats_update_timer) {
                m_detailed_stats_update_timer.Stop();
                m_detailed_stats_provider->TimerStopped();
                m_detailed_stats_update_timer = nullptr;
                m_detailed_stats_provider = nullptr;
            }
            return;
        }

        if (ds_provider == nullptr) {
            // Use a default provider to indicate that detailed stats are not supported
            ds_provider = std::make_shared<DetailedStatsProvider>();
        }

        auto media_player = MediaPlayer();
        // TODO: Handle App_UseCustomVideoPresenter
        if (::BiliUWP::App::get()->cfg_model().App_UseCustomVideoPresenter()) {
            media_player.IsVideoFrameServerEnabled(true);
        }
        media_player.AudioCategory(MediaPlayerAudioCategory::Media);
        media_player.Source(source);
        // TODO: Add individual volume support (+ optionally sync to global)
        this->EstablishMediaPlayerVolumeTwoWayBinding(media_player);
        media_player.IsLoopingEnabled(true);
        // TODO: Maybe (?) set some of the handlers in XAML?
        // Workaround a MediaPlayer resource leak bug by keeping MediaPlayer alive until media is actually opened
        // (at the cost of wasting some data)
        auto et_mo = std::make_shared_for_overwrite<event_token>();
        *et_mo = media_player.MediaOpened(
            [et_mo, strong_this = this->get_strong()](MediaPlayer const& sender, IInspectable const&) {
                util::debug::log_trace(L"Media opened");
                sender.MediaOpened(*et_mo);
            }
        );
        media_player.MediaFailed([](MediaPlayer const& sender, MediaPlayerFailedEventArgs const& e) {
            auto hresult = e.ExtendedErrorCode();
            util::debug::log_trace(std::format(
                L"Media failed: {}: {} (0x{:08x}: {})",
                std::to_underlying(e.Error()),
                e.ErrorMessage(),
                static_cast<uint32_t>(hresult),
                hresult_error(hresult).message()
            ));
        });
        media_player_elem.SetMediaPlayer(media_player);
        media_player_elem.PosterSource(poster_source);
        media_player_elem.AreTransportControlsEnabled(true);

        // Set up detailed stats
        auto ds_ctx = DetailedStatsContext(this);
        m_detailed_stats_provider = ds_provider;
        ds_ctx.ClearElements();
        m_detailed_stats_provider->InitStats(&ds_ctx);
        if (auto interval = m_detailed_stats_provider->DesiredUpdateInterval()) {
            m_detailed_stats_update_timer = DispatcherTimer();
            m_detailed_stats_update_timer.Interval(*interval);
            m_detailed_stats_update_timer.Tick(
                [ds_ctx = std::move(ds_ctx), ds_provider = std::move(ds_provider)]
                (IInspectable const&, IInspectable const&) mutable {
                    ds_provider->UpdateStats(&ds_ctx);
                }
            );
            if (MediaDetailedStatsToggleMenuItem().IsChecked()) {
                m_detailed_stats_update_timer.Start();
                m_detailed_stats_provider->TimerStarted();
            }
        }
    }
    void MediaPlayPage::TriggerMediaDetailedStatsUpdate(void) {
        if (m_detailed_stats_provider) {
            auto ds_ctx = DetailedStatsContext(this);
            m_detailed_stats_provider->UpdateStats(&ds_ctx);
        }
    }
    void MediaPlayPage::EstablishMediaPlayerVolumeTwoWayBinding(MediaPlayer const& player) {
        player.Volume(::BiliUWP::App::get()->cfg_model().App_GlobalVolume() / 10000.0);
        // MediaPlayer -> GlobalConfig
        m_volume_changed_revoker = player.VolumeChanged(auto_revoke,
            [dispatcher = Dispatcher()](MediaPlayer const& sender, IInspectable const&) {
                // NOTE: Currently, in order to correctly broadcast PropertyChanged to `x:Bind`s,
                //       we must manually switch to UI threads
                dispatcher.RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal,
                    [volume = static_cast<uint32_t>(std::llround(sender.Volume() * 10000))] {
                        ::BiliUWP::App::get()->cfg_model().App_GlobalVolume(volume);
                    }
                );
            }
        );
        // GlobalConfig -> MediaPlayer
        m_cfg_changed_revoker = ::BiliUWP::App::get()->cfg_model().PropertyChanged(auto_revoke,
            [weak_mp = make_weak(player)](IInspectable const&, PropertyChangedEventArgs const& e) {
                auto strong_mp = weak_mp.get();
                if (!strong_mp) { return; }
                if (e.PropertyName() == L"App_GlobalVolume") {
                    strong_mp.Volume(::BiliUWP::App::get()->cfg_model().App_GlobalVolume() / 10000.0);
                }
            }
        );
    }
}
