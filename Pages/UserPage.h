#pragma once

#include "UserPage.g.h"
#include "util.hpp"
#include "AdaptiveGridView.h"
#include "BiliClient.hpp"

namespace winrt::BiliUWP::implementation {
    struct UserPage : UserPageT<UserPage> {
        UserPage();

        bool IsBiliResReady(void) { return m_bili_res_is_ready; }
        hstring BiliResId() {
            return m_bili_res_is_valid ? L"uid" + to_hstring(m_uid) : L"";
        }
        hstring BiliResUrl() {
            return m_bili_res_is_valid ? L"https://space.bilibili.com/" + to_hstring(m_uid) : L"";
        }
        hstring BiliResId2() {
            return L"";
        }
        hstring BiliResUrl2() {
            return L"";
        }

        void OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs const& e);

        void VideosItemsGridView_ItemClick(
            Windows::Foundation::IInspectable const&, Windows::UI::Xaml::Controls::ItemClickEventArgs const& e
        );
        void AudiosItemsGridView_ItemClick(
            Windows::Foundation::IInspectable const&, Windows::UI::Xaml::Controls::ItemClickEventArgs const& e
        );
        void FavouritesItemsGridView_ItemClick(
            Windows::Foundation::IInspectable const&, Windows::UI::Xaml::Controls::ItemClickEventArgs const& e
        );

        static void final_release(std::unique_ptr<UserPage> ptr) {
            ptr->VideosItemsGridView().ItemsSource(nullptr);
            ptr->AudiosItemsGridView().ItemsSource(nullptr);
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
        bool m_bili_res_is_ready, m_bili_res_is_valid;
    };
}

namespace winrt::BiliUWP::factory_implementation {
    struct UserPage : UserPageT<UserPage, implementation::UserPage> {};
}
