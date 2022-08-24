#include "pch.h"
#include "SettingsPage.h"
#if __has_include("SettingsPage.g.cpp")
#include "SettingsPage.g.cpp"
#endif
#include "App.h"

using namespace winrt;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;

namespace winrt::BiliUWP::implementation {
    SettingsPage::SettingsPage() : m_cfg_model(::BiliUWP::App::get()->cfg_model()) {}
    void SettingsPage::InitializeComponent() {
        using namespace Windows::ApplicationModel;

        SettingsPageT::InitializeComponent();

        auto cur_pkg = Package::Current();
        auto cur_pkg_id = cur_pkg.Id();
        auto pkg_name = cur_pkg.DisplayName();
        auto pkg_version = cur_pkg_id.Version();
        AppNameVerText().Text(std::format(
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
