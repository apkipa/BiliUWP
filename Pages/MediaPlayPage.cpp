#include "pch.h"
#include "MediaPlayPage.h"
#if __has_include("MediaPlayPage.g.cpp")
#include "MediaPlayPage.g.cpp"
#endif

using namespace winrt;
using namespace Windows::UI::Xaml;

namespace winrt::BiliUWP::implementation
{
    MediaPlayPage::MediaPlayPage()
    {
        InitializeComponent();
    }

    int32_t MediaPlayPage::MyProperty()
    {
        throw hresult_not_implemented();
    }

    void MediaPlayPage::MyProperty(int32_t /* value */)
    {
        throw hresult_not_implemented();
    }

    void MediaPlayPage::ClickHandler(IInspectable const&, RoutedEventArgs const&)
    {
        Button().Content(box_value(L"Clicked"));
    }
}
