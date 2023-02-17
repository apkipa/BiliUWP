#pragma once

#include "FavouritesFolderPage.g.h"
#include "IncrementalLoadingCollection.h"
#include "AdaptiveGridView.h"
#include "BiliClient.hpp"

namespace winrt::BiliUWP::implementation {
    struct FavouritesFolderPage : FavouritesFolderPageT<FavouritesFolderPage> {
        FavouritesFolderPage();

        bool IsBiliResReady(void) { return m_bili_res_is_ready; }
        hstring BiliResId() {
            return m_bili_res_is_valid ? L"ffid" + to_hstring(m_fid) : L"";
        }
        hstring BiliResUrl() {
            return m_bili_res_is_valid ?
                hstring(std::format(L"https://space.bilibili.com/{}/favlist?fid={}", m_uid, m_fid)) : L"";
        }
        hstring BiliResId2() {
            return L"";
        }
        hstring BiliResUrl2() {
            return L"";
        }

        void OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs const& e);
        void OnPointerReleased(Windows::UI::Xaml::Input::PointerRoutedEventArgs const& e);
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
        void ItemsGridView_RightTapped(
            Windows::Foundation::IInspectable const& sender,
            Windows::UI::Xaml::Input::RightTappedRoutedEventArgs const& e
        );

        static void final_release(std::unique_ptr<FavouritesFolderPage> ptr) {
            // Try to release memory which would otherwise be leaked by ItemsGridView
            ptr->ItemsGridView().ItemsSource(nullptr);
        }

    private:
        BiliUWP::IncrementalLoadingCollection m_items_collection;
        bool m_items_load_error;
        bool m_bili_res_is_ready;
        bool m_bili_res_is_valid;

        uint64_t m_uid, m_fid;
    };
}

namespace winrt::BiliUWP::factory_implementation {
    struct FavouritesFolderPage : FavouritesFolderPageT<FavouritesFolderPage, implementation::FavouritesFolderPage> {};
}
