#pragma once

#include "Settings/AboutPage.g.h"
#include "Settings/MainPage.h"

namespace winrt::BiliUWP::Settings::implementation {
    struct AboutPage : AboutPageT<AboutPage> {
        AboutPage();

        void OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs const& e);
        void AppNameVerTextBlock_Tapped(
            Windows::Foundation::IInspectable const&,
            Windows::UI::Xaml::Input::TappedRoutedEventArgs const& e
        );
        void ViewLicensesButton_Click(
            Windows::Foundation::IInspectable const&,
            Windows::UI::Xaml::RoutedEventArgs const&
        );

    private:
        MainPage* m_main_page{};
        unsigned m_app_name_ver_text_block_click_times{};
    };
}

namespace winrt::BiliUWP::Settings::factory_implementation {
    struct AboutPage : AboutPageT<AboutPage, implementation::AboutPage> {};
}
