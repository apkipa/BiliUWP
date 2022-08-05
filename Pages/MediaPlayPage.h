#pragma once

#include "MediaPlayPage.g.h"

namespace winrt::BiliUWP::implementation
{
    struct MediaPlayPage : MediaPlayPageT<MediaPlayPage>
    {
        MediaPlayPage();

        int32_t MyProperty();
        void MyProperty(int32_t value);

        void ClickHandler(Windows::Foundation::IInspectable const& sender, Windows::UI::Xaml::RoutedEventArgs const& args);
    };
}

namespace winrt::BiliUWP::factory_implementation
{
    struct MediaPlayPage : MediaPlayPageT<MediaPlayPage, implementation::MediaPlayPage>
    {
    };
}
