#pragma once

#include "FavouritesFolderPage.g.h"
#include "FavouritesFolderPage_ViewItem.g.h"
#include "FavouritesFolderPage_ViewItemSource.g.h"

#include "AdaptiveGridView.h"
#include "BiliClient.hpp"

namespace winrt::BiliUWP::implementation {
    struct FavouritesFolderPage_ViewItem : FavouritesFolderPage_ViewItemT<FavouritesFolderPage_ViewItem> {
        FavouritesFolderPage_ViewItem(::BiliUWP::FavFolderResList_Media const& data) : m_data(data) {}
        hstring Title() { return m_data.title; }
        hstring CoverUrl() {
            if (m_data.cover_url != L"") {
                return m_data.cover_url;
            }
            return L"https://s1.hdslb.com/bfs/static/jinkela/space/assets/playlistbg.png";
        }
        hstring Description() { return m_data.intro; }
        hstring UpName() { return m_data.upper.name; }
        hstring UpFaceUrl() {
            if (m_data.upper.face_url != L"") {
                return m_data.upper.face_url;
            }
            return L"https://i0.hdslb.com/bfs/face/member/noface.jpg";
        }
        uint64_t PlayCount() { return m_data.cnt_info.play_count; }
        uint64_t DanmakuCount() { return m_data.cnt_info.danmaku_count; }
        uint64_t FavouriteCount() { return m_data.cnt_info.favourite_count; }
        uint64_t ResItemId() { return m_data.nid; }
        ::BiliUWP::ResItemType ResItemType() { return m_data.type; }

    private:
        ::BiliUWP::FavFolderResList_Media m_data;
    };
    struct FavouritesFolderPage_ViewItemSource : FavouritesFolderPage_ViewItemSourceT<FavouritesFolderPage_ViewItemSource> {
        FavouritesFolderPage_ViewItemSource(uint64_t folder_id);
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
        hstring UpperName() { return m_upper_name; }
        hstring FolderName() { return m_folder_name; }
        uint32_t TotalItemsCount() { return m_total_items_count; }

    private:
        Windows::Foundation::Collections::IObservableVector<IInspectable> m_vec;
        uint64_t m_folder_id;
        uint32_t m_cur_page_n;  // Current page n after loading
        bool m_has_more;
        uint32_t m_total_items_count;
        hstring m_upper_name;
        hstring m_folder_name;

        std::atomic_bool m_is_loading;
        event<Windows::Foundation::EventHandler<bool>> m_loading_state_changed;
    };
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

        BiliUWP::FavouritesFolderPage_ViewItemSource ViewItemSource() { return m_view_item_src; }

        static void final_release(std::unique_ptr<FavouritesFolderPage> ptr) {
            // Try to release memory which would otherwise be leaked by ItemsGridView
            auto items_grid_view = ptr->ItemsGridView();
            items_grid_view.ItemsSource(nullptr);
        }

    private:
        BiliUWP::FavouritesFolderPage_ViewItemSource m_view_item_src;
    };
}

namespace winrt::BiliUWP::factory_implementation {
    struct FavouritesFolderPage : FavouritesFolderPageT<FavouritesFolderPage, implementation::FavouritesFolderPage> {};
}
