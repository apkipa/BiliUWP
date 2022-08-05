#include "pch.h"
#include "util.hpp"
#include "BiliClientManaged.h"
#include "BiliClientManaged.g.cpp"
#include <ranges>

namespace {
    winrt::hstring get_ts(void) {
        return winrt::to_hstring(util::time::get_secs_since_epoch());
    }
    
    winrt::Windows::Foundation::Uri make_uri(
        winrt::hstring const& base, winrt::hstring const& relative
    ) {
        return winrt::Windows::Foundation::Uri(base, relative);
    }
    winrt::Windows::Foundation::Uri make_uri(
        winrt::hstring const& base, std::wstring_view const& relative, std::wstring_view param
    ) {
        std::wstring relative_buf{ relative };
        relative_buf += L'?';
        relative_buf += param;
        return winrt::Windows::Foundation::Uri(base, relative_buf);
    }

    class ApiParamMaker {
    public:
        ApiParamMaker() {
            this->initialize();
        }
        ~ApiParamMaker() = default;

        void initialize(void) {
            m_result = L"";
            m_params_vec.clear();
        }
        void finalize(std::optional<winrt::BiliUWP::APISignKeys> keys = std::nullopt) {
            using winrt::Windows::Foundation::Uri;

            if (m_result != L"") {
                return;
            }

            std::wstring res_buf;

            if (keys) {
                m_params_vec.emplace_back(L"appkey", keys->key);
                std::sort(
                    m_params_vec.begin(), m_params_vec.end(),
                    [](auto const& lhs, auto const& rhs) {
                        return lhs.first < rhs.first;
                    }
                );
                m_md5.initialize();
                res_buf = Uri::EscapeComponent(m_params_vec[0].first);
                res_buf += L'=';
                res_buf += Uri::EscapeComponent(m_params_vec[0].second);
                for (auto const& i : std::ranges::drop_view{ m_params_vec, 1 }) {
                    res_buf += L'&';
                    res_buf += Uri::EscapeComponent(i.first);
                    res_buf += L'=';
                    res_buf += Uri::EscapeComponent(i.second);
                }
                m_md5.add_string(res_buf);
                m_md5.add_string(keys->sec);
                m_md5.finialize();
                res_buf += L'&';
                res_buf += L"sign";
                res_buf += L'=';
                res_buf += m_md5.get_result_as_str();
            }
            else {
                if (!m_params_vec.empty()) {
                    res_buf = Uri::EscapeComponent(m_params_vec[0].first);
                    res_buf += L'=';
                    res_buf += Uri::EscapeComponent(m_params_vec[0].second);
                    for (auto const& i : std::ranges::drop_view{ m_params_vec, 1 }) {
                        res_buf += L'&';
                        res_buf += Uri::EscapeComponent(i.first);
                        res_buf += L'=';
                        res_buf += Uri::EscapeComponent(i.second);
                    }
                }
            }

            m_result = res_buf;
        }

        void add_param(winrt::hstring key, winrt::hstring value) {
            m_params_vec.emplace_back(std::move(key), std::move(value));
        }
        void add_param_ts(void) {
            this->add_param(L"ts", get_ts());
        }

        // NOTE: Will automatically call finalize(std::nullopt) if not finalized
        winrt::hstring get_as_str(void) {
            if (m_result == L"") {
                this->finalize(std::nullopt);
            }
            return m_result;
        }
    private:
        winrt::hstring m_result;
        // pair<key, value>
        std::vector<std::pair<winrt::hstring, winrt::hstring>> m_params_vec;

        util::cryptography::Md5 m_md5;
    };
}

namespace winrt::BiliUWP::implementation {
    using namespace Windows::Foundation;
    using namespace Windows::Web::Http;
    using namespace Windows::Web::Http::Filters;
    using namespace Windows::Data::Json;

    using AsyncJsonObjectResult = BiliClientManaged::AsyncJsonObjectResult;

    BiliClientManaged::BiliClientManaged() :
        m_http_filter(HttpBaseProtocolFilter()), m_http_client(nullptr),
        m_access_token(L"")
    {
        auto cache_control = m_http_filter.CacheControl();
        cache_control.ReadBehavior(HttpCacheReadBehavior::NoCache);
        cache_control.WriteBehavior(HttpCacheWriteBehavior::NoCache);
        m_http_filter.AllowAutoRedirect(false);
        m_http_client = HttpClient(m_http_filter);
        this->data_user_agent(L"Mozilla/5.0 BiliDroid/6.4.0 (bbcallen@gmail.com)");
    }
    hstring BiliClientManaged::data_user_agent() {
        return m_http_client.DefaultRequestHeaders().UserAgent().ToString();
    }
    void BiliClientManaged::data_user_agent(hstring const& value) {
        auto user_agent = m_http_client.DefaultRequestHeaders().UserAgent();
        user_agent.Clear();
        user_agent.ParseAdd(value);
    }
    hstring BiliClientManaged::data_access_token() {
        return m_access_token;
    }
    void BiliClientManaged::data_access_token(hstring const& value) {
        m_access_token = value;
    }
    BiliUWP::UserCookies BiliClientManaged::data_cookies() {
        BiliUWP::UserCookies user_cookies{};
        auto cookies = m_http_filter.CookieManager().GetCookies(Uri(L"https://www.bilibili.com"));
        for (auto i : cookies) {
            if (i.Name() == L"SESSDATA") {
                user_cookies.SESSDATA = i.Value();
            }
            else if (i.Name() == L"bili_jct") {
                user_cookies.bili_jct = i.Value();
            }
            else if (i.Name() == L"DedeUserID") {
                user_cookies.DedeUserID = i.Value();
            }
            else if (i.Name() == L"DedeUserID__ckMd5") {
                user_cookies.DedeUserID__ckMd5 = i.Value();
            }
            else if (i.Name() == L"sid") {
                user_cookies.sid = i.Value();
            }
        }
        return user_cookies;
    }
    void BiliClientManaged::data_cookies(BiliUWP::UserCookies const& value) {
        auto cookie_mgr = m_http_filter.CookieManager();
        auto gen_http_cookie = [](hstring const& name, hstring const& value) {
            auto cookie = HttpCookie(name, L".bilibili.com", L"");
            cookie.Value(value);
            return cookie;
        };
        cookie_mgr.SetCookie(gen_http_cookie(L"SESSDATA", value.SESSDATA));
        cookie_mgr.SetCookie(gen_http_cookie(L"bili_jct", value.bili_jct));
        cookie_mgr.SetCookie(gen_http_cookie(L"DedeUserID", value.DedeUserID));
        cookie_mgr.SetCookie(gen_http_cookie(L"DedeUserID__ckMd5", value.DedeUserID__ckMd5));
        cookie_mgr.SetCookie(gen_http_cookie(L"sid", value.sid));
    }

    // Authentication
    AsyncJsonObjectResult BiliClientManaged::api_passport_x_passport_tv_login_qrcode_auth_code(
        BiliUWP::APISignKeys const& keys, guid const& local_id
    ) {
        ApiParamMaker param_maker;

        auto cancellation_token = co_await get_cancellation_token();
        cancellation_token.enable_propagation();

        // TODO: Figure out the real structure of local_id, or simply ignore it
        //param_maker.add_param(L"local_id", util::winrt::to_hstring(local_id));
        param_maker.add_param_ts();
        param_maker.finalize(keys);
        auto uri = make_uri(
            L"https://passport.bilibili.com",
            L"/x/passport-tv-login/qrcode/auth_code",
            param_maker.get_as_str()
        );
        auto http_resp = co_await m_http_client.PostAsync(uri, nullptr);
        co_return JsonObject::Parse(co_await http_resp.Content().ReadAsStringAsync());
    }
    AsyncJsonObjectResult BiliClientManaged::api_passport_x_passport_tv_login_qrcode_poll(
        BiliUWP::APISignKeys const& keys, hstring const& auth_code, guid const& local_id
    ) {
        ApiParamMaker param_maker;

        auto cancellation_token = co_await get_cancellation_token();
        cancellation_token.enable_propagation();

        //param_maker.add_param(L"local_id", util::winrt::to_hstring(local_id));
        param_maker.add_param(L"auth_code", auth_code);
        param_maker.add_param_ts();
        param_maker.finalize(keys);
        auto uri = make_uri(
            L"https://passport.bilibili.com",
            L"/x/passport-tv-login/qrcode/poll",
            param_maker.get_as_str()
        );
        auto http_resp = co_await m_http_client.PostAsync(uri, nullptr);
        co_return JsonObject::Parse(co_await http_resp.Content().ReadAsStringAsync());
    }
    AsyncJsonObjectResult BiliClientManaged::api_passport_api_v2_oauth2_refresh_token(
        IReference<BiliUWP::APISignKeys> const& keys, hstring const& refresh_token
    ) {
        ApiParamMaker param_maker;

        auto cancellation_token = co_await get_cancellation_token();
        cancellation_token.enable_propagation();

        param_maker.add_param(L"access_key", m_access_token);
        param_maker.add_param(L"refresh_token", refresh_token);
        param_maker.add_param_ts();
        param_maker.finalize(keys);
        auto uri = make_uri(
            L"https://passport.bilibili.com",
            L"/api/v2/oauth2/refresh_token",
            param_maker.get_as_str()
        );
        auto http_resp = co_await m_http_client.PostAsync(uri, nullptr);
        co_return JsonObject::Parse(co_await http_resp.Content().ReadAsStringAsync());
    }
    AsyncJsonObjectResult BiliClientManaged::api_passport_x_passport_login_revoke(
        BiliUWP::APISignKeys const& keys
    ) {
        ApiParamMaker param_maker;

        auto cancellation_token = co_await get_cancellation_token();
        cancellation_token.enable_propagation();

        // TODO: Check if this API works correctly
        param_maker.add_param(L"access_key", m_access_token);
        param_maker.add_param(L"session", this->data_cookies().SESSDATA);
        param_maker.add_param_ts();
        param_maker.finalize(keys);
        auto uri = make_uri(
            L"https://passport.bilibili.com",
            L"/x/passport-login/revoke",
            param_maker.get_as_str()
        );
        auto http_resp = co_await m_http_client.PostAsync(uri, nullptr);
        co_return JsonObject::Parse(co_await http_resp.Content().ReadAsStringAsync());
    }

    // User information
    AsyncJsonObjectResult BiliClientManaged::api_app_x_v2_account_myinfo(
        BiliUWP::APISignKeys const& keys
    ) {
        ApiParamMaker param_maker;

        auto cancellation_token = co_await get_cancellation_token();
        cancellation_token.enable_propagation();

        param_maker.add_param(L"access_key", m_access_token);
        param_maker.add_param_ts();
        param_maker.finalize(keys);
        auto uri = make_uri(
            L"https://app.bilibili.com",
            L"/x/v2/account/myinfo",
            param_maker.get_as_str()
        );
        co_return JsonObject::Parse(co_await m_http_client.GetStringAsync(uri));
    }
    AsyncJsonObjectResult BiliClientManaged::api_app_x_web_interface_nav() {
        auto cancellation_token = co_await get_cancellation_token();
        cancellation_token.enable_propagation();

        auto uri = make_uri(
            L"https://app.bilibili.com",
            L"/x/web-interface/nav"
        );
        co_return JsonObject::Parse(co_await m_http_client.GetStringAsync(uri));
    }
    AsyncJsonObjectResult BiliClientManaged::api_app_x_web_interface_nav_stat() {
        auto cancellation_token = co_await get_cancellation_token();
        cancellation_token.enable_propagation();

        auto uri = make_uri(
            L"https://app.bilibili.com",
            L"/x/web-interface/nav/stat"
        );
        co_return JsonObject::Parse(co_await m_http_client.GetStringAsync(uri));
    }

    // Video information
    AsyncJsonObjectResult BiliClientManaged::api_api_x_web_interface_view(
        uint64_t avid, hstring bvid
    ) {
        ApiParamMaker param_maker;

        auto cancellation_token = co_await get_cancellation_token();
        cancellation_token.enable_propagation();

        if (bvid != L"") {
            param_maker.add_param(L"bvid", bvid);
        }
        else {
            param_maker.add_param(L"aid", to_hstring(avid));
        }
        auto uri = make_uri(
            L"https://api.bilibili.com",
            L"/x/web-interface/view",
            param_maker.get_as_str()
        );
        co_return JsonObject::Parse(co_await m_http_client.GetStringAsync(uri));
    }
    AsyncJsonObjectResult BiliClientManaged::api_api_x_web_interface_view_detail(
        uint64_t avid, hstring bvid
    ) {
        ApiParamMaker param_maker;

        auto cancellation_token = co_await get_cancellation_token();
        cancellation_token.enable_propagation();

        if (bvid != L"") {
            param_maker.add_param(L"bvid", bvid);
        }
        else {
            param_maker.add_param(L"aid", to_hstring(avid));
        }
        auto uri = make_uri(
            L"https://api.bilibili.com",
            L"/x/web-interface/view",
            param_maker.get_as_str()
        );
        co_return JsonObject::Parse(co_await m_http_client.GetStringAsync(uri));
    }
    AsyncJsonObjectResult BiliClientManaged::api_api_x_player_playurl(
        uint64_t avid,
        hstring bvid,
        uint64_t cid,
        BiliUWP::ApiParam_VideoPlayUrlPreference prefers
    ) {
        ApiParamMaker param_maker;
        uint32_t fnval = 0, fourk = 0;

        auto cancellation_token = co_await get_cancellation_token();
        cancellation_token.enable_propagation();

        if (bvid != L"") {
            param_maker.add_param(L"bvid", bvid);
        }
        else {
            param_maker.add_param(L"aid", to_hstring(avid));
        }
        param_maker.add_param(L"cid", to_hstring(cid));
        if (prefers.prefer_dash) {
            // TODO: qn may also be required in url params; do tests later
            fnval |= 16;
            if (prefers.prefer_4k) {
                fnval |= 128;
                fourk = 1;
            }
            if (prefers.prefer_hdr) {
                fnval |= 64;
            }
            if (prefers.prefer_av1) {
                fnval |= 2048;
            }
        }
        param_maker.add_param(L"fnval", to_hstring(fnval));
        param_maker.add_param(L"fourk", to_hstring(fourk));
        auto uri = make_uri(
            L"https://api.bilibili.com",
            L"/x/player/playurl",
            param_maker.get_as_str()
        );
        co_return JsonObject::Parse(co_await m_http_client.GetStringAsync(uri));
    }

    // Audio information
    AsyncJsonObjectResult BiliClientManaged::api_api_audio_music_service_c_url(
        uint64_t auid,
        uint32_t quality
    ) {
        ApiParamMaker param_maker;

        auto cancellation_token = co_await get_cancellation_token();
        cancellation_token.enable_propagation();

        param_maker.add_param(L"access_key", m_access_token);
        param_maker.add_param(L"songid", to_hstring(auid));
        param_maker.add_param(L"quality", to_hstring(quality));
        param_maker.add_param(L"privilege", L"2");
        param_maker.add_param(L"mid", this->data_cookies().DedeUserID);
        param_maker.add_param(L"platform", L"pc");
        auto uri = make_uri(
            L"https://api.bilibili.com",
            L"/audio/music-service-c/url",
            param_maker.get_as_str()
        );
        co_return JsonObject::Parse(co_await m_http_client.GetStringAsync(uri));
    }

    // Favourites information
    AsyncJsonObjectResult BiliClientManaged::api_api_x_v3_fav_folder_created_list(
        uint64_t mid,
        BiliUWP::ApiParam_Page page,
        Windows::Foundation::IReference<BiliUWP::ApiParam_FavItemLookup> const& item_to_find
    ) {
        ApiParamMaker param_maker;

        auto cancellation_token = co_await get_cancellation_token();
        cancellation_token.enable_propagation();

        param_maker.add_param(L"up_mid", to_hstring(mid));
        param_maker.add_param(L"pn", to_hstring(page.n));
        param_maker.add_param(L"ps", to_hstring(page.size));
        if (item_to_find) {
            auto lookup_value = item_to_find.Value();
            param_maker.add_param(L"rid", to_hstring(lookup_value.res_id));
            param_maker.add_param(L"type", to_hstring(util::misc::enum_to_int(lookup_value.res_type)));
        }
        auto uri = make_uri(
            L"https://api.bilibili.com",
            L"/x/v3/fav/folder/created/list",
            param_maker.get_as_str()
        );
        co_return JsonObject::Parse(co_await m_http_client.GetStringAsync(uri));
    }
    AsyncJsonObjectResult BiliClientManaged::api_api_x_v3_fav_resource_list(
        uint64_t folder_id,
        BiliUWP::ApiParam_Page page,
        hstring keyword,
        BiliUWP::ApiParam_FavResSortOrder order
    ) {
        ApiParamMaker param_maker;

        auto cancellation_token = co_await get_cancellation_token();
        cancellation_token.enable_propagation();

        param_maker.add_param(L"media_id", to_hstring(folder_id));
        if (keyword != L"") {
            param_maker.add_param(L"keyword", keyword);
        }
        hstring order_str;
        switch (order) {
        case BiliUWP::ApiParam_FavResSortOrder::ByFavouriteTime:    order_str = L"mtime";       break;
        case BiliUWP::ApiParam_FavResSortOrder::ByViewCount:        order_str = L"view";        break;
        case BiliUWP::ApiParam_FavResSortOrder::ByPublishTime:      order_str = L"pubtime";     break;
        default:
            throw hresult_invalid_argument();
        }
        param_maker.add_param(L"order", order_str);
        param_maker.add_param(L"pn", to_hstring(page.n));
        param_maker.add_param(L"ps", to_hstring(page.size));
        //param_maker.add_param(L"platform", L"web");
        auto uri = make_uri(
            L"https://api.bilibili.com",
            L"/x/v3/fav/resource/list",
            param_maker.get_as_str()
        );
        co_return JsonObject::Parse(co_await m_http_client.GetStringAsync(uri));
    }
}
