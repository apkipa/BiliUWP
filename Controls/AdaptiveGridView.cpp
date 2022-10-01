#include "pch.h"
#include "AdaptiveGridView.h"
#if __has_include("AdaptiveGridView.g.cpp")
#include "AdaptiveGridView.g.cpp"
#endif
#include "util.hpp"

using namespace winrt;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Data;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Interop;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;

namespace winrt::BiliUWP::implementation {
    struct AdaptiveHeightValueConverter : implements<AdaptiveHeightValueConverter, IValueConverter> {
        AdaptiveHeightValueConverter() {}

        Thickness DefaultItemMargin() { return m_thickness; }
        void DefaultItemMargin(Thickness const& value) { m_thickness = value; }

        IInspectable Convert(
            IInspectable const& value,
            TypeName const& targetType,
            IInspectable const& parameter,
            hstring const& language
        ) {
            (void)targetType; (void)language;
            if (value) {
                auto grid_view = parameter.try_as<GridView>();
                if (!grid_view) {
                    return value;
                }
                if (auto parsed = util::num::try_parse_f64(value.as<IStringable>().ToString())) {
                    auto padding = grid_view.Padding();
                    auto margin = GetItemMargin(grid_view, DefaultItemMargin());
                    auto height = *parsed + margin.Top + margin.Bottom + padding.Top + padding.Bottom;
                    return box_value(height);
                }
                else {
                    return value;
                }
            }
            return box_value(std::numeric_limits<double>::quiet_NaN());
        }
        IInspectable ConvertBack(
            IInspectable const& value,
            TypeName const& targetType,
            IInspectable const& parameter,
            hstring const& language
        ) {
            (void)value; (void)targetType; (void)parameter; (void)language;
            throw hresult_not_implemented();
        }

        static Thickness GetItemMargin(GridView const& view, Thickness const& fallback = Thickness{}) {
            auto item_container_style = view.ItemContainerStyle();
            Setter setter{ nullptr };
            if (item_container_style) {
                auto framework_element_margin_property = FrameworkElement::MarginProperty();
                for (auto&& i : item_container_style.Setters()) {
                    auto i2 = i.try_as<Setter>();
                    if (!i2) {
                        continue;
                    }
                    if (i2.Property() == framework_element_margin_property) {
                        setter = i2;
                        break;
                    }
                }
            }
            if (setter) {
                return unbox_value<Thickness>(setter.Value());
            }
            else {
                if (view.Items().Size() > 0) {
                    auto container = view.ContainerFromIndex(0).try_as<GridViewItem>();
                    if (container) {
                        return container.Margin();
                    }
                }

                // Use the default thickness for a GridViewItem
                return fallback;
            }
        }

    private:
        Thickness m_thickness{ 0, 0, 4, 4 };
    };

    AdaptiveGridView::AdaptiveGridView() {
        IsTabStop(false);
        SizeChanged([this](IInspectable const&, SizeChangedEventArgs const& e) {
            // If we are in center alignment, we only care about relayout if the number of columns we can display changes
            // Fixes #1737
            auto previous_size = e.PreviousSize();
            auto new_size = e.NewSize();
            if (HorizontalAlignment() != HorizontalAlignment::Stretch) {
                auto desired_width = DesiredWidth();
                auto prev_columns = CalculateColumns(previous_size.Width, desired_width);
                auto new_columns = CalculateColumns(new_size.Width, desired_width);

                // If the width of the internal list view changes, check if more or less columns needs to be rendered.
                if (prev_columns != new_columns) {
                    RecalculateLayout(new_size.Width);
                }
            }
            else if (previous_size.Width != new_size.Width)
            {
                // We need to recalculate width as our size changes to adjust internal items.
                RecalculateLayout(new_size.Width);
            }
        });
        ItemClick([this](IInspectable const&, ItemClickEventArgs const& e) {
            if (auto cmd = ItemClickCommand()) {
                auto clicked_item = e.ClickedItem();
                if (cmd.CanExecute(clicked_item)) {
                    cmd.Execute(clicked_item);
                }
            }
        });
        Items().VectorChanged([this](IObservableVector<IInspectable> const&, IVectorChangedEventArgs const&) {
            auto actual_width = ActualWidth();
            if (!std::isnan(actual_width)) {
                RecalculateLayout(actual_width);
            }
        });
        Loaded([this](IInspectable const&, RoutedEventArgs const&) {
            m_is_loaded = true;
            DetermineOneRowMode();
        });
        Unloaded([this](IInspectable const&, RoutedEventArgs const&) {
            m_is_loaded = false;
        });

        UseLayoutRounding(false);
    }
    void AdaptiveGridView::PrepareContainerForItemOverride(
        DependencyObject const& obj, IInspectable const& item
    ) {
        AdaptiveGridViewT<AdaptiveGridView>::PrepareContainerForItemOverride(obj, item);

        if (auto element = obj.try_as<FrameworkElement>()) {
            auto height_binding = Binding();
            height_binding.Source(*this);
            height_binding.Path(PropertyPath(L"ItemHeight"));
            height_binding.Mode(BindingMode::TwoWay);

            auto width_binding = Binding();
            width_binding.Source(*this);
            width_binding.Path(PropertyPath(L"ItemWidth"));
            width_binding.Mode(BindingMode::TwoWay);

            element.SetBinding(FrameworkElement::HeightProperty(), height_binding);
            element.SetBinding(FrameworkElement::WidthProperty(), width_binding);
        }

        if (auto content_control = obj.try_as<ContentControl>()) {
            content_control.HorizontalContentAlignment(HorizontalAlignment::Stretch);
            content_control.VerticalContentAlignment(VerticalAlignment::Stretch);
        }

        if (m_need_container_margin_for_layout) {
            m_need_container_margin_for_layout = false;
            RecalculateLayout(ActualWidth());
        }
    }
    double AdaptiveGridView::CalculateItemWidth(double container_width) {
        auto desired_width = DesiredWidth();
        if (std::isnan(desired_width)) {
            return desired_width;
        }

        auto columns = CalculateColumns(container_width, desired_width);

        // If there's less items than there's columns, reduce the column count (if requested);
        if (auto items = Items()) {
            auto items_count = items.Size();
            if (0 < items_count && items_count < columns && StretchContentForSingleRow()) {
                columns = items_count;
            }
        }

        // subtract the margin from the width so we place the correct width for placement
        Thickness fallback_thickness{};
        auto item_margin = AdaptiveHeightValueConverter::GetItemMargin(*this, fallback_thickness);
        if (item_margin == fallback_thickness) {
            // No style explicitly defined, or no items or no container for the items
            // We need to get an actual margin for proper layout
            m_need_container_margin_for_layout = true;
        }

        return (container_width / columns) - item_margin.Left - item_margin.Right;
    }
    void AdaptiveGridView::OnApplyTemplate() {
        AdaptiveGridViewT<AdaptiveGridView>::OnApplyTemplate();

        OnOneRowModeEnabledChanged(*this, box_value(OneRowModeEnabled()));
    }
    void AdaptiveGridView::RecalculateLayout(double container_width) {
        double panel_margin;
        if (auto items_panel = ItemsPanelRoot().try_as<Panel>()) {
            auto temp_margin = items_panel.Margin();
            panel_margin = temp_margin.Left + temp_margin.Right;
        }
        else {
            panel_margin = 0;
        }
        auto temp_padding = Padding();
        auto padding = temp_padding.Left + temp_padding.Right;
        auto temp_border_thickness = BorderThickness();
        auto border = temp_border_thickness.Left + temp_border_thickness.Right;

        // width should be the displayable width
        container_width = container_width - padding - panel_margin - border;
        if (container_width > 0) {
            auto new_width = CalculateItemWidth(container_width);
            ItemWidth(std::floor(new_width));
        }
    }
    void AdaptiveGridView::OnOneRowModeEnabledChanged(
        Windows::UI::Xaml::DependencyObject const& d, IInspectable const&
    ) {
        auto self = d.as<AdaptiveGridView>();
        self->DetermineOneRowMode();
    }
    void AdaptiveGridView::DesiredWidthChanged(
        Windows::UI::Xaml::DependencyObject const& d,
        Windows::UI::Xaml::DependencyPropertyChangedEventArgs const&
    ) {
        auto self = d.as<AdaptiveGridView>();
        self->RecalculateLayout(self->ActualWidth());
    }
    void AdaptiveGridView::OnStretchContentForSingleRowPropertyChanged(
        Windows::UI::Xaml::DependencyObject const& d,
        Windows::UI::Xaml::DependencyPropertyChangedEventArgs const&
    ) {
        auto self = d.as<AdaptiveGridView>();
        self->RecalculateLayout(self->ActualWidth());
    }
    void AdaptiveGridView::DetermineOneRowMode() {
        if (m_is_loaded) {
            auto items_wrap_grid_panel = ItemsPanelRoot().try_as<ItemsWrapGrid>();

            if (OneRowModeEnabled()) {
                auto b = Binding();
                b.Source(*this);
                b.Path(PropertyPath(L"ItemHeight"));
                b.Converter(winrt::make<AdaptiveHeightValueConverter>());
                b.ConverterParameter(*this);

                if (items_wrap_grid_panel) {
                    m_saved_orientation = items_wrap_grid_panel.Orientation();
                    items_wrap_grid_panel.Orientation(Orientation::Vertical);
                }

                SetBinding(FrameworkElement::MaxHeightProperty(), b);

                m_saved_horizontal_scroll_mode = ScrollViewer::GetHorizontalScrollMode(*this);
                m_saved_vertical_scroll_mode = ScrollViewer::GetVerticalScrollMode(*this);
                m_saved_horizontal_scroll_bar_visibility = ScrollViewer::GetHorizontalScrollBarVisibility(*this);
                m_saved_vertical_scroll_bar_visibility = ScrollViewer::GetVerticalScrollBarVisibility(*this);
                m_need_to_restore_scroll_states = true;

                ScrollViewer::SetVerticalScrollMode(*this, ScrollMode::Disabled);
                ScrollViewer::SetVerticalScrollBarVisibility(*this, ScrollBarVisibility::Hidden);
                ScrollViewer::SetHorizontalScrollBarVisibility(*this, ScrollBarVisibility::Visible);
                ScrollViewer::SetHorizontalScrollMode(*this, ScrollMode::Enabled);
            }
            else
            {
                ClearValue(FrameworkElement::MaxHeightProperty());

                if (!m_need_to_restore_scroll_states) {
                    return;
                }

                m_need_to_restore_scroll_states = false;

                if (items_wrap_grid_panel) {
                    items_wrap_grid_panel.Orientation(m_saved_orientation);
                }

                ScrollViewer::SetVerticalScrollMode(*this, m_saved_vertical_scroll_mode);
                ScrollViewer::SetVerticalScrollBarVisibility(*this, m_saved_vertical_scroll_bar_visibility);
                ScrollViewer::SetHorizontalScrollBarVisibility(*this, m_saved_horizontal_scroll_bar_visibility);
                ScrollViewer::SetHorizontalScrollMode(*this, m_saved_horizontal_scroll_mode);
            }
        }
    }

    ICommand AdaptiveGridView::ItemClickCommand() {
        return winrt::unbox_value<ICommand>(GetValue(m_ItemClickCommandProperty));
    }
    void AdaptiveGridView::ItemClickCommand(ICommand const& value) {
        SetValue(m_ItemClickCommandProperty, winrt::box_value(value));
    }
    double AdaptiveGridView::ItemHeight() {
        return winrt::unbox_value<double>(GetValue(m_ItemHeightProperty));
    }
    void AdaptiveGridView::ItemHeight(double value) {
        SetValue(m_ItemHeightProperty, winrt::box_value(value));
    }
    bool AdaptiveGridView::OneRowModeEnabled() {
        return winrt::unbox_value<bool>(GetValue(m_OneRowModeEnabledProperty));
    }
    void AdaptiveGridView::OneRowModeEnabled(bool value) {
        SetValue(m_OneRowModeEnabledProperty, winrt::box_value(value));
    }
    double AdaptiveGridView::ItemWidth() {
        return winrt::unbox_value<double>(GetValue(m_ItemWidthProperty));
    }
    void AdaptiveGridView::ItemWidth(double value) {
        SetValue(m_ItemWidthProperty, winrt::box_value(value));
    }
    double AdaptiveGridView::DesiredWidth() {
        return winrt::unbox_value<double>(GetValue(m_DesiredWidthProperty));
    }
    void AdaptiveGridView::DesiredWidth(double value) {
        SetValue(m_DesiredWidthProperty, winrt::box_value(value));
    }
    bool AdaptiveGridView::StretchContentForSingleRow() {
        return winrt::unbox_value<bool>(GetValue(m_StretchContentForSingleRowProperty));
    }
    void AdaptiveGridView::StretchContentForSingleRow(bool value) {
        SetValue(m_StretchContentForSingleRowProperty, winrt::box_value(value));
    }

#define gen_dp_instantiation_self_type AdaptiveGridView
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

    // TODO: Fix DPs
    gen_dp_instantiation(ItemClickCommand, nullptr);
    gen_dp_instantiation(ItemHeight, box_value(std::numeric_limits<double>::quiet_NaN()));
    gen_dp_instantiation(OneRowModeEnabled, box_value(false), PropertyChangedCallback{
        [](DependencyObject const& o, DependencyPropertyChangedEventArgs const& e) {
            AdaptiveGridView::OnOneRowModeEnabledChanged(o, e.NewValue());
        }
    });
    gen_dp_instantiation(ItemWidth, box_value(std::numeric_limits<double>::quiet_NaN()));
    gen_dp_instantiation(DesiredWidth, box_value(std::numeric_limits<double>::quiet_NaN()),
        PropertyChangedCallback{ AdaptiveGridView::DesiredWidthChanged }
    );
    gen_dp_instantiation(StretchContentForSingleRow, box_value(true),
        PropertyChangedCallback{ AdaptiveGridView::OnStretchContentForSingleRowPropertyChanged }
    );

#undef gen_dp_instantiation
#undef gen_dp_instantiation_self_type

    unsigned long AdaptiveGridView::CalculateColumns(double container_width, double item_width) {
        auto columns = std::lround(container_width / item_width);
        if (columns == 0) {
            columns = 1;
        }
        return static_cast<unsigned long>(columns);
    }
}
