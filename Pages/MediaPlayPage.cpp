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

/* Unrelated notes:
* 1.
* IMFMediaEngine (inside MFMediaEngine.dll, which is used by Windows.Media.Playback.MediaPlayer)
* registers for CoreWindow.VisibilityChanged event, and posts tasks via MFPutWorkItem to call
* CMediaEngine::OnAppVisibilityChanged. GPU priority is adjusted accordingly, and
* CSimpleTimer::SetTimer is used to set up a 20s timer, which, when expires, calls
* CMediaEngine::HandleThinningTimer to throttle video decoding. This is achieved by calling
* CMediaEngine::SetPlaybackRateInternal, with the second parameter (r8: bool: should_throttle)
* set to true. (-> CMFPOuterPlayer::SetRateEx -> CMFPInnerPlayer::SetRate ->
*     CSetRateOperation::Start -> CSetRateOperation::StartState ->
*     IMFRateControl::SetRate(fThin = true))
* 2.
* Windows.Media.Playback.MediaPlayer allocates a new SMTC by doing Win32 tricks and creating
* a native hidden Win32 window via CreateWindowEx (named "MediaPlayer SMTC window - {GUID}").
* It then receives the SMTC by calling ISystemMediaTransportControlsInterop::GetForWindow.
* 3.
* MediaPlayerElement implements media scrubbing by setting playback rate to zero.
*/

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
    // NOTE: trait, shared_ptr-like object
    // NOTE: Supports automatic fetch & cache & filter of media source input streams
    template<typename T>
    struct MediaSourceInputContext {
        /*
        * type Input;
        * 
        * fn get_input(&self) -> Future<Output = Input>;
        * fn purge_cache(&self);
        * fn has_cache(&self) -> bool;
        */
    };

    struct MSICInput_DashStream {
        uint64_t duration;
        double min_buffer_time;
        ::BiliUWP::VideoPlayUrl_Dash_Stream const& vstream;
        ::BiliUWP::VideoPlayUrl_Dash_Stream const& astream;
    };
    struct MSICInput_DashStreamNoAudio {
        uint64_t duration;
        double min_buffer_time;
        ::BiliUWP::VideoPlayUrl_Dash_Stream const& vstream;
    };
    template<>
    struct MediaSourceInputContext<MSICInput_DashStream> {
        using Input = MSICInput_DashStream;

        // Returns (vstream, astream)
        using SelectPolicyFnRet = std::pair<
            ::BiliUWP::VideoPlayUrl_Dash_Stream const&,
            ::BiliUWP::VideoPlayUrl_Dash_Stream const&>;
        using SelectPolicyFn = SelectPolicyFnRet(::BiliUWP::VideoPlayUrl_Dash const&);

        struct InputCore {
            InputCore(
                ::BiliUWP::BiliClient* client,
                uint64_t avid,
                uint64_t cid,
                ::BiliUWP::VideoPlayUrlPreferenceParam prefers,
                std::function<SelectPolicyFn> const& select_policy_fn,
                ::BiliUWP::VideoPlayUrl_Dash const* initial_data = nullptr
            ) : m_data(std::make_shared<data>())
            {
                if (!client) { throw hresult_invalid_argument(L"client is nullptr"); }
                m_data->has_cache = false;
                m_data->select_policy_fn = select_policy_fn;
                m_data->client = client;
                m_data->avid = avid;
                m_data->cid = cid;
                m_data->prefers = prefers;
                if (initial_data) {
                    m_data->dash = *initial_data;

                    m_data->has_cache = true;
                }
            }

            // TODO: Handle concurrent calls
            util::winrt::task<Input> get_input() const {
                auto cancellation_token = co_await get_cancellation_token();
                cancellation_token.enable_propagation();

                auto strong_data = m_data;

                auto const& dash = strong_data->dash;
                const bool should_update_cache = !strong_data->has_cache;
                if (should_update_cache) {
                    co_await strong_data->async.run_if_idle([](std::shared_ptr<data> data) -> util::winrt::task<> {
                        data->dash = std::move((co_await data->client->video_play_url(
                            data->avid, data->cid, data->prefers
                        )).dash.value());
                    }, strong_data);
                }
                auto [vstream, astream] = strong_data->select_policy_fn(dash);

                if (should_update_cache) {
                    strong_data->has_cache = true;
                }
                co_return Input{
                    .duration = dash.duration,
                    .min_buffer_time = dash.min_buffer_time,
                    .vstream = vstream,
                    .astream = astream,
                };
            }

            void purge_cache() const {
                m_data->has_cache = false;
            }

            bool has_cache() const { return m_data->has_cache; }

        private:
            struct data {
                ::BiliUWP::BiliClient* client;
                uint64_t avid;
                uint64_t cid;
                ::BiliUWP::VideoPlayUrlPreferenceParam prefers;
                bool has_cache;     // Cache possibly expensive fetch results
                std::function<SelectPolicyFn> select_policy_fn;
                ::BiliUWP::VideoPlayUrl_Dash dash;

                util::winrt::typed_task_storage<> async;
            };

            std::shared_ptr<data> m_data;
        };

        MediaSourceInputContext(
            ::BiliUWP::BiliClient* client,
            uint64_t avid,
            uint64_t cid,
            ::BiliUWP::VideoPlayUrlPreferenceParam prefers,
            std::function<SelectPolicyFn> const& select_policy_fn,
            ::BiliUWP::VideoPlayUrl_Dash const* initial_data = nullptr
        ) : m_input_core(client, avid, cid, prefers, select_policy_fn, initial_data),
            m_data_hras(std::make_shared<data_hras>())
        {}

        util::winrt::task<Input> get_input() const {
            return m_input_core.get_input();
        }

        void purge_cache() const {
            m_input_core.purge_cache();

            //this->set_hras_cache({ nullptr, nullptr });
        }

        bool has_cache() const { return m_input_core.has_cache(); }

        /*bool are_params_equal(uint64_t avid,
            uint64_t cid,
            ::BiliUWP::VideoPlayUrlPreferenceParam prefers
        ) const {
            return std::tie(m_data->avid, m_data->cid, m_data->prefers) ==
                std::tie(avid, cid, prefers);
        }*/

        auto get_input_core() const { return m_input_core; }

        // TODO: Maybe refactor later, unclear responsibility?
        // We might want to change video / audio quality only, keeping the other
        // unchanged & buffered. This is where HRASCache comes into play.
        struct HRASCache {
            BiliUWP::HttpRandomAccessStream vhras{ nullptr }, ahras{ nullptr };
        };
        HRASCache get_hras_cache() const { return m_data_hras->hras_cache; }
        void set_hras_cache(HRASCache const& cache) const { m_data_hras->hras_cache = cache; }
        void purge_hras_video_cache() const { m_data_hras->hras_cache.vhras = nullptr; }
        void purge_hras_audio_cache() const { m_data_hras->hras_cache.ahras = nullptr; }

    private:
        struct data_hras {
            HRASCache hras_cache;
        };

        InputCore m_input_core;
        std::shared_ptr<data_hras> m_data_hras;
    };
    template<>
    struct MediaSourceInputContext<MSICInput_DashStreamNoAudio> {
        using Input = MSICInput_DashStreamNoAudio;

        // Returns vstream
        using SelectPolicyFnRet = ::BiliUWP::VideoPlayUrl_Dash_Stream const&;
        // Returns vindex
        using SelectPolicyFn = SelectPolicyFnRet(::BiliUWP::VideoPlayUrl_Dash const&);

        MediaSourceInputContext(
            ::BiliUWP::BiliClient* client,
            uint64_t avid,
            uint64_t cid,
            ::BiliUWP::VideoPlayUrlPreferenceParam prefers,
            std::function<SelectPolicyFn> const& select_policy_fn,
            ::BiliUWP::VideoPlayUrl_Dash const* initial_data = nullptr
        ) : m_data(std::make_shared<data>())
        {
            if (!client) { throw hresult_invalid_argument(L"client is nullptr"); }
            m_data->has_cache = false;
            m_data->select_policy_fn = select_policy_fn;
            m_data->client = client;
            m_data->avid = avid;
            m_data->cid = cid;
            m_data->prefers = prefers;
            if (initial_data) {
                m_data->dash = *initial_data;

                m_data->has_cache = true;
            }
        }

        // TODO: Handle concurrent calls
        util::winrt::task<Input> get_input() const {
            auto cancellation_token = co_await get_cancellation_token();
            cancellation_token.enable_propagation();

            auto strong_data = m_data;

            auto const& dash = strong_data->dash;
            const bool should_update_cache = !strong_data->has_cache;
            if (should_update_cache) {
                co_await strong_data->async.run_if_idle([](std::shared_ptr<data> data) -> util::winrt::task<> {
                    data->dash = std::move((co_await data->client->video_play_url(
                        data->avid, data->cid, data->prefers
                    )).dash.value());
                }, strong_data);
            }
            auto& vstream = strong_data->select_policy_fn(dash);

            if (should_update_cache) {
                strong_data->has_cache = true;
            }
            co_return Input{
                .duration = dash.duration,
                .min_buffer_time = dash.min_buffer_time,
                .vstream = vstream,
            };
        }

        void purge_cache() const {
            m_data->has_cache = false;
        }

        bool has_cache() const { return m_data->has_cache; }

    private:
        struct data {
            ::BiliUWP::BiliClient* client;
            uint64_t avid;
            uint64_t cid;
            ::BiliUWP::VideoPlayUrlPreferenceParam prefers;
            bool has_cache;     // Cache possibly expensive fetch results
            std::function<SelectPolicyFn> select_policy_fn;
            ::BiliUWP::VideoPlayUrl_Dash dash;

            util::winrt::typed_task_storage<> async;
        };

        std::shared_ptr<data> m_data;
    };
    // TODO: MediaSourceInputContext<MSICInput_FlvStream>
}

namespace winrt::BiliUWP::implementation {
    // Synchronously returns a DetailedStatsProvider, and asynchronously returns a MediaSource
    struct DetailedMediaSourceProvider : std::enable_shared_from_this<DetailedMediaSourceProvider> {
        DetailedMediaSourceProvider() {}

        // NOTE: DetailedStatsProvider should be immediately available for display
        virtual std::shared_ptr<DetailedStatsProvider> get_detailed_stats_provider() {
            return nullptr;
        }
        virtual util::winrt::task<Windows::Media::Core::MediaSource> get_media_source_async() = 0;
    };

    struct DetailedMediaPlaybackSourceProvider : std::enable_shared_from_this<DetailedMediaPlaybackSourceProvider> {
        DetailedMediaPlaybackSourceProvider() {}

        // NOTE: DetailedStatsProvider should be immediately available for display
        virtual std::shared_ptr<DetailedStatsProvider> get_detailed_stats_provider() {
            return nullptr;
        }
        virtual util::winrt::task<Windows::Media::Playback::IMediaPlaybackSource>
            get_media_playback_source_async() = 0;
        // WARN: This is a HACK method
        // Must be called when the media session has been successfully built;
        // required by non-trivial resource clean-up
        virtual void notify_completed() {}
    };

    // NOTE: VideoType + PlayerType + FetchingBackendType, e.g. DMSP_DashNativeNative 

    // NOTE: No MakeDMSP_DashNativeNative (legacy and not maintained)

    IRandomAccessStream MakeDMSP_DashNative_MakeDashMpdStream(
        uint64_t duration,
        double min_buffer_time,
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
            auto& astream = *pastream;
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
                min_buffer_time,
                vstream.id, vstream.bandwidth, vstream.mime_type, vstream.codecs,
                vstream.start_with_sap, vstream.sar, vstream.frame_rate,
                vstream.width, vstream.height,
                vstream.segment_base.index_range, vstream.segment_base.initialization,
                astream.id, astream.bandwidth, astream.mime_type, astream.codecs,
                astream.start_with_sap,
                astream.segment_base.index_range, astream.segment_base.initialization
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
                min_buffer_time,
                vstream.id, vstream.bandwidth, vstream.mime_type, vstream.codecs,
                vstream.start_with_sap, vstream.sar, vstream.frame_rate,
                vstream.width, vstream.height,
                vstream.segment_base.index_range, vstream.segment_base.initialization
            );
        }
        util::debug::log_trace(std::format(L"Generated dash: {}", dash_mpd_str));
        return util::winrt::string_to_utf8_stream(hstring(dash_mpd_str));
    }

    // NOTE: Run in UI thread to correctly create DetailedStatsProvider
    std::shared_ptr<DetailedMediaSourceProvider> MakeDMSP_DashNativeHras(
        Windows::Web::Http::HttpClient http_client,
        MediaSourceInputContext<MSICInput_DashStream> msic,
        BiliUWP::SimpleStateIndicator media_state_overlay
    ) {
        using Windows::Web::Http::HttpClient;
        using MSIC = MediaSourceInputContext<MSICInput_DashStream>;
        using TaskMediaSource = util::winrt::task<Windows::Media::Core::MediaSource>;

        struct DMSP_DashNativeHras final : DetailedMediaSourceProvider {
            DMSP_DashNativeHras(
                std::shared_ptr<DetailedStatsProvider> dsp,
                TaskMediaSource task
            ) : m_dsp(std::move(dsp)), m_task(std::move(task)) {}

            std::shared_ptr<DetailedStatsProvider> get_detailed_stats_provider() {
                return m_dsp;
            }
            TaskMediaSource get_media_source_async() {
                return m_task;
            }

        private:
            std::shared_ptr<DetailedStatsProvider> m_dsp;
            TaskMediaSource m_task;
        };

        struct SharedStatsData final {
            bool is_enabled{};
            BiliUWP::HttpRandomAccessStream vhras{ nullptr }, ahras{ nullptr };
            TextBlock mime_type_tb;
            TextBlock resolution_tb;

            std::atomic_bool last_failed{};
        };
        auto shared_data = std::make_shared<SharedStatsData>();

        static constexpr auto UPDATE_INTERVAL = std::chrono::milliseconds(500);
        struct DetailedStatsProvider_AdaptiveMediaSource final : DetailedStatsProvider {
            DetailedStatsProvider_AdaptiveMediaSource(
                std::shared_ptr<SharedStatsData> shared_data
            ) : m_shared_data(std::move(shared_data)) {}
            void InitStats(DetailedStatsContext* ctx) {
                using util::winrt::make_text_block;
                ctx->AddElement(L"Mime Type", m_shared_data->mime_type_tb);
                ctx->AddElement(L"Player Type", make_text_block(
                    L"NativeDashPlayer <- NativeBuffering <- HRAS(Full)"
                ));
                ctx->AddElement(L"Resolution", m_shared_data->resolution_tb);
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
                m_shared_data->is_enabled = true;
                if (m_shared_data->vhras) {
                    m_shared_data->vhras.EnableMetricsCollection(true, 0);
                    m_shared_data->vhras.GetMetrics(true);
                }
                if (m_shared_data->ahras) {
                    m_shared_data->ahras.EnableMetricsCollection(true, 0);
                    m_shared_data->ahras.GetMetrics(true);
                }
            }
            void TimerStopped(void) {
                m_shared_data->is_enabled = false;
                if (m_shared_data->vhras) {
                    m_shared_data->vhras.EnableMetricsCollection(false, 0);
                }
                if (m_shared_data->ahras) {
                    m_shared_data->ahras.EnableMetricsCollection(false, 0);
                }
            }
            void UpdateStats(DetailedStatsContext* ctx) {
                using util::str::byte_size_to_str;
                using util::str::bit_size_to_str;
                static constexpr size_t MAX_POINTS = 60;
                if (!m_shared_data->vhras || !m_shared_data->ahras) { return; }
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
            std::shared_ptr<SharedStatsData> m_shared_data;
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

        auto dsp = std::make_shared<DetailedStatsProvider_AdaptiveMediaSource>(
            shared_data);

        auto task = [](std::shared_ptr<SharedStatsData> shared_data,
            HttpClient hc, MSIC msic, BiliUWP::SimpleStateIndicator mso) -> TaskMediaSource
        {
            auto ui_dispatcher = Windows::UI::Core::CoreWindow::GetForCurrentThread().Dispatcher();
            auto cancellation_token = co_await get_cancellation_token();
            cancellation_token.enable_propagation();

            auto [vhras, ahras] = msic.get_hras_cache();
            bool has_cache = msic.has_cache();
            auto input = co_await msic.get_input();
            auto* pinput = &input;

            if (!vhras || !ahras) {
                // Fetch input and make HRAS

                // Try to use cache
                bool should_fetch = !has_cache;
                while (true) try {
                    if (should_fetch) {
                        // TODO: Maybe there's a better way?
                        std::destroy_at(pinput);
                        pinput = new (pinput) MSICInput_DashStream(co_await msic.get_input());
                    }

                    // Make HRAS
                    auto get_hras_from_stream_fn = [&](::BiliUWP::VideoPlayUrl_Dash_Stream const& stm)
                        -> util::winrt::task<BiliUWP::HttpRandomAccessStream>
                    {
                        Uri main_uri = Uri(stm.base_url);
                        auto backup_uris = std::ranges::to<std::vector>(
                            stm.backup_url | std::views::transform([](auto&& v) { return Uri(v); }));
                        while (true) {
                            try {
                                auto hras = co_await BiliUWP::HttpRandomAccessStream::CreateAsync(
                                    main_uri,
                                    hc,
                                    HttpRandomAccessStreamBufferOptions::Full,
                                    0, false
                                );
                                // Add backup uris
                                hras.SupplyNewUri(backup_uris);
                                co_return hras;
                            }
                            catch (...) {
                                // Uri is invalid
                                if (backup_uris.empty()) { throw; }
                                util::winrt::log_current_exception();
                                main_uri = std::move(backup_uris.front());
                                backup_uris.erase(backup_uris.begin());
                            }
                        }
                    };
                    {
                        util::winrt::task<BiliUWP::HttpRandomAccessStream> vhrasop = nullptr, ahrasop = nullptr;
                        if (!vhras) { vhrasop = get_hras_from_stream_fn(pinput->vstream); }
                        if (!ahras) { ahrasop = get_hras_from_stream_fn(pinput->astream); }
                        if (vhrasop) { vhras = co_await std::move(vhrasop); }
                        if (ahrasop) { ahras = co_await std::move(ahrasop); }
                    }
                    // Set NUR handlers
                    struct new_uri_requested_shared_data : std::enable_shared_from_this<new_uri_requested_shared_data> {
                        std::mutex mutex;
                        util::winrt::task<> op;
                        uint32_t backoff_secs;
                    };
                    // TODO: Remove this hack when msvc fixes the bug
                    auto nur_shared_data = std::make_shared<new_uri_requested_shared_data>();
                    auto new_uri_requested_inner_fn = [
                        weak_vhras = make_weak(vhras), weak_ahras = make_weak(ahras)
                    ](MSIC::InputCore msic, void* pnur_shared_data)
                        -> util::winrt::task<>
                    {
                        auto nur_shared_data =
                            reinterpret_cast<new_uri_requested_shared_data*>(pnur_shared_data)->shared_from_this();

                        auto vhras = weak_vhras.get();
                        if (!vhras) { co_return; }
                        auto ahras = weak_ahras.get();
                        if (!ahras) { co_return; }
                        util::debug::log_debug(L"NewUriRequested: Getting new links");
                        // Backoff policy
                        if (nur_shared_data->backoff_secs > 0) {
                            co_await resume_after(std::chrono::seconds(nur_shared_data->backoff_secs));
                        }
                        msic.purge_cache();
                        auto se_backoff = util::misc::scope_exit([&] {
                            if (nur_shared_data->backoff_secs == 0) {
                                // BACKOFF_INITIAL_SECS
                                nur_shared_data->backoff_secs = 10;
                            }
                            else {
                                // BACKOFF_FACTOR
                                nur_shared_data->backoff_secs =
                                    std::lround(nur_shared_data->backoff_secs * 1.2);
                            }
                        });
                        auto input = co_await msic.get_input();
                        std::vector<Uri> supply_uris;
                        supply_uris.emplace_back(input.vstream.base_url);
                        for (auto const& i : input.vstream.backup_url) { supply_uris.emplace_back(i); }
                        vhras.SupplyNewUri(supply_uris);
                        supply_uris.clear();
                        supply_uris.emplace_back(input.astream.base_url);
                        for (auto const& i : input.astream.backup_url) { supply_uris.emplace_back(i); }
                        ahras.SupplyNewUri(supply_uris);
                        util::debug::log_debug(L"NewUriRequested: Done getting new links");

                        se_backoff.release();
                        nur_shared_data->backoff_secs = 0;
                    };
                    auto new_uri_requested_fn = [new_uri_requested_inner_fn, nur_shared_data, msic = msic.get_input_core()]
                    (BiliUWP::HttpRandomAccessStream const&, BiliUWP::NewUriRequestedEventArgs const& e) -> fire_forget_except
                    {
                        co_safe_capture(nur_shared_data);
                        auto deferral = e.GetDeferral();
                        deferred([&] { deferral.Complete(); });
                        bool owns_op = false;
                        util::winrt::task<> op = nullptr;
                        auto correlation_id = static_cast<uint32_t>(util::num::gen_global_seqid());
                        {
                            std::scoped_lock guard(nur_shared_data->mutex);
                            op = nur_shared_data->op;
                            if (!op) {
                                util::debug::log_trace(std::format(L"Establishing inner NUR async operation (CorrelationId: {:08x})", correlation_id));
                                op = nur_shared_data->op = new_uri_requested_inner_fn(msic, nur_shared_data.get());
                                owns_op = true;
                            }
                        }
                        deferred([&] {
                            if (owns_op) {
                                util::debug::log_trace(std::format(L"Releasing inner NUR async operation (CorrelationId: {:08x})", correlation_id));
                                std::scoped_lock guard(nur_shared_data->mutex);
                                nur_shared_data->op = nullptr;
                            }
                        });
                        util::debug::log_trace(std::format(L"Waiting for inner NUR async operation to complete (CorrelationId: {:08x})", correlation_id));
                        co_await op;
                        util::debug::log_trace(std::format(L"Done awaiting inner NUR async operation (CorrelationId: {:08x})", correlation_id));
                    };
                    vhras.NewUriRequested(new_uri_requested_fn);
                    ahras.NewUriRequested(new_uri_requested_fn);

                    break;
                }
                catch (...) {
                    msic.purge_cache();
                    if (!should_fetch) {
                        // Retry with fetch
                        should_fetch = true;
                        util::winrt::log_current_exception();
                    }
                    else {
                        // Still failed with fetch, throw
                        throw;
                    }
                }
            }

            // Enable metrics beforehand as we have `fetch ahead`
            vhras.EnableMetricsCollection(true, 0);
            ahras.EnableMetricsCollection(true, 0);

            // Update detailed stats
            hstring mine_type_str{ std::format(
                L"{};codecs=\"{}\" | {};codecs=\"{}\"",
                pinput->vstream.mime_type, pinput->vstream.codecs,
                pinput->astream.mime_type, pinput->astream.codecs) };
            hstring resolution_str{ std::format(L"{}x{}@{}",
                    pinput->vstream.width, pinput->vstream.height, pinput->vstream.frame_rate) };
            ui_dispatcher.RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, [=] {
                shared_data->mime_type_tb.Text(mine_type_str);
                shared_data->resolution_tb.Text(resolution_str);
                shared_data->vhras = vhras;
                shared_data->ahras = ahras;
                vhras.EnableMetricsCollection(shared_data->is_enabled, 0);
                ahras.EnableMetricsCollection(shared_data->is_enabled, 0);
            });

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

            msic.set_hras_cache({ .vhras = vhras, .ahras = ahras });

            // Make AdaptiveMediaSource
            auto adaptive_media_src_result = co_await AdaptiveMediaSource::CreateFromStreamAsync(
                MakeDMSP_DashNative_MakeDashMpdStream(
                    pinput->duration, pinput->min_buffer_time, pinput->vstream, &pinput->astream),
                Uri(L"about:blank"),
                L"application/dash+xml",
                hc
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
            auto se_adaptive_media_src = util::misc::scope_exit([&] {
                adaptive_media_src.Close();
            });

            // Use buffer replacing (may waste some memory)
            adaptive_media_src.DownloadRequested(
                [mso, shared_data = std::move(shared_data)]
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
                            mso.Dispatcher().RunAsync(
                                Windows::UI::Core::CoreDispatcherPriority::Normal,
                                [=] { mso.SwitchToFailed(res_str(L"App/Common/LoadFailed")); }
                            );
                        }
                        throw;
                    }
                    if (shared_data->last_failed) {
                        shared_data->last_failed = false;
                        mso.Dispatcher().RunAsync(
                            Windows::UI::Core::CoreDispatcherPriority::Normal,
                            [=] { mso.SwitchToHidden(); }
                        );
                    }
                    /*e.Result().InputStream(target_hras->GetInputStreamAt(
                        e.ResourceByteRangeOffset().as<uint64_t>()));*/
                }
            );

            auto media_src = MediaSource::CreateFromAdaptiveMediaSource(adaptive_media_src);
            se_adaptive_media_src.release();
            co_return media_src;
        }(std::move(shared_data), std::move(http_client), std::move(msic), std::move(media_state_overlay));

        return std::make_shared<DMSP_DashNativeHras>(std::move(dsp), std::move(task));
    }
    std::shared_ptr<DetailedMediaSourceProvider> MakeDMSP_DashNativeHrasNoAudio(
        Windows::Web::Http::HttpClient http_client,
        MediaSourceInputContext<MSICInput_DashStreamNoAudio> msic,
        BiliUWP::SimpleStateIndicator media_state_overlay
    ) {
        using Windows::Web::Http::HttpClient;
        using MSIC = MediaSourceInputContext<MSICInput_DashStreamNoAudio>;
        using TaskMediaSource = util::winrt::task<Windows::Media::Core::MediaSource>;

        struct DMSP_DashNativeHras final : DetailedMediaSourceProvider {
            DMSP_DashNativeHras(
                std::shared_ptr<DetailedStatsProvider> dsp,
                TaskMediaSource task
            ) : m_dsp(std::move(dsp)), m_task(std::move(task)) {}

            std::shared_ptr<DetailedStatsProvider> get_detailed_stats_provider() {
                return m_dsp;
            }
            TaskMediaSource get_media_source_async() {
                return m_task;
            }

        private:
            std::shared_ptr<DetailedStatsProvider> m_dsp;
            TaskMediaSource m_task;
        };

        struct SharedStatsData final {
            bool is_enabled{};
            BiliUWP::HttpRandomAccessStream vhras{ nullptr };
            TextBlock mime_type_tb;
            TextBlock resolution_tb;

            std::atomic_bool last_failed{};
        };
        auto shared_data = std::make_shared<SharedStatsData>();

        static constexpr auto UPDATE_INTERVAL = std::chrono::milliseconds(500);
        struct DetailedStatsProvider_AdaptiveMediaSourceNoAudio final : DetailedStatsProvider {
            DetailedStatsProvider_AdaptiveMediaSourceNoAudio(
                std::shared_ptr<SharedStatsData> shared_data
            ) : m_shared_data(std::move(shared_data)) {}
            void InitStats(DetailedStatsContext* ctx) {
                using util::winrt::make_text_block;
                ctx->AddElement(L"Mime Type", m_shared_data->mime_type_tb);
                ctx->AddElement(L"Player Type", make_text_block(
                    L"NativeDashPlayer <- NativeBuffering <- HRAS(Full)"
                ));
                ctx->AddElement(L"Resolution", m_shared_data->resolution_tb);
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
                m_shared_data->is_enabled = true;
                if (m_shared_data->vhras) {
                    m_shared_data->vhras.EnableMetricsCollection(true, 0);
                    m_shared_data->vhras.GetMetrics(true);
                }
            }
            void TimerStopped(void) {
                m_shared_data->is_enabled = false;
                if (m_shared_data->vhras) {
                    m_shared_data->vhras.EnableMetricsCollection(false, 0);
                }
            }
            void UpdateStats(DetailedStatsContext* ctx) {
                using util::str::byte_size_to_str;
                using util::str::bit_size_to_str;
                static constexpr size_t MAX_POINTS = 60;
                if (!m_shared_data->vhras) { return; }
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
            std::shared_ptr<SharedStatsData> m_shared_data;
            TextBlock m_video_host_tb;
            TextBlock m_audio_host_tb;
            PolylineWithTextElemForDetailedStats m_video_conn_speed;
            std::deque<uint64_t> m_video_conn_speed_points;
            PolylineWithTextElemForDetailedStats m_net_activity;
            std::deque<uint64_t> m_net_activity_points;
            TextBlock m_mystery_text_tb;
        };

        auto dsp = std::make_shared<DetailedStatsProvider_AdaptiveMediaSourceNoAudio>(
            shared_data);

        auto task = [](std::shared_ptr<SharedStatsData> shared_data,
            HttpClient hc, MSIC msic, BiliUWP::SimpleStateIndicator mso) -> TaskMediaSource
        {
            auto ui_dispatcher = Windows::UI::Core::CoreWindow::GetForCurrentThread().Dispatcher();
            auto cancellation_token = co_await get_cancellation_token();
            cancellation_token.enable_propagation();

            bool has_cache = msic.has_cache();
            auto input = co_await msic.get_input();
            auto* pinput = &input;

            BiliUWP::HttpRandomAccessStream vhras{ nullptr };

            {
                // Fetch input and make HRAS

                // Try to use cache
                bool should_fetch = !has_cache;
                while (true) try {
                    if (should_fetch) {
                        // TODO: Maybe there's a better way?
                        std::destroy_at(pinput);
                        pinput = new (pinput) MSICInput_DashStreamNoAudio(co_await msic.get_input());
                    }

                    // Make HRAS
                    auto get_hras_from_stream_fn = [&](::BiliUWP::VideoPlayUrl_Dash_Stream const& stm)
                        -> util::winrt::task<BiliUWP::HttpRandomAccessStream>
                    {
                        Uri main_uri = Uri(stm.base_url);
                        auto backup_uris = std::ranges::to<std::vector>(
                            stm.backup_url | std::views::transform([](auto&& v) { return Uri(v); }));
                        while (true) {
                            try {
                                auto hras = co_await BiliUWP::HttpRandomAccessStream::CreateAsync(
                                    main_uri,
                                    hc,
                                    HttpRandomAccessStreamBufferOptions::Full,
                                    0, false
                                );
                                // Add backup uris
                                hras.SupplyNewUri(backup_uris);
                                co_return hras;
                            }
                            catch (...) {
                                // Uri is invalid
                                if (backup_uris.empty()) { throw; }
                                util::winrt::log_current_exception();
                                main_uri = std::move(backup_uris.front());
                                backup_uris.erase(backup_uris.begin());
                            }
                        }
                    };
                    vhras = co_await get_hras_from_stream_fn(pinput->vstream);
                    // Set NUR handlers
                    struct new_uri_requested_shared_data : std::enable_shared_from_this<new_uri_requested_shared_data> {
                        uint32_t backoff_secs;
                    };
                    auto nur_shared_data = std::make_shared<new_uri_requested_shared_data>();
                    auto new_uri_requested_fn = [msic, nur_shared_data]
                    (BiliUWP::HttpRandomAccessStream const& sender, BiliUWP::NewUriRequestedEventArgs const& e)
                        -> fire_forget_except
                    {
                        co_safe_capture(msic);
                        co_safe_capture(nur_shared_data);

                        auto deferral = e.GetDeferral();
                        deferred([&] { deferral.Complete(); });

                        util::debug::log_debug(L"NewUriRequested: Getting new links");
                        // Backoff policy
                        if (nur_shared_data->backoff_secs > 0) {
                            co_await resume_after(std::chrono::seconds(nur_shared_data->backoff_secs));
                        }
                        msic.purge_cache();
                        auto se_backoff = util::misc::scope_exit([&] {
                            if (nur_shared_data->backoff_secs == 0) {
                                // BACKOFF_INITIAL_SECS
                                nur_shared_data->backoff_secs = 10;
                            }
                            else {
                                // BACKOFF_FACTOR
                                nur_shared_data->backoff_secs =
                                    std::lround(nur_shared_data->backoff_secs * 1.2);
                            }
                        });
                        auto input = co_await msic.get_input();
                        std::vector<Uri> supply_uris;
                        supply_uris.emplace_back(input.vstream.base_url);
                        for (auto const& i : input.vstream.backup_url) { supply_uris.emplace_back(i); }
                        sender.SupplyNewUri(supply_uris);
                        supply_uris.clear();
                        util::debug::log_debug(L"NewUriRequested: Done getting new links");

                        se_backoff.release();
                        nur_shared_data->backoff_secs = 0;
                    };
                    vhras.NewUriRequested(new_uri_requested_fn);

                    break;
                }
                catch (...) {
                    msic.purge_cache();
                    if (!should_fetch) {
                        // Retry with fetch
                        should_fetch = true;
                        util::winrt::log_current_exception();
                    }
                    else {
                        // Still failed with fetch, throw
                        throw;
                    }
                }
            }

            // Enable metrics beforehand as we have `fetch ahead`
            vhras.EnableMetricsCollection(true, 0);

            // Update detailed stats
            hstring mine_type_str{ std::format(L"{};codecs=\"{}\"",
                pinput->vstream.mime_type, pinput->vstream.codecs) };
            hstring resolution_str{ std::format(L"{}x{}@{}",
                    pinput->vstream.width, pinput->vstream.height, pinput->vstream.frame_rate) };
            ui_dispatcher.RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, [=] {
                shared_data->mime_type_tb.Text(mine_type_str);
                shared_data->resolution_tb.Text(resolution_str);
                shared_data->vhras = vhras;
                vhras.EnableMetricsCollection(shared_data->is_enabled, 0);
            });

            // Fetch ahead, so we can make sure opening will succeed
            {
                constexpr uint32_t bufsize = 4;
                co_await vhras.ReadAsync(Buffer(bufsize), bufsize, InputStreamOptions::None);
                vhras.Seek(0);
            }

            // Make AdaptiveMediaSource
            auto adaptive_media_src_result = co_await AdaptiveMediaSource::CreateFromStreamAsync(
                MakeDMSP_DashNative_MakeDashMpdStream(
                    pinput->duration, pinput->min_buffer_time, pinput->vstream, nullptr),
                Uri(L"about:blank"),
                L"application/dash+xml",
                hc
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
            auto se_adaptive_media_src = util::misc::scope_exit([&] {
                adaptive_media_src.Close();
            });

            // Use buffer replacing (may waste some memory)
            adaptive_media_src.DownloadRequested(
                [mso, shared_data = std::move(shared_data)]
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
                            mso.Dispatcher().RunAsync(
                                Windows::UI::Core::CoreDispatcherPriority::Normal,
                                [=] { mso.SwitchToFailed(res_str(L"App/Common/LoadFailed")); }
                            );
                        }
                        throw;
                    }
                    if (shared_data->last_failed) {
                        shared_data->last_failed = false;
                        mso.Dispatcher().RunAsync(
                            Windows::UI::Core::CoreDispatcherPriority::Normal,
                            [=] { mso.SwitchToHidden(); }
                        );
                    }
                    /*e.Result().InputStream(target_hras->GetInputStreamAt(
                        e.ResourceByteRangeOffset().as<uint64_t>()));*/
                }
            );

            auto media_src = MediaSource::CreateFromAdaptiveMediaSource(adaptive_media_src);
            se_adaptive_media_src.release();
            co_return media_src;
        }(std::move(shared_data), std::move(http_client), std::move(msic), std::move(media_state_overlay));

        return std::make_shared<DMSP_DashNativeHras>(std::move(dsp), std::move(task));
    }
}

namespace winrt::BiliUWP::implementation {
    void cleanup_media_player(MediaPlayerElement const& player_elem) {
        auto player = player_elem.MediaPlayer();
        player_elem.SetMediaPlayer(nullptr);
        if (auto session = util::winrt::try_get_media_playback_session(player)) {
            if (session.CanPause()) { player.Pause(); }
            // Clean up manually as MediaPlayer does not clean itself up
            // even when there are no more references to it
            [](MediaPlayer player) -> fire_forget_except {
                co_await resume_background();
                player.Close();
            }(std::move(player));
        }
    }

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
        Unloaded([this](auto&&, auto&&) {
            if (MediaPlayerElem().IsFullWindow()) {
                MediaPlayerElem().IsFullWindow(false);
            }
        });
        MediaPlayerElem().RegisterPropertyChangedCallback(
            MediaPlayerElement::IsFullWindowProperty(),
            [this](DependencyObject const& sender, DependencyProperty const&) {
                util::winrt::force_focus_element(*this, FocusState::Programmatic);
            }
        );

        if (::BiliUWP::App::get()->cfg_model().App_OverrideSpaceForPlaybackControl()) {
            MediaPlayerElem().TransportControls().as<BiliUWP::CustomMediaTransportControls>()
                .OverrideSpaceForPlaybackControl(true);
        }

        {
            // TODO: Remove this (open local file temporaily)
            KeyboardAccelerator ka_open;
            ka_open.Key(Windows::System::VirtualKey::O);
            ka_open.Invoked([this](auto&&, KeyboardAcceleratorInvokedEventArgs const& e) -> fire_forget_except {
                e.Handled(true);
                auto weak_store = util::winrt::make_weak_storage(*this);
                auto picker = Windows::Storage::Pickers::FileOpenPicker();
                auto picker_ftf = picker.FileTypeFilter();
                picker_ftf.Append(L".mp4");
                picker_ftf.Append(L"*");
                auto file = co_await weak_store.ual(picker.PickSingleFileAsync());
                if (!file) { co_return; }
                auto media_source = MediaSource::CreateFromStorageFile(file);
                this->SubmitMediaPlaybackSourceToNativePlayer(media_source);
            });
            KeyboardAccelerators().Append(ka_open);
        }
    }
    fire_forget_except MediaPlayPage::final_release(std::unique_ptr<MediaPlayPage> ptr) noexcept {
        // Gracefully release MediaPlayer and prevent UI from freezing
        ptr->m_cur_async.cancel_running();
        util::debug::log_trace(L"Final release");
        auto dispatcher = ptr->Dispatcher();
        // Ensure we are on the UI thread (MediaPlayerElem may hold a reference
        // in a non-UI thread during media opening)
        co_await dispatcher;
        // Clean up UpListView
        ptr->UpListView().ItemsSource(nullptr);
        ptr->PartsListView().ItemsSource(nullptr);
        // Clean up MediaPlayer
        auto player_elem = ptr->MediaPlayerElem();
        player_elem.AreTransportControlsEnabled(false);
        cleanup_media_player(player_elem);
        // Clean up DetailedStatsTimer
        if (ptr->m_detailed_stats_update_timer) {
            ptr->m_detailed_stats_update_timer.Stop();
            ptr->m_detailed_stats_provider->TimerStopped();
        }
        // Clean up VideoDanmakuControl
        if (ptr->m_video_danmaku_ctrl) {
            ptr->m_video_danmaku_ctrl.SetBackgroundPopulator(nullptr, true);
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
    void MediaPlayPage::OnPreviewKeyDown(Windows::UI::Xaml::Input::KeyRoutedEventArgs const& e) {
        using Windows::System::VirtualKey;
        auto key = e.Key();
        if (key == VirtualKey::Space && ::BiliUWP::App::get()->cfg_model().App_OverrideSpaceForPlaybackControl()) {
            SwitchMediaPlayPause();
            e.Handled(true);
        }
    }
    void MediaPlayPage::OnKeyDown(Windows::UI::Xaml::Input::KeyRoutedEventArgs const& e) {
        using Windows::System::VirtualKey;
        auto key = e.Key();
        if (key == VirtualKey::Space) {
            SwitchMediaPlayPause();
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
    void MediaPlayPage::MTCDanmakuSwitchButton_Click(IInspectable const&, RoutedEventArgs const&) {
        m_danmaku_enabled = !m_danmaku_enabled;
        if (m_danmaku_enabled) {
            VisualStateManager::GoToState(*this, L"MTCDanmakuSwitchButtonEnabled", true);
        }
        else {
            VisualStateManager::GoToState(*this, L"MTCDanmakuSwitchButtonDisabled", true);
        }
        UpdateVideoDanmakuControlState();
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
        apartment_context ui_ctx;

        auto cancellation_token = co_await get_cancellation_token();
        cancellation_token.enable_propagation();
        auto weak_store = util::winrt::make_weak_storage(*this);

        co_await resume_background();

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
        auto se_adaptive_media_src = util::misc::scope_exit([&] {
            adaptive_media_src.Close();
        });

        // Post setup (detailed stats & DownloadRequested event)
        auto vuri = Uri(vstream.base_url);
        auto auri = Uri(astream.base_url);

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
                    util::debug::log_trace(std::format(L"Establishing inner NUR async operation (CorrelationId: {:08x})", correlation_id));
                    op = shared_data->op = new_uri_requested_inner_fn();
                    owns_op = true;
                }
            }
            deferred([&] {
                if (owns_op) {
                    util::debug::log_trace(std::format(L"Releasing inner NUR async operation (CorrelationId: {:08x})", correlation_id));
                    std::scoped_lock guard(shared_data->mutex);
                    shared_data->op = nullptr;
                }
            });
            util::debug::log_trace(std::format(L"Waiting for inner NUR async operation to complete (CorrelationId: {:08x})", correlation_id));
            co_await op;
            util::debug::log_trace(std::format(L"Done awaiting inner NUR async operation (CorrelationId: {:08x})", correlation_id));
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

        co_await ui_ctx;

        auto ds_provider = std::make_shared<DetailedStatsProvider_AdaptiveMediaSource>(
            hstring(std::format(
                L"{};codecs=\"{}\" | {};codecs=\"{}\"",
                vstream.mime_type, vstream.codecs,
                astream.mime_type, astream.codecs)),
            hstring(std::format(L"{}x{}@{}", vstream.width, vstream.height, vstream.frame_rate)),
            shared_data
        );

        co_await resume_background();

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

        auto ret_result = std::pair{ MediaSource::CreateFromAdaptiveMediaSource(adaptive_media_src), std::move(ds_provider) };

        se_adaptive_media_src.release();

        co_return ret_result;
    }
    util::winrt::task<MediaPlayPage::MediaSrcDetailedStatsPair> MediaPlayPage::PlayVideoWithCidInner_DashNativeHrasNoAudio(
        ::BiliUWP::VideoPlayUrl_Dash const& dash_info,
        ::BiliUWP::VideoPlayUrl_Dash_Stream const& vstream,
        std::function<util::winrt::task<::BiliUWP::VideoPlayUrl_Dash_Stream>(void)> get_new_stream_fn
    ) {
        apartment_context ui_ctx;

        auto cancellation_token = co_await get_cancellation_token();
        cancellation_token.enable_propagation();
        auto weak_store = util::winrt::make_weak_storage(*this);

        co_await resume_background();

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
        auto se_adaptive_media_src = util::misc::scope_exit([&] {
            adaptive_media_src.Close();
        });

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

        co_await ui_ctx;

        auto ds_provider = std::make_shared<DetailedStatsProvider_AdaptiveMediaSource>(
            hstring(std::format(L"{};codecs=\"{}\"", vstream.mime_type, vstream.codecs)),
            hstring(std::format(L"{}x{}@{}", vstream.width, vstream.height, vstream.frame_rate)),
            shared_data
        );

        co_await resume_background();

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

        auto ret_result = std::pair{ MediaSource::CreateFromAdaptiveMediaSource(adaptive_media_src), std::move(ds_provider) };

        se_adaptive_media_src.release();

        co_return ret_result;
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
        auto ws = util::winrt::make_weak_storage(*this);

        // Body
        // WARN: Never access video_vinfo after any suspension point,
        //       as the media info may have changed type
        auto& video_vinfo = std::get<::BiliUWP::VideoViewInfoResult>(m_media_info);
        auto client = ::BiliUWP::App::get()->bili_client();

        this->SubmitMediaPlaybackSourceToNativePlayer(nullptr);

        auto video_avid = video_vinfo.avid;
        auto video_bvid = video_vinfo.bvid;

        struct VideoPartMetadata {
            uint64_t online_count;
            std::vector<TimedTextSource> tts_vec;
        };
        auto fetch_vpart_meta_fn = [](uint64_t avid, uint64_t cid,
            ::BiliUWP::BiliClient* client,
            Windows::Web::Http::HttpClient hc) -> util::winrt::task<VideoPartMetadata>
        {
            auto cancellation_token = co_await get_cancellation_token();
            cancellation_token.enable_propagation();

            std::vector<TimedTextSource> tts_vec;
            util::debug::log_trace(L"Start fetching video metadata");
            co_await resume_background();
            auto vinfo2 = co_await client->video_info_v2(avid, cid);
            for (auto const& st : vinfo2.subtitle.list) {
                // TODO: Use ExternalTimedMetadataTracks to populate subtitles instead
                // Add BOM header so that TimedTextSource can load subtitles correctly
                std::wstring srt_str{ 0xfeff };
                util::debug::log_trace(std::format(L"Downloading subtitle from `{}`...", st.subtitle_url));
                auto subtitle_json_str = co_await hc.GetStringAsync(Uri(st.subtitle_url));
                util::debug::log_trace(std::format(L"Parsing subtitle JSON: {}", subtitle_json_str));
                auto jo = Windows::Data::Json::JsonObject::Parse(subtitle_json_str);
                auto jv_body = jo.GetNamedValue(L"body");
                if (jv_body.ValueType() == Windows::Data::Json::JsonValueType::Null) {
                    // Invalid subtitle data, skip generation
                    continue;
                }
                for (size_t idx = 0; auto&& i : jo.GetNamedArray(L"body")) {
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

        //m_media_insrc = std::monostate{};

        this->ResetMTCStreamSelectionMenu();

        ::BiliUWP::VideoPlayUrlPreferenceParam param{
            .prefer_dash = true, .prefer_hdr = true, .prefer_4k = true,
            .prefer_dolby = true, .prefer_8k = true, .prefer_av1 = false
        };

        auto play_url_task = client->video_play_url(video_bvid, cid, param);

        // Find video titles at the same time
        hstring video_title = video_vinfo.title;
        hstring video_part_title{};
        {
            bool found = false;
            for (auto const& i : video_vinfo.pages) {
                if (cid == i.cid) {
                    video_part_title = i.part_title;
                    found = true;
                    break;
                }
            }
            if (!found) {
                util::debug::log_error(std::format(L"Failed to find part title with given cid {}", cid));
            }
        }

        auto video_cover_uri = Uri(video_vinfo.cover_url);

        auto video_pinfo = std::move(co_await ws.ual(std::move(play_url_task)));

        bool is_60fps_video{ false };

        std::shared_ptr<DetailedMediaSourceProvider> dmsp;

        struct DMPSP_Video final : DetailedMediaPlaybackSourceProvider {
            struct Context {
                Windows::Media::Core::MediaSource media_src{ nullptr };
                ~Context() {
                    if (!media_src) { return; }
                    // TODO: Find out the proper way to dispose resources
                    if (auto src = media_src.AdaptiveMediaSource()) { src.Close(); }
                    media_src.Close();
                }
            };

            DMPSP_Video(std::shared_ptr<DetailedMediaSourceProvider> dmsp,
                BiliUWP::SimpleStateIndicator mso,
                util::winrt::task<VideoPartMetadata> vpart_meta_task,
                hstring video_title,
                hstring video_part_title,
                Uri video_cover_uri
            ) : m_dmsp(dmsp), m_ctx(std::make_shared<Context>())
            {
                using RetType = util::winrt::task<Windows::Media::Playback::IMediaPlaybackSource>;

                m_task = [](std::shared_ptr<Context> ctx,
                    std::shared_ptr<DetailedMediaSourceProvider> dmsp,
                    BiliUWP::SimpleStateIndicator mso,
                    util::winrt::task<VideoPartMetadata> vpart_meta_task,
                    hstring video_title,
                    hstring video_part_title,
                    Uri video_cover_uri
                    ) -> RetType
                {
                    auto cancellation_token = co_await get_cancellation_token();
                    cancellation_token.enable_propagation();

                    auto video_task = dmsp->get_media_source_async();

                    cancellation_token.callback([=] {
                        video_task.cancel();
                        vpart_meta_task.cancel();
                    });

                    auto media_src = co_await video_task;
                    ctx->media_src = media_src;

                    // TODO: Improve subtitle handling
                    mso.SwitchToLoading(res_str(L"App/Page/MediaPlayPage/LoadingSubtitles"));
                    auto vpart_meta = std::move(co_await vpart_meta_task);
                    media_src.ExternalTimedTextSources().ReplaceAll(vpart_meta.tts_vec);

                    auto media_playback_item = MediaPlaybackItem(media_src);
                    auto display_props = media_playback_item.GetDisplayProperties();
                    display_props.Type(MediaPlaybackType::Video);
                    display_props.Thumbnail(RandomAccessStreamReference::CreateFromUri(video_cover_uri));
                    auto display_props_video_props = display_props.VideoProperties();
                    display_props_video_props.Title(video_title);
                    display_props_video_props.Subtitle(video_part_title);
                    media_playback_item.ApplyDisplayProperties(display_props);

                    co_return media_playback_item;
                }(m_ctx, std::move(dmsp), std::move(mso), std::move(vpart_meta_task),
                    std::move(video_title), std::move(video_part_title), std::move(video_cover_uri));
            }

            std::shared_ptr<DetailedStatsProvider> get_detailed_stats_provider() {
                return m_dmsp->get_detailed_stats_provider();
            }
            util::winrt::task<Windows::Media::Playback::IMediaPlaybackSource>
                get_media_playback_source_async()
            {
                return m_task;
            }
            void notify_completed() {
                m_ctx->media_src = nullptr;
            }

        private:
            std::shared_ptr<DetailedMediaSourceProvider> m_dmsp;
            util::winrt::task<Windows::Media::Playback::IMediaPlaybackSource> m_task;
            std::shared_ptr<Context> m_ctx;
        };

        // TODO: Properly handle this
        // Steals information from current source
        struct DMPSP_Video_Quick final : DetailedMediaPlaybackSourceProvider {
            struct Context {
                Windows::Media::Core::MediaSource media_src{ nullptr };
                ~Context() {
                    if (!media_src) { return; }
                    // TODO: Find out the proper way to dispose resources
                    if (auto src = media_src.AdaptiveMediaSource()) { src.Close(); }
                    media_src.Close();
                }
            };

            DMPSP_Video_Quick(std::shared_ptr<DetailedMediaSourceProvider> dmsp,
                MediaPlaybackItem mpi
            ) : m_dmsp(dmsp), m_ctx(std::make_shared<Context>())
            {
                using RetType = util::winrt::task<Windows::Media::Playback::IMediaPlaybackSource>;

                auto src = mpi.Source();
                auto dp = mpi.GetDisplayProperties();
                auto dpvp = dp.VideoProperties();

                // TODO: Unclear responsibility?
                std::optional<uint32_t> selected_subtitle;
                {
                    auto tmi = mpi.TimedMetadataTracks();
                    auto tmi_size = tmi.Size();
                    for (uint32_t j = 0; j < tmi_size; j++) {
                        auto pm = tmi.GetPresentationMode(j);
                        if (pm == TimedMetadataTrackPresentationMode::PlatformPresented) {
                            selected_subtitle = j;
                            break;
                        }
                    }
                }

                m_task = [](std::shared_ptr<Context> ctx,
                    std::shared_ptr<DetailedMediaSourceProvider> dmsp,
                    std::vector<TimedTextSource> tts,
                    RandomAccessStreamReference thumbnail_rasr,
                    hstring video_title,
                    hstring video_subtitle,
                    std::optional<uint32_t> selected_subtitle
                    ) -> RetType
                {
                    auto cancellation_token = co_await get_cancellation_token();
                    cancellation_token.enable_propagation();

                    auto media_src = co_await dmsp->get_media_source_async();
                    ctx->media_src = media_src;

                    media_src.ExternalTimedTextSources().ReplaceAll(tts);

                    auto media_playback_item = MediaPlaybackItem(media_src);
                    auto display_props = media_playback_item.GetDisplayProperties();
                    display_props.Type(MediaPlaybackType::Video);
                    display_props.Thumbnail(thumbnail_rasr);
                    auto display_props_video_props = display_props.VideoProperties();
                    display_props_video_props.Title(video_title);
                    display_props_video_props.Subtitle(video_subtitle);
                    media_playback_item.ApplyDisplayProperties(display_props);

                    if (selected_subtitle) {
                        // Oneshot event
                        auto et = std::make_shared_for_overwrite<event_token>();
                        *et = media_playback_item.TimedMetadataTracksChanged(
                            [=, idx = *selected_subtitle](MediaPlaybackItem const& sender, auto&&) {
                                auto tmt = sender.TimedMetadataTracks();
                                if (idx + 1 > tmt.Size()) { return; }
                                sender.TimedMetadataTracksChanged(*et);
                                tmt.SetPresentationMode(idx,
                                    TimedMetadataTrackPresentationMode::PlatformPresented);
                            }
                        );
                    }

                    co_return media_playback_item;
                }(m_ctx, std::move(dmsp),
                    std::ranges::to<std::vector>(src.ExternalTimedTextSources()),
                    dp.Thumbnail(),
                    dpvp.Title(),
                    dpvp.Subtitle(),
                    selected_subtitle);
            }

            std::shared_ptr<DetailedStatsProvider> get_detailed_stats_provider() {
                return m_dmsp->get_detailed_stats_provider();
            }
            util::winrt::task<Windows::Media::Playback::IMediaPlaybackSource>
                get_media_playback_source_async()
            {
                return m_task;
            }
            void notify_completed() {
                m_ctx->media_src = nullptr;
            }

        private:
            std::shared_ptr<DetailedMediaSourceProvider> m_dmsp;
            util::winrt::task<Windows::Media::Playback::IMediaPlaybackSource> m_task;
            std::shared_ptr<Context> m_ctx;
        };

        // For video / audio quality selection changes
        auto start_quick_reload_fn = [this](auto msic, bool is_60fps, auto dmsp_gen_fn) {
            m_cur_async.cancel_and_run([&](MediaPlayPage* that, auto msic, bool is_60fps) -> IAsyncAction {
                auto cancellation_token = co_await get_cancellation_token();
                cancellation_token.enable_propagation();
                auto ws = util::winrt::make_weak_storage(*that);

                auto mp = that->MediaPlayerElem().MediaPlayer();
                auto mpi = mp.Source().as<MediaPlaybackItem>();

                auto previous_time = mp.PlaybackSession().Position();

                auto dmsp = dmsp_gen_fn(
                    that->m_http_client_m, msic, that->MediaPlayerStateOverlay());
                auto dmpsp = std::make_shared<DMPSP_Video_Quick>(dmsp, mpi);

                mp = nullptr;
                mpi = nullptr;

                that->SubmitMediaPlaybackSourceToNativePlayer(nullptr);

                co_await that->SubmitMediaPlaybackSourceToNativePlayer_V2(dmpsp, nullptr,
                    that->m_cfg_model.App_UseCustomVideoPresenter(), is_60fps);

                // Restore previous time and subtitle selection
                if (!ws.lock()) { co_return; }
                mp = that->MediaPlayerElem().MediaPlayer();
                mp.PlaybackSession().Position(previous_time);
            }, this, std::move(msic), is_60fps);
        };

        if (video_pinfo.dash) {
            struct DashStreamId {
                uint32_t id{};
                uint32_t codecid{};
                DashStreamId() {}
                DashStreamId(::BiliUWP::VideoPlayUrl_Dash_Stream const& stm) :
                    id(stm.id), codecid(stm.codecid) {}
                bool operator==(DashStreamId const&) const = default;
                bool operator==(::BiliUWP::VideoPlayUrl_Dash_Stream const& rhs) const {
                    return operator==(DashStreamId(rhs));
                }
            };
            struct SharedData {
                DashStreamId vid, aid;
                bool cur_video_is_60fps{};
                bool audio_use_flac{};
            };
            auto shared_data = std::make_shared<SharedData>();

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
            shared_data->vid = video_stream;

            util::debug::log_trace(std::format(L"Selecting video stream {}, codecid = {}",
                shared_data->vid.id, shared_data->vid.codecid));

            // A heuristic method for determining how VideoDanmakuPresenter should populate
            // the background (proactive or reactive), which may help reduce dropped frames
            auto is_60fps_video_fn = [](::BiliUWP::VideoPlayUrl_Dash_Stream const& video_stream) {
                return util::num::try_parse_f64(video_stream.frame_rate).value_or(0) > 58;
            };
            is_60fps_video = is_60fps_video_fn(video_stream);
            shared_data->cur_video_is_60fps = is_60fps_video;

            auto setup_streams_selection_menu_fn = [&](auto msic, auto dmsp_gen_fn) {
                // TODO: Use custom RadioMenuFlyoutItem
                auto items = MTCStreamSelectionMenu().Items();
                for (auto const& i : video_pinfo.support_formats) {
                    auto item = ToggleMenuFlyoutItem();
                    item.Text(i.new_description);
                    auto cnt = static_cast<uint32_t>(video_pinfo.support_formats.size());
                    auto quality = i.quality;
                    bool is_60fps = false;
                    {
                        DashStreamId id{ video_stream };
                        id.id = quality;
                        if (id == video_stream) {
                            item.IsChecked(true);
                        }
                        for (auto const& j : video_dash.video) {
                            if (j == id) {
                                is_60fps = is_60fps_video_fn(j);
                                break;
                            }
                        }
                    }
                    item.Click([this, start_quick_reload_fn, cnt, quality, msic, shared_data, is_60fps, dmsp_gen_fn]
                        (IInspectable const& sender, auto&&) {
                            auto item = sender.as<ToggleMenuFlyoutItem>();
                            bool last_selected = !item.IsChecked();
                            if (last_selected) { item.IsChecked(true); }
                            else {
                                // Remove other selections
                                auto items = MTCStreamSelectionMenu().Items();
                                for (uint32_t i = 0; i < cnt; i++) {
                                    auto cur_item = items.GetAt(i).as<ToggleMenuFlyoutItem>();
                                    if (cur_item == item) { continue; }
                                    cur_item.IsChecked(false);
                                }

                                // Reload stream
                                if constexpr (requires{ msic.purge_hras_video_cache(); }) {
                                    msic.purge_hras_video_cache();
                                }
                                shared_data->vid.id = quality;
                                shared_data->cur_video_is_60fps = is_60fps;
                                start_quick_reload_fn(msic, is_60fps, dmsp_gen_fn);
                            }
                        }
                    );
                    items.Append(item);
                }
                if (video_dash.flac && video_dash.flac->audio) {
                    items.Append(MenuFlyoutSeparator());
                    auto item = ToggleMenuFlyoutItem();
                    item.Text(::BiliUWP::App::res_str(L"App/Page/MediaPlayPage/EnableLosslessAudio/Text"));
                    item.IsChecked(shared_data->audio_use_flac);
                    items.Append(item);
                    item.Click([start_quick_reload_fn, msic, shared_data, dmsp_gen_fn](IInspectable const& sender, auto&&) {
                        auto item = sender.as<ToggleMenuFlyoutItem>();
                        // Reload stream
                        if constexpr (requires{ msic.purge_hras_audio_cache(); }) {
                            msic.purge_hras_audio_cache();
                        }
                        shared_data->audio_use_flac = item.IsChecked();
                        start_quick_reload_fn(msic, shared_data->cur_video_is_60fps, dmsp_gen_fn);
                    });
                }
            };

            if (video_dash.audio.empty()) {
                using MSIC = MediaSourceInputContext<MSICInput_DashStreamNoAudio>;

                auto msic = MSIC(client, video_avid, cid, param,
                    [=](::BiliUWP::VideoPlayUrl_Dash const& v) -> MSIC::SelectPolicyFnRet {
                        ::BiliUWP::VideoPlayUrl_Dash_Stream const* pv = &v.video[0];
                        auto vit = std::find(v.video.begin(), v.video.end(), shared_data->vid);
                        if (vit != v.video.end()) { pv = &*vit; }
                        return *pv;
                    }, &video_dash
                );

                setup_streams_selection_menu_fn(msic, MakeDMSP_DashNativeHrasNoAudio);

                dmsp = MakeDMSP_DashNativeHrasNoAudio(
                    m_http_client_m, msic, MediaPlayerStateOverlay());
            }
            else {
                // TODO: Let user choose audio quality (including lossless ones)
                auto& audio_stream = video_dash.audio[0];
                shared_data->aid = audio_stream;

                util::debug::log_trace(std::format(L"Selecting audio stream {}, codecid = {}",
                    shared_data->aid.id, shared_data->aid.codecid));

                using MSIC = MediaSourceInputContext<MSICInput_DashStream>;

                /*m_media_insrc = MSIC(client, video_avid, cid, param,
                    [=](::BiliUWP::VideoPlayUrl_Dash const& v) -> MSIC::SelectPolicyFnRet {
                        ::BiliUWP::VideoPlayUrl_Dash_Stream const *pv = &v.video[0], *pa = &v.audio[0];
                        auto vit = std::find(v.video.begin(), v.video.end(), shared_data->vid);
                        if (vit != v.video.end()) { pv = &*vit; }
                        auto ait = std::find(v.audio.begin(), v.audio.end(), shared_data->aid);
                        if (ait != v.audio.end()) { pa = &*ait; }
                        return { *pv, *pa };
                    }, &video_dash
                );
                auto& msic = std::get<MSIC>(m_media_insrc);*/
                auto msic = MSIC(client, video_avid, cid, param,
                    [=](::BiliUWP::VideoPlayUrl_Dash const& v) -> MSIC::SelectPolicyFnRet {
                        ::BiliUWP::VideoPlayUrl_Dash_Stream const *pv = &v.video[0], *pa = &v.audio[0];
                        auto vit = std::find(v.video.begin(), v.video.end(), shared_data->vid);
                        if (vit != v.video.end()) { pv = &*vit; }
                        if (shared_data->audio_use_flac && v.flac && v.flac->audio) {
                            pa = &*v.flac->audio;
                        }
                        else {
                            auto ait = std::find(v.audio.begin(), v.audio.end(), shared_data->aid);
                            if (ait != v.audio.end()) { pa = &*ait; }
                        }
                        return { *pv, *pa };
                    }, &video_dash
                );

                setup_streams_selection_menu_fn(msic, MakeDMSP_DashNativeHras);

                dmsp = MakeDMSP_DashNativeHras(
                    m_http_client_m, msic, MediaPlayerStateOverlay());
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

        auto fetch_vpart_meta_task = fetch_vpart_meta_fn(video_avid, cid,
            client, m_http_client);

        auto dmpsp = std::make_shared<DMPSP_Video>(dmsp, MediaPlayerStateOverlay(),
            fetch_vpart_meta_task, video_title, video_part_title, video_cover_uri);

        // TODO: Add configurable support for heartbeat packages

        SetDanmakuSource(cid, video_avid);

        co_await this->SubmitMediaPlaybackSourceToNativePlayer_V2(dmpsp, nullptr,
            m_cfg_model.App_UseCustomVideoPresenter(), is_60fps_video);
    }
    // TODO: Remove this
#if 1
    util::winrt::task<> MediaPlayPage::PlayVideoWithCidInner_Legacy(uint64_t cid) {
        auto cancellation_token = co_await get_cancellation_token();
        cancellation_token.enable_propagation();
        auto ws = util::winrt::make_weak_storage(*this);

        // Body
        // WARN: Never access video_vinfo after any suspension point,
        //       as the media info may have changed type
        auto& video_vinfo = std::get<::BiliUWP::VideoViewInfoResult>(m_media_info);
        auto client = ::BiliUWP::App::get()->bili_client();

        this->SubmitMediaPlaybackSourceToNativePlayer(nullptr);

        auto video_avid = video_vinfo.avid;
        auto video_bvid = video_vinfo.bvid;

        struct VideoPartMetadata {
            uint64_t online_count;
            std::vector<TimedTextSource> tts_vec;
        };
        auto fetch_vpart_meta_fn = [&]() -> util::winrt::task<VideoPartMetadata> {
            std::vector<TimedTextSource> tts_vec;
            auto avid = video_avid;
            auto bvid = video_bvid;
            co_safe_capture(cid);
            auto weak_store = util::winrt::make_weak_storage(*this);
            util::debug::log_trace(L"Start fetching video metadata");
            weak_store.lock();
            co_await resume_background();
            auto client = ::BiliUWP::App::get()->bili_client();
            auto vinfo2 = co_await weak_store.ual(client->video_info_v2(bvid, cid));
            for (auto const& st : vinfo2.subtitle.list) {
                // TODO: Use ExternalTimedMetadataTracks to populate subtitles instead
                // Add BOM header so that TimedTextSource can load subtitles correctly
                std::wstring srt_str{ 0xfeff };
                util::debug::log_trace(std::format(L"Downloading subtitle from `{}`...", st.subtitle_url));
                auto jo = Windows::Data::Json::JsonObject::Parse(
                    co_await weak_store.ual(weak_store->m_http_client.GetStringAsync(Uri(st.subtitle_url)))
                );
                for (size_t idx = 0; auto&& i : jo.GetNamedArray(L"body")) {;
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
        auto video_pinfo = std::move(co_await ws.ual(client->video_play_url(
            video_bvid, cid, param
        )));

        std::shared_ptr<DetailedStatsProvider> ds_provider;
        MediaSource media_src{ nullptr };

        util::winrt::task<MediaPlayPage::MediaSrcDetailedStatsPair> video_task;

        bool is_60fps_video{ false };

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
            // A heuristic method for determining how VideoDanmakuPresenter should populate
            // the background (proactive or reactive), which may help reduce dropped frames
            if (util::num::try_parse_f64(video_stream.frame_rate).value_or(0) > 58) {
                is_60fps_video = true;
            }
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

        cancellation_token.callback([=] {
            video_task.cancel();
            fetch_vpart_meta_task.cancel();
        });

        ws.unlock();
        std::tie(media_src, ds_provider) = co_await video_task;
        auto se_media_src = util::misc::scope_exit([&] {
            // TODO: Find out the proper way to dispose resources
            if (auto src = media_src.AdaptiveMediaSource()) { src.Close(); }
            media_src.Close();
        });
        if (!ws.lock()) { co_return; }

        // TODO: Add configurable support for heartbeat packages

        // TODO: Improve subtitle handling
        MediaPlayerStateOverlay().SwitchToLoading(res_str(L"App/Page/MediaPlayPage/LoadingSubtitles"));
        auto vpart_meta = std::move(co_await ws.ual(fetch_vpart_meta_task));
        media_src.ExternalTimedTextSources().ReplaceAll(vpart_meta.tts_vec);

        util::winrt::check_cancellation(cancellation_token);

        SetDanmakuSource(cid, video_avid);

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
        co_await this->SubmitMediaPlaybackSourceToNativePlayer(media_playback_item, nullptr, ds_provider,
            m_cfg_model.App_UseCustomVideoPresenter(), is_60fps_video);

        se_media_src.release();

        util::debug::log_trace(std::format(L"Video pic url: {}", video_vinfo.cover_url));
        util::debug::log_trace(std::format(L"Video title: {}", video_vinfo.title));
    }
#endif
    util::winrt::task<> MediaPlayPage::PlayVideoWithCid(uint64_t cid) {
        auto cancellation_token = co_await get_cancellation_token();
        cancellation_token.enable_propagation();

        auto media_player_state_overlay = MediaPlayerStateOverlay();
        media_player_state_overlay.SwitchToLoading(res_str(L"App/Common/Loading"));
        try {
            // TODO: Change this
            if (m_cfg_model.App_UseHRASForVideo()) {
                co_await this->PlayVideoWithCidInner(cid);
            }
            else {
                co_await this->PlayVideoWithCidInner_Legacy(cid);
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
        MediaSource media_src{ nullptr };

        this->SubmitMediaPlaybackSourceToNativePlayer(nullptr);

        {
            apartment_context ui_ctx;

            co_await resume_background();

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
            media_src = MediaSource::CreateFromStream(http_stream, http_stream.ContentType());
            auto se_media_src = util::misc::scope_exit([&] {
                media_src.Close();
            });

            co_await ui_ctx;

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

            se_media_src.release();
        }
        auto se_media_src = util::misc::scope_exit([&] {
            // TODO: Find out the proper way to dispose resources
            media_src.Close();
        });

        auto media_playback_item = MediaPlaybackItem(media_src);
        auto display_props = media_playback_item.GetDisplayProperties();
        display_props.Type(MediaPlaybackType::Music);
        display_props.Thumbnail(RandomAccessStreamReference::CreateFromUri(Uri(audio_vinfo.cover_url)));
        display_props.MusicProperties().Title(audio_vinfo.audio_title);
        media_playback_item.ApplyDisplayProperties(display_props);
        co_await this->SubmitMediaPlaybackSourceToNativePlayer(
            media_playback_item, BitmapImage(Uri(audio_vinfo.cover_url)), ds_provider
        );
        se_media_src.release();
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

    util::winrt::task<> MediaPlayPage::SubmitMediaPlaybackSourceToNativePlayer_V2(
        std::shared_ptr<DetailedMediaPlaybackSourceProvider> source,
        ImageSource poster_source,
        bool enable_custom_presenter,
        bool use_reactive_present_mode
    ) {
        auto cancellation_token = co_await get_cancellation_token();
        cancellation_token.enable_propagation();

        auto ds_provider = source->get_detailed_stats_provider();

        auto media_player_elem = MediaPlayerElem();

        if (source == nullptr) {
            // Release MediaPlayer and related resources (themselves don't do cleanup)
            media_player_elem.AreTransportControlsEnabled(false);
            cleanup_media_player(media_player_elem);
            media_player_elem.PosterSource(nullptr);
            // Release detailed stats provider
            if (m_detailed_stats_update_timer) {
                m_detailed_stats_update_timer.Stop();
                m_detailed_stats_provider->TimerStopped();
                m_detailed_stats_update_timer = nullptr;
                m_detailed_stats_provider = nullptr;
            }
            co_return;
        }

        if (ds_provider == nullptr) {
            // Use a default provider to indicate that detailed stats are not supported
            ds_provider = std::make_shared<DetailedStatsProvider>();
        }

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

        auto ws = util::winrt::make_weak_storage(*this);

        auto playback_src = co_await ws.ual(source->get_media_playback_source_async());

        // Build media player
        auto media_player = co_await [](auto that, IMediaPlaybackSource const& source) -> util::winrt::task<MediaPlayer> {
            // NOTE: Creating a MediaPlayer takes ~50ms
            // NOTE: We should create MediaPlayer in the UI thread, so that it
            //       is aware of the transition to background and can save power
            auto media_player = MediaPlayer();

            co_await resume_background();

            if (::BiliUWP::App::get()->cfg_model().App_EnableRealtimePlayback()) {
                media_player.RealTimePlayback(true);
            }
            // Workaround a MediaPlayer resource leak bug by keeping MediaPlayer alive until media is actually opened
            // (at the cost of wasting some data)
            auto et_mo = std::make_shared_for_overwrite<event_token>();
            *et_mo = media_player.MediaOpened(
                [et_mo, that](MediaPlayer const& sender, IInspectable const&) {
                    util::debug::log_trace(L"Media opened");
                    sender.MediaOpened(*et_mo);
                    // Autoplay
                    that->Dispatcher().RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal,
                        [sender, that] {
                            if (that->IsLoaded()) {
                                sender.Play();
                                that->UpdateVideoDanmakuControlState();
                            }
                        }
                    );
                }
            );
            media_player.AudioCategory(MediaPlayerAudioCategory::Media);
            media_player.Source(source);

            co_return media_player;
        }(ws.get(), playback_src);
        auto se_media_player = util::misc::scope_exit([&] {
            media_player.Close();
        });
        m_custom_presenter_active = enable_custom_presenter;
        if (enable_custom_presenter) {
            // Force the creation of VideoDanmakuControl
            using Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface;
            UpdateVideoDanmakuControlState();
            m_video_danmaku_ctrl.SetBackgroundPopulator(
                [mp = media_player](IDirect3DSurface const& surface) {
                    mp.CopyFrameToVideoSurface(surface);
                    return true;
                }
            , !use_reactive_present_mode);
            media_player.IsVideoFrameServerEnabled(true);
            media_player.VideoFrameAvailable(
                [ctrl = m_video_danmaku_ctrl](MediaPlayer const&, auto&&) {
                    ctrl.TriggerBackgroundUpdate();
                }
            );
        }
        else {
            if (m_video_danmaku_ctrl) {
                m_video_danmaku_ctrl.SetBackgroundPopulator(nullptr, true);
            }
        }
        // TODO: Add individual volume support (+ optionally sync to global)
        this->EstablishMediaPlayerVolumeTwoWayBinding(media_player);
        media_player.IsLoopingEnabled(true);
        auto media_cmd_mgr = media_player.CommandManager();
        media_cmd_mgr.AutoRepeatModeReceived(
            [](MediaPlaybackCommandManager const& sender,
                MediaPlaybackCommandManagerAutoRepeatModeReceivedEventArgs const& e)
            {
                auto media_player = sender.MediaPlayer();
                media_player.IsLoopingEnabled(!media_player.IsLoopingEnabled());
                e.Handled(true);
            }
        );
        // TODO: Maybe (?) set some of the handlers in XAML?
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
        se_media_player.release();
        media_player_elem.PosterSource(poster_source);
        media_player_elem.AreTransportControlsEnabled(true);

        // TODO: Figure out how to correctly deal with this hack
        source->notify_completed();
    }

    // TODO: Remove this
    util::winrt::task<> MediaPlayPage::SubmitMediaPlaybackSourceToNativePlayer(
        IMediaPlaybackSource source,
        ImageSource poster_source,
        std::shared_ptr<DetailedStatsProvider> ds_provider,
        bool enable_custom_presenter,
        bool use_reactive_present_mode
    ) {
        auto media_player_elem = MediaPlayerElem();

        if (source == nullptr) {
            // Release MediaPlayer and related resources (themselves don't do cleanup)
            media_player_elem.AreTransportControlsEnabled(false);
            cleanup_media_player(media_player_elem);
            media_player_elem.PosterSource(nullptr);
            // Release detailed stats provider
            if (m_detailed_stats_update_timer) {
                m_detailed_stats_update_timer.Stop();
                m_detailed_stats_provider->TimerStopped();
                m_detailed_stats_update_timer = nullptr;
                m_detailed_stats_provider = nullptr;
            }
            co_return;
        }

        if (ds_provider == nullptr) {
            // Use a default provider to indicate that detailed stats are not supported
            ds_provider = std::make_shared<DetailedStatsProvider>();
        }

        auto strong_this = get_strong();

        // Build media player
        auto media_player = co_await [](auto that, IMediaPlaybackSource const& source) -> util::winrt::task<MediaPlayer> {
            // NOTE: Creating a MediaPlayer takes ~50ms
            // NOTE: We should create MediaPlayer in the UI thread, so that it
            //       is aware of the transition to background and can save power
            auto media_player = MediaPlayer();

            co_await resume_background();

            if (::BiliUWP::App::get()->cfg_model().App_EnableRealtimePlayback()) {
                media_player.RealTimePlayback(true);
            }
            // Workaround a MediaPlayer resource leak bug by keeping MediaPlayer alive until media is actually opened
            // (at the cost of wasting some data)
            auto et_mo = std::make_shared_for_overwrite<event_token>();
            *et_mo = media_player.MediaOpened(
                [et_mo, that](MediaPlayer const& sender, IInspectable const&) {
                    util::debug::log_trace(L"Media opened");
                    sender.MediaOpened(*et_mo);
                    // Autoplay
                    that->Dispatcher().RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal,
                        [sender, that] {
                            if (that->IsLoaded()) {
                                sender.Play();
                                that->UpdateVideoDanmakuControlState();
                            }
                        }
                    );
                }
            );
            media_player.AudioCategory(MediaPlayerAudioCategory::Media);
            media_player.Source(source);

            co_return media_player;
        }(strong_this, source);
        auto se_media_player = util::misc::scope_exit([&] {
            media_player.Close();
        });
        m_custom_presenter_active = enable_custom_presenter;
        if (enable_custom_presenter) {
            // Force the creation of VideoDanmakuControl
            using Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface;
            UpdateVideoDanmakuControlState();
            m_video_danmaku_ctrl.SetBackgroundPopulator(
                [mp = media_player](IDirect3DSurface const& surface) {
                    mp.CopyFrameToVideoSurface(surface);
                    return true;
                }
            , !use_reactive_present_mode);
            media_player.IsVideoFrameServerEnabled(true);
            media_player.VideoFrameAvailable(
                [ctrl = m_video_danmaku_ctrl](MediaPlayer const&, auto&&) {
                    ctrl.TriggerBackgroundUpdate();
                }
            );
        }
        else {
            if (m_video_danmaku_ctrl) {
                m_video_danmaku_ctrl.SetBackgroundPopulator(nullptr, true);
            }
        }
        // TODO: Add individual volume support (+ optionally sync to global)
        this->EstablishMediaPlayerVolumeTwoWayBinding(media_player);
        media_player.IsLoopingEnabled(true);
        auto media_cmd_mgr = media_player.CommandManager();
        media_cmd_mgr.AutoRepeatModeReceived(
            [](MediaPlaybackCommandManager const& sender,
                MediaPlaybackCommandManagerAutoRepeatModeReceivedEventArgs const& e)
            {
                auto media_player = sender.MediaPlayer();
                media_player.IsLoopingEnabled(!media_player.IsLoopingEnabled());
                e.Handled(true);
            }
        );
        // TODO: Maybe (?) set some of the handlers in XAML?
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
        se_media_player.release();
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
                    [mp = sender] {
                        auto volume = static_cast<uint32_t>(std::llround(mp.Volume() * 10000));
                        ::BiliUWP::App::get()->cfg_model().App_GlobalVolume(volume);
                    }
                );
            }
        );
        // GlobalConfig -> MediaPlayer
        m_cfg_changed_revoker = ::BiliUWP::App::get()->cfg_model().PropertyChanged(auto_revoke,
            [weak_mp = make_weak(player)](IInspectable const&, PropertyChangedEventArgs const& e) {
                auto mp = weak_mp.get();
                if (!mp) { return; }
                if (e.PropertyName() == L"App_GlobalVolume") {
                    mp.Volume(::BiliUWP::App::get()->cfg_model().App_GlobalVolume() / 10000.0);
                }
            }
        );
    }
    void MediaPlayPage::SwitchMediaPlayPause() {
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
    }
    void MediaPlayPage::SetDanmakuSource(uint64_t cid, uint64_t avid) {
        m_async_danmaku.cancel_running();
        if (m_video_danmaku_ctrl) {
            m_video_danmaku_ctrl.Stop(false);
            m_video_danmaku_ctrl.Danmaku().ClearAll();
        }
        m_danmaku_segs.clear();
        m_danmaku_cid = cid;
        m_danmaku_avid = avid;
        UpdateVideoDanmakuControlState();
    }
    void MediaPlayPage::QueueLoadDanmakuFromTimestamp(uint32_t sec) {
        if (m_danmaku_cid == 0 || m_danmaku_avid == 0) { return; }
        bool need_queue_task{};
        // NOTE: API danmaku segment size is 6min
        uint32_t segment_idx = sec / (6 * 60);
        auto is_seg_expired_fn = [this](size_t seg_idx) {
            constexpr std::chrono::system_clock::time_point zero_tp{};
            return m_danmaku_segs[seg_idx].last_update_tp == zero_tp;
        };
        if (m_danmaku_segs.empty() || is_seg_expired_fn(segment_idx)) {
            need_queue_task = true;
        }
        if (need_queue_task) {
            m_async_danmaku.run_if_idle([](MediaPlayPage* that, uint32_t seg_idx) -> IAsyncAction {
                auto cancellation_token = co_await get_cancellation_token();
                cancellation_token.enable_propagation();

                co_await resume_background();

                util::debug::log_trace(L"MediaPlayPage: Danmaku load task started");

                auto is_seg_expired_fn = [that](size_t seg_idx) {
                    constexpr std::chrono::system_clock::time_point zero_tp{};
                    return that->m_danmaku_segs[seg_idx].last_update_tp == zero_tp;
                };
                auto weak_store = util::winrt::make_weak_storage(*that);
                auto client = ::BiliUWP::App::get()->bili_client();
                // Get total danmaku segments count
                if (that->m_danmaku_segs.empty()) {
                    auto result = co_await weak_store.ual(client->danmaku_web_view_info(
                        that->m_danmaku_cid, that->m_danmaku_avid));
                    if (result.dm_seg.page_size != 6 * 60 * 1000) {
                        util::debug::log_error(std::format(
                            L"Got unexpected danmaku segment size (expected {}, got {})",
                            6 * 60 * 1000, result.dm_seg.page_size
                        ));
                    }
                    that->m_danmaku_segs.resize(result.dm_seg.total);
                }
                if (!is_seg_expired_fn(seg_idx)) { co_return; }
                // Load all danmaku
                auto dm_collection = that->m_video_danmaku_ctrl.Danmaku();
                dm_collection.ClearAll();
                std::vector<VideoDanmakuNormalItem> dm_normal_items;
                for (uint32_t seg_idx = 0; seg_idx < that->m_danmaku_segs.size(); seg_idx++) {
                    dm_normal_items.clear();
                    that->m_danmaku_segs[seg_idx].last_update_tp = std::chrono::system_clock::now();
                    auto result = co_await weak_store.ual(client->danmaku_normal_list(
                        that->m_danmaku_cid, that->m_danmaku_avid, seg_idx + 1));
                    auto& dm_elems = result.elems;
                    util::winrt::check_cancellation(cancellation_token);
                    dm_normal_items.reserve(dm_elems.size());
                    std::transform(dm_elems.begin(), dm_elems.end(),
                        std::back_inserter(dm_normal_items),
                        [](::BiliUWP::DanmakuNormalList_DanmakuElement const& dm) {
                            VideoDanmakuNormalItem item;
                            item.id = dm.id;
                            item.appear_time = dm.progress;
                            switch (dm.mode) {
                            default:
                                [[fallthrough]];
                            case 1:
                                item.mode = VideoDanmakuDisplayMode::Scroll;
                                break;
                            case 4:
                                item.mode = VideoDanmakuDisplayMode::Bottom;
                                break;
                            case 5:
                                item.mode = VideoDanmakuDisplayMode::Top;
                                break;
                            }
                            item.font_size = static_cast<float>(dm.font_size);
                            item.color = util::winrt::to_color(dm.color);
                            item.content = dm.content;
                            return item;
                        }
                    );
                    dm_collection.AddManyNormal(dm_normal_items);
                }

                util::debug::log_trace(L"MediaPlayPage: Danmaku load task finished");
            }, this, segment_idx);
        }
    }
    void MediaPlayPage::UpdateVideoDanmakuControlState(void) {
        bool ctrl_should_exist{}, should_link_danmaku{};
        auto sess = util::winrt::try_get_media_playback_session(MediaPlayerElem().MediaPlayer());
        if (sess && m_danmaku_enabled && (m_danmaku_cid != 0 && m_danmaku_avid != 0)) {
            should_link_danmaku = true;
        }
        if (should_link_danmaku || m_custom_presenter_active) {
            ctrl_should_exist = true;
        }

        if (ctrl_should_exist) {
            if (!m_video_danmaku_ctrl) {
                m_video_danmaku_ctrl = BiliUWP::VideoDanmakuControl();
                m_video_danmaku_ctrl.EnableDebugOutput(
                    ::BiliUWP::App::get()->cfg_model().App_DebugVideoDanmakuControl());
                MTCMiddleLayerGrid().Children().InsertAt(0, m_video_danmaku_ctrl);
            }
        }
        if (should_link_danmaku) {
            // Assuming m_video_danmaku_ctrl exists
            if (m_danmaku_media_play_sess != sess) {
                auto handler = [ctrl = m_video_danmaku_ctrl](MediaPlaybackSession const& session, IInspectable const&) {
                    auto state = session.PlaybackState();
                    if (state == MediaPlaybackState::Playing) {
                        ctrl.Start();
                        ctrl.UpdateCurrentTime(session.Position());
                    }
                    else {
                        ctrl.Pause();
                    }
                };
                sess.PlaybackStateChanged(handler);
                handler(sess, nullptr);
                sess.PositionChanged([ctrl = m_video_danmaku_ctrl](MediaPlaybackSession const& session, auto&&) {
                    auto sess_pos = session.Position();
                    ctrl.UpdateCurrentTime(sess_pos);
                });
                // TODO: Adjust danmaku load logic
                QueueLoadDanmakuFromTimestamp(0);
                m_danmaku_media_play_sess = sess;
            }
            m_video_danmaku_ctrl.IsDanmakuVisible(true);
            m_video_danmaku_ctrl.Visibility(Visibility::Visible);
        }
        else {
            if (m_video_danmaku_ctrl) {
                m_video_danmaku_ctrl.IsDanmakuVisible(false);
                if (!ctrl_should_exist) {
                    m_video_danmaku_ctrl.Visibility(Visibility::Collapsed);
                }
            }
        }
    }
    void MediaPlayPage::ResetMTCStreamSelectionMenu() {
        // https://github.com/microsoft/microsoft-ui-xaml/issues/7797
        auto orig_menu = MTCStreamSelectionMenu();
        auto icon = orig_menu.Icon();
        auto additional_items = MediaTransportCtrl().AdditionalItems();
        uint32_t index = 0;
        additional_items.IndexOf(orig_menu, index);
        additional_items.RemoveAt(index);

        auto menu = MenuFlyoutSubItem();
        menu.Name(L"MTCStreamSelectionMenu");
        // We cannot set x:Uid in code behind
        menu.Text(::BiliUWP::App::res_str(L"App/Page/MediaPlayPage/MTCStreamSelectionMenu/Text"));
        menu.Icon(icon);
        CustomMediaTransportControlsHelper::SetInsertBefore(menu, L"ZoomMenuItem");
        MTCStreamSelectionMenu(menu);
        additional_items.InsertAt(index, menu);
    }
}
