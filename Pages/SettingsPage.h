#pragma once

#include "SettingsPage.g.h"

namespace winrt::BiliUWP::implementation {
    struct SettingsPage : SettingsPageT<SettingsPage> {
        SettingsPage();
        void InitializeComponent();

        void OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs const& e);
        void OpenStorageFolderButton_Click(
            Windows::Foundation::IInspectable const&,
            Windows::UI::Xaml::RoutedEventArgs const&
        );

        BiliUWP::AppCfgModel CfgModel() { return m_cfg_model; }
    private:
        BiliUWP::AppCfgModel m_cfg_model;
    };
}

namespace winrt::BiliUWP::factory_implementation {
    struct SettingsPage : SettingsPageT<SettingsPage, implementation::SettingsPage> {};
}
