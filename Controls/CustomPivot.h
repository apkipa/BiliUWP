#pragma once

#include "CustomPivot.g.h"

namespace winrt::BiliUWP::implementation {
    struct CustomPivot : CustomPivotT<CustomPivot> {
        CustomPivot() = default;

        void OnKeyDown(Windows::UI::Xaml::Input::KeyRoutedEventArgs const& e);
    };
}

namespace winrt::BiliUWP::factory_implementation {
    struct CustomPivot : CustomPivotT<CustomPivot, implementation::CustomPivot> {};
}
