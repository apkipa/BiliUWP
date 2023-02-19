#include "pch.h"
#include "CustomMediaTransportControls.h"
#if __has_include("CustomMediaTransportControls.g.cpp")
#include "CustomMediaTransportControls.g.cpp"
#endif
#include "util.hpp"

using namespace winrt;
using namespace Windows::System;
using namespace Windows::UI::ViewManagement;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::Media::Playback;

namespace winrt::BiliUWP::implementation {
    CustomMediaTransportControls::CustomMediaTransportControls() {}

    void CustomMediaTransportControls::OnPointerEntered(PointerRoutedEventArgs const& e) {
        this->Show();
    }
    void CustomMediaTransportControls::OnPointerExited(PointerRoutedEventArgs const& e) {
        VisualStateGroup vsg{ nullptr };
        auto root_grid = GetTemplateChild(L"RootGrid").as<Grid>();
        for (auto&& i : VisualStateManager::GetVisualStateGroups(root_grid)) {
            if (i.Name() == L"PlayPauseStates") {
                vsg = std::move(i);
                break;
            }
        }
        bool is_playing = vsg.CurrentState().Name() == L"PauseState";
        if (is_playing) {
            this->Hide();
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
                app_view.ExitFullScreenMode();
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
    MediaPlayerElement CustomMediaTransportControls::try_get_parent_container() {
        auto dparent = VisualTreeHelper::GetParent(
            VisualTreeHelper::GetParent(VisualTreeHelper::GetParent(*this)));
        return dparent.try_as<MediaPlayerElement>();
    }
    MediaPlaybackCommandManager CustomMediaTransportControls::try_get_command_manager() {
        auto layout_root = VisualTreeHelper::GetParent(
            VisualTreeHelper::GetParent(*this)).try_as<Grid>();
        if (!layout_root) { return nullptr; }
        auto presenter = layout_root.FindName(L"MediaPlayerPresenter")
            .try_as<MediaPlayerPresenter>();
        if (!presenter) { return nullptr; }
        return presenter.MediaPlayer().CommandManager();
    }
    void CustomMediaTransportControls::switch_fullscreen(void) {
        auto app_view = ApplicationView::GetForCurrentView();
        if (app_view.IsFullScreenMode()) {
            app_view.ExitFullScreenMode();
        }
        else {
            auto container = try_get_parent_container();
            if (!container) { return; }
            container.IsFullWindow(!container.IsFullWindow());
        }
    }
    void CustomMediaTransportControls::switch_play_pause(void) {
        auto cmd_mgr = try_get_command_manager();
        if (!cmd_mgr) { return; }
        auto player = cmd_mgr.MediaPlayer();
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
