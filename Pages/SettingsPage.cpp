#include "pch.h"
#include "SettingsPage.h"
#if __has_include("SettingsPage.g.cpp")
#include "SettingsPage.g.cpp"
#endif

using namespace winrt;
using namespace Windows::UI::Xaml;

namespace winrt::BiliUWP::implementation {
    SettingsPage::SettingsPage() {
        InitializeComponent();
    }

    void SettingsPage::ClickHandler(IInspectable const&, RoutedEventArgs const&) {
        Button().Content(box_value(L"Clicked"));
    }
}
