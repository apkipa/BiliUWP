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
        m_weak_mpe = mpe->get_weak();
    }
    void CustomMediaTransportControls::UnlinkMPE(void) {
        m_weak_mpe = nullptr;
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
