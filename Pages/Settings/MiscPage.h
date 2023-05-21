#pragma once

#include "Settings/MiscPage.g.h"
#include "Settings/MainPage.h"

namespace winrt::BiliUWP::Settings::implementation {
    struct MiscPage : MiscPageT<MiscPage> {
        MiscPage();

        void OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs const&);

        BiliUWP::AppCfgModel CfgModel() const { return m_main_page->CfgModel(); }

    private:
        MainPage* m_main_page{};
    };
}

namespace winrt::BiliUWP::Settings::factory_implementation {
    struct MiscPage : MiscPageT<MiscPage, implementation::MiscPage> {};
}
