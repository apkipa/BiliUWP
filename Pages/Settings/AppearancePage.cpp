#include "pch.h"
#include "Settings/AppearancePage.h"
#if __has_include("Settings/AppearancePage.g.cpp")
#include "Settings/AppearancePage.g.cpp"
#endif

using namespace winrt;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Navigation;

namespace winrt::BiliUWP::Settings::implementation {
    AppearancePage::AppearancePage() {}
    void AppearancePage::OnNavigatedTo(NavigationEventArgs const& e) {
        m_main_page = e.Parameter().as<MainPage>().get();
    }
}
