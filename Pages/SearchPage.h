#pragma once

#include "SearchPage.g.h"

namespace winrt::BiliUWP::implementation {
    struct SearchPage : SearchPageT<SearchPage> {
        SearchPage();
        void OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs const& e);
    };
}

namespace winrt::BiliUWP::factory_implementation {
    struct SearchPage : SearchPageT<SearchPage, implementation::SearchPage> {};
}
