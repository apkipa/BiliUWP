#include "pch.h"
#include "SimpleStateOverlay.h"
#if __has_include("SimpleStateOverlay.g.cpp")
#include "SimpleStateOverlay.g.cpp"
#endif

using namespace winrt;
using namespace Windows::UI::Xaml;

namespace winrt::BiliUWP::implementation {
    void SimpleStateOverlay::SwitchToHidden() {
        StateText().Text(L"");
        VisualStateManager::GoToState(*this, L"Hidden", true);
    }
    void SimpleStateOverlay::SwitchToLoading(hstring const& text) {
        StateText().Text(text);
        VisualStateManager::GoToState(*this, L"OnLoading", true);
    }
    void SimpleStateOverlay::SwitchToFailed(hstring const& text) {
        StateText().Text(text);
        VisualStateManager::GoToState(*this, L"OnFailed", true);
    }
}
