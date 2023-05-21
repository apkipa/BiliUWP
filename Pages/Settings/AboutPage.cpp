#include "pch.h"
#include "Settings/AboutPage.h"
#if __has_include("Settings/AboutPage.g.cpp")
#include "Settings/AboutPage.g.cpp"
#endif

#include <winsqlite/winsqlite3.h>

constexpr unsigned ENTER_DEV_MODE_TAP_TIMES = 3;

using namespace winrt;
using namespace Windows::UI::Xaml;

namespace winrt::BiliUWP::Settings::implementation {
    AboutPage::AboutPage() {}
    void AboutPage::OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs const& e) {
        m_main_page = e.Parameter().as<MainPage>().get();

        auto cur_pkg = Windows::ApplicationModel::Package::Current();
        auto cur_pkg_id = cur_pkg.Id();
        auto pkg_name = cur_pkg.DisplayName();
        auto pkg_version = cur_pkg_id.Version();
        AppNameVerTextBlock().Text(std::format(
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

        SqliteVersionTextBlock().Text(
            L"winsqlite3 version: " + to_hstring(sqlite3_libversion()));
    }
    void AboutPage::AppNameVerTextBlock_Tapped(
        Windows::Foundation::IInspectable const&,
        Windows::UI::Xaml::Input::TappedRoutedEventArgs const& e
    ) {
        e.Handled(true);

        if (!m_main_page->GetIsDeveloperModeEnabled()) {
            m_app_name_ver_text_block_click_times++;
            if (m_app_name_ver_text_block_click_times >= ENTER_DEV_MODE_TAP_TIMES) {
                m_app_name_ver_text_block_click_times = 0;
                m_main_page->RequestEnableDeveloperMode();
            }
        }
        else {
            m_app_name_ver_text_block_click_times = 0;
        }
    }
    void AboutPage::ViewLicensesButton_Click(IInspectable const&, RoutedEventArgs const&) {
        BiliUWP::SimpleContentDialog cd;
        cd.Title(box_value(L"Open Source Licenses"));
        cd.Content(box_value(L""
#include "AppDepsLicense.rawstr.txt"
        ));
        cd.CloseButtonText(::BiliUWP::App::res_str(L"App/Common/Close"));
        m_main_page->GetAppTab()->show_dialog(cd);
    }
}
