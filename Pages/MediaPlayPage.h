﻿#pragma once

#include "MediaPlayPage.g.h"
#include "MediaPlayPage_UpItem.g.h"
#include "util.hpp"
#include "BiliClient.hpp"

namespace winrt::BiliUWP::implementation {
    struct DetailedStatsContext;
    struct DetailedStatsProvider;

    struct MediaPlayPage_UpItem : MediaPlayPage_UpItemT<MediaPlayPage_UpItem> {
        MediaPlayPage_UpItem(hstring up_name, hstring up_face_url, uint64_t up_mid) :
            m_up_name(up_name), m_up_face_url(up_face_url), m_up_mid(up_mid) {}
        hstring UpName() { return m_up_name; }
        hstring UpFaceUrl() { return m_up_face_url; }
        uint64_t UpMid() { return m_up_mid; }
    private:
        hstring m_up_name;
        hstring m_up_face_url;
        uint64_t m_up_mid;
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
            return L"";
        }
        Windows::Foundation::Collections::IObservableVector<Windows::Foundation::IInspectable> UpList() {
            return m_up_list;
        }

        static fire_forget_except final_release(std::unique_ptr<MediaPlayPage> ptr) noexcept;

    private:
        friend struct DetailedStatsContext;

        Windows::Foundation::IAsyncAction NavHandleVideoPlay(uint64_t avid, hstring bvid);
        Windows::Foundation::IAsyncAction NavHandleAudioPlay(uint64_t auid);
        util::winrt::task<> UpdateVideoInfoInner(std::variant<uint64_t, hstring> vid);
        util::winrt::task<> UpdateVideoInfo(std::variant<uint64_t, hstring> vid);
        util::winrt::task<> PlayVideoWithCidInner(uint64_t cid);
        util::winrt::task<> PlayVideoWithCid(uint64_t cid);
        util::winrt::task<> UpdateAudioInfoInner(uint64_t auid);
        util::winrt::task<> UpdateAudioInfo(uint64_t auid);
        util::winrt::task<> PlayAudioInner();
        util::winrt::task<> PlayAudio();

        // NOTE: Setting source to null will only stop current media
        void SubmitMediaPlaybackSourceToNativePlayer(
            Windows::Media::Playback::IMediaPlaybackSource const& source,
            Windows::UI::Xaml::Media::ImageSource const& poster_source = nullptr,
            std::shared_ptr<DetailedStatsProvider> ds_provider = nullptr
        );
        void TriggerMediaDetailedStatsUpdate(void);
        void EstablishMediaPlayerVolumeTwoWayBinding(Windows::Media::Playback::MediaPlayer const& player);

        Windows::Web::Http::HttpClient m_http_client;

        util::winrt::async_storage m_cur_async;

        bool m_bili_res_is_ready;

        Windows::Foundation::Collections::IObservableVector<Windows::Foundation::IInspectable> m_up_list;
        std::variant<
            std::monostate, ::BiliUWP::VideoViewInfoResult, ::BiliUWP::AudioBasicInfoResult
        > m_media_info;

        Windows::Media::Playback::MediaPlayer::VolumeChanged_revoker m_volume_changed_revoker;
        BiliUWP::AppCfgModel::PropertyChanged_revoker m_cfg_changed_revoker;

        Windows::UI::Xaml::DispatcherTimer m_detailed_stats_update_timer;
        std::shared_ptr<DetailedStatsProvider> m_detailed_stats_provider;
    };
}

namespace winrt::BiliUWP::factory_implementation {
    struct MediaPlayPage : MediaPlayPageT<MediaPlayPage, implementation::MediaPlayPage> {};
}
