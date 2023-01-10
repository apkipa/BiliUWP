#pragma once

#include "FavouritesFolderViewItem.g.h"
#include "FavouritesUserViewItem.g.h"
#include "UserVideosViewItem.g.h"
#include "IncrementalLoadingCollection.h"
#include "BiliClient.hpp"
#include "util.hpp"

namespace winrt::BiliUWP::implementation {
    struct FavouritesFolderViewItem : FavouritesFolderViewItemT<FavouritesFolderViewItem> {
        FavouritesFolderViewItem(::BiliUWP::FavFolderResList_Media const& data) : m_data(data) {}
        hstring Title() { return m_data.title; }
        hstring CoverUrl() {
            if (m_data.cover_url != L"") {
                return m_data.cover_url;
            }
            return L"https://s1.hdslb.com/bfs/static/jinkela/space/assets/playlistbg.png";
        }
        hstring Description() { return m_data.intro; }
        uint64_t UpMid() { return m_data.upper.mid; }
        hstring UpName() { return m_data.upper.name; }
        hstring UpFaceUrl() {
            if (m_data.upper.face_url != L"") {
                return m_data.upper.face_url;
            }
            return L"https://i0.hdslb.com/bfs/face/member/noface.jpg";
        }
        uint64_t PlayCount() { return m_data.cnt_info.play_count; }
        uint64_t DanmakuCount() { return m_data.cnt_info.danmaku_count; }
        uint64_t FavouriteCount() { return m_data.cnt_info.favourite_count; }
        uint64_t ResItemId() { return m_data.nid; }
        uint32_t ResItemType() { return static_cast<uint32_t>(m_data.type); }

    private:
        ::BiliUWP::FavFolderResList_Media m_data;
    };
    struct FavouritesUserViewItem : FavouritesUserViewItemT<FavouritesUserViewItem> {
        FavouritesUserViewItem(::BiliUWP::UserFavFoldersList_Folder const& data) : m_data(data) {}
        hstring Title() { return m_data.title; }
        hstring CoverUrl() {
            if (m_data.cover_url != L"") {
                return m_data.cover_url;
            }
            return L"https://s1.hdslb.com/bfs/static/jinkela/space/assets/playlistbg.png";
        }
        hstring AttrStr();
        uint64_t MediaCount() { return m_data.media_count; }
        uint64_t FolderId() { return m_data.id; }

    private:
        ::BiliUWP::UserFavFoldersList_Folder m_data;
    };
    struct UserVideosViewItem : UserVideosViewItemT<UserVideosViewItem> {
        UserVideosViewItem(::BiliUWP::UserSpacePublishedVideos_Video const& data) : m_data(data) {}
        hstring Title() { return m_data.title; }
        hstring CoverUrl() {
            if (m_data.cover_url != L"") {
                return m_data.cover_url;
            }
            return L"https://s1.hdslb.com/bfs/static/jinkela/space/assets/playlistbg.png";
        }
        hstring PublishTimeStr();
        hstring Description() { return m_data.description; }
        hstring PlayCountStr() {
            if (!m_data.play_count) { return L"--"; }
            return to_hstring(*m_data.play_count);
        }
        uint64_t DanmakuCount() { return m_data.danmaku_count; }
        uint64_t AvId() { return m_data.avid; }
        bool IsUnionVideo() { return m_data.is_union_video; }

    private:
        ::BiliUWP::UserSpacePublishedVideos_Video m_data;
    };
}

namespace BiliUWP {
    struct FavouritesFolderViewItemsSource : ::BiliUWP::IIncrementalSource {
        FavouritesFolderViewItemsSource(uint64_t folder_id) :
            m_folder_id(folder_id), m_pn(0), m_ps(20), m_total_items_count(0) {}
        util::winrt::task<std::vector<winrt::Windows::Foundation::IInspectable>> GetMoreItemsAsync(
            uint32_t expected_count
        );
        void Reset(void);

        uint64_t UpperId(void) { return m_upper_id; }
        winrt::hstring UpperName(void) { return m_upper_name; }
        winrt::hstring FolderName(void) { return m_folder_name; }
        uint32_t TotalItemsCount(void) { return m_total_items_count; }

    private:
        uint64_t m_folder_id;
        uint32_t m_pn, m_ps;
        uint64_t m_upper_id;
        winrt::hstring m_upper_name;
        winrt::hstring m_folder_name;
        uint32_t m_total_items_count;
    };
    struct FavouritesUserViewItemsSource : ::BiliUWP::IIncrementalSource {
        FavouritesUserViewItemsSource(uint64_t user_mid) :
            m_user_mid(user_mid), m_pn(0), m_ps(20), m_total_items_count(0) {}
        util::winrt::task<std::vector<winrt::Windows::Foundation::IInspectable>> GetMoreItemsAsync(
            uint32_t expected_count
        );
        void Reset(void);

        uint32_t TotalItemsCount(void) { return m_total_items_count; }

    private:
        uint64_t m_user_mid;
        uint32_t m_pn, m_ps;
        uint32_t m_total_items_count;
    };
    struct UserVideosViewItemsSource : ::BiliUWP::IIncrementalSource {
        UserVideosViewItemsSource(uint64_t mid) :
            m_mid(mid), m_pn(0), m_ps(20), m_total_items_count(0) {}
        util::winrt::task<std::vector<winrt::Windows::Foundation::IInspectable>> GetMoreItemsAsync(
            uint32_t expected_count
        );
        void Reset(void);

        uint32_t TotalItemsCount(void) { return m_total_items_count; }

    private:
        uint64_t m_mid;
        uint32_t m_pn, m_ps;
        uint32_t m_total_items_count;
    };
}
