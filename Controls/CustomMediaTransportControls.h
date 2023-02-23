#pragma once

#include "CustomMediaTransportControls.g.h"

#include "CustomMediaPlayerElement.h"

namespace winrt::BiliUWP::implementation {
    struct CustomMediaPlayerElement;

    struct CustomMediaTransportControls : CustomMediaTransportControlsT<CustomMediaTransportControls> {
        CustomMediaTransportControls();

        void OnPointerEntered(Windows::UI::Xaml::Input::PointerRoutedEventArgs const& e);
        void OnPointerExited(Windows::UI::Xaml::Input::PointerRoutedEventArgs const& e);
        void OnPointerReleased(Windows::UI::Xaml::Input::PointerRoutedEventArgs const& e);
        void OnPreviewKeyDown(Windows::UI::Xaml::Input::KeyRoutedEventArgs const& e);
        void OnKeyDown(Windows::UI::Xaml::Input::KeyRoutedEventArgs const& e);
        void OnDoubleTapped(Windows::UI::Xaml::Input::DoubleTappedRoutedEventArgs const& e);

        void OverrideSpaceForPlaybackControl(bool value);
        bool OverrideSpaceForPlaybackControl();

        static Windows::UI::Xaml::DependencyProperty OverrideSpaceForPlaybackControlProperty() {
            return m_OverrideSpaceForPlaybackControlProperty;
        }

    private:
        friend struct CustomMediaPlayerElement;

        void LinkMPE(CustomMediaPlayerElement* mpe);
        void UnlinkMPE(void);

        auto try_get_mpe(void) { return m_weak_mpe ? m_weak_mpe.get() : nullptr; }
        bool get_is_playing(void);
        bool get_is_full_window(void);
        void switch_fullscreen(void);
        void switch_play_pause(void);

        static Windows::UI::Xaml::DependencyProperty m_OverrideSpaceForPlaybackControlProperty;

        weak_ref<CustomMediaPlayerElement> m_weak_mpe{ nullptr };
        event_token m_ev_mp_volume_changed;
    };
}

namespace winrt::BiliUWP::factory_implementation {
    struct CustomMediaTransportControls : CustomMediaTransportControlsT<CustomMediaTransportControls, implementation::CustomMediaTransportControls> {};
}
