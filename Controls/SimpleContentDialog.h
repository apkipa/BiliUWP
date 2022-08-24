#pragma once

#include "winrt/Windows.UI.Xaml.h"
#include "winrt/Windows.UI.Xaml.Markup.h"
#include "winrt/Windows.UI.Xaml.Interop.h"
#include "winrt/Windows.UI.Xaml.Controls.Primitives.h"
#include "SimpleContentDialog.g.h"
#include "util.hpp"

namespace winrt::BiliUWP::implementation {
    struct SimpleContentDialog : SimpleContentDialogT<SimpleContentDialog> {
        SimpleContentDialog();

        Windows::Foundation::IAsyncOperation<SimpleContentDialogResult> ShowAsync();
        void Hide();

        Windows::Foundation::IInspectable Title();
        void Title(Windows::Foundation::IInspectable const& value);
        Windows::UI::Xaml::DataTemplate TitleTemplate();
        void TitleTemplate(Windows::UI::Xaml::DataTemplate const& value);
        hstring PrimaryButtonText();
        void PrimaryButtonText(hstring const& value);
        hstring SecondaryButtonText();
        void SecondaryButtonText(hstring const& value);
        hstring CloseButtonText();
        void CloseButtonText(hstring const& value);
        Windows::UI::Xaml::Style PrimaryButtonStyle();
        void PrimaryButtonStyle(Windows::UI::Xaml::Style const& value);
        Windows::UI::Xaml::Style SecondaryButtonStyle();
        void SecondaryButtonStyle(Windows::UI::Xaml::Style const& value);
        Windows::UI::Xaml::Style CloseButtonStyle();
        void CloseButtonStyle(Windows::UI::Xaml::Style const& value);
        bool IsPrimaryButtonEnabled();
        void IsPrimaryButtonEnabled(bool value);
        bool IsSecondaryButtonEnabled();
        void IsSecondaryButtonEnabled(bool value);

        static Windows::UI::Xaml::DependencyProperty TitleProperty() { return m_TitleProperty; }
        static Windows::UI::Xaml::DependencyProperty TitleTemplateProperty() { return m_TitleTemplateProperty; }
        static Windows::UI::Xaml::DependencyProperty PrimaryButtonTextProperty() { return m_PrimaryButtonTextProperty; }
        static Windows::UI::Xaml::DependencyProperty SecondaryButtonTextProperty() { return m_SecondaryButtonTextProperty; }
        static Windows::UI::Xaml::DependencyProperty CloseButtonTextProperty() { return m_CloseButtonTextProperty; }
        static Windows::UI::Xaml::DependencyProperty PrimaryButtonStyleProperty() { return m_PrimaryButtonStyleProperty; }
        static Windows::UI::Xaml::DependencyProperty SecondaryButtonStyleProperty() { return m_SecondaryButtonStyleProperty; }
        static Windows::UI::Xaml::DependencyProperty CloseButtonStyleProperty() { return m_CloseButtonStyleProperty; }
        static Windows::UI::Xaml::DependencyProperty IsPrimaryButtonEnabledProperty() { return m_IsPrimaryButtonEnabledProperty; }
        static Windows::UI::Xaml::DependencyProperty IsSecondaryButtonEnabledProperty() { return m_IsSecondaryButtonEnabledProperty; }

        static void final_release(std::unique_ptr<SimpleContentDialog> ptr) noexcept {
            ptr->m_finish_event.set();
        }

    private:
        std::atomic_bool m_dialog_showing;
        util::winrt::awaitable_event m_finish_event;

        static Windows::UI::Xaml::DependencyProperty m_TitleProperty;
        static Windows::UI::Xaml::DependencyProperty m_TitleTemplateProperty;
        static Windows::UI::Xaml::DependencyProperty m_PrimaryButtonTextProperty;
        static Windows::UI::Xaml::DependencyProperty m_SecondaryButtonTextProperty;
        static Windows::UI::Xaml::DependencyProperty m_CloseButtonTextProperty;
        static Windows::UI::Xaml::DependencyProperty m_PrimaryButtonStyleProperty;
        static Windows::UI::Xaml::DependencyProperty m_SecondaryButtonStyleProperty;
        static Windows::UI::Xaml::DependencyProperty m_CloseButtonStyleProperty;
        static Windows::UI::Xaml::DependencyProperty m_IsPrimaryButtonEnabledProperty;
        static Windows::UI::Xaml::DependencyProperty m_IsSecondaryButtonEnabledProperty;
    };
}

namespace winrt::BiliUWP::factory_implementation {
    struct SimpleContentDialog : SimpleContentDialogT<SimpleContentDialog, implementation::SimpleContentDialog> {};
}
