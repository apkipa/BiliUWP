#pragma once

#include "ContainerPage.g.h"

// ContainerPage: A container page which wraps a UI element

namespace winrt::BiliUWP::implementation {
    struct ContainerPage : ContainerPageT<ContainerPage> {
        ContainerPage();
        void OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs const& e);
    };
}

namespace winrt::BiliUWP::factory_implementation {
    struct ContainerPage : ContainerPageT<ContainerPage, implementation::ContainerPage> {};
}
