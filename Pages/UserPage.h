#pragma once

#include "UserPage.g.h"

namespace winrt::BiliUWP::implementation {
    struct UserPage : UserPageT<UserPage> {
        UserPage() {
            // Xaml objects should not call InitializeComponent during construction.
            // See https://github.com/microsoft/cppwinrt/tree/master/nuget#initializecomponent
        }

        void ClickHandler(Windows::Foundation::IInspectable const& sender, Windows::UI::Xaml::RoutedEventArgs const& args);
    };
}

namespace winrt::BiliUWP::factory_implementation {
    struct UserPage : UserPageT<UserPage, implementation::UserPage> {};
}
