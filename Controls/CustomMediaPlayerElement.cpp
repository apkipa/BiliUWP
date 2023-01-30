#include "pch.h"
#include "CustomMediaPlayerElement.h"
#if __has_include("CustomMediaPlayerElement.g.cpp")
#include "CustomMediaPlayerElement.g.cpp"
#endif

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::UI::Xaml;

namespace winrt::BiliUWP::implementation {
    CustomMediaPlayerElement::CustomMediaPlayerElement() {}
    IInspectable CustomMediaPlayerElement::MiddleLayerContent() {
        return GetValue(m_MiddleLayerContentProperty);
    }
    void CustomMediaPlayerElement::MiddleLayerContent(IInspectable const& value) {
        SetValue(m_MiddleLayerContentProperty, value);
    }
    IInspectable CustomMediaPlayerElement::UpperLayerContent() {
        return GetValue(m_UpperLayerContentProperty);
    }
    void CustomMediaPlayerElement::UpperLayerContent(IInspectable const& value) {
        SetValue(m_UpperLayerContentProperty, value);
    }

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

#define gen_dp_instantiation_self_type CustomMediaPlayerElement
    gen_dp_instantiation(MiddleLayerContent, nullptr);
    gen_dp_instantiation(UpperLayerContent, nullptr);
#undef gen_dp_instantiation_self_type

#undef gen_dp_instantiation
}
