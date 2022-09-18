#include "pch.h"
#include "FavouritesFolderPage.h"
#if __has_include("FavouritesFolderPage.g.cpp")
#include "FavouritesFolderPage.g.cpp"
#endif
#include "FavouritesFolderPage_ViewItem.g.cpp"
#include "FavouritesFolderPage_ViewItemSource.g.cpp"
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
    FavouritesFolderPage_ViewItemSource::FavouritesFolderPage_ViewItemSource(uint64_t folder_id) :
        m_vec(single_threaded_observable_vector<Windows::Foundation::IInspectable>()),
        m_folder_id(folder_id), m_cur_page_n(0), m_has_more(true), m_total_items_count(0),
        m_upper_name(), m_folder_name(), m_is_loading() {}
    IAsyncOperation<LoadMoreItemsResult> FavouritesFolderPage_ViewItemSource::LoadMoreItemsAsync(
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
            auto result = std::move(co_await weak_store.ual(bili_client->fav_folder_res_list(
                m_folder_id, { .n = m_cur_page_n + 1, .size = 20 },
                L"", ::BiliUWP::FavResSortOrderParam::ByFavouriteTime
            )));
            m_has_more = result.has_more;
            m_total_items_count = static_cast<uint32_t>(result.info.media_count);
            m_upper_name = result.info.upper.name;
            m_folder_name = result.info.title;
            if (m_vec.Size() == 0) {
                std::vector<winrt::BiliUWP::FavouritesFolderPage_ViewItem> vec;
                for (auto& i : result.media_list) {
                    vec.push_back(winrt::make<FavouritesFolderPage_ViewItem>(i));
                }
                m_vec.ReplaceAll(std::move(vec));
            }
            else {
                for (auto& i : result.media_list) {
                    m_vec.Append(winrt::make<FavouritesFolderPage_ViewItem>(i));
                }
            }

            m_cur_page_n++;

            co_return{ .Count = static_cast<uint32_t>(result.media_list.size()) };
        }
        catch (hresult_canceled const&) { on_exception_cleanup_fn(); }
        catch (...) {
            util::winrt::log_current_exception();
            on_exception_cleanup_fn();
        }

        co_return{};
    }
    FavouritesFolderPage::FavouritesFolderPage() : m_view_item_src(nullptr) {}
    void FavouritesFolderPage::OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs const& e) {
        auto app = ::BiliUWP::App::get();
        auto tab = app->tab_from_page(*this);
        tab->set_icon(static_cast<Symbol>(0xE728));     // FavoriteList
        tab->set_title(::BiliUWP::App::res_str(L"App/Page/FavouritesFolderPage/Title"));
        if (auto opt = e.Parameter().try_as<FavouritesFolderPageNavParam>()) {
            auto folder_id = opt->folder_id;
            m_view_item_src = make<FavouritesFolderPage_ViewItemSource>(folder_id);
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
                        strong_this->TopTextInfoTitle().Text(::BiliUWP::App::res_str(
                            L"App/Page/FavouritesFolderPage/TopTextInfoTitle",
                            strong_this->m_view_item_src.UpperName(),
                            strong_this->m_view_item_src.FolderName()
                        ));
                        strong_this->TopTextInfoDesc().Text(::BiliUWP::App::res_str(
                            L"App/Page/FavouritesFolderPage/TopTextInfoDesc",
                            to_hstring(strong_this->m_view_item_src.TotalItemsCount())
                        ));
                    }
                }
            );
        }
        else {
            util::debug::log_error(L"OnNavigatedTo only expects FavouritesFolderPageNavParam");
        }
    }
    void FavouritesFolderPage::AccKeyF5Invoked(
        KeyboardAccelerator const&,
        KeyboardAcceleratorInvokedEventArgs const& e
    ) {
        m_view_item_src.Reload();
        e.Handled(true);
    }
    void FavouritesFolderPage::RefreshItem_Click(
        Windows::Foundation::IInspectable const&, Windows::UI::Xaml::RoutedEventArgs const&
    ) {
        m_view_item_src.Reload();
    }
    void FavouritesFolderPage::ItemsGridView_ItemClick(
        Windows::Foundation::IInspectable const&, Windows::UI::Xaml::Controls::ItemClickEventArgs const& e
    ) {
        auto vi = e.ClickedItem().as<FavouritesFolderPage_ViewItem>();
        auto tab = ::BiliUWP::make<::BiliUWP::AppTab>();
        auto res_item_type = vi->ResItemType();
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
            box_value(MediaPlayPageNavParam{ param_type, vi->ResItemId(), L"" })
        );
        ::BiliUWP::App::get()->add_tab(tab);
        tab->activate();
    }
}
