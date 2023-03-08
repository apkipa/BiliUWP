#pragma once
#include "BiliClientManaged.g.h"

// BiliClientManaged: A thin wrapper for simple RPC purpose

namespace winrt::BiliUWP::implementation {
    struct BiliClientManaged : BiliClientManagedT<BiliClientManaged> {
        using AsyncJsonObjectResult = Windows::Foundation::IAsyncOperation<Windows::Data::Json::JsonObject>;
        using AsyncIBufferResult = Windows::Foundation::IAsyncOperation<Windows::Storage::Streams::IBuffer>;

        BiliClientManaged();

        hstring data_user_agent();
        void data_user_agent(hstring const& value);
        hstring data_access_token();
        void data_access_token(hstring const& value);
        BiliUWP::UserCookies data_cookies();
        void data_cookies(BiliUWP::UserCookies const& value);

        // Authentication
        AsyncJsonObjectResult api_passport_x_passport_tv_login_qrcode_auth_code(
            BiliUWP::APISignKeys const& keys,
            guid const& local_id
        );
        AsyncJsonObjectResult api_passport_x_passport_tv_login_qrcode_poll(
            BiliUWP::APISignKeys const& keys,
            hstring const& auth_code,
            winrt::guid const& local_id
        );
        AsyncJsonObjectResult api_passport_api_v2_oauth2_refresh_token(
            Windows::Foundation::IReference<BiliUWP::APISignKeys> const& keys,
            hstring const& refresh_token
        );
        AsyncJsonObjectResult api_passport_x_passport_login_revoke(
            BiliUWP::APISignKeys const& keys
        );

        // User information
        AsyncJsonObjectResult api_app_x_v2_account_myinfo(
            BiliUWP::APISignKeys const& keys
        );
        AsyncJsonObjectResult api_api_x_web_interface_nav(void);
        AsyncJsonObjectResult api_api_x_web_interface_nav_stat(void);
        AsyncJsonObjectResult api_api_x_web_interface_card(uint64_t mid, bool get_photo);
        AsyncJsonObjectResult api_api_x_space_acc_info(uint64_t mid);
        AsyncJsonObjectResult api_api_x_space_upstat(uint64_t mid);
        AsyncJsonObjectResult api_api_x_space_arc_search(
            uint64_t mid,
            BiliUWP::ApiParam_Page page,
            hstring keyword,
            uint64_t tid,
            BiliUWP::ApiParam_SpaceArcSearchOrder order
        );
        AsyncJsonObjectResult api_api_audio_music_service_web_song_upper(
            uint64_t mid,
            BiliUWP::ApiParam_Page page,
            BiliUWP::ApiParam_AudioMusicServiceWebSongUpperOrder order
        );

        // Video information
        AsyncJsonObjectResult api_api_x_web_interface_view(
            uint64_t avid,
            hstring bvid
        );
        AsyncJsonObjectResult api_api_x_web_interface_view_detail(
            uint64_t avid,
            hstring bvid
        );
        AsyncJsonObjectResult api_api_x_player_v2(
            uint64_t avid,
            hstring bvid,
            uint64_t cid
        );
        AsyncJsonObjectResult api_api_x_player_playurl(
            uint64_t avid,
            hstring bvid,
            uint64_t cid,
            BiliUWP::ApiParam_VideoPlayUrlPreference prefers
        );
        AsyncJsonObjectResult api_api_x_player_videoshot(
            uint64_t avid,
            hstring bvid,
            uint64_t cid,
            bool index
        );

        // Audio information
        AsyncJsonObjectResult api_www_audio_music_service_c_web_song_info(
            uint64_t auid
        );
        AsyncJsonObjectResult api_api_audio_music_service_c_url(
            uint64_t auid,
            uint32_t quality
        );

        // Favourites information
        AsyncJsonObjectResult api_api_x_v3_fav_folder_created_list(
            uint64_t mid,
            BiliUWP::ApiParam_Page page,
            Windows::Foundation::IReference<BiliUWP::ApiParam_FavItemLookup> const& item_to_find
        );
        AsyncJsonObjectResult api_api_x_v3_fav_folder_created_list_all(
            uint64_t mid,
            Windows::Foundation::IReference<BiliUWP::ApiParam_FavItemLookup> const& item_to_find
        );
        AsyncJsonObjectResult api_api_x_v3_fav_resource_list(
            uint64_t folder_id,
            BiliUWP::ApiParam_Page page,
            hstring keyword,
            BiliUWP::ApiParam_FavResSortOrder order
        );

        // Danmaku information
        AsyncIBufferResult api_api_x_v2_dm_web_seg_so(
            uint32_t type,
            uint64_t cid,
            uint64_t avid,
            uint32_t segment_index
        );
        AsyncIBufferResult api_api_x_v2_dm_web_view(
            uint32_t type,
            uint64_t cid,
            uint64_t avid
        );

    private:
        Windows::Web::Http::Filters::HttpBaseProtocolFilter m_http_filter;
        Windows::Web::Http::HttpClient m_http_client;

        hstring m_access_token;
    };
}
namespace winrt::BiliUWP::factory_implementation {
    struct BiliClientManaged : BiliClientManagedT<BiliClientManaged, implementation::BiliClientManaged> {};
}
