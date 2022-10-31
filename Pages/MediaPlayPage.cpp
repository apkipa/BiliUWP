#include "pch.h"
#include "MediaPlayPage.h"
#if __has_include("MediaPlayPage.g.cpp")
#include "MediaPlayPage.g.cpp"
#endif
#include "MediaPlayPage_UpItem.g.cpp"
#include "HttpRandomAccessStream.h"
#include "App.h"
#include <deque>

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::UI;
using namespace Windows::UI::Text;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Data;
using namespace Windows::UI::Xaml::Media;
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
        m_http_client(nullptr), m_bili_res_is_ready(false),
        m_up_list(single_threaded_observable_vector<IInspectable>()),
        m_detailed_stats_update_timer(nullptr)
    {
        using namespace Windows::Web::Http;
        using namespace Windows::Web::Http::Filters;

        MediaPlayPageT::InitializeComponent();

        auto http_filter = HttpBaseProtocolFilter();
        auto cache_control = http_filter.CacheControl();
        cache_control.ReadBehavior(HttpCacheReadBehavior::NoCache);
        cache_control.WriteBehavior(HttpCacheWriteBehavior::NoCache);
        http_filter.AllowAutoRedirect(false);
        m_http_client = HttpClient(http_filter);
        auto http_client_drh = m_http_client.DefaultRequestHeaders();
        auto user_agent = http_client_drh.UserAgent();
        user_agent.Clear();
        user_agent.ParseAdd(L"Mozilla/5.0 BiliDroid/6.4.0 (bbcallen@gmail.com)");
        http_client_drh.Referer(Uri(L"https://www.bilibili.com"));
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
    }
    fire_forget_except MediaPlayPage::final_release(std::unique_ptr<MediaPlayPage> ptr) noexcept {
        // Perform time-consuming destruction of media player in background;
        // prevents UI from freezing
        using namespace std::chrono_literals;
        ptr->m_cur_async.cancel_running();
        util::debug::log_trace(L"Final release");
        auto dispatcher = ptr->Dispatcher();
        MediaPlayer player{ nullptr };
        auto cleanup_mediaplayer_fn = [&] {
            ptr->MediaPlayerElem().AreTransportControlsEnabled(false);
            player = ptr->MediaPlayerElem().MediaPlayer();
            ptr->MediaPlayerElem().SetMediaPlayer(nullptr);
            if (player && player.Source()) {
                auto session = player.PlaybackSession();
                if (session && session.CanPause()) {
                    player.Pause();
                }
            }
        };
        // Ensure we are on the UI thread (MediaPlayerElem may hold a reference
        // in a non-UI thread)
        co_await dispatcher;
        // Clean up UpListView
        ptr->UpListView().ItemsSource(nullptr);
        // Clean up MediaPlayer
        cleanup_mediaplayer_fn();
        // Clean up DetailedStatsTimer
        if (ptr->m_detailed_stats_update_timer) {
            ptr->m_detailed_stats_update_timer.Stop();
            ptr->m_detailed_stats_provider->TimerStopped();
        }
        // TODO: Is double check really needed?
        // Try to wait for async operations to stop completely
        /*
        co_await 3s;
        player = nullptr;
        co_await dispatcher;
        cleanup_mediaplayer_fn();
        */
        co_await resume_background();
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
        m_up_list.Clear();
        m_up_list.Append(make<MediaPlayPage_UpItem>(
            video_vinfo.owner.name, video_vinfo.owner.face_url, video_vinfo.owner.mid
            ));

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
    util::winrt::task<> MediaPlayPage::PlayVideoWithCidInner(uint64_t cid) {
        auto cancellation_token = co_await get_cancellation_token();
        cancellation_token.enable_propagation();

        auto weak_store = util::winrt::make_weak_storage(*this);

        // Body
        auto& video_vinfo = std::get<::BiliUWP::VideoViewInfoResult>(m_media_info);
        auto client = ::BiliUWP::App::get()->bili_client();

        std::shared_ptr<DetailedStatsProvider> ds_provider;

        this->SubmitMediaPlaybackSourceToNativePlayer(nullptr);

        ::BiliUWP::VideoPlayUrlPreferenceParam param{
            .prefer_dash = true, .prefer_hdr = true, .prefer_4k = true,
            .prefer_dolby = true, .prefer_8k = true, .prefer_av1 = false
        };
        auto video_pinfo = std::move(co_await weak_store.ual(client->video_play_url(
            video_vinfo.bvid, cid, param
        )));
        MediaSource media_src{ nullptr };
        // NOTE: Optimize control flow for dash streams
        if (video_pinfo.dash) {
            auto* pvideo_stream = &video_pinfo.dash->video.at(0);
            auto* paudio_stream = &video_pinfo.dash->audio.at(0);
            // TODO: Deal with videos without audio streams in the future
            //auto* paudio_stream = video_pinfo.dash->audio.empty() ? nullptr : &video_pinfo.dash->audio[0];
            for (auto& i : video_pinfo.dash->video) {
                // Skip avc codec
                if (i.codecid == 7) { continue; }
                pvideo_stream = &i;
                break;
            }
            auto& video_stream = *pvideo_stream;
            auto& audio_stream = *paudio_stream;
            util::debug::log_trace(std::format(L"Selecting video stream {}", video_stream.id));
            util::debug::log_trace(std::format(L"Selecting audio stream {}", audio_stream.id));
            // TODO: Add support for backup url
            // Generate Dash MPD from parsed dash info on the fly
            // Dash MPD specification: refer to ISO IEC 23009-1
            // NOTE: minBufferTime type: xs:duration ("PT<Time description>")
            auto dash_mpd_str = std::format(LR"(<?xml version="1.0" encoding="utf-8"?>)"
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
                video_pinfo.dash->min_buffer_time,
                video_stream.id, video_stream.bandwidth, video_stream.mime_type, video_stream.codecs,
                video_stream.start_with_sap, video_stream.sar, video_stream.frame_rate,
                video_stream.width, video_stream.height,
                video_stream.segment_base.index_range, video_stream.segment_base.initialization,
                audio_stream.id, audio_stream.bandwidth, audio_stream.mime_type, audio_stream.codecs,
                audio_stream.start_with_sap,
                audio_stream.segment_base.index_range, audio_stream.segment_base.initialization
            );
            util::debug::log_trace(std::format(L"Generated dash: {}", dash_mpd_str));
            auto dash_mpd_mem_stream = InMemoryRandomAccessStream();
            co_await dash_mpd_mem_stream.WriteAsync(
                CryptographicBuffer::ConvertStringToBinary(dash_mpd_str, BinaryStringEncoding::Utf8)
            );
            dash_mpd_mem_stream.Seek(0);
            auto adaptive_media_src_result = co_await AdaptiveMediaSource::CreateFromStreamAsync(
                dash_mpd_mem_stream,
                Uri(L"about:blank"),
                L"application/dash+xml",
                m_http_client
            );
            if (adaptive_media_src_result.Status() != AdaptiveMediaSourceCreationStatus::Success) {
                auto hresult = adaptive_media_src_result.ExtendedError();
                throw hresult_error(E_FAIL, std::format(L"Dash parse failed: {} (0x{:08x}: {})",
                    std::to_underlying(adaptive_media_src_result.Status()),
                    static_cast<uint32_t>(hresult),
                    hresult_error(hresult).message()
                ));
            }
            auto adaptive_media_src = adaptive_media_src_result.MediaSource();

            // Make DetailedStatsProvider
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
                    weak_ref<AdaptiveMediaSource> adaptive_media_src,
                    hstring video_mime, hstring audio_mime,
                    hstring video_codec, hstring audio_codec,
                    hstring resolution,
                    hstring video_host, hstring audio_host,
                    std::shared_ptr<shared_stats_data> shared_data
                ) : m_adaptive_media_src(std::move(adaptive_media_src)),
                    m_video_mime(std::move(video_mime)), m_audio_mime(std::move(audio_mime)),
                    m_video_codec(std::move(video_codec)), m_audio_codec(std::move(audio_codec)),
                    m_resolution(std::move(resolution)),
                    m_video_host(std::move(video_host)), m_audio_host(std::move(audio_host)),
                    m_shared_data(std::move(shared_data)) {}
                void InitStats(DetailedStatsContext* ctx) {
                    using util::winrt::make_text_block;
                    ctx->AddElement(L"Mime Type", make_text_block(hstring(std::format(
                        L"{};codecs=\"{}\" | {};codecs=\"{}\"",
                        m_video_mime, m_video_codec,
                        m_audio_mime, m_audio_codec
                    ))));
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
                weak_ref<AdaptiveMediaSource> m_adaptive_media_src;

                hstring m_video_mime, m_audio_mime;
                hstring m_video_codec, m_audio_codec;
                hstring m_resolution;
                hstring m_video_host, m_audio_host;

                std::shared_ptr<shared_stats_data> m_shared_data;
                PolylineWithTextElemForDetailedStats m_conn_speed;
                std::deque<uint64_t> m_conn_speed_points;
                PolylineWithTextElemForDetailedStats m_net_activity;
                std::deque<uint64_t> m_net_activity_points;
                TextBlock m_mystery_text_tb;
            };
            ds_provider = std::make_shared<DetailedStatsProvider_AdaptiveMediaSource>(
                make_weak(adaptive_media_src),
                video_stream.mime_type, audio_stream.mime_type,
                video_stream.codecs, audio_stream.codecs,
                hstring(std::format(L"{}x{}@{}", video_stream.width, video_stream.height, video_stream.frame_rate)),
                Uri(video_stream.base_url).Host(), Uri(audio_stream.base_url).Host(),
                shared_data
                );

            // TODO: Add support for DownloadFailed event (refresh urls)

#if 0
            // Use uri replacing
            adaptive_media_src.DownloadRequested(
                [vuri = Uri(video_stream.base_url), auri = Uri(audio_stream.base_url)]
            (AdaptiveMediaSource const&, AdaptiveMediaSourceDownloadRequestedEventArgs const& e) {
                    if (e.ResourceContentType().starts_with(L"video")) {
                        e.Result().ResourceUri(vuri);
                    }
                    else if (e.ResourceContentType().starts_with(L"audio")) {
                        e.Result().ResourceUri(auri);
                    }
                    else {
                        util::debug::log_error(L"Suspicious: Hacked nothing");
                    }
                }
            );
#else
            // Use buffer replacing (may waste some memory)
            // TODO: Add option to switch to HRAS backend
            adaptive_media_src.DownloadRequested(
                [http_client = m_http_client, vuri = Uri(video_stream.base_url), auri = Uri(audio_stream.base_url),
                shared_data = std::move(shared_data)]
            (AdaptiveMediaSource const&, AdaptiveMediaSourceDownloadRequestedEventArgs const& e) -> IAsyncAction {
                    const Uri* target_uri;
                    if (e.ResourceContentType().starts_with(L"video")) { target_uri = &vuri; }
                    else if (e.ResourceContentType().starts_with(L"audio")) { target_uri = &auri; }
                    else {
                        util::debug::log_error(L"Suspicious: Hacked nothing");
                        co_return;
                    }
                    auto content_size = e.ResourceByteRangeLength().as<uint64_t>();
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
                    auto read_op = util::winrt::fetch_partial_http_as_buffer(
                        *target_uri, http_client,
                        e.ResourceByteRangeOffset().as<uint64_t>(),
                        content_size
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
                    e.Result().Buffer(co_await read_op);
                }
            );
#endif
            media_src = MediaSource::CreateFromAdaptiveMediaSource(adaptive_media_src);
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

        // TODO: Fix SMTC loop button control (ModernFlyouts has this functionality)

        // TODO: Add configurable support for heartbeat packages

        auto media_player = MediaPlayer();
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
        m_up_list.Append(make<MediaPlayPage_UpItem>(
            audio_vinfo.author, up_card_info.card.face_url, audio_vinfo.uid
            ));

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
        // TODO: Switch to HttpRandomAccessStreamBufferOptions::Full
        auto audio_uri = Uri(audio_pinfo.urls.at(0));
        auto http_stream = co_await weak_store.ual(HttpRandomAccessStream::CreateAsync(
            audio_uri,
            m_http_client,
            HttpRandomAccessStreamBufferOptions::Full,
            0, false
        ));
        auto media_src = MediaSource::CreateFromStream(http_stream, http_stream.ContentType());

        // Make DetailedStatsProvider
        struct DetailedStatsProvider_AudioHRAS : DetailedStatsProvider {
            DetailedStatsProvider_AudioHRAS(BiliUWP::HttpRandomAccessStream hras, hstring audio_host) :
                m_hras(std::move(hras)), m_audio_host(std::move(audio_host)) {}
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
                    L"c:{} rd:{} hrasb:{}/{}",
                    metrics.ActiveConnectionsCount, metrics.SentRequestsDelta,
                    metrics.UsedBufferSize, metrics.AllocatedBufferSize
                )));
            }
        private:
            BiliUWP::HttpRandomAccessStream m_hras;
            hstring m_audio_host;
            PolylineWithTextElemForDetailedStats m_conn_speed;
            std::deque<uint64_t> m_conn_speed_points;
            PolylineWithTextElemForDetailedStats m_net_activity;
            std::deque<uint64_t> m_net_activity_points;
            TextBlock m_mystery_text_tb;
        };
        ds_provider = std::make_shared<DetailedStatsProvider_AudioHRAS>(std::move(http_stream), audio_uri.Host());

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
            media_player_elem.SetMediaPlayer(nullptr);
            media_player_elem.PosterSource(nullptr);
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
