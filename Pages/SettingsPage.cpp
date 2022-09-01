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

std::wstring size_to_str(size_t size, double precision) {
    double float_size = static_cast<double>(size);
    const wchar_t* size_postfix;
    uint64_t power_of_size = 0;

    while (float_size >= 1024) {
        float_size /= 1024;
        power_of_size++;
    }
    switch (power_of_size) {
    case 0:     size_postfix = L"B";        break;
    case 1:     size_postfix = L"KiB";      break;
    case 2:     size_postfix = L"MiB";      break;
    case 3:     size_postfix = L"GiB";      break;
    case 4:     size_postfix = L"TiB";      break;
    case 5:     size_postfix = L"PiB";      break;
    case 6:     size_postfix = L"EiB";      break;
    default:    size_postfix = L"<ERROR>";  break;
    }
    return std::format(L"{} {}",
        std::round(float_size * precision) / precision,
        size_postfix
    );
}

namespace winrt::BiliUWP::implementation {
    SettingsPage::SettingsPage() :
        m_cfg_model(::BiliUWP::App::get()->cfg_model()), m_app_name_ver_text_click_times(0),
        m_import_config_from_clipboard_op(nullptr), m_cache_op(nullptr) {}
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
                        switch (co_await app->tab_from_page(*this)->show_dialog(cd)) {
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
    void SettingsPage::final_release(std::unique_ptr<SettingsPage> ptr) noexcept {
        util::winrt::cancel_async(ptr->m_import_config_from_clipboard_op);
        util::winrt::cancel_async(ptr->m_cache_op);
    }
    void SettingsPage::OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs const& e) {
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
        if (util::winrt::is_async_running(m_import_config_from_clipboard_op)) { return; }
        m_import_config_from_clipboard_op = [](SettingsPage* that) -> IAsyncAction {
            auto cancellation_token = co_await get_cancellation_token();
            cancellation_token.enable_propagation();

            deferred([weak_this = that->get_weak()] {
                auto strong_this = weak_this.get();
                if (!strong_this) { return; }
                auto prog_ring = strong_this->ImportConfigFromClipboardProgRing();
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
                that->m_cfg_model.DeserializeFromString(co_await dp_view.GetTextAsync());

                success_mark.Visibility(Visibility::Visible);
            }
            catch (hresult_error const& e) {
                util::debug::log_error(std::format(L"Unable to import config (0x{:08x}: {})",
                    static_cast<uint32_t>(e.code()), e.message()
                ));
            }
        }(this);
    }
    void SettingsPage::OpenStorageFolderButton_Click(IInspectable const&, RoutedEventArgs const&) {
        Windows::System::Launcher::LaunchFolderAsync(
            Windows::Storage::ApplicationData::Current().LocalFolder()
        );
    }
    void SettingsPage::CalculateCacheButton_Click(IInspectable const&, RoutedEventArgs const&) {
        if (util::winrt::is_async_running(m_cache_op)) { return; }
        m_cache_op = [](SettingsPage* that) -> IAsyncAction {
            constexpr double cache_size_precision = 1e2;

            auto cancellation_token = co_await get_cancellation_token();
            cancellation_token.enable_propagation();

            deferred([weak_this = that->get_weak()] {
                auto strong_this = weak_this.get();
                if (!strong_this) { return; }
                auto prog_ring = strong_this->CalculateCacheProgRing();
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
                result_text.Text(size_to_str(local_cache_size + sys_cache_size, cache_size_precision));
                ToolTipService::SetToolTip(result_text, box_value(::BiliUWP::App::res_str(
                    L"App/Page/SettingsPage/CalculateCacheResultText_ToolTip",
                    size_to_str(local_cache_size, cache_size_precision),
                    size_to_str(sys_cache_size, cache_size_precision)
                )));

                result_text.Visibility(Visibility::Visible);
            }
            catch (hresult_error const& e) {
                util::debug::log_error(std::format(L"Unable to calculate cache size (0x{:08x}: {})",
                    static_cast<uint32_t>(e.code()), e.message()
                ));
            }
        }(this);
    }
    void SettingsPage::ClearCacheButton_Click(IInspectable const&, RoutedEventArgs const&) {
        if (util::winrt::is_async_running(m_cache_op)) { return; }
        m_cache_op = [](SettingsPage* that) -> IAsyncAction {
            auto cancellation_token = co_await get_cancellation_token();
            cancellation_token.enable_propagation();

            deferred([weak_this = that->get_weak()] {
                auto strong_this = weak_this.get();
                if (!strong_this) { return; }
                auto prog_ring = strong_this->ClearCacheProgRing();
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
            catch (hresult_error const& e) {
                util::debug::log_error(std::format(L"Unable to clear cache (0x{:08x}: {})",
                    static_cast<uint32_t>(e.code()), e.message()
                ));
            }
        }(this);
    }
}
