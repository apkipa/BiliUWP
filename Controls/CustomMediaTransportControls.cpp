#include "pch.h"
#include "CustomMediaTransportControls.h"
#if __has_include("CustomMediaTransportControls.g.cpp")
#include "CustomMediaTransportControls.g.cpp"
#endif
#include "CustomMediaTransportControlsHelper.g.cpp"
#include "util.hpp"

using namespace winrt;
using namespace Windows::System;
using namespace Windows::UI::ViewManagement;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::Media::Playback;
using namespace Windows::Foundation::Collections;

namespace winrt::BiliUWP::implementation {
    hstring CustomMediaTransportControlsHelper::GetInsertBefore(UIElement const& target) {
        return unbox_value<hstring>(target.GetValue(m_InsertBeforeProperty));
    }
    void CustomMediaTransportControlsHelper::SetInsertBefore(UIElement const& target, hstring const& value) {
        target.SetValue(m_InsertBeforeProperty, box_value(value));
    }
    DependencyProperty CustomMediaTransportControlsHelper::m_InsertBeforeProperty =
        DependencyProperty::RegisterAttached(
            L"InsertBefore",
            winrt::xaml_typename<bool>(),
            winrt::xaml_typename<winrt::BiliUWP::CustomMediaTransportControlsHelper>(),
            PropertyMetadata{ winrt::box_value(hstring{}) }
    );

    CustomMediaTransportControls::CustomMediaTransportControls() :
        m_additional_items(single_threaded_observable_vector<UIElement>()) {}
    void CustomMediaTransportControls::OnApplyTemplate() {
        base_type::OnApplyTemplate();

        /*
        auto cmd_flyout = GetTemplateChild(L"SettingsFlyout")
            .as<Microsoft::UI::Xaml::Controls::CommandBarFlyout>();
        cmd_flyout.AreOpenCloseAnimationsEnabled(false);
        for (auto cmd : cmd_flyout.SecondaryCommands()) {
            if (auto abb = cmd.try_as<AppBarButton>()) {
                auto tt = ToolTipService::GetToolTip(abb).as<ToolTip>();
                abb.Label(unbox_value_or<hstring>(tt.Content(), {}));
                ToolTipService::SetToolTip(abb, nullptr);
            }
        }
        */

        GetTemplateChild(L"CastMenuItem").as<MenuFlyoutItem>().Click([this](auto&&, auto&&) {
            using namespace Windows::Media::Casting;
            CastingDevicePicker casting_picker;
            auto cp_filter = casting_picker.Filter();
            cp_filter.SupportsVideo(true);
            cp_filter.SupportsAudio(true);
            cp_filter.SupportsPictures(true);
            auto set_btn = GetTemplateChild(L"CMTCSettingsButton").as<UIElement>();
            auto set_btn_size = set_btn.ActualSize();
            auto transform = set_btn.TransformToVisual(Window::Current().Content());
            auto pt = transform.TransformPoint({ 0, 0 });
            // TODO: Pause the media while casting picker is being displayed?
            casting_picker.Show({ pt.X, pt.Y, set_btn_size.x, set_btn_size.y },
                Windows::UI::Popups::Placement::Above);
            casting_picker.CastingDeviceSelected(
                [weak_this = get_weak()](auto&&, CastingDeviceSelectedEventArgs const& e) -> fire_forget_except {
                    auto that = weak_this.get();
                    if (!that) { co_return; }
                    auto mpe = that->try_get_mpe();
                    if (!mpe) { co_return; }
                    auto cast_dev = e.SelectedCastingDevice();
                    co_await that->Dispatcher();
                    auto cast_conn = cast_dev.CreateCastingConnection();
                    co_await cast_conn.RequestStartCastingAsync(mpe->MediaPlayer().GetAsCastingSource());
                }
            );
        });
        GetTemplateChild(L"ZoomMenuItem").as<MenuFlyoutItem>().Click([this](auto&&, auto&&) {
            auto mpe = try_get_mpe();
            if (!mpe) { return; }
            if (mpe->Stretch() != Stretch::Uniform) {
                mpe->Stretch(Stretch::Uniform);
            }
            else {
                mpe->Stretch(Stretch::UniformToFill);
            }
        });

        auto find_and_remove_item_fn = [this](UIElement const& item) {
            uint32_t idx;
            if (auto mfib = item.try_as<MenuFlyoutItemBase>()) {
                // Find in SettingsFlyout
                auto sf = GetTemplateChild(L"SettingsFlyout").as<MenuFlyout>();
                auto sf_items = sf.Items();
                if (!sf_items.IndexOf(mfib, idx)) { return false; }
                sf_items.RemoveAt(idx);
                return true;
            }
            else if (auto icbe = item.try_as<ICommandBarElement>()) {
                // Find in MediaControlsCommandBar
                auto mccb = GetTemplateChild(L"MediaControlsCommandBar").as<CommandBar>();
                auto mccb_pc = mccb.PrimaryCommands();
                if (!mccb_pc.IndexOf(icbe, idx)) { return false; }
                mccb_pc.RemoveAt(idx);
                return true;
            }
            else {
                return false;
            }
        };
        auto insert_item_fn = [this](UIElement const& item) {
            uint32_t i;
            if (auto mfib = item.try_as<MenuFlyoutItemBase>()) {
                // Add to SettingsFlyout
                auto sf = GetTemplateChild(L"SettingsFlyout").as<MenuFlyout>();
                auto sf_items = sf.Items();
                hstring insert_before_name{ CustomMediaTransportControlsHelper::GetInsertBefore(item) };
                if (insert_before_name.empty()) {
                    // Append item
                    sf_items.Append(mfib);
                    return true;
                }
                // Insert item
                auto sf_items_size = sf_items.Size();
                for (i = 0; i < sf_items_size; i++) {
                    if (sf_items.GetAt(i).Name() == insert_before_name) {
                        break;
                    }
                }
                sf_items.InsertAt(i, mfib);
                return true;
            }
            else if (auto icbe = item.try_as<ICommandBarElement>()) {
                // Add to MediaControlsCommandBar
                auto mccb = GetTemplateChild(L"MediaControlsCommandBar").as<CommandBar>();
                auto mccb_pc = mccb.PrimaryCommands();
                // Apply uniform style
                if (auto abb = item.try_as<AppBarButton>()) {
                    auto root_res = GetTemplateChild(L"RootGrid").as<Grid>().Resources();
                    abb.Style(root_res.Lookup(box_value(L"AppBarButtonStyle")).as<Windows::UI::Xaml::Style>());
                }
                hstring insert_before_name{ CustomMediaTransportControlsHelper::GetInsertBefore(item) };
                if (insert_before_name.empty()) {
                    // Append item
                    mccb_pc.Append(icbe);
                    return true;
                }
                auto mccb_pc_size = mccb_pc.Size();
                for (i = 0; i < mccb_pc_size; i++) {
                    if (mccb_pc.GetAt(i).as<FrameworkElement>().Name() == insert_before_name) {
                        break;
                    }
                }
                mccb_pc.InsertAt(i, icbe);
                return true;
            }
            else {
                return false;
            }
        };

        auto reset_items_fn = [=] {
            while (!m_additional_items_copy.empty()) {
                find_and_remove_item_fn(m_additional_items_copy.back());
                m_additional_items_copy.pop_back();
            }
            for (auto&& item : m_additional_items) {
                m_additional_items_copy.push_back(item);
                if (!insert_item_fn(m_additional_items_copy.back())) {
                    throw hresult_error(E_FAIL, L"CustomMediaTransportControls: Invalid item to insert");
                }
            }
        };
        m_additional_items.VectorChanged(
            [=](IObservableVector<UIElement> const& sender, IVectorChangedEventArgs const& e) {
                uint32_t idx;
                switch (e.CollectionChange()) {
                case CollectionChange::ItemChanged:
                    idx = e.Index();
                    find_and_remove_item_fn(m_additional_items_copy[idx]);
                    m_additional_items_copy[idx] = sender.GetAt(idx);
                    if (!insert_item_fn(m_additional_items_copy[idx])) {
                        throw hresult_error(E_FAIL, L"CustomMediaTransportControls: Invalid item to insert");
                    }
                    break;
                case CollectionChange::ItemInserted:
                    idx = e.Index();
                    m_additional_items_copy.insert(m_additional_items_copy.begin() + idx, sender.GetAt(idx));
                    if (!insert_item_fn(m_additional_items_copy[idx])) {
                        throw hresult_error(E_FAIL, L"CustomMediaTransportControls: Invalid item to insert");
                    }
                    break;
                case CollectionChange::ItemRemoved:
                    idx = e.Index();
                    find_and_remove_item_fn(m_additional_items_copy[idx]);
                    m_additional_items_copy.erase(m_additional_items_copy.begin() + idx);
                    break;
                case CollectionChange::Reset:
                    reset_items_fn();
                    break;
                }
            }
        );
        reset_items_fn();
    }
    void CustomMediaTransportControls::OnPointerEntered(PointerRoutedEventArgs const& e) {
        this->Show();
    }
    void CustomMediaTransportControls::OnPointerExited(PointerRoutedEventArgs const& e) {
        if (get_is_playing()) {
            this->Hide();
        }
    }
    void CustomMediaTransportControls::OnPointerReleased(PointerRoutedEventArgs const& e) {
        if (get_is_full_window()) {
            util::winrt::force_focus_element(*this, FocusState::Programmatic);
            e.Handled(true);
        }
    }
    void CustomMediaTransportControls::OnPreviewKeyDown(KeyRoutedEventArgs const& e) {
        auto key = e.Key();
        if (key == VirtualKey::Space && OverrideSpaceForPlaybackControl()) {
            switch_play_pause();
            e.Handled(true);
        }
    }
    void CustomMediaTransportControls::OnKeyDown(KeyRoutedEventArgs const& e) {
        auto key = e.Key();
        if (key == VirtualKey::Escape) {
            auto app_view = ApplicationView::GetForCurrentView();
            if (app_view.IsFullScreenMode()) {
                auto mpe = try_get_mpe();
                if (!mpe) { return; }
                mpe->IsFullWindow(false);
                e.Handled(true);
            }
        }
        else if (key == VirtualKey::F11) {
            switch_fullscreen();
            e.Handled(true);
        }
    }
    void CustomMediaTransportControls::OnDoubleTapped(DoubleTappedRoutedEventArgs const& e) {
        if (e.OriginalSource() == GetTemplateChild(L"RootGrid")) {
            // TODO: Maybe let user choose double-click behavior?
            //switch_fullscreen();
            switch_play_pause();
            e.Handled(true);
        }
    }
    void CustomMediaTransportControls::OverrideSpaceForPlaybackControl(bool value) {
        SetValue(m_OverrideSpaceForPlaybackControlProperty, box_value(value));
    }
    bool CustomMediaTransportControls::OverrideSpaceForPlaybackControl() {
        return unbox_value<bool>(GetValue(m_OverrideSpaceForPlaybackControlProperty));
    }
    void CustomMediaTransportControls::LinkMPE(CustomMediaPlayerElement* mpe) {
        UnlinkMPE();
        m_weak_mpe = mpe->get_weak();
        m_ev_ar_mp_volume_changed = mpe->MediaPlayer().VolumeChanged(auto_revoke,
            [weak_this = get_weak(), dispatcher = Dispatcher()](MediaPlayer const& sender, IInspectable const&) {
                dispatcher.RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, [=] {
                    auto that = weak_this.get();
                    if (!that) { return; }
                    that->GetTemplateChild(L"VolumeSlider").as<Slider>().Value(sender.Volume() * 100);
                });
            }
        );
    }
    void CustomMediaTransportControls::UnlinkMPE(void) {
        auto mpe = try_get_mpe();
        if (!mpe) { return; }
        m_weak_mpe = nullptr;
        m_ev_ar_mp_volume_changed.revoke();
    }
    bool CustomMediaTransportControls::get_is_playing(void) {
        auto mpe = try_get_mpe();
        if (!mpe) { return false; }
        if (auto session = util::winrt::try_get_media_playback_session(mpe->MediaPlayer())) {
            return session.PlaybackState() == MediaPlaybackState::Playing;
        }
        return false;
    }
    bool CustomMediaTransportControls::get_is_full_window(void) {
        auto mpe = try_get_mpe();
        if (!mpe) { return false; }
        return mpe->IsFullWindow();
    }
    void CustomMediaTransportControls::switch_fullscreen(void) {
        auto mpe = try_get_mpe();
        if (!mpe) { return; }
        auto app_view = ApplicationView::GetForCurrentView();
        if (app_view.IsFullScreenMode()) {
            mpe->IsFullWindow(false);
        }
        else {
            mpe->IsFullWindow(!mpe->IsFullWindow());
        }
    }
    void CustomMediaTransportControls::switch_play_pause(void) {
        auto mpe = try_get_mpe();
        if (!mpe) { return; }
        auto player = mpe->MediaPlayer();
        if (auto session = util::winrt::try_get_media_playback_session(player)) {
            switch (session.PlaybackState()) {
            case MediaPlaybackState::Paused:
                player.Play();
                break;
            case MediaPlaybackState::Playing:
                if (session.CanPause()) {
                    player.Pause();
                }
                break;
            }
        }
    }

#define gen_dp_instantiation(prop_name, ...)                                                \
    DependencyProperty gen_dp_instantiation_self_type::m_ ## prop_name ## Property =        \
        DependencyProperty::Register(                                                       \
            L"" #prop_name,                                                                 \
            winrt::xaml_typename<                                                           \
                decltype(std::declval<gen_dp_instantiation_self_type>().prop_name())        \
            >(),                                                                            \
            winrt::xaml_typename<winrt::BiliUWP::gen_dp_instantiation_self_type>(),         \
            Windows::UI::Xaml::PropertyMetadata{ __VA_ARGS__ }                              \
    )

#define gen_dp_instantiation_self_type CustomMediaTransportControls
    gen_dp_instantiation(OverrideSpaceForPlaybackControl, box_value(false));
#undef gen_dp_instantiation_self_type
}
