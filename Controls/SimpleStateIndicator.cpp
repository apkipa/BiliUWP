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
    void SimpleStateIndicator::SwitchToDone(hstring const& text) {
        StateText().Text(text);
        VisualStateManager::GoToState(*this, L"OnDone", true);
    }
    SimpleStateIndicatorLayoutType SimpleStateIndicator::LayoutType() {
        return winrt::unbox_value<SimpleStateIndicatorLayoutType>(GetValue(m_LayoutTypeProperty));
    }
    void SimpleStateIndicator::LayoutType(SimpleStateIndicatorLayoutType value) {
        SetValue(m_LayoutTypeProperty, winrt::box_value(value));
    }
    void SimpleStateIndicator::OnLayoutTypeValueChanged(
        DependencyObject const& d,
        DependencyPropertyChangedEventArgs const& e
    ) {
        auto old_value = unbox_value<SimpleStateIndicatorLayoutType>(e.OldValue());
        auto new_value = unbox_value<SimpleStateIndicatorLayoutType>(e.NewValue());
        if (old_value == new_value) { return; }
        auto that = d.as<BiliUWP::SimpleStateIndicator>();
        switch (new_value) {
        case SimpleStateIndicatorLayoutType::Full:
            VisualStateManager::GoToState(that, L"Full", true);
            break;
        case SimpleStateIndicatorLayoutType::Inline:
            VisualStateManager::GoToState(that, L"Inline", true);
            break;
        default:
            throw hresult_invalid_argument(L"Invalid SimpleStateIndicatorLayoutType");
            break;
        }
    }

#define gen_dp_instantiation_self_type SimpleStateIndicator
#define gen_dp_instantiation(prop_name, ...)                                                \
    DependencyProperty gen_dp_instantiation_self_type::m_ ## prop_name ## Property =        \
        DependencyProperty::Register(                                                       \
            L"" #prop_name,                                                                 \
            winrt::xaml_typename<                                                           \
                decltype(std::declval<gen_dp_instantiation_self_type>().prop_name())        \
            >(),                                                                            \
            winrt::xaml_typename<winrt::BiliUWP::gen_dp_instantiation_self_type>(),         \
            Windows::UI::Xaml::PropertyMetadata{ __VA_ARGS__ }                              \
    )

    gen_dp_instantiation(LayoutType, box_value(SimpleStateIndicatorLayoutType::Full), 
        PropertyChangedCallback{ &SimpleStateIndicator::OnLayoutTypeValueChanged });

#undef gen_dp_instantiation
#undef gen_dp_instantiation_self_type
}
