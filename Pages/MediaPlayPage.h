#pragma once

#include "MediaPlayPage.g.h"

namespace winrt::BiliUWP::implementation {
    struct MediaPlayPage : MediaPlayPageT<MediaPlayPage> {
        MediaPlayPage();

        void OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs const& e);

        static fire_and_forget final_release(std::unique_ptr<MediaPlayPage> ptr) noexcept {
            if (auto cur_async_op = ptr->m_cur_async_op) {
                cur_async_op.Cancel();
            }
            // Perform time-consuming destruction of media player in background;
            // prevents UI from freezing
            co_await ptr->Dispatcher();
            ptr->MediaPlayerElem().AreTransportControlsEnabled(false);
            auto player = ptr->MediaPlayerElem().MediaPlayer();
            ptr->MediaPlayerElem().SetMediaPlayer(nullptr);
            co_await resume_background();
        }

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
