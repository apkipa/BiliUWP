#pragma once

#include "CustomMediaTransportControls.g.h"

namespace winrt::BiliUWP::implementation {
    struct CustomMediaTransportControls : CustomMediaTransportControlsT<CustomMediaTransportControls> {
        CustomMediaTransportControls();

        void OnPointerEntered(Windows::UI::Xaml::Input::PointerRoutedEventArgs const& e);
        void OnPointerExited(Windows::UI::Xaml::Input::PointerRoutedEventArgs const& e);
        void OnKeyDown(Windows::UI::Xaml::Input::KeyRoutedEventArgs const& e);

    private:
        Windows::UI::Xaml::Controls::MediaPlayerElement try_get_parent_container();
        Windows::Media::Playback::MediaPlaybackCommandManager try_get_command_manager();
    };
}

namespace winrt::BiliUWP::factory_implementation {
    struct CustomMediaTransportControls : CustomMediaTransportControlsT<CustomMediaTransportControls, implementation::CustomMediaTransportControls> {};
}
