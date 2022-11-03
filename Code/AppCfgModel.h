#pragma once
#include "AppCfgModel.g.h"

namespace winrt::BiliUWP::implementation {
    struct AppCfgModel : AppCfgModelT<AppCfgModel> {
        AppCfgModel();

        event_token PropertyChanged(Windows::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(event_token const& token) noexcept;

        hstring SerializeAsString(void);
        void DeserializeFromString(hstring const& s);

        void ResetConfig(void);

        void SetItemBoxed(hstring const& key, IInspectable const& value);
        IInspectable TryGetItemBoxed(hstring const& key);

        hstring ConfigVersion();

        bool App_UseTabView();
        void App_UseTabView(bool value);
        bool App_ShowTabThumbnails();
        void App_ShowTabThumbnails(bool value);
        bool App_AlwaysSyncPlayingCfg();
        void App_AlwaysSyncPlayingCfg(bool value);
        uint32_t App_GlobalVolume();
        void App_GlobalVolume(uint32_t value);
        bool App_PersistClipboardAfterExit();
        void App_PersistClipboardAfterExit(bool value);
        double App_SimplifyVisualsLevel();
        void App_SimplifyVisualsLevel(double value);
        bool App_IsDeveloper();
        void App_IsDeveloper(bool value);
        bool App_ShowDetailedStats();
        void App_ShowDetailedStats(bool value);
        bool App_RedactLogs();
        void App_RedactLogs(bool value);
        bool App_StoreLogs();
        void App_StoreLogs(bool value);
        uint32_t App_LogLevel();
        void App_LogLevel(uint32_t value);
        bool App_ShowDebugConsole();
        void App_ShowDebugConsole(bool value);
        guid App_LocalId();
        void App_LocalId(guid const& value);

        hstring User_AccessToken();
        void User_AccessToken(hstring const& value);
        hstring User_RefreshToken();
        void User_RefreshToken(hstring const& value);
        hstring User_Cookies_SESSDATA();
        void User_Cookies_SESSDATA(hstring const& value);
        hstring User_Cookies_bili_jct();
        void User_Cookies_bili_jct(hstring const& value);
        hstring User_Cookies_DedeUserID();
        void User_Cookies_DedeUserID(hstring const& value);
        hstring User_Cookies_DedeUserID__ckMd5();
        void User_Cookies_DedeUserID__ckMd5(hstring const& value);
        hstring User_Cookies_sid();
        void User_Cookies_sid(hstring const& value);

    private:
        void ConfigVersion(hstring const& value);

        event<Windows::UI::Xaml::Data::PropertyChangedEventHandler> m_PropertyChanged;

        Windows::Storage::ApplicationDataContainer m_LocalData;
    };
}
namespace winrt::BiliUWP::factory_implementation {
    struct AppCfgModel : AppCfgModelT<AppCfgModel, implementation::AppCfgModel> {};
}
