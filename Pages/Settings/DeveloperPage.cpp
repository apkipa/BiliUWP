#include "pch.h"
#include "Settings/DeveloperPage.h"
#if __has_include("Settings/DeveloperPage.g.cpp")
#include "Settings/DeveloperPage.g.cpp"
#endif

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Navigation;
using namespace Windows::ApplicationModel::DataTransfer;
using namespace Windows::Storage;

namespace winrt::BiliUWP::Settings::implementation {
    DeveloperPage::DeveloperPage() {}
    void DeveloperPage::OnNavigatedTo(NavigationEventArgs const& e) {
        m_main_page = e.Parameter().as<MainPage>().get();

        auto cfg_model = m_main_page->CfgModel();
        auto expire_ts = cfg_model.User_CredentialEffectiveEndTime();
        auto tp = std::chrono::system_clock::time_point(std::chrono::seconds(expire_ts));
        CredentialsExpireAfterText().Text(
            ::BiliUWP::App::res_str(L"App/Page/Settings/DeveloperPage/CredentialsExpireAfterText",
                std::format("{}", std::chrono::zoned_time{ std::chrono::current_zone(), tp })
            )
        );
    }
    void DeveloperPage::ExportConfigToClipboardButton_Click(
        IInspectable const&, RoutedEventArgs const&
    ) {
        // NOTE: Do not persist the clipboard after app close as config
        //       contains sensitive information
        auto success_mark = ExportConfigToClipboardSuccessMark();
        success_mark.Visibility(Visibility::Collapsed);
        util::winrt::set_clipboard_text(m_main_page->CfgModel().SerializeAsString(), false);
        success_mark.Visibility(Visibility::Visible);
    }
    void DeveloperPage::ImportConfigFromClipboardButton_Click(
        IInspectable const&, RoutedEventArgs const&
    ) {
        m_import_config_from_clipboard_async.run_if_idle([](DeveloperPage* that) -> IAsyncAction {
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
                that->m_main_page->CfgModel().DeserializeFromString(std::move(text));

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
    fire_forget_except DeveloperPage::OpenStorageFolderButton_Click(IInspectable const&, RoutedEventArgs const&) {
        co_await Windows::System::Launcher::LaunchFolderAsync(
            Windows::Storage::ApplicationData::Current().LocalFolder()
        );
    }
    void DeveloperPage::CalculateCacheButton_Click(IInspectable const&, RoutedEventArgs const&) {
        // NOTE: In order to keep accessing this safe, either obtain
        //       weak ptr & upgrade, or avoid accessing this after suspension points
        m_cache_async.run_if_idle([](DeveloperPage* that) -> IAsyncAction {
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

                auto appdata_cur = ApplicationData::Current();
                auto local_cache_folder_path = appdata_cur.LocalCacheFolder().Path();
                auto temp_state_folder_path = appdata_cur.TemporaryFolder().Path();
                auto ac_inetcache_folder_path = local_cache_folder_path + L"\\..\\AC\\INetCache";
                auto local_cache_size = co_await util::winrt::calc_folder_size(local_cache_folder_path);
                auto temp_state_size = co_await util::winrt::calc_folder_size(temp_state_folder_path);
                auto sys_cache_size = co_await util::winrt::calc_folder_size(ac_inetcache_folder_path);
                result_text.Text(util::str::byte_size_to_str(
                    local_cache_size + temp_state_size + sys_cache_size,
                    cache_size_precision));
                ToolTipService::SetToolTip(result_text, box_value(::BiliUWP::App::res_str(
                    L"App/Page/Settings/DeveloperPage/CalculateCacheResultText_ToolTip",
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
    void DeveloperPage::ClearCacheButton_Click(IInspectable const&, RoutedEventArgs const&) {
        m_cache_async.run_if_idle([](DeveloperPage* that) -> IAsyncAction {
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
    void DeveloperPage::SwitchDebugConsoleButton_Click(IInspectable const&, RoutedEventArgs const&) {
        auto app = ::BiliUWP::App::get();
        app->enable_debug_console(!app->debug_console(), true);
    }
    void DeveloperPage::RefreshCredentialTokensButton_Click(IInspectable const&, RoutedEventArgs const&) {
        m_cache_async.run_if_idle([](DeveloperPage* that) -> IAsyncAction {
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
                auto cfg_model = that->m_main_page->CfgModel();
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
                    ::BiliUWP::App::res_str(L"App/Page/Settings/DeveloperPage/CredentialsExpireAfterText",
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
    fire_forget_except DeveloperPage::RestartSelfButton_Click(IInspectable const&, RoutedEventArgs const&) {
        using namespace Windows::ApplicationModel::Core;
        auto fail_reason = co_await CoreApplication::RequestRestartAsync({});
    }
}
