﻿#pragma once

#include "VideoDanmakuCollection.g.h"
#include "VideoDanmakuControl.g.h"

namespace winrt::BiliUWP::implementation {
    struct VideoDanmakuControl;
    struct VideoDanmakuControl_SharedData;
    struct VideoDanmakuNormalItemContainer;

    struct VideoDanmakuCollection : VideoDanmakuCollectionT<VideoDanmakuCollection> {
        VideoDanmakuCollection(VideoDanmakuControl* ctrl);

        void AddManyNormal(array_view<BiliUWP::VideoDanmakuNormalItem const> items);
        uint32_t GetManyNormal(uint32_t startIndex, array_view<BiliUWP::VideoDanmakuNormalItem> items);
        void RemoveMany(array_view<uint64_t const> itemIds);
        void UpdateManyVisibility(array_view<BiliUWP::VideoDanmakuItemWithVisibility const> items);
        void ClearAll();

    private:
        friend struct VideoDanmakuControl;

        enum class DanmakuChangeState {
            Unchanged = 0,
            AppendedOnly,
            Mixed,
        };

        // Functor returns what changes have happened to the danmaku collection
        template<typename Functor>
        void run_with_lock(Functor&& functor);

        weak_ref<VideoDanmakuControl> m_weak_ctrl;

        std::mutex m_mutex;
        std::vector<VideoDanmakuNormalItemContainer> m_normal_items;
        DanmakuChangeState m_danmaku_change_state{ DanmakuChangeState::Unchanged };
    };

    struct VideoDanmakuControl : VideoDanmakuControlT<VideoDanmakuControl> {
        VideoDanmakuControl();
        ~VideoDanmakuControl();

        void InitializeComponent();

        BiliUWP::VideoDanmakuCollection Danmaku();
        void IsDanmakuVisible(bool value);
        bool IsDanmakuVisible();
        bool IsRunning();
        void EnableDebugOutput(bool value);
        bool EnableDebugOutput();
        void Start();
        void Pause();
        void Stop(bool continueDanmakuRunning);
        void UpdateCurrentTime(Windows::Foundation::TimeSpan const& time);
        void SetAssociatedMediaTimelineController(Windows::Media::MediaTimelineController const& controller);
        Windows::Media::MediaTimelineController GetAssociatedMediaTimelineController();
        void SetBackgroundPopulator(BiliUWP::VideoDanmakuPopulateD3DSurfaceDelegate const& handler, bool isProactive);
        void TriggerBackgroundUpdate();

    private:
        friend struct VideoDanmakuCollection;

        std::shared_ptr<VideoDanmakuControl_SharedData> m_shared_data;
        Windows::UI::Core::CoreWindow m_core_window{ nullptr };
        event_token m_et_core_window_visibility_changed{};
        event_token m_et_core_window_size_changed{};
        bool m_is_visible{};
        bool m_is_loaded{};
        bool m_is_core_window_visible{};
    };
}

namespace winrt::BiliUWP::factory_implementation {
    struct VideoDanmakuControl : VideoDanmakuControlT<VideoDanmakuControl, implementation::VideoDanmakuControl> {};
}
