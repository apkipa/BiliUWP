#include "pch.h"
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
        m_dialog_showing(false), m_finish_event()
    {
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
        using namespace std::chrono_literals;

        auto strong_this = get_strong();

        m_finish_event.reset();

        auto cancellation_token = co_await winrt::get_cancellation_token();
        cancellation_token.enable_propagation();
        cancellation_token.callback([this] {
            VisualStateManager::GoToState(*this, L"DialogHidden", false);
        });

        // We comment this because there are scenarios where XAML tree is not yet
        // built, and VisualTreeHelper::GetParent() won't return anything
        /*if (!VisualTreeHelper::GetParent(*this)) {
            throw hresult_invalid_argument(L"SimpleContentDialog requires a parent to display");
        }*/
        if (m_dialog_showing.exchange(true)) {
            throw hresult_error(E_FAIL, L"Dialog is already being shown");
        }

        Button::Click_revoker primary_btn_click_revoker, secondary_btn_click_revoker, close_btn_click_revoker;
        SimpleContentDialog::Loaded_revoker loaded_revoker;
        SimpleContentDialog::KeyDown_revoker key_down_revoker;

        SimpleContentDialogResult result = SimpleContentDialogResult::None;

        struct shared_data : std::enable_shared_from_this<shared_data> {
            std::mutex mutex;
            bool should_continue = true;
        };
        auto sdata = std::make_shared<shared_data>();

        auto run_fn = [&] {
            unsigned show_primary = PrimaryButtonText() != L"";
            unsigned show_secondary = SecondaryButtonText() != L"";
            unsigned show_close = CloseButtonText() != L"";
            switch (show_close | (show_primary << 1) | (show_secondary << 2)) {
            case 0x0:   VisualStateManager::GoToState(*this, L"NoneVisible", true);                 break;
            case 0x1:   VisualStateManager::GoToState(*this, L"CloseVisible", true);                break;
            case 0x2:   VisualStateManager::GoToState(*this, L"PrimaryVisible", true);              break;
            case 0x3:   VisualStateManager::GoToState(*this, L"PrimaryAndCloseVisible", true);      break;
            case 0x4:   VisualStateManager::GoToState(*this, L"SecondaryVisible", true);            break;
            case 0x5:   VisualStateManager::GoToState(*this, L"SecondaryAndCloseVisible", true);    break;
            case 0x6:   VisualStateManager::GoToState(*this, L"PrimaryAndSecondaryVisible", true);  break;
            case 0x7:   VisualStateManager::GoToState(*this, L"AllVisible", true);                  break;
            default:    throw hresult_error(E_FAIL, L"Unreachable");
            }
            VisualStateManager::GoToState(*this, L"DialogShowing", true);

            auto gen_btn_click_handler_fn = [&](SimpleContentDialogResult value) {
                return [&, value](IInspectable const&, RoutedEventArgs const&) {
                    result = value;
                    m_finish_event.set();
                };
            };
            primary_btn_click_revoker = GetTemplateChild(L"PrimaryButton").as<Button>().Click(
                auto_revoke, gen_btn_click_handler_fn(SimpleContentDialogResult::Primary)
            );
            secondary_btn_click_revoker = GetTemplateChild(L"SecondaryButton").as<Button>().Click(
                auto_revoke, gen_btn_click_handler_fn(SimpleContentDialogResult::Secondary)
            );
            close_btn_click_revoker = GetTemplateChild(L"CloseButton").as<Button>().Click(
                auto_revoke, gen_btn_click_handler_fn(SimpleContentDialogResult::None)
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
            loaded_revoker = Loaded(auto_revoke, [&, sdata](IInspectable const&, RoutedEventArgs const&) {
                std::scoped_lock guard(sdata->mutex);
                if (!sdata->should_continue) { return; }
                run_fn();
            });
        }

        {
            deferred([&] {
                std::scoped_lock guard(sdata->mutex);
                sdata->should_continue = false;
            });
            co_await m_finish_event;
        }
        co_await Dispatcher();

        if (IsLoaded()) {
            // TODO: Reusing m_finish_event may not be reliable enough here
            m_finish_event.reset();
            VisualStateManager::GoToState(*this, L"DialogHidden", true);
            co_await m_finish_event;
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
    void SimpleContentDialog::OnHidden(IInspectable const&, IInspectable const&) {
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
        return winrt::unbox_value_or<hstring>(GetValue(m_PrimaryButtonTextProperty), L"");
    }
    void SimpleContentDialog::PrimaryButtonText(hstring const& value) {
        SetValue(m_PrimaryButtonTextProperty, winrt::box_value(value));
    }
    hstring SimpleContentDialog::SecondaryButtonText() {
        return winrt::unbox_value_or<hstring>(GetValue(m_SecondaryButtonTextProperty), L"");
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

#define gen_dp_instantiation_self_type SimpleContentDialog
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

    gen_dp_instantiation(Title, nullptr);
    gen_dp_instantiation(TitleTemplate, nullptr);
    gen_dp_instantiation(PrimaryButtonText, nullptr);
    gen_dp_instantiation(SecondaryButtonText, nullptr);
    gen_dp_instantiation(CloseButtonText, nullptr);
    gen_dp_instantiation(PrimaryButtonStyle, nullptr);
    gen_dp_instantiation(SecondaryButtonStyle, nullptr);
    gen_dp_instantiation(CloseButtonStyle, nullptr);
    gen_dp_instantiation(IsPrimaryButtonEnabled, box_value(true));
    gen_dp_instantiation(IsSecondaryButtonEnabled, box_value(true));

#undef gen_dp_instantiation
#undef gen_dp_instantiation_self_type
}
