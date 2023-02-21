#pragma once

#include "CustomMediaPlayerElement.g.h"

namespace winrt::BiliUWP::implementation {
    struct CustomMediaTransportControls;

    struct CustomMediaPlayerElement : CustomMediaPlayerElementT<CustomMediaPlayerElement> {
        CustomMediaPlayerElement();
        ~CustomMediaPlayerElement();

        void OnApplyTemplate();

        Windows::Foundation::IInspectable MiddleLayerContent();
        void MiddleLayerContent(Windows::Foundation::IInspectable const& value);
        Windows::Foundation::IInspectable UpperLayerContent();
        void UpperLayerContent(Windows::Foundation::IInspectable const& value);

        static Windows::UI::Xaml::DependencyProperty MiddleLayerContentProperty() { return m_MiddleLayerContentProperty; }
        static Windows::UI::Xaml::DependencyProperty UpperLayerContentProperty() { return m_UpperLayerContentProperty; }

    private:
        friend struct CustomMediaTransportControls;

        static Windows::UI::Xaml::DependencyProperty m_MiddleLayerContentProperty;
        static Windows::UI::Xaml::DependencyProperty m_UpperLayerContentProperty;

        void OnMediaPlayerChanged(
            Windows::UI::Xaml::DependencyObject const& sender,
            Windows::UI::Xaml::DependencyProperty const&
        );
        void OnAreTransportControlsEnabledChanged(
            Windows::UI::Xaml::DependencyObject const& sender,
            Windows::UI::Xaml::DependencyProperty const&
        );

        com_ptr<CustomMediaTransportControls> m_old_tc{ nullptr };
    };
}

namespace winrt::BiliUWP::factory_implementation {
    struct CustomMediaPlayerElement : CustomMediaPlayerElementT<CustomMediaPlayerElement, implementation::CustomMediaPlayerElement> {};
}
