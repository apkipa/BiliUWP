﻿#include "pch.h"
#include "SettingsPage.h"
#if __has_include("SettingsPage.g.cpp")
#include "SettingsPage.g.cpp"
#endif
#include "App.h"

#include <winsqlite/winsqlite3.h>

constexpr unsigned ENTER_DEV_MODE_TAP_TIMES = 3;

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::ApplicationModel::DataTransfer;
using namespace Windows::Storage;

namespace winrt::BiliUWP::implementation {
    SettingsPage::SettingsPage() :
        m_cfg_model(::BiliUWP::App::get()->cfg_model()),
        m_app_dbg_settings(Application::Current().DebugSettings()),
        m_app_name_ver_text_click_times(0) {}
    void SettingsPage::InitializeComponent() {
        using namespace Windows::ApplicationModel;

        SettingsPageT::InitializeComponent();

        auto cur_pkg = Package::Current();
        auto cur_pkg_id = cur_pkg.Id();
        auto pkg_name = cur_pkg.DisplayName();
        auto pkg_version = cur_pkg_id.Version();
        auto app_name_ver_text = AppNameVerText();
        app_name_ver_text.Text(std::format(
#ifdef NDEBUG
            L"{} v{}.{}.{}",
#else
            L"{} (Dev) v{}.{}.{}",
#endif
            pkg_name,
            pkg_version.Major,
            pkg_version.Minor,
            pkg_version.Build
        ));
        app_name_ver_text.Tapped([this](IInspectable const&, TappedRoutedEventArgs const& e) {
            e.Handled(true);

            if (!m_cfg_model.App_IsDeveloper()) {
                m_app_name_ver_text_click_times++;
                if (m_app_name_ver_text_click_times >= ENTER_DEV_MODE_TAP_TIMES) {
                    m_app_name_ver_text_click_times = 0;

                    BiliUWP::SimpleContentDialog cd;
                    cd.Title(box_value(::BiliUWP::App::res_str(L"App/Dialog/AskEnableDevMode/Title")));
                    cd.Content(box_value(::BiliUWP::App::res_str(L"App/Dialog/AskEnableDevMode/Content")));
                    cd.PrimaryButtonText(::BiliUWP::App::res_str(L"App/Common/Yes"));
                    cd.CloseButtonText(::BiliUWP::App::res_str(L"App/Common/No"));
                    [=](void) -> fire_forget_except {
                        auto app = ::BiliUWP::App::get();
                        auto show_dialog_op = app->tab_from_page(*this)->show_dialog(cd);
                        switch (co_await std::move(show_dialog_op)) {
                        case SimpleContentDialogResult::None:
                            break;
                        case SimpleContentDialogResult::Primary:
                            app->cfg_model().App_IsDeveloper(true);
                            break;
                        default:
                            util::debug::log_error(L"Unreachable");
                            break;
                        }
                    }();
                }
            }
            else {
                m_app_name_ver_text_click_times = 0;
            }
        });

        auto expire_ts = m_cfg_model.User_CredentialEffectiveEndTime();
        auto tp = std::chrono::system_clock::time_point(std::chrono::seconds(expire_ts));
        CredentialsExpireAfterText().Text(
            ::BiliUWP::App::res_str(L"App/Page/SettingsPage/CredentialsExpireAfterText",
                std::format("{}", std::chrono::zoned_time{ std::chrono::current_zone(), tp })
            )
        );

        SqliteVersionTextBlock().Text(
            L"winsqlite3 version: " + to_hstring(sqlite3_libversion()));
    }
    void SettingsPage::OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs const&) {
        auto tab = ::BiliUWP::App::get()->tab_from_page(*this);
        tab->set_icon(Symbol::Setting);
        tab->set_title(::BiliUWP::App::res_str(L"App/Page/SettingsPage/Title"));
    }
    void SettingsPage::ExportConfigToClipboardButton_Click(
        IInspectable const&, RoutedEventArgs const&
    ) {
        // NOTE: Do not persist the clipboard after app close as config
        //       contains sensitive information
        auto success_mark = ExportConfigToClipboardSuccessMark();
        success_mark.Visibility(Visibility::Collapsed);
        util::winrt::set_clipboard_text(m_cfg_model.SerializeAsString(), false);
        success_mark.Visibility(Visibility::Visible);
    }
    void SettingsPage::ImportConfigFromClipboardButton_Click(
        IInspectable const&, RoutedEventArgs const&
    ) {
        m_import_config_from_clipboard_async.run_if_idle([](SettingsPage* that) -> IAsyncAction {
            auto cancellation_token = co_await get_cancellation_token();
            cancellation_token.enable_propagation();

            auto weak_store = util::winrt::make_weak_storage(*that);

            deferred([&weak_store] {
                if (!weak_store.lock()) { return; }
                auto prog_ring = weak_store->ImportConfigFromClipboardProgRing();
                prog_ring.IsActive(false);
                prog_ring.Visibility(Visibility::Collapsed);
            });

            try {
                auto success_mark = that->ImportConfigFromClipboardSuccessMark();
                success_mark.Visibility(Visibility::Collapsed);
                auto prog_ring = that->ImportConfigFromClipboardProgRing();
                prog_ring.IsActive(true);
                prog_ring.Visibility(Visibility::Visible);

                auto dp_view = Clipboard::GetContent();
                if (!dp_view.Contains(StandardDataFormats::Text())) {
                    util::debug::log_error(L"Only text can be imported as config");
                    co_return;
                }
                auto text = co_await dp_view.GetTextAsync();
                if (!weak_store.lock()) { co_return; }
                that->m_cfg_model.DeserializeFromString(std::move(text));

                success_mark.Visibility(Visibility::Visible);
            }
            catch (hresult_canceled const&) { throw; }
            catch (...) {
                util::debug::log_error(L"Unable to import config");
                util::winrt::log_current_exception();
                throw;
            }
        }, this);
    }
    fire_forget_except SettingsPage::OpenStorageFolderButton_Click(IInspectable const&, RoutedEventArgs const&) {
        co_await Windows::System::Launcher::LaunchFolderAsync(
            Windows::Storage::ApplicationData::Current().LocalFolder()
        );
    }
    void SettingsPage::CalculateCacheButton_Click(IInspectable const&, RoutedEventArgs const&) {
        // NOTE: In order to keep accessing this safe, either obtain
        //       weak ptr & upgrade, or avoid accessing this after suspension points
        m_cache_async.run_if_idle([](SettingsPage* that) -> IAsyncAction {
            constexpr double cache_size_precision = 1e2;

            auto cancellation_token = co_await get_cancellation_token();
            cancellation_token.enable_propagation();

            auto weak_store = util::winrt::make_weak_storage(*that);

            deferred([&weak_store] {
                if (!weak_store.lock()) { return; }
                auto prog_ring = weak_store->CalculateCacheProgRing();
                prog_ring.IsActive(false);
                prog_ring.Visibility(Visibility::Collapsed);
            });

            try {
                auto result_text = that->CalculateCacheResultText();
                result_text.Visibility(Visibility::Collapsed);
                auto prog_ring = that->CalculateCacheProgRing();
                prog_ring.IsActive(true);
                prog_ring.Visibility(Visibility::Visible);

                auto local_cache_folder_path = ApplicationData::Current().LocalCacheFolder().Path();
                auto temp_state_folder_path = ApplicationData::Current().TemporaryFolder().Path();
                auto ac_inetcache_folder_path = local_cache_folder_path + L"\\..\\AC\\INetCache";
                auto local_cache_size = co_await util::winrt::calc_folder_size(local_cache_folder_path);
                auto temp_state_size = co_await util::winrt::calc_folder_size(temp_state_folder_path);
                auto sys_cache_size = co_await util::winrt::calc_folder_size(ac_inetcache_folder_path);
                result_text.Text(util::str::byte_size_to_str(
                    local_cache_size + temp_state_size + sys_cache_size,
                    cache_size_precision));
                ToolTipService::SetToolTip(result_text, box_value(::BiliUWP::App::res_str(
                    L"App/Page/SettingsPage/CalculateCacheResultText_ToolTip",
                    util::str::byte_size_to_str(local_cache_size + temp_state_size, cache_size_precision),
                    util::str::byte_size_to_str(sys_cache_size, cache_size_precision)
                )));

                result_text.Visibility(Visibility::Visible);
            }
            catch (hresult_canceled const&) { throw; }
            catch (...) {
                util::debug::log_error(L"Unable to calculate cache size");
                util::winrt::log_current_exception();
                throw;
            }
        }, this);
    }
    void SettingsPage::ClearCacheButton_Click(IInspectable const&, RoutedEventArgs const&) {
        m_cache_async.run_if_idle([](SettingsPage* that) -> IAsyncAction {
            auto cancellation_token = co_await get_cancellation_token();
            cancellation_token.enable_propagation();

            auto weak_store = util::winrt::make_weak_storage(*that);

            deferred([&weak_store] {
                if (!weak_store.lock()) { return; }
                auto prog_ring = weak_store->ClearCacheProgRing();
                prog_ring.IsActive(false);
                prog_ring.Visibility(Visibility::Collapsed);
            });

            try {
                auto success_mark = that->ClearCacheSuccessMark();
                success_mark.Visibility(Visibility::Collapsed);
                auto prog_ring = that->ClearCacheProgRing();
                prog_ring.IsActive(true);
                prog_ring.Visibility(Visibility::Visible);

                // NOTE: Currently not guaranteed that all caches will be removed
                auto local_cache_folder_path = ApplicationData::Current().LocalCacheFolder().Path();
                auto temp_state_folder_path = ApplicationData::Current().TemporaryFolder().Path();
                auto ac_inetcache_folder_path = local_cache_folder_path + L"\\..\\AC\\INetCache";
                co_await util::winrt::delete_all_inside_folder(local_cache_folder_path);
                co_await util::winrt::delete_all_inside_folder(temp_state_folder_path);
                co_await util::winrt::delete_all_inside_folder(ac_inetcache_folder_path);

                success_mark.Visibility(Visibility::Visible);
            }
            catch (hresult_canceled const&) { throw; }
            catch (...) {
                util::debug::log_error(L"Unable to clear cache");
                util::winrt::log_current_exception();
                throw;
            }
        }, this);
    }
    void SettingsPage::SwitchDebugConsoleButton_Click(IInspectable const&, RoutedEventArgs const&) {
        auto app = ::BiliUWP::App::get();
        app->enable_debug_console(!app->debug_console(), true);
    }
    void SettingsPage::RefreshCredentialTokensButton_Click(IInspectable const&, RoutedEventArgs const&) {
        m_cache_async.run_if_idle([](SettingsPage* that) -> IAsyncAction {
            auto cancellation_token = co_await get_cancellation_token();
            cancellation_token.enable_propagation();

            auto weak_store = util::winrt::make_weak_storage(*that);

            deferred([&weak_store] {
                if (!weak_store.lock()) { return; }
                auto prog_ring = weak_store->RefreshCredentialTokensProgRing();
                prog_ring.IsActive(false);
                prog_ring.Visibility(Visibility::Collapsed);
            });

            try {
                auto success_mark = that->RefreshCredentialTokensSuccessMark();
                success_mark.Visibility(Visibility::Collapsed);
                auto prog_ring = that->RefreshCredentialTokensProgRing();
                prog_ring.IsActive(true);
                prog_ring.Visibility(Visibility::Visible);

                util::debug::log_trace(L"Refreshing credential tokens");
                auto cfg_model = that->m_cfg_model;
                auto cred_expire_text = that->CredentialsExpireAfterText();
                auto client = ::BiliUWP::App::get()->bili_client();
                auto cur_ts = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());
                auto result = std::move(co_await client->oauth2_refresh_token());
                auto expire_ts = cur_ts + result.expires_in;
                cfg_model.User_CredentialEffectiveStartTime(cur_ts);
                cfg_model.User_CredentialEffectiveEndTime(expire_ts);
                cfg_model.User_AccessToken(result.access_token);
                cfg_model.User_RefreshToken(result.refresh_token);
                cfg_model.User_Cookies_SESSDATA(result.user_cookies.SESSDATA);
                cfg_model.User_Cookies_bili_jct(result.user_cookies.bili_jct);
                cfg_model.User_Cookies_DedeUserID(result.user_cookies.DedeUserID);
                cfg_model.User_Cookies_DedeUserID__ckMd5(result.user_cookies.DedeUserID__ckMd5);
                cfg_model.User_Cookies_sid(result.user_cookies.sid);
                util::debug::log_trace(L"Done refreshing credential tokens");
                auto tp = std::chrono::system_clock::time_point(std::chrono::seconds(expire_ts));
                cred_expire_text.Text(
                    ::BiliUWP::App::res_str(L"App/Page/SettingsPage/CredentialsExpireAfterText",
                        std::format("{}", std::chrono::zoned_time{ std::chrono::current_zone(), tp })
                    )
                );

                success_mark.Visibility(Visibility::Visible);
            }
            catch (hresult_canceled const&) { throw; }
            catch (...) {
                util::debug::log_error(L"Unable to refresh credential tokens");
                util::winrt::log_current_exception();
                throw;
            }
        }, this);
    }
    fire_forget_except SettingsPage::RestartSelfButton_Click(IInspectable const&, RoutedEventArgs const&) {
        using namespace Windows::ApplicationModel::Core;
        auto fail_reason = co_await CoreApplication::RequestRestartAsync({});
    }
    void SettingsPage::ViewLicensesButton_Click(IInspectable const&, RoutedEventArgs const&) {
        BiliUWP::SimpleContentDialog cd;
        cd.Title(box_value(L"Open Source Licenses"));
        cd.Content(box_value(L""
#include "AppDepsLicense.rawstr.txt"
        ));
        cd.CloseButtonText(::BiliUWP::App::res_str(L"App/Common/Close"));
        ::BiliUWP::App::get()->tab_from_page(*this)->show_dialog(cd);
    }
}
