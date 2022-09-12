#pragma once

#include "AdaptiveGridView.g.h"

namespace winrt::BiliUWP::implementation {
    struct AdaptiveGridView : AdaptiveGridViewT<AdaptiveGridView> {
        AdaptiveGridView();

        void PrepareContainerForItemOverride(
            Windows::UI::Xaml::DependencyObject const& obj, IInspectable const& item
        );
        void OnApplyTemplate();

        Windows::UI::Xaml::Input::ICommand ItemClickCommand();
        void ItemClickCommand(Windows::UI::Xaml::Input::ICommand const& value);
        double ItemHeight();
        void ItemHeight(double value);
        bool OneRowModeEnabled();
        void OneRowModeEnabled(bool value);
        double ItemWidth();
        void ItemWidth(double value);
        double DesiredWidth();
        void DesiredWidth(double value);
        bool StretchContentForSingleRow();
        void StretchContentForSingleRow(bool value);

        static Windows::UI::Xaml::DependencyProperty ItemClickCommandProperty() { return m_ItemClickCommandProperty; }
        static Windows::UI::Xaml::DependencyProperty ItemHeightProperty() { return m_ItemHeightProperty; }
        static Windows::UI::Xaml::DependencyProperty OneRowModeEnabledProperty() { return m_OneRowModeEnabledProperty; }
        static Windows::UI::Xaml::DependencyProperty ItemWidthProperty() { return m_ItemWidthProperty; }
        static Windows::UI::Xaml::DependencyProperty DesiredWidthProperty() { return m_DesiredWidthProperty; }
        static Windows::UI::Xaml::DependencyProperty StretchContentForSingleRowProperty() { return m_StretchContentForSingleRowProperty; }

        // This property overrides the base ItemsPanel to prevent changing it.
        Windows::UI::Xaml::Controls::ItemsPanelTemplate ItemsPanel() {
            return AdaptiveGridViewT<AdaptiveGridView>::ItemsPanel();
        }
        void ItemsPanel(Windows::UI::Xaml::Controls::ItemsPanelTemplate const& value) {
            AdaptiveGridViewT<AdaptiveGridView>::ItemsPanel(value);
        }
        
        static unsigned long CalculateColumns(double container_width, double item_width);

        static void final_release(std::unique_ptr<AdaptiveGridView> ptr) {
            // NOTE: To correctly release GridView, one must set ItemsSource to null
        }

    private:
        double CalculateItemWidth(double container_width);
        void RecalculateLayout(double container_width);
        static void OnOneRowModeEnabledChanged(
            Windows::UI::Xaml::DependencyObject const& d, IInspectable const&
        );
        static void DesiredWidthChanged(
            Windows::UI::Xaml::DependencyObject const& d,
            Windows::UI::Xaml::DependencyPropertyChangedEventArgs const&
        );
        static void OnStretchContentForSingleRowPropertyChanged(
            Windows::UI::Xaml::DependencyObject const& d,
            Windows::UI::Xaml::DependencyPropertyChangedEventArgs const&
        );
        void DetermineOneRowMode();

        static Windows::UI::Xaml::DependencyProperty m_ItemClickCommandProperty;
        static Windows::UI::Xaml::DependencyProperty m_ItemHeightProperty;
        static Windows::UI::Xaml::DependencyProperty m_OneRowModeEnabledProperty;
        static Windows::UI::Xaml::DependencyProperty m_ItemWidthProperty;
        static Windows::UI::Xaml::DependencyProperty m_DesiredWidthProperty;
        static Windows::UI::Xaml::DependencyProperty m_StretchContentForSingleRowProperty;

        bool m_is_loaded{};
        Windows::UI::Xaml::Controls::ScrollMode m_saved_vertical_scroll_mode{};
        Windows::UI::Xaml::Controls::ScrollMode m_saved_horizontal_scroll_mode{};
        Windows::UI::Xaml::Controls::ScrollBarVisibility m_saved_vertical_scroll_bar_visibility{};
        Windows::UI::Xaml::Controls::ScrollBarVisibility m_saved_horizontal_scroll_bar_visibility{};
        Windows::UI::Xaml::Controls::Orientation m_saved_orientation{};
        bool m_need_to_restore_scroll_states{};
        bool m_need_container_margin_for_layout{};
    };
}

namespace winrt::BiliUWP::factory_implementation {
    struct AdaptiveGridView : AdaptiveGridViewT<AdaptiveGridView, implementation::AdaptiveGridView> {};
}
