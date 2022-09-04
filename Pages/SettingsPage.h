#pragma once

#include "SettingsPage.g.h"
#include "util.hpp"

namespace winrt::BiliUWP::implementation {
    struct SettingsPage : SettingsPageT<SettingsPage> {
        SettingsPage();
        void InitializeComponent();

        void OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs const& e);
        void ExportConfigToClipboardButton_Click(
            IInspectable const&, Windows::UI::Xaml::RoutedEventArgs const&
        );
        void ImportConfigFromClipboardButton_Click(
            IInspectable const&, Windows::UI::Xaml::RoutedEventArgs const&
        );
        void OpenStorageFolderButton_Click(
            Windows::Foundation::IInspectable const&,
            Windows::UI::Xaml::RoutedEventArgs const&
        );
        void CalculateCacheButton_Click(
            Windows::Foundation::IInspectable const&,
            Windows::UI::Xaml::RoutedEventArgs const&
        );
        void ClearCacheButton_Click(
            Windows::Foundation::IInspectable const&,
            Windows::UI::Xaml::RoutedEventArgs const&
        );

        BiliUWP::AppCfgModel CfgModel() { return m_cfg_model; }

    private:
        BiliUWP::AppCfgModel m_cfg_model;

        unsigned m_app_name_ver_text_click_times;

        util::winrt::async_storage m_import_config_from_clipboard_async;
        util::winrt::async_storage m_cache_async;
    };
}

namespace winrt::BiliUWP::factory_implementation {
    struct SettingsPage : SettingsPageT<SettingsPage, implementation::SettingsPage> {};
}
