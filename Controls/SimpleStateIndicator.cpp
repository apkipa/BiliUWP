#include "pch.h"
#include "SimpleStateIndicator.h"
#if __has_include("SimpleStateIndicator.g.cpp")
#include "SimpleStateIndicator.g.cpp"
#endif
#include "util.hpp"

using namespace winrt;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;

namespace winrt::BiliUWP::implementation {
    void SimpleStateIndicator::LayoutType(SimpleStateIndicatorLayoutType value) {
        switch (value) {
        case SimpleStateIndicatorLayoutType::Full:
            VisualStateManager::GoToState(*this, L"Full", true);
            break;
        case SimpleStateIndicatorLayoutType::Inline:
            VisualStateManager::GoToState(*this, L"Inline", true);
            break;
        default:
            throw hresult_invalid_argument(L"Invalid SimpleStateIndicatorLayoutType");
        }
    }
    void SimpleStateIndicator::SwitchToHidden() {
        StateText().Text(L"");
        VisualStateManager::GoToState(*this, L"Hidden", true);
    }
    void SimpleStateIndicator::SwitchToLoading(hstring const& text) {
        StateText().Text(text);
        VisualStateManager::GoToState(*this, L"OnLoading", true);
    }
    void SimpleStateIndicator::SwitchToFailed(hstring const& text) {
        StateText().Text(text);
        VisualStateManager::GoToState(*this, L"OnFailed", true);
    }
}
