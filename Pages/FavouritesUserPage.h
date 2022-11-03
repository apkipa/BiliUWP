#pragma once

#include "FavouritesUserPage.g.h"
#include "IncrementalLoadingCollection.h"
#include "AdaptiveGridView.h"
#include "BiliClient.hpp"

namespace winrt::BiliUWP::implementation {
    struct FavouritesUserPage : FavouritesUserPageT<FavouritesUserPage> {
        FavouritesUserPage();

        bool IsBiliResReady(void) { return m_bili_res_is_ready; }
        hstring BiliResId() {
            return m_bili_res_is_valid ? L"fuid" + to_hstring(m_uid) : L"";
        }
        hstring BiliResUrl() {
            return m_bili_res_is_valid ?
                hstring(std::format(L"https://space.bilibili.com/{}/favlist", m_uid)) : L"";
        }
        hstring BiliResId2() {
            return L"";
        }
        hstring BiliResUrl2() {
            return L"";
        }

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

        static void final_release(std::unique_ptr<FavouritesUserPage> ptr) {
            // Try to release memory which would otherwise be leaked by ItemsGridView
            ptr->ItemsGridView().ItemsSource(nullptr);
        }

    private:
        BiliUWP::IncrementalLoadingCollection m_items_collection;
        bool m_items_load_error;
        bool m_bili_res_is_ready;
        bool m_bili_res_is_valid;

        uint64_t m_uid;

        util::winrt::async_storage m_cur_async;
    };
}

namespace winrt::BiliUWP::factory_implementation {
    struct FavouritesUserPage : FavouritesUserPageT<FavouritesUserPage, implementation::FavouritesUserPage> {};
}
