#include "pch.h"
#include "FavouritesFolderPage.h"
#if __has_include("FavouritesFolderPage.g.cpp")
#include "FavouritesFolderPage.g.cpp"
#endif
#include "AppItemsCollection.h"
#include "App.h"

using namespace winrt;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Data;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Navigation;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;

namespace winrt::BiliUWP::implementation {
    FavouritesFolderPage::FavouritesFolderPage() : m_items_collection(nullptr), m_items_load_error(false) {}
    void FavouritesFolderPage::OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs const& e) {
        auto app = ::BiliUWP::App::get();
        auto tab = app->tab_from_page(*this);
        tab->set_icon(static_cast<Symbol>(0xE728));     // FavoriteList
        tab->set_title(::BiliUWP::App::res_str(L"App/Page/FavouritesFolderPage/Title"));
        if (auto opt = e.Parameter().try_as<FavouritesFolderPageNavParam>()) {
            auto folder_id = opt->folder_id;
            m_items_collection = MakeIncrementalLoadingCollection(
                std::make_shared<::BiliUWP::FavouritesFolderViewItemsSource>(folder_id));
            m_items_collection.OnStartLoading(
                [weak_this = get_weak()](BiliUWP::IncrementalLoadingCollection const&, IInspectable const&) {
                    auto strong_this = weak_this.get();
                    if (!strong_this) { return; }
                    strong_this->BottomState().SwitchToLoading(::BiliUWP::App::res_str(L"App/Common/Loading"));
                }
            );
            m_items_collection.OnEndLoading(
                [weak_this = get_weak()](BiliUWP::IncrementalLoadingCollection const&, IInspectable const&) {
                    auto strong_this = weak_this.get();
                    if (!strong_this) { return; }
                    if (strong_this->m_items_load_error.load()) { return; }
                    strong_this->BottomState().SwitchToDone(::BiliUWP::App::res_str(L"App/Common/AllLoaded"));
                    auto src = std::dynamic_pointer_cast<::BiliUWP::FavouritesFolderViewItemsSource>(
                        IncrementalSourceFromCollection(strong_this->m_items_collection));
                    strong_this->TopTextInfoTitle().Text(::BiliUWP::App::res_str(
                        L"App/Page/FavouritesFolderPage/TopTextInfoTitle",
                        src->UpperName(), src->FolderName()
                    ));
                    strong_this->TopTextInfoDesc().Text(::BiliUWP::App::res_str(
                        L"App/Page/FavouritesFolderPage/TopTextInfoDesc",
                        to_hstring(src->TotalItemsCount())
                    ));
                }
            );
            m_items_collection.OnError(
                [weak_this = get_weak()](BiliUWP::IncrementalLoadingCollection const&, IInspectable const&) {
                    auto strong_this = weak_this.get();
                    if (!strong_this) { return; }
                    strong_this->m_items_load_error.store(true);
                    strong_this->BottomState().SwitchToFailed(::BiliUWP::App::res_str(L"App/Common/LoadFailed"));
                }
            );
            ItemsGridView().ItemsSource(m_items_collection);
        }
        else {
            util::debug::log_error(L"OnNavigatedTo only expects FavouritesFolderPageNavParam");
        }
    }
    void FavouritesFolderPage::AccKeyF5Invoked(
        KeyboardAccelerator const&,
        KeyboardAcceleratorInvokedEventArgs const& e
    ) {
        m_items_collection.Reload();
        e.Handled(true);
    }
    void FavouritesFolderPage::RefreshItem_Click(IInspectable const&, RoutedEventArgs const&) {
        m_items_collection.Reload();
    }
    void FavouritesFolderPage::ItemsGridView_ItemClick(IInspectable const&, ItemClickEventArgs const& e) {
        auto vi = e.ClickedItem().as<BiliUWP::FavouritesFolderViewItem>();
        auto tab = ::BiliUWP::make<::BiliUWP::AppTab>();
        auto res_item_type = static_cast<::BiliUWP::ResItemType>(vi.ResItemType());
        MediaPlayPage_MediaType param_type;
        switch (res_item_type) {
        case ::BiliUWP::ResItemType::Video:
            param_type = MediaPlayPage_MediaType::Video;
            break;
        case ::BiliUWP::ResItemType::Audio:
            param_type = MediaPlayPage_MediaType::Audio;
            break;
        default:
            util::debug::log_error(std::format(
                L"Unsupported or unimplemented resource type {}",
                std::to_underlying(res_item_type)
            ));
            return;
        }
        tab->navigate(
            xaml_typename<winrt::BiliUWP::MediaPlayPage>(),
            box_value(MediaPlayPageNavParam{ param_type, vi.ResItemId(), L"" })
        );
        ::BiliUWP::App::get()->add_tab(tab);
        tab->activate();
    }
    void FavouritesFolderPage::ItemsGridView_RightTapped(
        IInspectable const& sender, RightTappedRoutedEventArgs const& e
    ) {
        auto vi = e.OriginalSource().as<FrameworkElement>()
            .DataContext().try_as<BiliUWP::FavouritesFolderViewItem>();
        if (!vi) { return; }
        e.Handled(true);
        auto items_view = sender.as<BiliUWP::AdaptiveGridView>();
        auto mf = MenuFlyout();
        auto mfi_jump_to_up = MenuFlyoutItem();
        mfi_jump_to_up.Icon(util::winrt::make_symbol_icon(Symbol::ContactInfo));
        mfi_jump_to_up.Text(::BiliUWP::App::res_str(L"App/Page/FavouritesFolderPage/JumpToUpPage"));
        mfi_jump_to_up.Click([mid = vi.UpMid()](IInspectable const&, RoutedEventArgs const&) {
            auto tab = ::BiliUWP::make<::BiliUWP::AppTab>();
            tab->navigate(
                xaml_typename<winrt::BiliUWP::UserPage>(),
                box_value(UserPageNavParam{ mid, UserPage_TargetPart::PublishedVideos })
            );
            ::BiliUWP::App::get()->add_tab(tab);
            tab->activate();
        });
        mf.Items().Append(mfi_jump_to_up);
        mf.ShowAt(items_view, e.GetPosition(items_view));
    }
}
