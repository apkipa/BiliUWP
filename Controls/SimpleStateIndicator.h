#pragma once

#include "winrt/Windows.UI.Xaml.h"
#include "winrt/Windows.UI.Xaml.Markup.h"
#include "winrt/Windows.UI.Xaml.Interop.h"
#include "winrt/Windows.UI.Xaml.Controls.Primitives.h"
#include "SimpleStateIndicator.g.h"

namespace winrt::BiliUWP::implementation {
    struct SimpleStateIndicator : SimpleStateIndicatorT<SimpleStateIndicator> {
        SimpleStateIndicator() {}

        void SwitchToHidden();
        void SwitchToLoading(hstring const& text);
        void SwitchToFailed(hstring const& text);
        void SwitchToDone(hstring const& text);

        SimpleStateIndicatorLayoutType LayoutType();
        void LayoutType(SimpleStateIndicatorLayoutType value);

        static Windows::UI::Xaml::DependencyProperty LayoutTypeProperty() { return m_LayoutTypeProperty; }

        static void OnLayoutTypeValueChanged(
            Windows::UI::Xaml::DependencyObject const& d,
            Windows::UI::Xaml::DependencyPropertyChangedEventArgs const& e
        );

    private:
        static Windows::UI::Xaml::DependencyProperty m_LayoutTypeProperty;
    };
}

namespace winrt::BiliUWP::factory_implementation {
    struct SimpleStateIndicator : SimpleStateIndicatorT<SimpleStateIndicator, implementation::SimpleStateIndicator> {};
}
