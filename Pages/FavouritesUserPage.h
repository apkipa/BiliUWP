#pragma once

#include "FavouritesUserPage.g.h"

#include "AdaptiveGridView.h"

namespace winrt::BiliUWP::implementation {
    struct FavouritesUserPage : FavouritesUserPageT<FavouritesUserPage> {
        FavouritesUserPage();
        void OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs const& e);
    };
}

namespace winrt::BiliUWP::factory_implementation {
    struct FavouritesUserPage : FavouritesUserPageT<FavouritesUserPage, implementation::FavouritesUserPage> {};
}
