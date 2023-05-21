#pragma once

#include "Settings/DeveloperPage.g.h"
#include "Settings/MainPage.h"
#include "util.hpp"

namespace winrt::BiliUWP::Settings::implementation {
    struct DeveloperPage : DeveloperPageT<DeveloperPage> {
        DeveloperPage();

        void OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs const&);
        void ExportConfigToClipboardButton_Click(
            Windows::Foundation::IInspectable const&, Windows::UI::Xaml::RoutedEventArgs const&
        );
        void ImportConfigFromClipboardButton_Click(
            Windows::Foundation::IInspectable const&, Windows::UI::Xaml::RoutedEventArgs const&
        );
        fire_forget_except OpenStorageFolderButton_Click(
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
        void SwitchDebugConsoleButton_Click(
            Windows::Foundation::IInspectable const&,
            Windows::UI::Xaml::RoutedEventArgs const&
        );
        void RefreshCredentialTokensButton_Click(
            Windows::Foundation::IInspectable const&,
            Windows::UI::Xaml::RoutedEventArgs const&
        );
        fire_forget_except RestartSelfButton_Click(
            Windows::Foundation::IInspectable const&,
            Windows::UI::Xaml::RoutedEventArgs const&
        );

        BiliUWP::AppCfgModel CfgModel() const { return m_main_page->CfgModel(); }
        Windows::UI::Xaml::DebugSettings AppDebugSettings() const { return m_main_page->AppDebugSettings(); }

    private:
        MainPage* m_main_page{};

        util::winrt::async_storage m_import_config_from_clipboard_async;
        util::winrt::async_storage m_cache_async;
        util::winrt::async_storage m_refresh_credentials_async;
    };
}

namespace winrt::BiliUWP::Settings::factory_implementation {
    struct DeveloperPage : DeveloperPageT<DeveloperPage, implementation::DeveloperPage> {};
}
