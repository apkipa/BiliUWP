#pragma once

#include "UserPage.g.h"
#include "UserVideosViewItem.g.h"
#include "UserVideosViewItemSource.g.h"
#include "util.hpp"
#include "AdaptiveGridView.h"
#include "BiliClient.hpp"

namespace winrt::BiliUWP::implementation {
    struct UserVideosViewItem : UserVideosViewItemT<UserVideosViewItem> {
        UserVideosViewItem(::BiliUWP::UserSpacePublishedVideos_Video const& data) : m_data(data) {}
        hstring Title() { return m_data.title; }
        hstring CoverUrl() {
            if (m_data.cover_url != L"") {
                return m_data.cover_url;
            }
            return L"https://s1.hdslb.com/bfs/static/jinkela/space/assets/playlistbg.png";
        }
        hstring PublishTimeStr() { return to_hstring(m_data.publish_time); }
        hstring Description() { return m_data.description; }
        uint64_t PlayCount() { return m_data.play_count; }
        uint64_t DanmakuCount() { return m_data.danmaku_count; }
        uint64_t AvId() { return m_data.avid; }
        bool IsUnionVideo() { return m_data.is_union_video; }

    private:
        ::BiliUWP::UserSpacePublishedVideos_Video m_data;
    };
    struct UserVideosViewItemSource : UserVideosViewItemSourceT<UserVideosViewItemSource> {
        UserVideosViewItemSource(uint64_t mid);
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
        uint64_t m_mid;
        uint32_t m_cur_page_n;  // Current page n after loading
        bool m_has_more;
        uint32_t m_total_items_count;

        std::atomic_bool m_is_loading;
        event<Windows::Foundation::EventHandler<bool>> m_loading_state_changed;
    };
    struct UserPage : UserPageT<UserPage> {
        UserPage();

        void OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs const& e);

        void VideosItemsGridView_ItemClick(
            Windows::Foundation::IInspectable const&, Windows::UI::Xaml::Controls::ItemClickEventArgs const& e
        );
        void FavouritesItemsGridView_ItemClick(
            Windows::Foundation::IInspectable const&, Windows::UI::Xaml::Controls::ItemClickEventArgs const& e
        );

        static void final_release(std::unique_ptr<UserPage> ptr) {
            ptr->FavouritesItemsGridView().ItemsSource(nullptr);
        }

    private:
        void ApplyHacksToCurrentItem(void);
        void ReconnectExpressionAnimations(void);
        void UpdateInnerScrollViewerOffset(void);

        struct sv_state_record {
            double sv_offset;
            double progress;
        };

        // NOTE: Keep the capacity in sync with items count in XAML!
        std::array<sv_state_record, 6> m_view_offsets = {};
        // NOTE: is_volatile means the view is known to be changing soon,
        //       and its current state should not be relied on
        std::array<bool, 6> m_view_is_volatile = {};
        uint32_t m_cur_tab_idx = 0;
        // NOTE: -1 means there are active animations and progress is not available
        float m_latest_p = -1;

        Windows::UI::Composition::CompositionPropertySet m_comp_props = nullptr;
        // Properties::ScrollViewer[Vector2](prev_scroll_offset, cur_scroll_offset)
        Windows::UI::Composition::ExpressionAnimation m_ea_props_sv = nullptr;
        // Properties::Progress[Scalar]
        Windows::UI::Composition::ExpressionAnimation m_ea_props_p = nullptr;
        Windows::UI::Composition::CompositionScopedBatch m_batch_props = nullptr;

        // tl: translation; sl: scale
        Windows::UI::Composition::ExpressionAnimation m_ea_header_tl = nullptr;
        Windows::UI::Composition::ExpressionAnimation m_ea_userface_sl = nullptr;
        Windows::UI::Composition::ExpressionAnimation m_ea_userface_tl = nullptr;
        Windows::UI::Composition::ExpressionAnimation m_ea_username_sl = nullptr;
        Windows::UI::Composition::ExpressionAnimation m_ea_username_tl = nullptr;
        Windows::UI::Composition::ExpressionAnimation m_ea_usersign_sl = nullptr;
        Windows::UI::Composition::ExpressionAnimation m_ea_usersign_tl = nullptr;
        Windows::UI::Composition::ExpressionAnimation m_ea_usersign_opacity = nullptr;
        // pih: PivotItemsHeader
        Windows::UI::Composition::ExpressionAnimation m_ea_pih_tl = nullptr;

        bool m_ignore_next_viewchanging = false;

        Windows::UI::Xaml::Controls::ScrollViewer::ViewChanging_revoker m_ev_viewchanging_revoker;

        util::winrt::async_storage m_cur_async;

        uint64_t m_uid;
    };
}

namespace winrt::BiliUWP::factory_implementation {
    struct UserPage : UserPageT<UserPage, implementation::UserPage> {};
}
