#pragma once

#include "MediaPlayPage.g.h"

namespace winrt::BiliUWP::implementation {
    struct MediaPlayPage : MediaPlayPageT<MediaPlayPage> {
        MediaPlayPage();

        void OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs const& e);

        static fire_and_forget final_release(std::unique_ptr<MediaPlayPage> ptr) noexcept;

    private:
        Windows::Foundation::IAsyncAction HandleVideoPlay(uint64_t avid, hstring bvid);
        Windows::Foundation::IAsyncAction HandleAudioPlay(uint64_t auid);

        Windows::Web::Http::HttpClient m_http_client;

        Windows::Foundation::IAsyncAction m_cur_async_op;
    };
}

namespace winrt::BiliUWP::factory_implementation {
    struct MediaPlayPage : MediaPlayPageT<MediaPlayPage, implementation::MediaPlayPage> {};
}
