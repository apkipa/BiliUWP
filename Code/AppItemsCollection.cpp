#include "pch.h"
#include "FavouritesFolderViewItem.g.cpp"
#include "FavouritesUserViewItem.g.cpp"
#include "AppItemsCollection.h"
#include "App.h"

using namespace winrt;
using namespace Windows::Foundation;

namespace winrt::BiliUWP::implementation {
    hstring FavouritesUserViewItem::AttrStr() {
        if (m_data.attr & 1) {
            return ::BiliUWP::App::res_str(L"App/Page/FavouritesUserPage/Attributes/Private");
        }
        else {
            return ::BiliUWP::App::res_str(L"App/Page/FavouritesUserPage/Attributes/Public");
        }
    }
}

namespace BiliUWP {
    util::winrt::task<std::vector<IInspectable>> FavouritesFolderViewItemsSource::GetMoreItemsAsync(
        uint32_t expected_count
    ) {
        auto cancellation_token = co_await get_cancellation_token();
        cancellation_token.enable_propagation();

        if (m_pn > 0 && m_pn * m_ps >= m_total_items_count) { co_return{}; }

        auto bili_client = ::BiliUWP::App::get()->bili_client();

        auto result = std::move(co_await bili_client->fav_folder_res_list(
            m_folder_id, { .n = m_pn + 1, .size = m_ps },
            L"", ::BiliUWP::FavResSortOrderParam::ByFavouriteTime
        ));
        m_total_items_count = static_cast<uint32_t>(result.info.media_count);
        m_upper_name = result.info.upper.name;
        m_folder_name = result.info.title;
        std::vector<IInspectable> vec;
        vec.reserve(result.media_list.size());
        for (auto const& i : result.media_list) {
            vec.push_back(winrt::make<winrt::BiliUWP::implementation::FavouritesFolderViewItem>(i));
        }

        m_pn++;

        co_return vec;
    }
    void FavouritesFolderViewItemsSource::Reset(void) {
        m_pn = 0;
        m_total_items_count = 0;
        m_upper_name = L"";
        m_folder_name = L"";
    }
    util::winrt::task<std::vector<IInspectable>> FavouritesUserViewItemsSource::GetMoreItemsAsync(
        uint32_t expected_count
    ) {
        auto cancellation_token = co_await get_cancellation_token();
        cancellation_token.enable_propagation();

        if (m_pn > 0 && m_pn * m_ps >= m_total_items_count) { co_return{}; }

        auto bili_client = ::BiliUWP::App::get()->bili_client();

        auto result = std::move(co_await bili_client->user_fav_folders_list(
            m_user_mid, { .n = m_pn + 1, .size = m_ps }, std::nullopt
        ));
        m_total_items_count = static_cast<uint32_t>(result.count);
        std::vector<IInspectable> vec;
        vec.reserve(result.list.size());
        for (auto const& i : result.list) {
            vec.push_back(winrt::make<winrt::BiliUWP::implementation::FavouritesUserViewItem>(i));
        }

        m_pn++;

        co_return vec;
    }
    void FavouritesUserViewItemsSource::Reset(void) {
        m_pn = 0;
        m_total_items_count = 0;
    }
    util::winrt::task<std::vector<IInspectable>> UserVideosViewItemsSource::GetMoreItemsAsync(
        uint32_t expected_count
    ) {
        auto cancellation_token = co_await get_cancellation_token();
        cancellation_token.enable_propagation();

        if (m_pn > 0 && m_pn * m_ps >= m_total_items_count) { co_return{}; }

        auto bili_client = ::BiliUWP::App::get()->bili_client();

        auto result = std::move(co_await bili_client->user_space_published_videos(
            m_mid, { .n = m_pn + 1, .size = m_ps },
            L"", ::BiliUWP::UserPublishedVideosOrderParam::ByPublishTime
        ));
        m_total_items_count = static_cast<uint32_t>(result.page.count);
        std::vector<IInspectable> vec;
        vec.reserve(result.list.vlist.size());
        for (auto const& i : result.list.vlist) {
            vec.push_back(winrt::make<winrt::BiliUWP::implementation::UserVideosViewItem>(i));
        }

        m_pn++;

        co_return vec;
    }
    void UserVideosViewItemsSource::Reset(void) {
        m_pn = 0;
        m_total_items_count = 0;
    }
}
