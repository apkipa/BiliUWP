#pragma once

#include "MediaPlayPage.g.h"
#include "util.hpp"

namespace winrt::BiliUWP::implementation {
    struct MediaPlayPage : MediaPlayPageT<MediaPlayPage> {
        MediaPlayPage();

        bool IsBiliResReady(void) { return m_bili_res_is_ready; }
        hstring BiliResId() { return m_bili_res_id_a; }
        hstring BiliResUrl() {
            if (m_bili_res_id_a == L"") { return L""; }
            return hstring{ L"https://www.bilibili.com/video/" } + m_bili_res_id_a;
        }
        hstring BiliResId2() { return m_bili_res_id_b; }
        hstring BiliResUrl2() {
            if (m_bili_res_id_b == L"") { return L""; }
            return hstring{ L"https://www.bilibili.com/video/" } + m_bili_res_id_b;
        }

        void OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs const& e);

        static fire_forget_except final_release(std::unique_ptr<MediaPlayPage> ptr) noexcept;

    private:
        Windows::Foundation::IAsyncAction HandleVideoPlay(uint64_t avid, hstring bvid);
        Windows::Foundation::IAsyncAction HandleAudioPlay(uint64_t auid);

        Windows::Web::Http::HttpClient m_http_client;

        util::winrt::async_storage m_cur_async;

        bool m_bili_res_is_ready;
        hstring m_bili_res_id_a, m_bili_res_id_b;
    };
}

namespace winrt::BiliUWP::factory_implementation {
    struct MediaPlayPage : MediaPlayPageT<MediaPlayPage, implementation::MediaPlayPage> {};
}
