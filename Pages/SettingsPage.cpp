#include "pch.h"
#include "SettingsPage.h"
#if __has_include("SettingsPage.g.cpp")
#include "SettingsPage.g.cpp"
#endif
#include "App.h"

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
        m_cfg_model(::BiliUWP::App::get()->cfg_model()), m_app_name_ver_text_click_times(0) {}
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
                auto ac_inetcache_folder_path = local_cache_folder_path + L"\\..\\AC\\INetCache";
                auto local_cache_size = co_await util::winrt::calc_folder_size(local_cache_folder_path);
                auto sys_cache_size = co_await util::winrt::calc_folder_size(ac_inetcache_folder_path);
                result_text.Text(
                    util::str::byte_size_to_str(local_cache_size + sys_cache_size, cache_size_precision)
                );
                ToolTipService::SetToolTip(result_text, box_value(::BiliUWP::App::res_str(
                    L"App/Page/SettingsPage/CalculateCacheResultText_ToolTip",
                    util::str::byte_size_to_str(local_cache_size, cache_size_precision),
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
                auto ac_inetcache_folder_path = local_cache_folder_path + L"\\..\\AC\\INetCache";
                co_await util::winrt::delete_all_inside_folder(local_cache_folder_path);
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
        auto& dbg_con = ::BiliUWP::App::get()->debug_console();
        if (dbg_con) {
            dbg_con = nullptr;
        }
        else {
            []() -> fire_forget_except {
                ::BiliUWP::App::get()->debug_console() = co_await ::BiliUWP::DebugConsole::CreateAsync();
            }();
        }
    }
    void SettingsPage::ViewLicensesButton_Click(IInspectable const&, RoutedEventArgs const&) {
        BiliUWP::SimpleContentDialog cd;
        cd.Title(box_value(L"Open Source Licenses"));
        cd.Content(box_value(L""
            "QR Code generator library (C++)\n"
            "\n"
            "Copyright (c) Project Nayuki. (MIT License)\n"
            "https://www.nayuki.io/page/qr-code-generator-library\n"
            "\n"
            "Permission is hereby granted, free of charge, to any person obtaining a copy of\n"
            "this software and associated documentation files (the \"Software\"), to deal in\n"
            "the Software without restriction, including without limitation the rights to\n"
            "use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of\n"
            "the Software, and to permit persons to whom the Software is furnished to do so,\n"
            "subject to the following conditions:\n"
            "- The above copyright notice and this permission notice shall be included in\n"
            "  all copies or substantial portions of the Software.\n"
            "- The Software is provided \"as is\", without warranty of any kind, express or\n"
            "  implied, including but not limited to the warranties of merchantability,\n"
            "  fitness for a particular purpose and noninfringement. In no event shall the\n"
            "  authors or copyright holders be liable for any claim, damages or other\n"
            "  liability, whether in an action of contract, tort or otherwise, arising from,\n"
            "  out of or in connection with the Software or the use or other dealings in the\n"
            "  Software.\n"
            "\n"
            "Microsoft.UI.Xaml\n"
            "\n"
            "MIT License\n"
            "\n"
            "Copyright (c) Microsoft Corporation. All rights reserved.\n"
            "\n"
            "Permission is hereby granted, free of charge, to any person obtaining a copy\n"
            "of this software and associated documentation files (the \"Software\"), to deal\n"
            "in the Software without restriction, including without limitation the rights\n"
            "to use, copy, modify, merge, publish, distribute, sublicense, and/or sell\n"
            "copies of the Software, and to permit persons to whom the Software is\n"
            "furnished to do so, subject to the following conditions:\n"
            "\n"
            "The above copyright notice and this permission notice shall be included in all\n"
            "copies or substantial portions of the Software.\n"
            "\n"
            "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\n"
            "IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\n"
            "FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE\n"
            "AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER\n"
            "LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,\n"
            "OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE\n"
            "SOFTWARE\n"
            "\n"
            "Microsoft.Windows.CppWinRT\n"
            "\n"
            "MIT License\n"
            "\n"
            "Copyright (c) Microsoft Corporation. All rights reserved.\n"
            "\n"
            "Permission is hereby granted, free of charge, to any person obtaining a copy\n"
            "of this software and associated documentation files (the \"Software\"), to deal\n"
            "in the Software without restriction, including without limitation the rights\n"
            "to use, copy, modify, merge, publish, distribute, sublicense, and/or sell\n"
            "copies of the Software, and to permit persons to whom the Software is\n"
            "furnished to do so, subject to the following conditions:\n"
            "\n"
            "The above copyright notice and this permission notice shall be included in all\n"
            "copies or substantial portions of the Software.\n"
            "\n"
            "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\n"
            "IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\n"
            "FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE\n"
            "AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER\n"
            "LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,\n"
            "OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE\n"
            "SOFTWARE\n"
            "\n"
            "Windows Community Toolkit\n"
            "\n"
            "MIT License\n"
            "\n"
            "Copyright © .NET Foundation and Contributors. All rights reserved.\n"
            "\n"
            "Permission is hereby granted, free of charge, to any person obtaining a copy\n"
            "of this software and associated documentation files (the \"Software\"), to deal\n"
            "in the Software without restriction, including without limitation the rights\n"
            "to use, copy, modify, merge, publish, distribute, sublicense, and/or sell\n"
            "copies of the Software, and to permit persons to whom the Software is\n"
            "furnished to do so, subject to the following conditions:\n"
            "\n"
            "The above copyright notice and this permission notice shall be included in all\n"
            "copies or substantial portions of the Software.\n"
            "\n"
            "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\n"
            "IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\n"
            "FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE\n"
            "AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER\n"
            "LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,\n"
            "OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE\n"
            "SOFTWARE\n"
            "\n"
        ));
        cd.CloseButtonText(::BiliUWP::App::res_str(L"App/Common/Close"));
        ::BiliUWP::App::get()->tab_from_page(*this)->show_dialog(cd);
    }
}
