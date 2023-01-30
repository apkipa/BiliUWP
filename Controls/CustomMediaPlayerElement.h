#pragma once

#include "winrt/Windows.UI.Xaml.h"
#include "winrt/Windows.UI.Xaml.Markup.h"
#include "winrt/Windows.UI.Xaml.Interop.h"
#include "winrt/Windows.UI.Xaml.Controls.Primitives.h"
#include "CustomMediaPlayerElement.g.h"

namespace winrt::BiliUWP::implementation {
    struct CustomMediaPlayerElement : CustomMediaPlayerElementT<CustomMediaPlayerElement> {
        CustomMediaPlayerElement();

        Windows::Foundation::IInspectable MiddleLayerContent();
        void MiddleLayerContent(Windows::Foundation::IInspectable const& value);
        Windows::Foundation::IInspectable UpperLayerContent();
        void UpperLayerContent(Windows::Foundation::IInspectable const& value);

        static Windows::UI::Xaml::DependencyProperty MiddleLayerContentProperty() { return m_MiddleLayerContentProperty; }
        static Windows::UI::Xaml::DependencyProperty UpperLayerContentProperty() { return m_UpperLayerContentProperty; }

    private:
        static Windows::UI::Xaml::DependencyProperty m_MiddleLayerContentProperty;
        static Windows::UI::Xaml::DependencyProperty m_UpperLayerContentProperty;
    };
}

namespace winrt::BiliUWP::factory_implementation {
    struct CustomMediaPlayerElement : CustomMediaPlayerElementT<CustomMediaPlayerElement, implementation::CustomMediaPlayerElement> {};
}
