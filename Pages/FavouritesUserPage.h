#pragma once

#include "FavouritesUserPage.g.h"

#include "AdaptiveGridView.h"

namespace winrt::BiliUWP::implementation {
    struct FavouritesUserPage : FavouritesUserPageT<FavouritesUserPage> {
        FavouritesUserPage();
        void OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs const& e);
        void RefreshItem_Click(
            Windows::Foundation::IInspectable const&, Windows::UI::Xaml::RoutedEventArgs const&
        );
    };
}

namespace winrt::BiliUWP::factory_implementation {
    struct FavouritesUserPage : FavouritesUserPageT<FavouritesUserPage, implementation::FavouritesUserPage> {};
}
