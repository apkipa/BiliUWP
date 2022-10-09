#pragma once

#include "FavouritesFolderPage.g.h"
#include "IncrementalLoadingCollection.h"
#include "AdaptiveGridView.h"
#include "BiliClient.hpp"

namespace winrt::BiliUWP::implementation {
    struct FavouritesFolderPage : FavouritesFolderPageT<FavouritesFolderPage> {
        FavouritesFolderPage();
        void OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs const& e);
        void AccKeyF5Invoked(
            Windows::UI::Xaml::Input::KeyboardAccelerator const&,
            Windows::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& e
        );
        void RefreshItem_Click(
            Windows::Foundation::IInspectable const&, Windows::UI::Xaml::RoutedEventArgs const&
        );
        void ItemsGridView_ItemClick(
            Windows::Foundation::IInspectable const&, Windows::UI::Xaml::Controls::ItemClickEventArgs const& e
        );

        static void final_release(std::unique_ptr<FavouritesFolderPage> ptr) {
            // Try to release memory which would otherwise be leaked by ItemsGridView
            ptr->ItemsGridView().ItemsSource(nullptr);
        }

    private:
        BiliUWP::IncrementalLoadingCollection m_items_collection;
    };
}

namespace winrt::BiliUWP::factory_implementation {
    struct FavouritesFolderPage : FavouritesFolderPageT<FavouritesFolderPage, implementation::FavouritesFolderPage> {};
}
