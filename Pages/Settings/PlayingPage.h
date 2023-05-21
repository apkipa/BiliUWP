#pragma once

#include "Settings/PlayingPage.g.h"
#include "Settings/MainPage.h"

namespace winrt::BiliUWP::Settings::implementation {
    struct PlayingPage : PlayingPageT<PlayingPage> {
        PlayingPage();

        void OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs const&);

        BiliUWP::AppCfgModel CfgModel() const { return m_main_page->CfgModel(); }

    private:
        MainPage* m_main_page{};
    };
}

namespace winrt::BiliUWP::Settings::factory_implementation {
    struct PlayingPage : PlayingPageT<PlayingPage, implementation::PlayingPage> {};
}
