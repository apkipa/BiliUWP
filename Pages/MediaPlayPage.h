#pragma once

#include "MediaPlayPage.g.h"
#include "MediaPlayPage_UpItem.g.h"
#include "MediaPlayPage_PartItem.g.h"
#include "util.hpp"
#include "BiliClient.hpp"

namespace winrt::BiliUWP::implementation {
    struct DetailedStatsContext;
    struct DetailedStatsProvider;
    struct DetailedMediaPlaybackSourceProvider;
    template<typename T> struct MediaSourceInputContext;
    struct MSICInput_DashStream;

    struct MediaPlayPage_UpItem : MediaPlayPage_UpItemT<MediaPlayPage_UpItem> {
        MediaPlayPage_UpItem(hstring up_name, hstring up_face_url, uint64_t up_mid) :
            m_up_name(std::move(up_name)), m_up_face_url(std::move(up_face_url)), m_up_mid(up_mid) {}
        hstring UpName() { return m_up_name; }
        hstring UpFaceUrl() { return m_up_face_url; }
        uint64_t UpMid() { return m_up_mid; }
    private:
        hstring m_up_name;
        hstring m_up_face_url;
        uint64_t m_up_mid;
    };
    struct MediaPlayPage_PartItem : MediaPlayPage_PartItemT<MediaPlayPage_PartItem> {
        MediaPlayPage_PartItem(uint64_t no, hstring part_name, uint64_t cid) :
            m_no(no), m_part_name(std::move(part_name)), m_cid(cid) {}
        uint64_t PartNo() { return m_no; }
        hstring PartNoText() { return L"P" + to_hstring(m_no); }
        hstring PartName() { return m_part_name; }
        uint64_t PartCid() { return m_cid; }
    private:
        uint64_t m_no;
        hstring m_part_name;
        uint64_t m_cid;
    };

    struct MediaPlayPage : MediaPlayPageT<MediaPlayPage> {
        MediaPlayPage();
        void InitializeComponent();

        bool IsBiliResReady(void) { return m_bili_res_is_ready; }
        hstring BiliResId() {
            if (auto p = std::get_if<::BiliUWP::VideoViewInfoResult>(&m_media_info)) {
                return L"av" + to_hstring(p->avid);
            }
            if (auto p = std::get_if<::BiliUWP::AudioBasicInfoResult>(&m_media_info)) {
                return L"au" + to_hstring(p->auid);
            }
            return L"";
        }
        hstring BiliResUrl() {
            if (auto p = std::get_if<::BiliUWP::VideoViewInfoResult>(&m_media_info)) {
                return L"https://www.bilibili.com/video/av" + to_hstring(p->avid);
            }
            if (auto p = std::get_if<::BiliUWP::AudioBasicInfoResult>(&m_media_info)) {
                return L"https://www.bilibili.com/audio/au" + to_hstring(p->auid);
            }
            return L"";
        }
        hstring BiliResId2() {
            if (auto p = std::get_if<::BiliUWP::VideoViewInfoResult>(&m_media_info)) {
                return p->bvid;
            }
            return L"";
        }
        hstring BiliResUrl2() {
            if (auto p = std::get_if<::BiliUWP::VideoViewInfoResult>(&m_media_info)) {
                return L"https://www.bilibili.com/video/" + p->bvid;
            }
            return L"";
        }

        void OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs const& e);
        void OnPreviewKeyDown(Windows::UI::Xaml::Input::KeyRoutedEventArgs const& e);
        void OnKeyDown(Windows::UI::Xaml::Input::KeyRoutedEventArgs const& e);
        void OnPointerReleased(Windows::UI::Xaml::Input::PointerRoutedEventArgs const& e);

        void UpListView_ItemClick(
            Windows::Foundation::IInspectable const&, Windows::UI::Xaml::Controls::ItemClickEventArgs const& e
        );
        void PartsListView_SelectionChanged(
            Windows::Foundation::IInspectable const&,
            Windows::UI::Xaml::Controls::SelectionChangedEventArgs const& e
        );
        void PartsListView_RightTapped(
            Windows::Foundation::IInspectable const& sender,
            Windows::UI::Xaml::Input::RightTappedRoutedEventArgs const& e
        );
        void MTCDanmakuSwitchButton_Click(
            Windows::Foundation::IInspectable const&,
            Windows::UI::Xaml::RoutedEventArgs const&
        );

        hstring MediaTitle() {
            if (auto p = std::get_if<::BiliUWP::VideoViewInfoResult>(&m_media_info)) {
                return p->title;
            }
            if (auto p = std::get_if<::BiliUWP::AudioBasicInfoResult>(&m_media_info)) {
                return p->audio_title;
            }
            return L"";
        }
        hstring MediaCoverImageUrl() {
            hstring result_url = L"";
            if (auto p = std::get_if<::BiliUWP::VideoViewInfoResult>(&m_media_info)) {
                result_url = p->cover_url;
            }
            if (auto p = std::get_if<::BiliUWP::AudioBasicInfoResult>(&m_media_info)) {
                result_url = p->cover_url;
            }
            return result_url != L"" ? result_url : L"about:blank";
        }
        hstring MediaDescription() {
            if (auto p = std::get_if<::BiliUWP::VideoViewInfoResult>(&m_media_info)) {
                return p->desc;
            }
            if (auto p = std::get_if<::BiliUWP::AudioBasicInfoResult>(&m_media_info)) {
                return p->audio_intro;
            }
            return {};
        }
        hstring MediaPublishTimeStr() {
            uint64_t ts{};
            if (auto p = std::get_if<::BiliUWP::VideoViewInfoResult>(&m_media_info)) {
                ts = p->ctime;
            }
            if (auto p = std::get_if<::BiliUWP::AudioBasicInfoResult>(&m_media_info)) {
                ts = p->pubtime;
            }
            return util::time::timestamp_to_str(ts);
        }
        uint64_t MediaPlayCount() {
            if (auto p = std::get_if<::BiliUWP::VideoViewInfoResult>(&m_media_info)) {
                return p->stat.view_count.value_or(0);
            }
            if (auto p = std::get_if<::BiliUWP::AudioBasicInfoResult>(&m_media_info)) {
                return p->statistic.play_count;
            }
            return {};
        }
        uint64_t MediaDanmakuCount() {
            if (auto p = std::get_if<::BiliUWP::VideoViewInfoResult>(&m_media_info)) {
                return p->stat.danmaku_count;
            }
            if (auto p = std::get_if<::BiliUWP::AudioBasicInfoResult>(&m_media_info)) {
                return 0;
            }
            return {};
        }
        uint64_t MediaCommentsCount() {
            if (auto p = std::get_if<::BiliUWP::VideoViewInfoResult>(&m_media_info)) {
                return p->stat.reply_count;
            }
            if (auto p = std::get_if<::BiliUWP::AudioBasicInfoResult>(&m_media_info)) {
                return p->statistic.comment_count;
            }
            return {};
        }
        Windows::Foundation::Collections::IObservableVector<Windows::Foundation::IInspectable> UpList() {
            return m_up_list;
        }
        bool ShouldShowPartsList() {
            return m_parts_list.Size() > 0;
        }
        Windows::Foundation::Collections::IObservableVector<Windows::Foundation::IInspectable> PartsList() {
            return m_parts_list;
        }

        BiliUWP::AppCfgModel CfgModel() { return m_cfg_model; }

        static fire_forget_except final_release(std::unique_ptr<MediaPlayPage> ptr) noexcept;

    private:
        friend struct DetailedStatsContext;

        using MediaSrcDetailedStatsPair =
            std::pair<Windows::Media::Core::MediaSource, std::shared_ptr<DetailedStatsProvider>>;
        // [video_stream, audio_stream]
        using PlayUrlDashStreamPair =
            std::pair<::BiliUWP::VideoPlayUrl_Dash_Stream, ::BiliUWP::VideoPlayUrl_Dash_Stream>;

        Windows::Foundation::IAsyncAction NavHandleVideoPlay(uint64_t avid, hstring bvid);
        Windows::Foundation::IAsyncAction NavHandleAudioPlay(uint64_t auid);
        util::winrt::task<> UpdateVideoInfoInner(std::variant<uint64_t, hstring> vid);
        util::winrt::task<> UpdateVideoInfo(std::variant<uint64_t, hstring> vid);
        Windows::Storage::Streams::IRandomAccessStream PlayVideoWithCidInner_DashNative_MakeDashMpdStream(
            ::BiliUWP::VideoPlayUrl_Dash const& dash_info,
            ::BiliUWP::VideoPlayUrl_Dash_Stream const& vstream,
            ::BiliUWP::VideoPlayUrl_Dash_Stream const* pastream
        );
        // WARN: Caller must guarantee that passed parameters are valid during the whole invocation
        util::winrt::task<MediaSrcDetailedStatsPair> PlayVideoWithCidInner_DashNativeNative(
            ::BiliUWP::VideoPlayUrl_Dash const& dash_info,
            ::BiliUWP::VideoPlayUrl_Dash_Stream const& vstream,
            ::BiliUWP::VideoPlayUrl_Dash_Stream const* pastream
        );
        util::winrt::task<MediaSrcDetailedStatsPair> PlayVideoWithCidInner_DashNativeHras(
            ::BiliUWP::VideoPlayUrl_Dash const& dash_info,
            ::BiliUWP::VideoPlayUrl_Dash_Stream const& vstream,
            ::BiliUWP::VideoPlayUrl_Dash_Stream const& astream,
            std::function<util::winrt::task<PlayUrlDashStreamPair>(void)> get_new_stream_fn
        );
        util::winrt::task<MediaSrcDetailedStatsPair> PlayVideoWithCidInner_DashNativeHrasNoAudio(
            ::BiliUWP::VideoPlayUrl_Dash const& dash_info,
            ::BiliUWP::VideoPlayUrl_Dash_Stream const& vstream,
            std::function<util::winrt::task<::BiliUWP::VideoPlayUrl_Dash_Stream>(void)> get_new_stream_fn
        );
        util::winrt::task<> PlayVideoWithCidInner(uint64_t cid);
        util::winrt::task<> PlayVideoWithCidInner_Legacy(uint64_t cid);
        util::winrt::task<> PlayVideoWithCid(uint64_t cid);
        util::winrt::task<> UpdateAudioInfoInner(uint64_t auid);
        util::winrt::task<> UpdateAudioInfo(uint64_t auid);
        util::winrt::task<> PlayAudioInner();
        util::winrt::task<> PlayAudio();

        // NOTE: Setting source to null will only stop current media
        util::winrt::task<> SubmitMediaPlaybackSourceToNativePlayer_V2(
            std::shared_ptr<DetailedMediaPlaybackSourceProvider> source,
            Windows::UI::Xaml::Media::ImageSource poster_source = nullptr,
            bool enable_custom_presenter = false,
            bool use_reactive_present_mode = false      // For custom presenter
        );
        util::winrt::task<> SubmitMediaPlaybackSourceToNativePlayer(
            Windows::Media::Playback::IMediaPlaybackSource source,
            Windows::UI::Xaml::Media::ImageSource poster_source = nullptr,
            std::shared_ptr<DetailedStatsProvider> ds_provider = nullptr,
            bool enable_custom_presenter = false,
            bool use_reactive_present_mode = false      // For custom presenter
        );
        void TriggerMediaDetailedStatsUpdate(void);
        void EstablishMediaPlayerVolumeTwoWayBinding(Windows::Media::Playback::MediaPlayer const& player);
        void SwitchMediaPlayPause();

        // NOTE: Currently only video danmaku source is supported
        void SetDanmakuSource(uint64_t cid, uint64_t avid);
        // NOTE: You don't call this method directly; it's for internal use
        void QueueLoadDanmakuFromTimestamp(uint32_t sec);
        void UpdateVideoDanmakuControlState(void);

        void ResetMTCStreamSelectionMenu();

        BiliUWP::AppCfgModel m_cfg_model;

        // NOTE: HttpClient for general-purpose fetching and dedicated media fetching
        Windows::Web::Http::HttpClient m_http_client, m_http_client_m;

        util::winrt::async_storage m_cur_async;

        bool m_bili_res_is_ready;

        Windows::Foundation::Collections::IObservableVector<Windows::Foundation::IInspectable> m_up_list;
        Windows::Foundation::Collections::IObservableVector<Windows::Foundation::IInspectable> m_parts_list;
        std::variant<std::monostate, ::BiliUWP::VideoViewInfoResult, ::BiliUWP::AudioBasicInfoResult>
            m_media_info;

        //std::variant<std::monostate, MediaSourceInputContext<MSICInput_DashStream>> m_media_insrc;

        Windows::Media::Playback::MediaPlayer::VolumeChanged_revoker m_volume_changed_revoker;
        BiliUWP::AppCfgModel::PropertyChanged_revoker m_cfg_changed_revoker;

        Windows::UI::Xaml::DispatcherTimer m_detailed_stats_update_timer;
        std::shared_ptr<DetailedStatsProvider> m_detailed_stats_provider;

        // One-time flag to save a HTTP request and speed up 1p video playing
        // TODO: Use m_use_1p_data -> (more general) m_is_stream_fresh
        bool m_use_1p_data;

        bool m_danmaku_enabled{ false };
        bool m_custom_presenter_active{ false };
        BiliUWP::VideoDanmakuControl m_video_danmaku_ctrl{ nullptr };
        util::winrt::async_storage m_async_danmaku;
        struct DanmakuSegmentInfo {
            std::chrono::system_clock::time_point last_update_tp;
            std::vector<::BiliUWP::DanmakuNormalList_DanmakuElement> elems;
        };
        std::vector<DanmakuSegmentInfo> m_danmaku_segs;
        uint64_t m_danmaku_cid{}, m_danmaku_avid{};
        // TODO: Remove this
        Windows::Media::Playback::MediaPlaybackSession m_danmaku_media_play_sess{ nullptr };
    };
}

namespace winrt::BiliUWP::factory_implementation {
    struct MediaPlayPage : MediaPlayPageT<MediaPlayPage, implementation::MediaPlayPage> {};
}
