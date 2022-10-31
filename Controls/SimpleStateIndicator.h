#pragma once

#include "winrt/Windows.UI.Xaml.h"
#include "winrt/Windows.UI.Xaml.Markup.h"
#include "winrt/Windows.UI.Xaml.Interop.h"
#include "winrt/Windows.UI.Xaml.Controls.Primitives.h"
#include "SimpleStateIndicator.g.h"

namespace winrt::BiliUWP::implementation {
    struct SimpleStateIndicator : SimpleStateIndicatorT<SimpleStateIndicator> {
        SimpleStateIndicator() : m_layout_type(SimpleStateIndicatorLayoutType::Full) {}

        SimpleStateIndicatorLayoutType LayoutType() { return m_layout_type; }
        void LayoutType(SimpleStateIndicatorLayoutType value);

        void SwitchToHidden();
        void SwitchToLoading(hstring const& text);
        void SwitchToFailed(hstring const& text);

    private:
        SimpleStateIndicatorLayoutType m_layout_type;
    };
}

namespace winrt::BiliUWP::factory_implementation {
    struct SimpleStateIndicator : SimpleStateIndicatorT<SimpleStateIndicator, implementation::SimpleStateIndicator> {};
}
