#include "pch.h"
#include "CustomPivot.h"
#if __has_include("CustomPivot.g.cpp")
#include "CustomPivot.g.cpp"
#endif

using namespace winrt;
using namespace Windows::UI::Xaml;

namespace winrt::BiliUWP::implementation {
    void CustomPivot::OnKeyDown(Windows::UI::Xaml::Input::KeyRoutedEventArgs const& e) {
        // Do nothing
    }
}
