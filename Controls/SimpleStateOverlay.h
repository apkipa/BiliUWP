#pragma once

#include "winrt/Windows.UI.Xaml.h"
#include "winrt/Windows.UI.Xaml.Markup.h"
#include "winrt/Windows.UI.Xaml.Interop.h"
#include "winrt/Windows.UI.Xaml.Controls.Primitives.h"
#include "SimpleStateOverlay.g.h"

namespace winrt::BiliUWP::implementation {
    struct SimpleStateOverlay : SimpleStateOverlayT<SimpleStateOverlay> {
        SimpleStateOverlay() {}

        void SwitchToHidden();
        void SwitchToLoading(hstring const& text);
        void SwitchToFailed(hstring const& text);
    };
}

namespace winrt::BiliUWP::factory_implementation {
    struct SimpleStateOverlay : SimpleStateOverlayT<SimpleStateOverlay, implementation::SimpleStateOverlay> {};
}
