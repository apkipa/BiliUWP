#include "pch.h"
#include "Settings/PlayingPage.h"
#if __has_include("Settings/PlayingPage.g.cpp")
#include "Settings/PlayingPage.g.cpp"
#endif

using namespace winrt;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Navigation;

namespace winrt::BiliUWP::Settings::implementation {
    PlayingPage::PlayingPage() {}
    void PlayingPage::OnNavigatedTo(NavigationEventArgs const& e) {
        m_main_page = e.Parameter().as<MainPage>().get();
    }
}
