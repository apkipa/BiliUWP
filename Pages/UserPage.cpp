#include "pch.h"
#include "UserPage.h"
#if __has_include("UserPage.g.cpp")
#include "UserPage.g.cpp"
#endif
#include "AppItemsCollection.h"
#include "App.h"
#include <regex>


// NOTE: Keep these values in sync with XAML!
constexpr double HEADER_MAX_HEIGHT = 200;
constexpr double HEADER_MIN_HEIGHT = 56;

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Foundation::Collections;
using namespace Windows::System;
using namespace Windows::Devices::Input;
using namespace Windows::UI::Core;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Data;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Media::Imaging;
using namespace Windows::UI::Xaml::Hosting;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Composition;
using namespace Windows::UI::Composition::Interactions;
using ::BiliUWP::App::res_str;

#if 0
namespace winrt::BiliUWP::implementation {
    UserPage::UserPage() {}
    void UserPage::OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs const& e) {
        // TODO...

        struct InteractionOwner : implements<InteractionOwner, IInteractionTrackerOwner> {
            InteractionOwner(FrameworkElement const& header) : m_header(header) {}

            void CustomAnimationStateEntered(InteractionTracker sender, InteractionTrackerCustomAnimationStateEnteredArgs args) {
                //Logic to run when InteractionTracker enters CustomAnimation 
                util::debug::log_debug(L"");
            }
            void IdleStateEntered(InteractionTracker sender, InteractionTrackerIdleStateEnteredArgs args) {
                //Logic to run when InteractionTracker enters Idle
                util::debug::log_debug(L"");
            }
            void InertiaStateEntered(InteractionTracker sender, InteractionTrackerInertiaStateEnteredArgs args) {
                //Logic to run when InteractionTracker enters Inertia
                util::debug::log_debug(L"");
            }
            void InteractingStateEntered(InteractionTracker sender, InteractionTrackerInteractingStateEnteredArgs args) {
                //Logic to run when InteractionTracker enters Interacting
                util::debug::log_debug(L"");
            }
            void RequestIgnored(InteractionTracker sender, InteractionTrackerRequestIgnoredArgs args) {
                //Logic to run when a request to update position or scale is ignored
                util::debug::log_debug(L"");
            }
            void ValuesChanged(InteractionTracker sender, InteractionTrackerValuesChangedArgs args) {
                //Logic to run when position or scale change
                util::debug::log_debug(L"");
                m_header.Height(std::max(100 - sender.Position().y, 10.0f));
            }

        private:
            FrameworkElement m_header;
        };

        auto root = ElementCompositionPreview::GetElementVisual(*this);
        //ElementCompositionPreview::GetScrollViewerManipulationPropertySet()
        auto compositor = root.Compositor();
        auto tracker = InteractionTracker::CreateWithOwner(
            compositor, make<InteractionOwner>(Header())
        );
        //auto interaction_source = VisualInteractionSource::Create(root);
        auto interaction_source = VisualInteractionSource::Create(ElementCompositionPreview::GetElementVisual(ItemsList()));
        /*auto stopping_modifier = CompositionConditionalValue::Create(compositor);
        auto stopping_condition = compositor.CreateExpressionAnimation(L"Abs(tracker.Position.Y)>=30");
        auto stopping_alternate_value = compositor.CreateExpressionAnimation(L"0");
        stopping_condition.SetReferenceParameter(L"tracker", tracker);
        stopping_modifier.Condition(stopping_condition);
        stopping_modifier.Value(stopping_alternate_value);
        interaction_source.ConfigureDeltaPositionYModifiers({ stopping_modifier });*/
        /*interaction_source.ManipulationRedirectionMode(
            VisualInteractionSourceRedirectionMode::CapableTouchpadAndPointerWheel
        );*/
        interaction_source.PositionYSourceMode(InteractionSourceMode::EnabledWithInertia);
        interaction_source.PositionYChainingMode(InteractionChainingMode::Always);
        tracker.InteractionSources().Add(interaction_source);
        tracker.MaxPosition(float3(0, 60, 0));
        tracker.MinPosition(float3(0, 0, 0));
        static auto pos_exp = compositor.CreateExpressionAnimation(L"-tracker.Position.Y");
        pos_exp.SetReferenceParameter(L"tracker", tracker);
        /*ElementCompositionPreview::GetElementVisual(Container()).StartAnimation(L"Offset.Y", pos_exp);*/

        //ElementCompositionPreview::GetElementVisual(Container()).Clip();
        //tracker.Position()

        //ElementCompositionPreview::GetElementVisual(Container()).Offset(float3(0, 30, 0));

        using namespace std::chrono_literals;
        auto timer = DispatcherTimer();
        timer.Tick([=](IInspectable const&, IInspectable const&) {
            /*auto mode = ItemsList().ManipulationMode();
            ItemsList().ManipulationMode(mode ^ ManipulationModes::System);
            util::debug::log_debug(std::format(L"ManipulationMode: System => {}",
                static_cast<bool>(mode & ManipulationModes::System) ? L"Enabled" : L"Disabled"
            ));*/
            auto last_mode = interaction_source.ManipulationRedirectionMode();
            if (last_mode == VisualInteractionSourceRedirectionMode::Off) {
                interaction_source.ManipulationRedirectionMode(
                    VisualInteractionSourceRedirectionMode::CapableTouchpadAndPointerWheel
                );
                util::debug::log_debug(std::format(L"ManipulationRedirectionMode => CapableTouchpadAndPointerWheel"));
            }
            else {
                interaction_source.ManipulationRedirectionMode(
                    VisualInteractionSourceRedirectionMode::Off
                );
                util::debug::log_debug(std::format(L"ManipulationRedirectionMode => Off"));
            }
        });
        timer.Interval(1s);
        //timer.Start();

        auto report_pointer_fn = [](auto type, std::source_location sl = std::source_location::current())
        {
            switch (type) {
            case PointerDeviceType::Mouse:
                util::debug::log_debug(L"Pointer: Mouse", sl);
                break;
            case PointerDeviceType::Pen:
                util::debug::log_debug(L"Pointer: Pen", sl);
                break;
            case PointerDeviceType::Touch:
                util::debug::log_debug(L"Pointer: Touch", sl);
                break;
            }
        };

        Container().PointerPressed([=](IInspectable const&, PointerRoutedEventArgs const& e) {
            report_pointer_fn(e.Pointer().PointerDeviceType());
            if (e.Pointer().PointerDeviceType() == PointerDeviceType::Touch) {
                interaction_source.TryRedirectForManipulation(e.GetCurrentPoint(nullptr));
            }
        });
        Container().PointerReleased([=](IInspectable const&, PointerRoutedEventArgs const& e) {
            report_pointer_fn(e.Pointer().PointerDeviceType());
        });
        Container().PointerMoved([=](IInspectable const&, PointerRoutedEventArgs const& e) {
            report_pointer_fn(e.Pointer().PointerDeviceType());
        });
        /*Container().PointerWheelChanged([=](IInspectable const&, PointerRoutedEventArgs const& e) {
            report_pointer_fn(e.Pointer().PointerDeviceType());
        });*/
        ItemsList().AddHandler(UIElement::PointerWheelChangedEvent(), box_value(PointerEventHandler(
            [=](IInspectable const&, PointerRoutedEventArgs const& e) {
                report_pointer_fn(e.Pointer().PointerDeviceType());
                auto delta = e.GetCurrentPoint(nullptr).Properties().MouseWheelDelta();
                util::debug::log_debug(std::format(L"Handled? {}", e.Handled()));
#if 0
                /*if (delta < 0) {
                    interaction_source.ManipulationRedirectionMode(
                        VisualInteractionSourceRedirectionMode::CapableTouchpadAndPointerWheel
                    );
                }
                else {
                    interaction_source.ManipulationRedirectionMode(
                        VisualInteractionSourceRedirectionMode::CapableTouchpadOnly
                    );
                }*/
                /*interaction_source.ManipulationRedirectionMode(
                    VisualInteractionSourceRedirectionMode::CapableTouchpadAndPointerWheel
                );*/
                /*auto easing_func = compositor.CreateCubicBezierEasingFunction(float2(0, 0), float2(0.9f, 1));
                auto anim = compositor.CreateVector3KeyFrameAnimation();
                anim.InsertKeyFrame(0, float3(0, 0, 0), easing_func);
                anim.InsertKeyFrame(1, float3(0, -static_cast<float>(delta / fabs(delta) * 60), 0), easing_func);
                anim.Duration(100ms);*/
                //tracker.TryUpdatePositionWithAnimation(anim);
                //tracker.TryUpdatePositionWithAdditionalVelocity(float3(0, -static_cast<float>(delta) * 10, 0));
                //tracker.TryUpdatePositionBy(float3(0, -static_cast<float>(delta) * 10, 0));

                double RawPixelsPerViewPixel;
                uint32_t ScreenHeightInRawPixels;
                uint32_t ScreenWidthInRawPixels;
                try {
                    auto displayInformation = Windows::Graphics::Display::DisplayInformation::GetForCurrentView();
                    RawPixelsPerViewPixel = displayInformation.RawPixelsPerViewPixel();
                    ScreenWidthInRawPixels = displayInformation.ScreenWidthInRawPixels();
                    ScreenHeightInRawPixels = displayInformation.ScreenHeightInRawPixels();
                }
                catch (winrt::hresult_error const&) {
                    // Calling GetForCurrentView on threads without a CoreWindow throws an error. This comes up in places like LogonUI.
                    // In this circumstance, default values are used, resulting in good mouse-wheel scrolling increments:
                    RawPixelsPerViewPixel = 1.0;
                    ScreenWidthInRawPixels = 1024;
                    ScreenHeightInRawPixels = 738;
                }

                auto mouseWheelDelta = delta;
                const int32_t mouseWheelDeltaForVelocityUnit = 120;
                const int mouseWheelScrollLinesOrChars = 20;
                const bool isHorizontalMouseWheel = false;
                const float c_displayAdjustment = static_cast<float>((isHorizontalMouseWheel ? ScreenWidthInRawPixels : ScreenHeightInRawPixels) / RawPixelsPerViewPixel);
                static constexpr float s_mouseWheelInertiaDecayRate = 0.999972f;
                // Maximum absolute velocity. Any additional velocity has no effect.
                const float c_maxVelocity = 4000.0f;
                // Velocity per unit (which is a mouse wheel delta of 120 by default). That is the velocity required to achieve a change of c_offsetChangePerVelocityUnit pixels.
                const float c_unitVelocity = 0.524140190972223f * c_displayAdjustment * mouseWheelScrollLinesOrChars;
                // Effect of unit velocity on offset, to match the built-in RS5 behavior.
                const float c_offsetChangePerVelocityUnit = 0.05f * mouseWheelScrollLinesOrChars * c_displayAdjustment;
                float offsetVelocity = static_cast<float>(mouseWheelDelta) / mouseWheelDeltaForVelocityUnit * c_unitVelocity;
                if (!isHorizontalMouseWheel) {
                    offsetVelocity *= -1.0f;
                }
                offsetVelocity *= 0.1;
                float2 offsetsVelocity{
                    isHorizontalMouseWheel ? offsetVelocity : 0.0f,
                    isHorizontalMouseWheel ? 0.0f : offsetVelocity };
                const float minPosition = -60;
                const float maxPosition = 60;
                auto anticipatedEndOfInertiaPosition = 0;
                if (offsetVelocity > 0.0f) {
                    if (isHorizontalMouseWheel) {
                        // No point in exceeding the maximum effective velocity.
                        offsetsVelocity.x = std::min(c_maxVelocity, offsetsVelocity.x);

                        // Do not attempt to scroll beyond the MaxPosition value.
                        offsetsVelocity.x = std::min((maxPosition - anticipatedEndOfInertiaPosition) * c_unitVelocity / c_offsetChangePerVelocityUnit, offsetsVelocity.x);
                    }
                    else {
                        // No point in exceeding the maximum effective velocity.
                        offsetsVelocity.y = std::min(c_maxVelocity, offsetsVelocity.y);

                        // Do not attempt to scroll beyond the MaxPosition value.
                        offsetsVelocity.y = std::min((maxPosition - anticipatedEndOfInertiaPosition) * c_unitVelocity / c_offsetChangePerVelocityUnit, offsetsVelocity.y);
                    }
                }
                else {
                    if (isHorizontalMouseWheel) {
                        // No point in exceeding the minimum effective velocity.
                        offsetsVelocity.x = std::max(-c_maxVelocity, offsetsVelocity.x);

                        // Do not attempt to scroll beyond the MinPosition value.
                        offsetsVelocity.x = std::max((minPosition - anticipatedEndOfInertiaPosition) * c_unitVelocity / c_offsetChangePerVelocityUnit, offsetsVelocity.x);
                    }
                    else {
                        // No point in exceeding the minimum effective velocity.
                        offsetsVelocity.y = std::max(-c_maxVelocity, offsetsVelocity.y);

                        // Do not attempt to scroll beyond the MinPosition value.
                        offsetsVelocity.y = std::max((minPosition - anticipatedEndOfInertiaPosition) * c_unitVelocity / c_offsetChangePerVelocityUnit, offsetsVelocity.y);
                    }
                }
                //{
                //    // Minimum absolute velocity. Any lower velocity has no effect.
                //    const float c_minVelocity = 30.0f;
                //    // Maximum absolute velocity. Any additional velocity has no effect.
                //    const float c_maxVelocity = 4000.0f;

                //    if (offsetsVelocity.x > 0.0f && offsetsVelocity.x < c_maxVelocity)
                //    {
                //        offsetsVelocity.x = std::min(offsetsVelocity.x + c_minVelocity, c_maxVelocity);
                //    }
                //    else if (offsetsVelocity.x < 0.0f && offsetsVelocity.x > -c_maxVelocity)
                //    {
                //        offsetsVelocity.x = std::max(offsetsVelocity.x - c_minVelocity, -c_maxVelocity);
                //    }

                //    if (offsetsVelocity.y > 0.0f && offsetsVelocity.y < c_maxVelocity)
                //    {
                //        offsetsVelocity.y = std::min(offsetsVelocity.y + c_minVelocity, c_maxVelocity);
                //    }
                //    else if (offsetsVelocity.y < 0.0f && offsetsVelocity.y > -c_maxVelocity)
                //    {
                //        offsetsVelocity.y = std::max(offsetsVelocity.y - c_minVelocity, -c_maxVelocity);
                //    }
                //}
                tracker.PositionInertiaDecayRate(float3(0, s_mouseWheelInertiaDecayRate, 0));
                tracker.TryUpdatePositionWithAdditionalVelocity(
                    float3(offsetsVelocity, 0.0f));
                
                //util::winrt::get_descendant_scrollviewer(ItemsList()).SetVerticalScrollMode();
                //ScrollViewer::SetVerticalScrollBarVisibility(ItemsList(), ScrollBarVisibility::Disabled);
                //ItemsList().IsEnabled(false);
                //e.Handled(false);
#endif
                //e.Handled(false);
                interaction_source.ManipulationRedirectionMode(
                    VisualInteractionSourceRedirectionMode::CapableTouchpadAndPointerWheel
                );
            }
        )), true);
        Container().PointerWheelChanged([=](IInspectable const&, PointerRoutedEventArgs const& e) {
            util::debug::log_debug(L"PointerWheelChanged in Container");
        });
        Container().ManipulationStarted([=](IInspectable const&, ManipulationStartedRoutedEventArgs const& e) {
            report_pointer_fn(e.PointerDeviceType());
        });
        Container().ManipulationDelta([=](IInspectable const&, ManipulationDeltaRoutedEventArgs const& e) {
            report_pointer_fn(e.PointerDeviceType());
        });
        Container().ManipulationCompleted([=](IInspectable const&, ManipulationCompletedRoutedEventArgs const& e) {
            report_pointer_fn(e.PointerDeviceType());
        });
    }
}
#endif

namespace winrt::BiliUWP::implementation {
    UserPage::UserPage() : m_uid(0) {}
    void UserPage::OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs const& e) {
        auto tab = ::BiliUWP::App::get()->tab_from_page(*this);
        tab->set_icon(Symbol::ContactInfo);
        tab->set_title(L"UserPage");

        /*
        util::winrt::run_when_loaded([=](ListView const& items_list) {
            // [ABANDONED] ViewChanged-based method
            util::winrt::get_first_descendant<ScrollViewer>(items_list).ViewChanged(
                [=](IInspectable const&, ScrollViewerViewChangedEventArgs const& e) {
                    static double last_header_height = HEADER_MAX_HEIGHT;
                    static double last_voff = 0;
                    auto voff = util::winrt::get_first_descendant<ScrollViewer>(ItemsList()).VerticalOffset();
                    // Height = [cp.sv.Y>cp.sv.X?This.CurrentValue-(cp.sv.Y-cp.sv.X):(cp.sv.Y<{1}-{0}?Max(This.CurrentValue,{1}-cp.sv.Y):This.CurrentValue)]
                    if (voff > last_voff) {
                        // Shrink header
                        // Always apply delta
                        last_header_height -= voff - last_voff;
                    }
                    else {
                        // Expand header
                        // Apply delta only when scrolled to top
                        // 0        => MAX
                        // MAX-MIN  => MIN
                        // f(x) := MAX - x
                        if (voff < HEADER_MAX_HEIGHT - HEADER_MIN_HEIGHT) {
                            auto cur_header_height = HEADER_MAX_HEIGHT - voff;
                            if (cur_header_height > last_header_height) {
                                last_header_height = cur_header_height;
                            }
                        }
                    }
                    last_header_height = std::clamp(last_header_height, HEADER_MIN_HEIGHT, HEADER_MAX_HEIGHT);
                    last_voff = voff;
                    //Header().Translation(float3(0, last_header_height, 0));
                    //Header().Height(last_header_height);
                    util::debug::log_debug(std::format(L"Progress => {}, IsIntermediate = {}", last_voff, e.IsIntermediate()));
                }
            );
        }, ItemsList());
        */

        auto illegal_nav_fn = [this] {
            MainStateOverlay().SwitchToFailed(res_str(L"App/Common/IllegalNavParam"));
        };

        if (auto opt = e.Parameter().try_as<UserPageNavParam>()) {
            m_uid = opt->uid;
            m_cur_async.cancel_and_run([](UserPage* that) -> IAsyncAction {
                auto cancellation_token = co_await get_cancellation_token();
                cancellation_token.enable_propagation();

                auto weak_store = util::winrt::make_weak_storage(*that);

                auto overlay = that->MainStateOverlay();
                overlay.SwitchToLoading(res_str(L"App/Common/Loading"));
                try {
                    auto client = ::BiliUWP::App::get()->bili_client();
                    auto result = std::move(co_await weak_store.ual(client->user_space_info(that->m_uid)));

                    auto bmp_img = BitmapImage(Uri(result.top_photo_url));
                    auto img_brush = ImageBrush();
                    img_brush.Stretch(Stretch::UniformToFill);
                    img_brush.ImageSource(bmp_img);
                    that->HeaderBackground().Background(img_brush);
                    that->Header().Background(img_brush);
                    auto user_face_bmp_img = BitmapImage(Uri(result.face_url));
                    user_face_bmp_img.DecodePixelType(DecodePixelType::Logical);
                    user_face_bmp_img.DecodePixelWidth(80);
                    user_face_bmp_img.DecodePixelHeight(80);
                    that->UserFace().ProfilePicture(user_face_bmp_img);
                    that->UserName().Text(result.name);
                    that->UserSign().Text(hstring(
                        std::regex_replace(result.sign.c_str(), std::wregex(L"\\n", std::regex::optimize), L" ")
                    ));

                    auto idx_of_content_fn = [&](IInspectable const& cont) {
                        auto tabs_items = that->TabsPivot().Items();
                        auto size = tabs_items.Size();
                        for (uint32_t idx = 0; idx < size; idx++) {
                            if (tabs_items.GetAt(idx).as<PivotItem>().Content() == cont) {
                                return idx;
                            }
                        }
                        throw hresult_error(E_FAIL, L"Could not find tab index of given content");
                    };
                    auto apply_items_src = [&](auto const& grid_view, auto const& items_src) {
                        auto idx = idx_of_content_fn(grid_view);
                        grid_view.ItemsSource(items_src);
                        auto weak_this = that->get_weak();
                        items_src.OnStartLoading(
                            [=](BiliUWP::IncrementalLoadingCollection const& sender, IInspectable const&) {
                                auto strong_this = weak_this.get();
                                if (!strong_this) { return; }
                                if (sender.Size() == 0) {
                                    strong_this->m_view_is_volatile[idx] = true;
                                    util::debug::log_debug(std::format(L"Marked {} as volatile", idx));
                                }
                            }
                        );
                        items_src.OnEndLoading(
                            [=](BiliUWP::IncrementalLoadingCollection const&, IInspectable const&) {
                                auto strong_this = weak_this.get();
                                if (!strong_this) { return; }
                                if (strong_this->m_view_is_volatile[idx]) {
                                    strong_this->m_view_is_volatile[idx] = false;
                                    util::debug::log_debug(std::format(L"Marked {} as non-volatile", idx));
                                }
                            }
                        );
                    };
                    apply_items_src(
                        that->VideosItemsGridView(),
                        MakeIncrementalLoadingCollection(
                            std::make_shared<::BiliUWP::UserVideosViewItemsSource>(that->m_uid))
                    );
                    apply_items_src(
                        that->FavouritesItemsGridView(),
                        MakeIncrementalLoadingCollection(
                            std::make_shared<::BiliUWP::FavouritesUserViewItemsSource>(that->m_uid))
                    );
                }
                catch (::BiliUWP::BiliApiException const&) {
                    util::winrt::log_current_exception();
                    overlay.SwitchToFailed(res_str(L"App/Page/UserPage/Error/ServerAPIFailed"));
                    throw;
                }
                catch (hresult_canceled const&) { throw; }
                catch (...) {
                    util::winrt::log_current_exception();
                    overlay.SwitchToFailed(res_str(L"App/Page/UserPage/Error/Unknown"));
                    throw;
                }
                overlay.SwitchToHidden();
            }, this);
        }
        else {
            util::debug::log_error(L"OnNavigatedTo only expects UserPageNavParam");
            illegal_nav_fn();
        }

        util::winrt::discard_ctrl_tab_for_elem(TabsPivot(), *this);

        SizeChanged([this](IInspectable const&, SizeChangedEventArgs const&) {
            this->ApplyHacksToCurrentItem();
            if (this->IsLoaded()) {
                this->ReconnectExpressionAnimations();
            }
        });
        TabsPivot().SelectionChanged([this](IInspectable const& sender, SelectionChangedEventArgs const&) {
            auto tabs_pivot = sender.as<Pivot>();
            util::debug::log_debug(L"Selection changed");
            m_cur_tab_idx = static_cast<uint32_t>(tabs_pivot.SelectedIndex());

            this->ApplyHacksToCurrentItem();
            this->ReconnectExpressionAnimations();
        });

        TabsPivot().PivotItemLoading([](Pivot const& sender, PivotItemEventArgs const& e) {
            uint32_t idx;
            sender.Items().IndexOf(e.Item(), idx);
            util::debug::log_debug(std::format(L"Loading item {}", idx));
        });
        TabsPivot().PivotItemLoaded([this](Pivot const& sender, PivotItemEventArgs const& e) {
            uint32_t idx;
            sender.Items().IndexOf(e.Item(), idx);
            util::debug::log_debug(std::format(L"Loaded item {}", idx));
            m_cur_tab_idx = idx;
            this->ApplyHacksToCurrentItem();
            this->ReconnectExpressionAnimations();
        });
        TabsPivot().PivotItemUnloading([](Pivot const& sender, PivotItemEventArgs const& e) {
            uint32_t idx;
            sender.Items().IndexOf(e.Item(), idx);
            util::debug::log_debug(std::format(L"Unloading item {}", idx));
        });
        TabsPivot().PivotItemUnloaded([this](Pivot const& sender, PivotItemEventArgs const& e) {
            uint32_t idx;
            sender.Items().IndexOf(e.Item(), idx);
            util::debug::log_debug(std::format(L"Unloaded item {}", idx));
            m_cur_tab_idx = static_cast<uint32_t>(sender.SelectedIndex());
            this->ApplyHacksToCurrentItem();
            this->ReconnectExpressionAnimations();
        });
    }
    void UserPage::VideosItemsGridView_ItemClick(
        Windows::Foundation::IInspectable const&, Windows::UI::Xaml::Controls::ItemClickEventArgs const& e
    ) {
        auto vi = e.ClickedItem().as<UserVideosViewItem>();
        auto tab = ::BiliUWP::make<::BiliUWP::AppTab>();
        tab->navigate(
            xaml_typename<winrt::BiliUWP::MediaPlayPage>(),
            box_value(MediaPlayPageNavParam{ winrt::BiliUWP::MediaPlayPage_MediaType::Video, vi->AvId(), L""})
        );
        ::BiliUWP::App::get()->add_tab(tab);
        tab->activate();
    }
    void UserPage::FavouritesItemsGridView_ItemClick(
        Windows::Foundation::IInspectable const&, Windows::UI::Xaml::Controls::ItemClickEventArgs const& e
    ) {
        auto vi = e.ClickedItem().as<BiliUWP::FavouritesUserViewItem>();
        auto tab = ::BiliUWP::make<::BiliUWP::AppTab>();
        tab->navigate(
            xaml_typename<winrt::BiliUWP::FavouritesFolderPage>(),
            box_value(FavouritesFolderPageNavParam{ vi.FolderId() })
        );
        ::BiliUWP::App::get()->add_tab(tab);
        tab->activate();
    }
    void UserPage::ApplyHacksToCurrentItem(void) {
        using util::winrt::get_first_descendant;
        auto pivot_height = get_first_descendant<FrameworkElement>(TabsPivot(), L"HeaderClipper").ActualHeight();
        auto sv_presenter = get_first_descendant<ItemsPresenter>(TabsPivot().SelectedItem().as<UIElement>());
        util::winrt::run_when_loaded([=](FrameworkElement const& fe) {
            auto sv_presenter = get_first_descendant<ItemsPresenter>(fe);
            sv_presenter.Margin(ThicknessHelper::FromLengths(0, HEADER_MAX_HEIGHT - pivot_height, 0, 0));
            sv_presenter.MinHeight(this->ActualHeight() - HEADER_MIN_HEIGHT);
        }, TabsPivot().Items().GetAt(m_cur_tab_idx).as<PivotItem>().Content().as<FrameworkElement>());
    }
    void UserPage::ReconnectExpressionAnimations(void) {
        using util::winrt::get_first_descendant;
        auto tab_idx = TabsPivot().SelectedIndex();
        util::debug::log_debug(L"ReconnectExpressionAnimations called");
        if (m_view_is_volatile[tab_idx]) {
            util::debug::log_debug(std::format(L"Ignored ReconnectExpressionAnimations for tab {}", tab_idx));
            return;
        }
        util::winrt::run_when_loaded([=](FrameworkElement const& fe) {
            auto inner_sv = get_first_descendant<ScrollViewer>(fe);
            auto sv_props = ElementCompositionPreview::GetScrollViewerManipulationPropertySet(inner_sv);
            auto compositor = sv_props.Compositor();
            if (!m_comp_props) {
                m_comp_props = compositor.CreatePropertySet();
                m_comp_props.InsertVector2(L"sv", float2(0, 0));
                m_comp_props.InsertScalar(L"p", 0);
            }
            if (!m_ea_props_sv) {
                m_ea_props_sv = compositor.CreateExpressionAnimation(
                    L"Vector2(This.CurrentValue.Y,Max(-scroller.Translation.Y,0))"
                );
            }
            if (!m_ea_props_p) {
                m_ea_props_p = compositor.CreateExpressionAnimation(std::format(L""
                    "Clamp(This.Target.sv.Y>This.Target.sv.X?"
                    "This.CurrentValue+(This.Target.sv.Y-This.Target.sv.X)/({1}-{0}):"
                    //"(This.Target.sv.Y<{1}-{0}?Min(This.CurrentValue,This.Target.sv.Y/({1}-{0})):This.CurrentValue),0,1)",
                    "Min(This.CurrentValue,This.Target.sv.Y/({1}-{0})),0,1)",
                    HEADER_MIN_HEIGHT, HEADER_MAX_HEIGHT
                ));
            }
            {   // Disconnect previous animations & update states
                m_comp_props.StopAnimation(L"sv");
                m_comp_props.StopAnimation(L"p");
            }
            // Postpone animation reconnection until user scrolls
            m_ev_viewchanging_revoker = inner_sv.ViewChanging(auto_revoke,
                [=](IInspectable const&, ScrollViewerViewChangingEventArgs const&) {
                    util::debug::log_debug(L"Reconnecting animation");
                    if (m_ignore_next_viewchanging) {
                        util::debug::log_debug(std::format(
                            L"Ignored animation reconnection for tab {} (Reason: NextViewChanging)", tab_idx));
                        m_ignore_next_viewchanging = false;
                        return;
                    }
                    if (m_view_is_volatile[tab_idx]) {
                        util::debug::log_debug(std::format(
                            L"Ignored animation reconnection for tab {} (Reason: ViewIsVolatile)", tab_idx));
                        return;
                    }
                    m_latest_p = -1;
                    m_ea_props_sv.SetReferenceParameter(L"scroller", sv_props);
                    m_batch_props = compositor.CreateScopedBatch(CompositionBatchTypes::AllAnimations);
                    m_comp_props.StartAnimation(L"sv", m_ea_props_sv);
                    m_comp_props.StartAnimation(L"p", m_ea_props_p);
                    m_batch_props.End();
                    m_batch_props.Completed(
                        [tab_idx, weak_this = get_weak()]
                        (IInspectable const&, CompositionBatchCompletedEventArgs const&) {
                            auto strong_this = weak_this.get();
                            if (!strong_this) { return; }
                            // Store old state
                            util::debug::log_debug(std::format(L"Idx: {} -> {}", tab_idx, strong_this->m_cur_tab_idx));
                            float2 new_sv;
                            strong_this->m_comp_props.TryGetVector2(L"sv", new_sv);
                            float new_p;
                            strong_this->m_comp_props.TryGetScalar(L"p", new_p);
                            strong_this->m_view_offsets[tab_idx] = { .sv_offset = new_sv.y, .progress = new_p };
                            strong_this->m_latest_p = new_p;
                            // Update current scroll viewer
                            strong_this->UpdateInnerScrollViewerOffset();
                        }
                    );
                    m_ev_viewchanging_revoker = {};
                }
            );
            if (m_latest_p >= 0) {
                // Update current scroll viewer
                this->UpdateInnerScrollViewerOffset();
            }
            if (!m_ea_header_tl) {
                m_ea_header_tl = compositor.CreateExpressionAnimation(std::format(
                    L"Lerp(0,{0}-{1},cp.p)",
                    HEADER_MIN_HEIGHT, HEADER_MAX_HEIGHT
                ));
                m_ea_header_tl.SetReferenceParameter(L"cp", m_comp_props);
                m_ea_header_tl.Target(L"Translation.Y");
                HeaderBackground().StartAnimation(m_ea_header_tl);
                Header().StartAnimation(m_ea_header_tl);
                Header2().StartAnimation(m_ea_header_tl);
            }
            if (!m_ea_userface_sl) {
                m_ea_userface_sl = compositor.CreateExpressionAnimation(
                    L"Lerp(Vector3(1,1,1),Vector3(0.45,0.45,1),cp.p)"
                );
                m_ea_userface_sl.SetReferenceParameter(L"cp", m_comp_props);
                m_ea_userface_sl.Target(L"Scale");
                UserFace().StartAnimation(m_ea_userface_sl);
            }
            if (!m_ea_userface_tl) {
                m_ea_userface_tl = compositor.CreateExpressionAnimation(
                    L"Lerp(Vector3(0,0,0),Vector3(-2,44,0),cp.p)"
                );
                m_ea_userface_tl.SetReferenceParameter(L"cp", m_comp_props);
                m_ea_userface_tl.Target(L"Translation");
                UserFace().StartAnimation(m_ea_userface_tl);
            }
            if (!m_ea_username_sl) {
                m_ea_username_sl = compositor.CreateExpressionAnimation(
                    L"Lerp(Vector3(1,1,1),Vector3(0.5,0.5,1),cp.p)"
                );
                m_ea_username_sl.SetReferenceParameter(L"cp", m_comp_props);
                m_ea_username_sl.Target(L"Scale");
                UserName().StartAnimation(m_ea_username_sl);
            }
            if (!m_ea_username_tl) {
                m_ea_username_tl = compositor.CreateExpressionAnimation(
                    L"Lerp(Vector3(0,0,0),Vector3(-48,50.5,0),cp.p)"
                );
                m_ea_username_tl.SetReferenceParameter(L"cp", m_comp_props);
                m_ea_username_tl.Target(L"Translation");
                UserName().StartAnimation(m_ea_username_tl);
            }
            if (!m_ea_usersign_sl) {
                m_ea_usersign_sl = compositor.CreateExpressionAnimation(
                    L"Lerp(Vector3(1,1,1),Vector3(0.5,0.5,1),cp.p)"
                );
                m_ea_usersign_sl.SetReferenceParameter(L"cp", m_comp_props);
                m_ea_usersign_sl.Target(L"Scale");
                UserSign().StartAnimation(m_ea_usersign_sl);
            }
            if (!m_ea_usersign_tl) {
                m_ea_usersign_tl = compositor.CreateExpressionAnimation(
                    L"This.Target.Opacity<=0?Vector3(-10000,0,0):Lerp(Vector3(0,0,0),Vector3(-48,24,0),cp.p)"
                );
                m_ea_usersign_tl.SetReferenceParameter(L"cp", m_comp_props);
                m_ea_usersign_tl.Target(L"Translation");
                UserSign().StartAnimation(m_ea_usersign_tl);
            }
            if (!m_ea_usersign_opacity) {
                m_ea_usersign_opacity = compositor.CreateExpressionAnimation(std::format(
                    L"Clamp((1-cp.p*2),0,1)"
                ));
                m_ea_usersign_opacity.SetReferenceParameter(L"cp", m_comp_props);
                m_ea_usersign_opacity.Target(L"Opacity");
                UserSign().StartAnimation(m_ea_usersign_opacity);
            }
            if (!m_ea_pih_tl) {
                auto pih = get_first_descendant<FrameworkElement>(TabsPivot(), L"HeaderClipper");
                pih.HorizontalAlignment(HorizontalAlignment::Left);
                m_comp_props.InsertScalar(L"pih_xoff", 0);
                UserName().SizeChanged([this](IInspectable const&, SizeChangedEventArgs const&) {
                    auto pih_xoff = 40 + UserFace().ActualWidth() * 0.45 + UserName().ActualWidth() * 0.5;
                    m_comp_props.InsertScalar(L"pih_xoff", static_cast<float>(pih_xoff));
                });
                m_ea_pih_tl = compositor.CreateExpressionAnimation(
                    L"Lerp(0,cp.pih_xoff,cp.p)"
                );
                m_ea_pih_tl.SetReferenceParameter(L"cp", m_comp_props);
                m_ea_pih_tl.Target(L"Translation.X");
                pih.StartAnimation(m_ea_pih_tl);
            }
        }, TabsPivot().SelectedItem().as<PivotItem>().Content().as<FrameworkElement>());
    }
    void UserPage::UpdateInnerScrollViewerOffset(void) {
        if (m_latest_p < 0) { return; }
        util::debug::log_debug(L"Updating inner scroll viewer");
        auto new_offset = m_view_offsets[m_cur_tab_idx].sv_offset +
            (m_latest_p - m_view_offsets[m_cur_tab_idx].progress) * (HEADER_MAX_HEIGHT - HEADER_MIN_HEIGHT);
        util::debug::log_debug(std::format(L"{}: Old info is ({},{})", m_cur_tab_idx, m_view_offsets[m_cur_tab_idx].sv_offset, m_view_offsets[m_cur_tab_idx].progress));
        m_view_offsets[m_cur_tab_idx] = { .sv_offset = new_offset, .progress = m_latest_p };
        m_comp_props.InsertVector2(L"sv",
            float2(static_cast<float>(new_offset), static_cast<float>(new_offset))
        );
        util::debug::log_debug(std::format(L"{}: New info is ({},{})", m_cur_tab_idx, new_offset, m_latest_p));
        auto cur_inner_sv = util::winrt::get_first_descendant<ScrollViewer>(
            TabsPivot().Items().GetAt(m_cur_tab_idx).as<PivotItem>().Content().as<FrameworkElement>());
        m_ignore_next_viewchanging = true;
        cur_inner_sv.ChangeView(nullptr, new_offset, nullptr, true);
    }
}
