#include "pch.h"
#include "UserPage.h"
#if __has_include("UserPage.g.cpp")
#include "UserPage.g.cpp"
#endif

using namespace winrt;
using namespace Windows::UI::Xaml;

namespace winrt::BiliUWP::implementation {
    void UserPage::ClickHandler(IInspectable const&, RoutedEventArgs const&) {
        Button().Content(box_value(L"Clicked"));
    }
}
