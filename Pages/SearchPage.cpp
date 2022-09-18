#include "pch.h"
#include "SearchPage.h"
#if __has_include("SearchPage.g.cpp")
#include "SearchPage.g.cpp"
#endif

using namespace winrt;
using namespace Windows::UI::Xaml;

namespace winrt::BiliUWP::implementation {
    void SearchPage::ClickHandler(IInspectable const&, RoutedEventArgs const&) {
        Button().Content(box_value(L"Clicked"));
    }
}
