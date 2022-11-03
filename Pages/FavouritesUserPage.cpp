#include "pch.h"
#include "FavouritesUserPage.h"
#if __has_include("FavouritesUserPage.g.cpp")
#include "FavouritesUserPage.g.cpp"
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

// TODO: We may use ItemsReorderAnimation for ItemsGridView in XAML

namespace winrt::BiliUWP::implementation {
    FavouritesUserPage::FavouritesUserPage() :
        m_items_collection(nullptr), m_items_load_error(false),
        m_bili_res_is_ready(false), m_bili_res_is_valid(false) {}
    void FavouritesUserPage::OnNavigatedTo(NavigationEventArgs const& e) {
        auto app = ::BiliUWP::App::get();
        auto tab = app->tab_from_page(*this);
        tab->set_icon(Symbol::OutlineStar);
        tab->set_title(::BiliUWP::App::res_str(L"App/Page/FavouritesUserPage/Title"));
        if (auto opt = e.Parameter().try_as<FavouritesUserPageNavParam>()) {
            auto uid = opt->uid;
            m_uid = uid;
            if (to_hstring(uid) == app->cfg_model().User_Cookies_DedeUserID()) {
                tab->set_title(::BiliUWP::App::res_str(L"App/Page/FavouritesUserPage/MyTitle"));
            }
            m_items_collection = MakeIncrementalLoadingCollection(
                std::make_shared<::BiliUWP::FavouritesUserViewItemsSource>(uid));
            TopTextInfoTitle().Text(::BiliUWP::App::res_str(
                L"App/Page/FavouritesUserPage/TopTextInfoTitle",
                std::format(L"uid{}", uid)
            ));
            m_cur_async.cancel_and_run([](FavouritesUserPage* that, uint64_t uid) -> IAsyncAction {
                auto cancellation_token = co_await get_cancellation_token();
                cancellation_token.enable_propagation();

                try {
                    auto top_text_info_title = that->TopTextInfoTitle();
                    auto card_info = std::move(
                        co_await ::BiliUWP::App::get()->bili_client()->user_card_info(uid)
                    );
                    top_text_info_title.Text(::BiliUWP::App::res_str(
                        L"App/Page/FavouritesUserPage/TopTextInfoTitle",
                        card_info.card.name
                    ));
                }
                catch (hresult_canceled const&) { throw; }
                catch (...) { util::winrt::log_current_exception(); throw; }
            }, this, uid);
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
                    strong_this->m_bili_res_is_ready = true;
                    if (strong_this->m_items_load_error) { return; }
                    strong_this->m_bili_res_is_valid = true;
                    strong_this->BottomState().SwitchToDone(::BiliUWP::App::res_str(L"App/Common/AllLoaded"));
                    auto src = std::dynamic_pointer_cast<::BiliUWP::FavouritesUserViewItemsSource>(
                        IncrementalSourceFromCollection(strong_this->m_items_collection));
                    strong_this->TopTextInfoDesc().Text(::BiliUWP::App::res_str(
                        L"App/Page/FavouritesUserPage/TopTextInfoDesc",
                        to_hstring(src->TotalItemsCount())
                    ));
                }
            );
            m_items_collection.OnError(
                [weak_this = get_weak()](BiliUWP::IncrementalLoadingCollection const&, IInspectable const&) {
                    auto strong_this = weak_this.get();
                    if (!strong_this) { return; }
                    strong_this->m_items_load_error = true;
                    strong_this->BottomState().SwitchToFailed(::BiliUWP::App::res_str(L"App/Common/LoadFailed"));
                }
            );
            ItemsGridView().ItemsSource(m_items_collection);
        }
        else {
            util::debug::log_error(L"OnNavigatedTo only expects FavouritesUserPageNavParam");
        }
    }
    void FavouritesUserPage::AccKeyF5Invoked(
        KeyboardAccelerator const&,
        KeyboardAcceleratorInvokedEventArgs const& e
    ) {
        m_items_load_error = false;
        m_items_collection.Reload();
        e.Handled(true);
    }
    void FavouritesUserPage::RefreshItem_Click(
        Windows::Foundation::IInspectable const&, Windows::UI::Xaml::RoutedEventArgs const&
    ) {
        m_items_load_error = false;
        m_items_collection.Reload();
    }
    void FavouritesUserPage::ItemsGridView_ItemClick(
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
}
