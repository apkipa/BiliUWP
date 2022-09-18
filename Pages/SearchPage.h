#pragma once

#include "SearchPage.g.h"

namespace winrt::BiliUWP::implementation {
    struct SearchPage : SearchPageT<SearchPage> {
        SearchPage() {
            // Xaml objects should not call InitializeComponent during construction.
            // See https://github.com/microsoft/cppwinrt/tree/master/nuget#initializecomponent
        }

        void ClickHandler(Windows::Foundation::IInspectable const& sender, Windows::UI::Xaml::RoutedEventArgs const& args);
    };
}

namespace winrt::BiliUWP::factory_implementation {
    struct SearchPage : SearchPageT<SearchPage, implementation::SearchPage> {};
}
