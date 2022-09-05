#include "pch.h"
#include "FavouritesUserPage.h"
#if __has_include("FavouritesUserPage.g.cpp")
#include "FavouritesUserPage.g.cpp"
#endif
#include "FavouriteUserViewPage_ViewItem.g.cpp"
#include "FavouriteUserViewPage_ViewItemSource.g.cpp"
#include "App.h"

using namespace winrt;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Data;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;

// TODO: We may use ItemsReorderAnimation for ItemsGridView in XAML

namespace winrt::BiliUWP::implementation {
    hstring FavouriteUserViewPage_ViewItem::AttrStr() {
        if (m_data.attr & 1) {
            return ::BiliUWP::App::res_str(L"App/Page/FavouritesUserPage/Attributes/Private");
        }
        else {
            return ::BiliUWP::App::res_str(L"App/Page/FavouritesUserPage/Attributes/Public");
        }
    }
    FavouriteUserViewPage_ViewItemSource::FavouriteUserViewPage_ViewItemSource(uint64_t user_mid) :
        m_vec(single_threaded_observable_vector<Windows::Foundation::IInspectable>()),
        m_user_mid(user_mid), m_cur_page_n(0), m_has_more(true), m_total_items_count(0), 
        m_is_loading() {}
    IAsyncOperation<LoadMoreItemsResult> FavouriteUserViewPage_ViewItemSource::LoadMoreItemsAsync(uint32_t count) {
        auto cancellation_token = co_await get_cancellation_token();
        cancellation_token.enable_propagation();

        // TODO: Should we store the async operation in order to cancel upon releasing self?

        if (m_is_loading.exchange(true)) { co_return{}; }
        m_loading_state_changed(*this, true);
        deferred([weak_this = get_weak()] {
            auto strong_this = weak_this.get();
            if (!strong_this) { return; }
            strong_this->m_loading_state_changed(*strong_this, false);
            strong_this->m_is_loading.store(false);
        });

        try {
            auto result = std::move(co_await ::BiliUWP::App::get()->bili_client()->user_fav_folders_list(
                m_user_mid, { .n = m_cur_page_n + 1, .size = 20 }, std::nullopt
            ));
            m_has_more = result.has_more;
            m_total_items_count = static_cast<uint32_t>(result.count);
            if (m_vec.Size() == 0) {
                std::vector<winrt::BiliUWP::FavouriteUserViewPage_ViewItem> vec;
                for (auto& i : result.list) {
                    vec.push_back(winrt::make<FavouriteUserViewPage_ViewItem>(i));
                }
                m_vec.ReplaceAll(std::move(vec));
            }
            else {
                for (auto& i : result.list) {
                    m_vec.Append(winrt::make<FavouriteUserViewPage_ViewItem>(i));
                }
            }

            m_cur_page_n++;

            co_return { .Count = static_cast<uint32_t>(result.list.size()) };
        }
        catch (::BiliUWP::BiliApiException const& e) {
            util::debug::log_error(winrt::to_hstring(e.what()));
            m_has_more = false;
            co_return{};
        }
        catch (hresult_canceled const&) {
            m_has_more = false;
            co_return{};
        }
        catch (hresult_error const& e) {
            util::debug::log_error(e.message());
            m_has_more = false;
            co_return{};
        }
    }

    FavouritesUserPage::FavouritesUserPage() : m_view_item_src(nullptr) {}
    void FavouritesUserPage::OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs const& e) {
        auto app = ::BiliUWP::App::get();
        auto tab = app->tab_from_page(*this);
        tab->set_icon(Symbol::OutlineStar);
        tab->set_title(::BiliUWP::App::res_str(L"App/Page/FavouritesUserPage/Title"));
        if (auto opt = e.Parameter().try_as<FavouritesUserPageNavParam>()) {
            auto uid = opt->uid;
            if (to_hstring(uid) == app->cfg_model().User_Cookies_DedeUserID()) {
                tab->set_title(::BiliUWP::App::res_str(L"App/Page/FavouritesUserPage/MyTitle"));
            }
            m_view_item_src = make<FavouriteUserViewPage_ViewItemSource>(uid);
            // TODO: Use additional request to convert uid into user name
            TopTextInfoTitle().Text(::BiliUWP::App::res_str(
                L"App/Page/FavouritesUserPage/TopTextInfoTitle",
                std::format(L"uid{}", uid)
            ));
            m_view_item_src.LoadingStateChanged(
                [weak_this = get_weak()](IInspectable const& sender, bool is_loading) {
                    auto strong_this = weak_this.get();
                    if (!strong_this) { return; }
                    auto prog_ring = strong_this->BottomProgRing();
                    auto info_text = strong_this->BottomInfoText();
                    if (is_loading) {
                        prog_ring.IsActive(true);
                        prog_ring.Visibility(Visibility::Visible);
                        info_text.Text(::BiliUWP::App::res_str(L"App/Page/FavouritesUserPage/Loading"));
                    }
                    else {
                        prog_ring.IsActive(false);
                        prog_ring.Visibility(Visibility::Collapsed);
                        info_text.Text(::BiliUWP::App::res_str(L"App/Page/FavouritesUserPage/AllLoaded"));
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
        Windows::UI::Xaml::Input::KeyboardAccelerator const&,
        Windows::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& e
    ) {
        m_view_item_src.Reload();
        e.Handled(true);
    }
    void FavouritesUserPage::RefreshItem_Click(
        Windows::Foundation::IInspectable const&, Windows::UI::Xaml::RoutedEventArgs const&
    ) {
        m_view_item_src.Reload();
    }
}
