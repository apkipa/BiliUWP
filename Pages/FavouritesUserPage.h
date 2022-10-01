#pragma once

#include "FavouritesUserPage.g.h"
#include "FavouritesUserPage_ViewItem.g.h"
#include "FavouritesUserPage_ViewItemSource.g.h"

#include "AdaptiveGridView.h"
#include "BiliClient.hpp"

namespace winrt::BiliUWP::implementation {
    struct FavouritesUserPage_ViewItem : FavouritesUserPage_ViewItemT<FavouritesUserPage_ViewItem> {
        FavouritesUserPage_ViewItem(::BiliUWP::UserFavFoldersList_Folder const& data) : m_data(data) {}
        hstring Title() { return m_data.title; }
        hstring CoverUrl() {
            if (m_data.cover_url != L"") {
                return m_data.cover_url;
            }
            return L"https://s1.hdslb.com/bfs/static/jinkela/space/assets/playlistbg.png";
        }
        hstring AttrStr();
        uint64_t MediaCount() { return m_data.media_count; }
        uint64_t FolderId() { return m_data.id; }

    private:
        ::BiliUWP::UserFavFoldersList_Folder m_data;
    };
    struct FavouritesUserPage_ViewItemSource : FavouritesUserPage_ViewItemSourceT<FavouritesUserPage_ViewItemSource> {
        FavouritesUserPage_ViewItemSource(uint64_t user_mid);
        Windows::Foundation::Collections::IIterator<Windows::Foundation::IInspectable> First() { return m_vec.First(); }
        Windows::Foundation::IInspectable GetAt(uint32_t index) { return m_vec.GetAt(index); }
        uint32_t Size() { return m_vec.Size(); }
        Windows::Foundation::Collections::IVectorView<Windows::Foundation::IInspectable> GetView() { return m_vec.GetView(); }
        bool IndexOf(Windows::Foundation::IInspectable const& value, uint32_t& index) { return m_vec.IndexOf(value, index); }
        void SetAt(uint32_t index, Windows::Foundation::IInspectable const& value) { m_vec.SetAt(index, value); }
        void InsertAt(uint32_t index, Windows::Foundation::IInspectable const& value) { m_vec.InsertAt(index, value); }
        void RemoveAt(uint32_t index) { m_vec.RemoveAt(index); }
        void Append(Windows::Foundation::IInspectable const& value) { m_vec.Append(value); }
        void RemoveAtEnd() { m_vec.RemoveAtEnd(); }
        void Clear() { m_vec.Clear(); }
        uint32_t GetMany(uint32_t startIndex, array_view<Windows::Foundation::IInspectable> items) {
            return m_vec.GetMany(startIndex, items);
        }
        void ReplaceAll(array_view<Windows::Foundation::IInspectable const> items) { m_vec.ReplaceAll(items); }
        event_token VectorChanged(
            Windows::Foundation::Collections::VectorChangedEventHandler<Windows::Foundation::IInspectable> const& vhnd
        ) {
            return m_vec.VectorChanged(vhnd);
        }
        void VectorChanged(event_token const& token) noexcept { m_vec.VectorChanged(token); };
        Windows::Foundation::IAsyncOperation<Windows::UI::Xaml::Data::LoadMoreItemsResult> LoadMoreItemsAsync(
            uint32_t count
        );
        bool HasMoreItems() {
            return m_has_more;
        }

        // true => Loading; false => Loaded
        event_token LoadingStateChanged(Windows::Foundation::EventHandler<bool> const& handler) {
            return m_loading_state_changed.add(handler);
        }
        void LoadingStateChanged(event_token const& token) noexcept {
            m_loading_state_changed.remove(token);
        }
        void Reload(void) {
            if (m_is_loading.load()) { return; }
            m_cur_page_n = 0;
            m_has_more = true;
            m_vec.Clear();
        }
        uint32_t TotalItemsCount() { return m_total_items_count; }

    private:
        Windows::Foundation::Collections::IObservableVector<IInspectable> m_vec;
        uint64_t m_user_mid;
        uint32_t m_cur_page_n;  // Current page n after loading
        bool m_has_more;
        uint32_t m_total_items_count;

        std::atomic_bool m_is_loading;
        event<Windows::Foundation::EventHandler<bool>> m_loading_state_changed;
    };
    struct FavouritesUserPage : FavouritesUserPageT<FavouritesUserPage> {
        FavouritesUserPage();
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

        BiliUWP::FavouritesUserPage_ViewItemSource ViewItemSource() { return m_view_item_src; }

        static void final_release(std::unique_ptr<FavouritesUserPage> ptr) {
            // Try to release memory which would otherwise be leaked by ItemsGridView
            auto items_grid_view = ptr->ItemsGridView();
            items_grid_view.ItemsSource(nullptr);
        }

    private:
        BiliUWP::FavouritesUserPage_ViewItemSource m_view_item_src;

        util::winrt::async_storage m_cur_async;
    };
}

namespace winrt::BiliUWP::factory_implementation {
    struct FavouritesUserPage : FavouritesUserPageT<FavouritesUserPage, implementation::FavouritesUserPage> {};
    struct FavouritesUserPage_ViewItemSource : FavouritesUserPage_ViewItemSourceT<FavouritesUserPage_ViewItemSource, implementation::FavouritesUserPage_ViewItemSource> {};
}
