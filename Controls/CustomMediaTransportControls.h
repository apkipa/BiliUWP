#pragma once

#include "CustomMediaTransportControls.g.h"

namespace winrt::BiliUWP::implementation {
    struct CustomMediaTransportControls : CustomMediaTransportControlsT<CustomMediaTransportControls> {
        CustomMediaTransportControls();

        void OnPointerEntered(Windows::UI::Xaml::Input::PointerRoutedEventArgs const& e);
        void OnPointerExited(Windows::UI::Xaml::Input::PointerRoutedEventArgs const& e);
        void OnPreviewKeyDown(Windows::UI::Xaml::Input::KeyRoutedEventArgs const& e);
        void OnKeyDown(Windows::UI::Xaml::Input::KeyRoutedEventArgs const& e);
        void OnDoubleTapped(Windows::UI::Xaml::Input::DoubleTappedRoutedEventArgs const& e);

        void OverrideSpaceForPlaybackControl(bool value);
        bool OverrideSpaceForPlaybackControl();

        static Windows::UI::Xaml::DependencyProperty OverrideSpaceForPlaybackControlProperty() {
            return m_OverrideSpaceForPlaybackControlProperty;
        }

    private:
        Windows::UI::Xaml::Controls::MediaPlayerElement try_get_parent_container();
        Windows::Media::Playback::MediaPlaybackCommandManager try_get_command_manager();
        void switch_fullscreen(void);
        void switch_play_pause(void);

        static Windows::UI::Xaml::DependencyProperty m_OverrideSpaceForPlaybackControlProperty;
    };
}

namespace winrt::BiliUWP::factory_implementation {
    struct CustomMediaTransportControls : CustomMediaTransportControlsT<CustomMediaTransportControls, implementation::CustomMediaTransportControls> {};
}
