#include "pch.h"
#include "Settings/MiscPage.h"
#if __has_include("Settings/MiscPage.g.cpp")
#include "Settings/MiscPage.g.cpp"
#endif

using namespace winrt;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Navigation;

namespace winrt::BiliUWP::Settings::implementation {
    MiscPage::MiscPage() {}
    void MiscPage::OnNavigatedTo(NavigationEventArgs const& e) {
        m_main_page = e.Parameter().as<MainPage>().get();
    }
}
