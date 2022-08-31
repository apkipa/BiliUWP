#include "pch.h"
#include "SettingsPage.h"
#if __has_include("SettingsPage.g.cpp")
#include "SettingsPage.g.cpp"
#endif
#include "App.h"

constexpr unsigned ENTER_DEV_MODE_TAP_TIMES = 2;

using namespace winrt;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Controls;

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
    void SettingsPage::OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs const& e) {
        auto tab = ::BiliUWP::App::get()->tab_from_page(*this);
        tab->set_icon(Symbol::Setting);
        tab->set_title(::BiliUWP::App::res_str(L"App/Page/SettingsPage/Title"));
    }
    void SettingsPage::OpenStorageFolderButton_Click(IInspectable const&, RoutedEventArgs const&) {
        Windows::System::Launcher::LaunchFolderAsync(
            Windows::Storage::ApplicationData::Current().LocalFolder()
        );
    }
}
