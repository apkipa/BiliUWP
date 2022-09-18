#include "pch.h"
#include "FavouritesUserPage.h"
#if __has_include("FavouritesUserPage.g.cpp")
#include "FavouritesUserPage.g.cpp"
#endif
#include "FavouritesUserPage_ViewItem.g.cpp"
#include "FavouritesUserPage_ViewItemSource.g.cpp"
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
    hstring FavouritesUserPage_ViewItem::AttrStr() {
        if (m_data.attr & 1) {
            return ::BiliUWP::App::res_str(L"App/Page/FavouritesUserPage/Attributes/Private");
        }
        else {
            return ::BiliUWP::App::res_str(L"App/Page/FavouritesUserPage/Attributes/Public");
        }
    }
    FavouritesUserPage_ViewItemSource::FavouritesUserPage_ViewItemSource(uint64_t user_mid) :
        m_vec(single_threaded_observable_vector<Windows::Foundation::IInspectable>()),
        m_user_mid(user_mid), m_cur_page_n(0), m_has_more(true), m_total_items_count(0), 
        m_is_loading() {}
    IAsyncOperation<LoadMoreItemsResult> FavouritesUserPage_ViewItemSource::LoadMoreItemsAsync(
        uint32_t count
    ) {
        auto cancellation_token = co_await get_cancellation_token();
        cancellation_token.enable_propagation();

        // TODO: Should we store the async operation in order to cancel upon releasing self?

        auto bili_client = ::BiliUWP::App::get()->bili_client();

        auto weak_store = util::winrt::make_weak_storage(*this);

        if (m_is_loading.exchange(true)) { co_return{}; }
        m_loading_state_changed(*this, true);
        deferred([&]() {
            if (!weak_store.lock()) { return; }
            weak_store->m_loading_state_changed(*weak_store, false);
            weak_store->m_is_loading.store(false);
        });

        auto on_exception_cleanup_fn = [&] {
            if (!weak_store.lock()) { return; }
            weak_store->m_has_more = false;
        };

        try {
            auto result = std::move(co_await weak_store.ual(bili_client->user_fav_folders_list(
                m_user_mid, { .n = m_cur_page_n + 1, .size = 20 }, std::nullopt
            )));
            m_has_more = result.has_more;
            m_total_items_count = static_cast<uint32_t>(result.count);
            if (m_vec.Size() == 0) {
                std::vector<winrt::BiliUWP::FavouritesUserPage_ViewItem> vec;
                for (auto& i : result.list) {
                    vec.push_back(winrt::make<FavouritesUserPage_ViewItem>(i));
                }
                m_vec.ReplaceAll(std::move(vec));
            }
            else {
                for (auto& i : result.list) {
                    m_vec.Append(winrt::make<FavouritesUserPage_ViewItem>(i));
                }
            }

            m_cur_page_n++;

            co_return { .Count = static_cast<uint32_t>(result.list.size()) };
        }
        catch (hresult_canceled const&) { on_exception_cleanup_fn(); }
        catch (...) {
            util::winrt::log_current_exception();
            on_exception_cleanup_fn();
        }

        co_return{};
    }

    FavouritesUserPage::FavouritesUserPage() : m_view_item_src(nullptr) {}
    void FavouritesUserPage::OnNavigatedTo(NavigationEventArgs const& e) {
        auto app = ::BiliUWP::App::get();
        auto tab = app->tab_from_page(*this);
        tab->set_icon(Symbol::OutlineStar);
        tab->set_title(::BiliUWP::App::res_str(L"App/Page/FavouritesUserPage/Title"));
        if (auto opt = e.Parameter().try_as<FavouritesUserPageNavParam>()) {
            auto uid = opt->uid;
            if (to_hstring(uid) == app->cfg_model().User_Cookies_DedeUserID()) {
                tab->set_title(::BiliUWP::App::res_str(L"App/Page/FavouritesUserPage/MyTitle"));
            }
            m_view_item_src = make<FavouritesUserPage_ViewItemSource>(uid);
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
            m_view_item_src.LoadingStateChanged(
                [weak_this = get_weak()](IInspectable const&, bool is_loading) {
                    auto strong_this = weak_this.get();
                    if (!strong_this) { return; }
                    auto prog_ring = strong_this->BottomProgRing();
                    auto info_text = strong_this->BottomInfoText();
                    if (is_loading) {
                        prog_ring.IsActive(true);
                        prog_ring.Visibility(Visibility::Visible);
                        info_text.Text(::BiliUWP::App::res_str(L"App/Common/Loading"));
                    }
                    else {
                        prog_ring.IsActive(false);
                        prog_ring.Visibility(Visibility::Collapsed);
                        info_text.Text(::BiliUWP::App::res_str(L"App/Common/AllLoaded"));
                        strong_this->TopTextInfoDesc().Text(::BiliUWP::App::res_str(
                            L"App/Page/FavouritesUserPage/TopTextInfoDesc",
                            to_hstring(strong_this->m_view_item_src.TotalItemsCount())
                        ));
                    }
                }
            );
        }
        else {
            util::debug::log_error(L"OnNavigatedTo only expects FavouritesUserPageNavParam");
        }
    }
    void FavouritesUserPage::AccKeyF5Invoked(
        KeyboardAccelerator const&,
        KeyboardAcceleratorInvokedEventArgs const& e
    ) {
        m_view_item_src.Reload();
        e.Handled(true);
    }
    void FavouritesUserPage::RefreshItem_Click(
        Windows::Foundation::IInspectable const&, Windows::UI::Xaml::RoutedEventArgs const&
    ) {
        m_view_item_src.Reload();
    }
    void FavouritesUserPage::ItemsGridView_ItemClick(
        Windows::Foundation::IInspectable const&, Windows::UI::Xaml::Controls::ItemClickEventArgs const& e
    ) {
        auto vi = e.ClickedItem().as<FavouritesUserPage_ViewItem>();
        auto tab = ::BiliUWP::make<::BiliUWP::AppTab>();
        tab->navigate(
            xaml_typename<winrt::BiliUWP::FavouritesFolderPage>(),
            box_value(FavouritesFolderPageNavParam{ vi->FolderId() })
        );
        ::BiliUWP::App::get()->add_tab(tab);
        tab->activate();
    }
}
