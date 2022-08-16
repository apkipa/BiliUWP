#pragma once

#include "SettingsPage.g.h"

namespace winrt::BiliUWP::implementation {
    struct SettingsPage : SettingsPageT<SettingsPage> {
        SettingsPage();

        void ClickHandler(Windows::Foundation::IInspectable const& sender, Windows::UI::Xaml::RoutedEventArgs const& args);
    };
}

namespace winrt::BiliUWP::factory_implementation {
    struct SettingsPage : SettingsPageT<SettingsPage, implementation::SettingsPage> {};
}
