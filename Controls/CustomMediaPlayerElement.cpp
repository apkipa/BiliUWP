#include "pch.h"
#include "CustomMediaPlayerElement.h"
#if __has_include("CustomMediaPlayerElement.g.cpp")
#include "CustomMediaPlayerElement.g.cpp"
#endif

#include "CustomMediaTransportControls.h"

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;

namespace winrt::BiliUWP::implementation {
    CustomMediaPlayerElement::CustomMediaPlayerElement() {
        RegisterPropertyChangedCallback(
            Windows::UI::Xaml::Controls::MediaPlayerElement::MediaPlayerProperty(),
            { this, &CustomMediaPlayerElement::OnMediaPlayerChanged }
        );
        RegisterPropertyChangedCallback(
            Windows::UI::Xaml::Controls::MediaPlayerElement::AreTransportControlsEnabledProperty(),
            { this, &CustomMediaPlayerElement::OnAreTransportControlsEnabledChanged }
        );
    }
    CustomMediaPlayerElement::~CustomMediaPlayerElement() {
        if (m_old_tc) {
            m_old_tc->UnlinkMPE();
        }
    }
    void CustomMediaPlayerElement::OnApplyTemplate() {
        auto tcp = GetTemplateChild(L"TransportControlsPresenter").as<ContentPresenter>();
        tcp.RegisterPropertyChangedCallback(ContentPresenter::ContentProperty(),
            [this](DependencyObject const& sender, DependencyProperty const&) {
                auto tc = sender.as<ContentPresenter>().Content().try_as<CustomMediaTransportControls>();
                if (!tc) {
                    throw hresult_invalid_argument(L"CustomMediaPlayerElement only accepts CustomMediaTransportControls");
                }
                if (m_old_tc) {
                    m_old_tc->UnlinkMPE();
                }
                m_old_tc = tc;
                tc->LinkMPE(this);
            }
        );
        base_type::OnApplyTemplate();
    }
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
    void CustomMediaPlayerElement::OnMediaPlayerChanged(
        DependencyObject const& sender, DependencyProperty const&
    ) {
        if (!m_old_tc) { return; }
        if (AreTransportControlsEnabled()) {
            m_old_tc->LinkMPE(this);
        }
    }
    void CustomMediaPlayerElement::OnAreTransportControlsEnabledChanged(
        DependencyObject const& sender, DependencyProperty const&
    ) {
        if (!m_old_tc) { return; }
        if (AreTransportControlsEnabled()) {
            m_old_tc->LinkMPE(this);
        }
        else {
            m_old_tc->UnlinkMPE();
        }
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
