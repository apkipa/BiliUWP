#pragma once

#include "Settings/MainPage.g.h"
#include "App.h"

namespace winrt::BiliUWP::Settings::implementation {
    struct MainPage : MainPageT<MainPage> {
        MainPage();

        void OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs const&);
        void MainNavView_ItemInvoked(
            Windows::Foundation::IInspectable const&,
            Microsoft::UI::Xaml::Controls::NavigationViewItemInvokedEventArgs const& e
        );

        BiliUWP::AppCfgModel CfgModel() const { return m_cfg_model; }
        Windows::UI::Xaml::DebugSettings AppDebugSettings() const { return m_app_dbg_settings; }

        // Non-midl methods
        bool GetIsDeveloperModeEnabled(void) const { return m_cfg_model.App_IsDeveloper(); }
        fire_forget_except RequestEnableDeveloperMode(void);
        auto GetAppTab(void) const { return ::BiliUWP::App::get()->tab_from_page(*this); }

    private:
        BiliUWP::AppCfgModel m_cfg_model;
        Windows::UI::Xaml::DebugSettings m_app_dbg_settings;

        bool m_cf_transition_enabled{ true };

        void UpdateNavigationFrame(Microsoft::UI::Xaml::Controls::NavigationViewItemBase const& nvi);
        void UpdateContentFrameTransition(bool enable);
    };
}

namespace winrt::BiliUWP::Settings::factory_implementation {
    struct MainPage : MainPageT<MainPage, implementation::MainPage> {};
}
