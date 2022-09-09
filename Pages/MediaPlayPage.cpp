#include "pch.h"
#include "MediaPlayPage.h"
#if __has_include("MediaPlayPage.g.cpp")
#include "MediaPlayPage.g.cpp"
#endif
#include "App.h"

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Media::Imaging;
using namespace Windows::Media;
using namespace Windows::Media::Core;
using namespace Windows::Media::Playback;
using namespace Windows::Media::Streaming::Adaptive;
using namespace Windows::Storage::Streams;
using namespace Windows::Security::Cryptography;

using ::BiliUWP::App::res_str;

namespace winrt::BiliUWP::implementation {
    MediaPlayPage::MediaPlayPage() :
        m_http_client(nullptr),
        m_bili_res_is_ready(false), m_bili_res_id_a(), m_bili_res_id_b()
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
        cleanup_mediaplayer_fn();
        // TODO: Is this really needed?
        // Try to wait for async operations to stop completely
        co_await 3s;
        player = nullptr;
        co_await dispatcher;
        cleanup_mediaplayer_fn();
        co_await resume_background();
    }
    void MediaPlayPage::OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs const& e) {
        auto tab = ::BiliUWP::App::get()->tab_from_page(*this);
        tab->set_icon(Symbol::Play);
        tab->set_title(res_str(L"App/Page/MediaPlayPage/Title"));

        if (auto opt = e.Parameter().try_as<MediaPlayPageNavParam>()) {
            // TODO: Parse media
            switch (opt->type) {
            case MediaPlayPage_MediaType::Video:
                m_cur_async.cancel_and_run(&MediaPlayPage::HandleVideoPlay, this, opt->nid, opt->sid);
                break;
            case MediaPlayPage_MediaType::Audio:
                m_cur_async.cancel_and_run(&MediaPlayPage::HandleAudioPlay, this, opt->nid);
                break;
            default:
                util::debug::log_error(L"Media type not recognized");
                MediaPlayerInfoText().Text(res_str(L"App/Page/MediaPlayPage/Error/IllegalNavParam"));
                MediaPlayerInfoText().Visibility(Visibility::Visible);
                break;
            }
        }
        else {
            util::debug::log_error(L"OnNavigatedTo only expects MediaPlayPageNavParam");
            MediaPlayerInfoText().Text(res_str(L"App/Page/MediaPlayPage/Error/IllegalNavParam"));
            MediaPlayerInfoText().Visibility(Visibility::Visible);
        }
    }
    Windows::Foundation::IAsyncAction MediaPlayPage::HandleVideoPlay(uint64_t avid, hstring bvid) {
        // TODO: Finish HandleVideoPlay

        auto cancellation_token = co_await get_cancellation_token();
        cancellation_token.enable_propagation();

        auto weak_store = util::winrt::make_weak_storage(*this);

        deferred([&weak_store] {
            if (!weak_store.lock()) { return; }
            weak_store->m_bili_res_is_ready = true;
        });

        auto media_player_info_text = MediaPlayerInfoText();
        auto http_client = m_http_client;

        try {
            auto client = ::BiliUWP::App::get()->bili_client();
            ::BiliUWP::VideoViewInfoResult video_vinfo{};
            if (bvid != L"") {
                util::debug::log_trace(std::format(L"Parsing video {}...", bvid));
                video_vinfo = std::move(co_await client->video_view_info(bvid));
            }
            else {
                util::debug::log_trace(std::format(L"Parsing video av{}...", avid));
                video_vinfo = std::move(co_await client->video_view_info(avid));
            }
            if (!weak_store.lock()) { co_return; }
            m_bili_res_id_a = L"av" + to_hstring(video_vinfo.avid);
            m_bili_res_id_b = video_vinfo.bvid;
            m_bili_res_is_ready = true;
            weak_store.unlock();

            ::BiliUWP::VideoPlayUrlPreferenceParam param;
            param.prefer_4k = true;
            param.prefer_av1 = false;
            param.prefer_dash = true;
            param.prefer_hdr = true;
            auto video_pinfo = std::move(co_await client->video_play_url(
                video_vinfo.avid, video_vinfo.cid_1p, param
            ));
            MediaSource media_src{ nullptr };
            // NOTE: Optimize control flow for dash streams
            if (video_pinfo.dash) {
                auto& video_stream = video_pinfo.dash->video[0];
                auto& audio_stream = video_pinfo.dash->audio[0];
                util::debug::log_trace(std::format(L"Selecting video stream {}", video_stream.id));
                util::debug::log_trace(std::format(L"Selecting audio stream {}", audio_stream.id));
                // TODO: Add support for backup url
                // Generate Dash MPD from parsed dash info on the fly
                // Dash MPD specification: refer to ISO IEC 23009-1
                // NOTE: minBufferTime type: xs:duration ("PT<Time description>")
                auto dash_mpd_str = std::format(LR"(<?xml version="1.0" encoding="utf-8"?>
<MPD xmlns="urn:mpeg:dash:schema:mpd:2011" profiles="urn:mpeg:dash:profile:isoff-on-demand:2011" minBufferTime="PT{}S" type="static">
<Period start="PT0S">
<AdaptationSet contentType="video">
<Representation id="{}" bandwidth="{}" mimeType="{}" codecs="{}" startWithSAP="{}" sar="{}" frame_rate="{}" width="{}" height="{}">
<SegmentBase indexRange="{}"><Initialization range="{}"/></SegmentBase>
</Representation>
</AdaptationSet>
<AdaptationSet contentType="audio">
<Representation id="{}" bandwidth="{}" mimeType="{}" codecs="{}" startWithSAP="{}">
<SegmentBase indexRange="{}"><Initialization range="{}"/></SegmentBase>
</Representation>
</AdaptationSet>
</Period>
</MPD>)",
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
                    Uri(L"https://example.com"),
                    L"application/dash+xml",
                    http_client
                );
                if (adaptive_media_src_result.Status() != AdaptiveMediaSourceCreationStatus::Success) {
                    auto hresult = adaptive_media_src_result.ExtendedError();
                    throw hresult_error(E_FAIL, std::format(L"Dash parse failed: {} (0x{:08x}: {})",
                        util::misc::enum_to_int(adaptive_media_src_result.Status()),
                        static_cast<uint32_t>(hresult),
                        hresult_error(hresult).message()
                    ));
                }
                auto adaptive_media_src = adaptive_media_src_result.MediaSource();

                // TODO: Add support for DownloadFailed event (refresh urls)

                adaptive_media_src.DownloadRequested(
                    [=](AdaptiveMediaSource const&, AdaptiveMediaSourceDownloadRequestedEventArgs const& e) {
                        if (e.ResourceContentType().starts_with(L"video")) {
                            //util::debug::log_trace(L"Hack video url");
                            e.Result().ResourceUri(Uri(video_stream.base_url));
                        }
                        else if (e.ResourceContentType().starts_with(L"audio")) {
                            //util::debug::log_trace(L"Hack audio url");
                            e.Result().ResourceUri(Uri(audio_stream.base_url));
                        }
                        else {
                            util::debug::log_trace(L"Suspicious: Hacked nothing");
                        }
                    }
                );
                media_src = MediaSource::CreateFromAdaptiveMediaSource(adaptive_media_src);
            }
            else if (video_pinfo.durl) {
                throw hresult_not_implemented();
            }
            else {
                throw hresult_error(E_FAIL, L"Invalid video play url info");
            }

            // TODO: Fix SMTC loop button control (ModernFlyouts has this functionality)

            // TODO: Add configurable support for heartbeat packages

            if (!weak_store.lock()) { co_return; }
            auto media_player = MediaPlayer();
            auto media_playback_item = MediaPlaybackItem(media_src);
            auto display_props = media_playback_item.GetDisplayProperties();
            display_props.Type(MediaPlaybackType::Video);
            display_props.Thumbnail(RandomAccessStreamReference::CreateFromUri(Uri(video_vinfo.cover_url)));
            display_props.VideoProperties().Title(video_vinfo.title);
            display_props.VideoProperties().Subtitle(video_vinfo.pages[0].part_title);
            media_playback_item.ApplyDisplayProperties(display_props);
            media_player.Source(media_playback_item);
            // TODO: Remove volume = 0.02
            media_player.Volume(0.02);
            media_player.IsLoopingEnabled(true);
            MediaPlayerElem().SetMediaPlayer(media_player);
            util::debug::log_trace(std::format(L"Video pic url: {}", video_vinfo.cover_url));
            util::debug::log_trace(std::format(L"Video title: {}", video_vinfo.title));

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
                    util::misc::enum_to_int(e.Error()),
                    e.ErrorMessage(),
                    static_cast<uint32_t>(hresult),
                    hresult_error(hresult).message()
                ));
            });

            MediaPlayerElem().AreTransportControlsEnabled(true);
        }
        catch (::BiliUWP::BiliApiException const& e) {
            // TODO: We may transform some specific exceptions into user-friendly errors
            util::winrt::log_current_exception();
            media_player_info_text.Text(res_str(L"App/Page/MediaPlayPage/Error/ServerAPIFailed"));
            media_player_info_text.Visibility(Visibility::Visible);
        }
        catch (hresult_canceled const&) { co_return; }
        catch (...) {
            util::winrt::log_current_exception();
            media_player_info_text.Text(res_str(L"App/Page/MediaPlayPage/Error/Unknown"));
            media_player_info_text.Visibility(Visibility::Visible);
        }
    }
    Windows::Foundation::IAsyncAction MediaPlayPage::HandleAudioPlay(uint64_t auid) {
        // TODO: Finish HandleAudioPlay

        auto cancellation_token = co_await get_cancellation_token();
        cancellation_token.enable_propagation();

        {
            using namespace std::chrono_literals;
            util::debug::log_error(L"Not implemented");
            BiliUWP::SimpleContentDialog cd;
            cd.Title(box_value(L"Not Implemented"));
            cd.CloseButtonText(L"Close");
            auto app = ::BiliUWP::App::get();
            app->tab_from_page(*this)->show_dialog(cd);
        }
    }
}
