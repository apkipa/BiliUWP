﻿#include "pch.h"
#include "SimpleContentDialog.h"
#if __has_include("SimpleContentDialog.g.cpp")
#include "SimpleContentDialog.g.cpp"
#endif
#include "util.hpp"

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::System;
using namespace Windows::UI::Core;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Controls;

namespace winrt::BiliUWP::implementation {
    SimpleContentDialog::SimpleContentDialog() :
        m_dialog_showing(false), m_finish_event(), m_self(nullptr)
    {
        InitializeComponent();

        Loaded([this](IInspectable const&, RoutedEventArgs const&) {
            auto dlg = GetTemplateChild(L"BackgroundElement").as<UIElement>();
            if (auto ts = dlg.Shadow().try_as<ThemeShadow>()) {
                ts.Receivers().ReplaceAll({ GetTemplateChild(L"BackgroundShadow").as<UIElement>() });
            }
        });
        PointerPressed([](IInspectable const&, PointerRoutedEventArgs const& e) {
            e.Handled(true);
        });
    }
    IAsyncOperation<SimpleContentDialogResult> SimpleContentDialog::ShowAsync() {
        // TODO: Finish SimpleContentDialog::ShowAsync()
        using namespace std::chrono_literals;

        m_finish_event.reset();

        auto cancellation_token = co_await winrt::get_cancellation_token();
        cancellation_token.enable_propagation();
        cancellation_token.callback([this] {
            VisualStateManager::GoToState(*this, L"DialogHidden", false);
        });

        if (!VisualTreeHelper::GetParent(*this)) {
            throw hresult_invalid_argument(L"SimpleContentDialog requires a parent to display");
        }
        if (m_dialog_showing.exchange(true)) {
            throw hresult_error(E_FAIL, L"Dialog is already being shown");
        }

        Button::Click_revoker close_btn_click_revoker;
        SimpleContentDialog::Loaded_revoker loaded_revoker;
        SimpleContentDialog::KeyDown_revoker key_down_revoker;

        SimpleContentDialogResult result = SimpleContentDialogResult::None;

        auto run_fn = [&] {
            if (CloseButtonText() != L"") {
                VisualStateManager::GoToState(*this, L"CloseVisible", true);
            }
            else {
                VisualStateManager::GoToState(*this, L"NoneVisible", true);
            }
            VisualStateManager::GoToState(*this, L"DialogShowing", true);

            auto close_btn = GetTemplateChild(L"CloseButton").as<Button>();
            close_btn_click_revoker = close_btn.Click(auto_revoke,
                [&](IInspectable const&, RoutedEventArgs const&) {
                    result = SimpleContentDialogResult::None;
                    m_finish_event.set();
                }
            );
            key_down_revoker = KeyDown(auto_revoke,
                [&](IInspectable const&, KeyRoutedEventArgs const& e) {
                    if (e.Key() == VirtualKey::Escape) {
                        m_finish_event.set();
                    }
                }
            );

            Focus(FocusState::Programmatic);
        };
        if (IsLoaded()) {
            run_fn();
        }
        else {
            loaded_revoker = Loaded(auto_revoke, [&](IInspectable const&, RoutedEventArgs const&) {
                run_fn();
            });
        }

        std::unique_ptr<SimpleContentDialog> self_ptr = nullptr;
        {
            // Use deferred to make sure ownership transfers in case of cancellation
            deferred([&] {
                if (m_self) {
                    self_ptr = std::move(m_self);
                }
            });
            co_await m_finish_event;
        }
        if (self_ptr) {
            // Event fired because of final_release; destroy self immediately
            co_return SimpleContentDialogResult::None;
        }

        co_await Dispatcher();
        if (IsLoaded()) {
            VisualStateManager::GoToState(*this, L"DialogHidden", true);
            // NOTE: In XAML, transition time is 0.5s, and we wait for it to complete
            co_await 500ms;
        }
        else {
            VisualStateManager::GoToState(*this, L"DialogHidden", false);
        }

        m_dialog_showing.store(false);

        co_return result;
    }
    void SimpleContentDialog::Hide() {
        m_finish_event.set();
    }
    IInspectable SimpleContentDialog::Title() {
        return GetValue(m_TitleProperty);
    }
    void SimpleContentDialog::Title(IInspectable const& value) {
        SetValue(m_TitleProperty, value);
    }
    DataTemplate SimpleContentDialog::TitleTemplate() {
        return winrt::unbox_value<DataTemplate>(GetValue(m_TitleTemplateProperty));
    }
    void SimpleContentDialog::TitleTemplate(DataTemplate const& value) {
        SetValue(m_TitleTemplateProperty, winrt::box_value(value));
    }
    hstring SimpleContentDialog::PrimaryButtonText() {
        return winrt::unbox_value<hstring>(GetValue(m_PrimaryButtonTextProperty));
    }
    void SimpleContentDialog::PrimaryButtonText(hstring const& value) {
        SetValue(m_PrimaryButtonTextProperty, winrt::box_value(value));
    }
    hstring SimpleContentDialog::SecondaryButtonText() {
        return winrt::unbox_value<hstring>(GetValue(m_SecondaryButtonTextProperty));
    }
    void SimpleContentDialog::SecondaryButtonText(hstring const& value) {
        SetValue(m_SecondaryButtonTextProperty, winrt::box_value(value));
    }
    hstring SimpleContentDialog::CloseButtonText() {
        return winrt::unbox_value_or<hstring>(GetValue(m_CloseButtonTextProperty), L"");
    }
    void SimpleContentDialog::CloseButtonText(hstring const& value) {
        SetValue(m_CloseButtonTextProperty, winrt::box_value(value));
    }
    Windows::UI::Xaml::Style SimpleContentDialog::PrimaryButtonStyle() {
        return winrt::unbox_value<Windows::UI::Xaml::Style>(GetValue(m_PrimaryButtonStyleProperty));
    }
    void SimpleContentDialog::PrimaryButtonStyle(Windows::UI::Xaml::Style const& value) {
        SetValue(m_PrimaryButtonStyleProperty, winrt::box_value(value));
    }
    Windows::UI::Xaml::Style SimpleContentDialog::SecondaryButtonStyle() {
        return winrt::unbox_value<Windows::UI::Xaml::Style>(GetValue(m_SecondaryButtonStyleProperty));
    }
    void SimpleContentDialog::SecondaryButtonStyle(Windows::UI::Xaml::Style const& value) {
        SetValue(m_SecondaryButtonStyleProperty, winrt::box_value(value));
    }
    Windows::UI::Xaml::Style SimpleContentDialog::CloseButtonStyle() {
        return winrt::unbox_value<Windows::UI::Xaml::Style>(GetValue(m_CloseButtonStyleProperty));
    }
    void SimpleContentDialog::CloseButtonStyle(Windows::UI::Xaml::Style const& value) {
        SetValue(m_CloseButtonStyleProperty, winrt::box_value(value));
    }
    bool SimpleContentDialog::IsPrimaryButtonEnabled() {
        return winrt::unbox_value<bool>(GetValue(m_IsPrimaryButtonEnabledProperty));
    }
    void SimpleContentDialog::IsPrimaryButtonEnabled(bool value) {
        SetValue(m_IsPrimaryButtonEnabledProperty, winrt::box_value(value));
    }
    bool SimpleContentDialog::IsSecondaryButtonEnabled() {
        return winrt::unbox_value<bool>(GetValue(m_IsSecondaryButtonEnabledProperty));
    }
    void SimpleContentDialog::IsSecondaryButtonEnabled(bool value) {
        SetValue(m_IsSecondaryButtonEnabledProperty, winrt::box_value(value));
    }

#define gen_dp_instantiation(prop_name)                                                             \
    DependencyProperty SimpleContentDialog::m_ ## prop_name ## Property =                           \
        DependencyProperty::Register(                                                               \
            L"" #prop_name,                                                                         \
            winrt::xaml_typename<decltype(std::declval<SimpleContentDialog>().prop_name())>(),      \
            winrt::xaml_typename<winrt::BiliUWP::SimpleContentDialog>(),                            \
            Windows::UI::Xaml::PropertyMetadata{ nullptr }                                          \
    )

    gen_dp_instantiation(Title);
    gen_dp_instantiation(TitleTemplate);
    gen_dp_instantiation(PrimaryButtonText);
    gen_dp_instantiation(SecondaryButtonText);
    gen_dp_instantiation(CloseButtonText);
    gen_dp_instantiation(PrimaryButtonStyle);
    gen_dp_instantiation(SecondaryButtonStyle);
    gen_dp_instantiation(CloseButtonStyle);
    gen_dp_instantiation(IsPrimaryButtonEnabled);
    gen_dp_instantiation(IsSecondaryButtonEnabled);

#undef gen_dp_instantiation
}
