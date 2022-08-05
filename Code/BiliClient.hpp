#pragma once
#include "BiliClientManaged.h"
#include <variant>

// BiliClient: A native layer wrapping BiliClientManaged to provide idiomatic RPC experience
namespace BiliUWP {
    enum class ApiCode : int32_t {
        Success = 0,
        // --------------------
        AppNotExistOrBanned_1 = -1,
        AccessKeyError = -2,
        ApiSignError = -3,
        CallerNoMethodPermission = -4,
        AccountNotLoggedIn = -101,
        AccountBanned = -102,
        NotEnoughPoints = -103,
        NotEnoughCoins = -104,
        CaptchaError = -105,
        AccountNotFullMember = -106,
        AppNotExistOrBanned_2 = -107,
        PhoneNotLinked_1 = -108,
        PhoneNotLinked_2 = -110,
        CsrfVerifyFail = -111,
        SystemUpgrading = -112,
        AccountNotRealNamed_1 = -113,
        PhoneNotLinked_3 = -114,
        AccountNotRealNamed_2 = -115,
        // --------------------
        NotChanged = -304,
        CollisionRedirect = -307,
        RequestError = -400,
        NotAuthenticated = -401,
        AccessDenied = -403,
        NotFound = -404,
        MethodNotSupported = -405,
        Collision = -409,
        ServerError = -500,
        OverloadProtected = -503,
        ServiceCallTimedOut = -504,
        LimitExceeded = -509,
        UploadFileNotExist = -616,
        UploadFileTooLarge = -617,
        TooManyFailedLoginAttempts = -625,
        UserNotExist = -626,
        PasswordTooWeak = -628,
        UserNameOrPasswordError = -629,
        OperateObjectsCountLimited = -632,
        LockedDown = -643,
        UserLevelTooLow = -650,
        DuplicatedUser = -652,
        TokenExpired = -658,
        PasswordTimestampExpired = -662,
        RegionLimited = -688,
        CopyrightLimited = -689,
        JieCaoDeductionFailed = -701,
        UnknownServerIssue = -8888, // TODO...
        // --------------------
        TvQrLogin_QrExpired = 86038,
        TvQrLogin_QrNotConfirmed = 86039,
    };

    struct JsonPropsWalkTree {
        JsonPropsWalkTree() : m_props{ "<root>" } {}
        // TODO: Support custom root name
        std::string stringify(void) const {
            if (m_props.empty()) {
                return {};
            }
            auto it = m_props.begin();
            auto ie = m_props.end();
            std::string result{ std::get<std::string_view>(*it++) };
            for (; it != ie; it++) {
                std::visit([&](auto&& arg) {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, size_t>) {
                        result += '[';
                        result += std::to_string(arg);
                        result += ']';
                    }
                    else if constexpr (std::is_same_v<T, std::string_view>) {
                        result += '.';
                        result += arg;
                    }
                    else {
                        static_assert(util::misc::always_false_v<T>, "Unknown kind to stringify");
                    }
                }, *it);
            }
            return result;
        }
        void push(size_t array_idx) {
            m_props.emplace_back(array_idx);
        }
        void push(std::string_view prop_name) {
            m_props.emplace_back(prop_name);
        }
        void pop(void) {
            m_props.pop_back();
        }
    private:
        std::vector<std::variant<size_t, std::string_view>> m_props;
    };

    // NOTE: Error messages are encoded in UTF-8
    class BiliApiException : public std::exception {};
    class BiliApiUpstreamException : public BiliApiException {
        std::string m_err_msg;
        static std::string_view code_to_msg(ApiCode api_code) {
            switch (api_code) {
#define gen_branch(value, msg)  case ApiCode::value:   return msg;
#define gen_branch_simple(value) gen_branch(value, #value)
                gen_branch_simple(Success);
                // --------------------
                gen_branch_simple(AppNotExistOrBanned_1);
                gen_branch_simple(AccessKeyError);
                gen_branch_simple(ApiSignError);
                gen_branch_simple(CallerNoMethodPermission);
                gen_branch_simple(AccountNotLoggedIn);
                gen_branch_simple(AccountBanned);
                gen_branch_simple(NotEnoughPoints);
                gen_branch_simple(NotEnoughCoins);
                gen_branch_simple(CaptchaError);
                gen_branch_simple(AccountNotFullMember);
                gen_branch_simple(PhoneNotLinked_1);
                gen_branch_simple(PhoneNotLinked_2);
                gen_branch_simple(CsrfVerifyFail);
                gen_branch_simple(SystemUpgrading);
                gen_branch_simple(AccountNotRealNamed_1);
                gen_branch_simple(PhoneNotLinked_3);
                gen_branch_simple(AccountNotRealNamed_2);
                // --------------------
                gen_branch_simple(NotChanged);
                gen_branch_simple(CollisionRedirect);
                gen_branch_simple(RequestError);
                gen_branch_simple(NotAuthenticated);
                gen_branch_simple(AccessDenied);
                gen_branch_simple(NotFound);
                gen_branch_simple(MethodNotSupported);
                gen_branch_simple(Collision);
                gen_branch_simple(ServerError);
                gen_branch_simple(OverloadProtected);
                gen_branch_simple(ServiceCallTimedOut);
                gen_branch_simple(LimitExceeded);
                gen_branch_simple(UploadFileNotExist);
                gen_branch_simple(UploadFileTooLarge);
                gen_branch_simple(TooManyFailedLoginAttempts);
                gen_branch_simple(UserNotExist);
                gen_branch_simple(PasswordTooWeak);
                gen_branch_simple(UserNameOrPasswordError);
                gen_branch_simple(OperateObjectsCountLimited);
                gen_branch_simple(LockedDown);
                gen_branch_simple(UserLevelTooLow);
                gen_branch_simple(DuplicatedUser);
                gen_branch_simple(TokenExpired);
                gen_branch_simple(PasswordTimestampExpired);
                gen_branch_simple(RegionLimited);
                gen_branch_simple(CopyrightLimited);
                gen_branch_simple(JieCaoDeductionFailed);
                gen_branch_simple(UnknownServerIssue);
                // --------------------
                gen_branch_simple(TvQrLogin_QrExpired);
                gen_branch_simple(TvQrLogin_QrNotConfirmed);
            default:        return "Unknown API result code";
#undef gen_branch_simple
#undef gen_branch
            }
        }
    public:
        BiliApiUpstreamException(ApiCode api_code) : m_err_msg("Server error") {
            m_err_msg += std::format("[{}: {}]",
                util::misc::enum_to_int(api_code), code_to_msg(api_code)
            );
        }
        BiliApiUpstreamException(ApiCode api_code, std::string_view msg) : m_err_msg("Server error") {
            m_err_msg += std::format("[{}: {}]: {}",
                util::misc::enum_to_int(api_code), code_to_msg(api_code), msg
            );
        }
        const char* what() const override { return m_err_msg.c_str(); }
    };
    class BiliApiParseException : public BiliApiException {
        std::string m_err_msg;
    public:
        static inline struct json_parse_no_prop_t {} json_parse_no_prop;
        static inline struct json_parse_wrong_type_t {} json_parse_wrong_type;
        static inline struct json_parse_out_of_bound_t {} json_parse_out_of_bound;
        static inline struct json_parse_user_defined_t {} json_parse_user_defined;
        enum class JsonParseFailKind {
            Unknown,
            NoProperty,
            WrongType
        };
        BiliApiParseException(std::string msg) : m_err_msg(std::move(msg)) {}
        BiliApiParseException(json_parse_no_prop_t, JsonPropsWalkTree const& props_tree) :
            m_err_msg("JSON parse error: ")
        {
            m_err_msg += std::format("Property `{}` not found", props_tree.stringify());
        }
        BiliApiParseException(json_parse_wrong_type_t,
            JsonPropsWalkTree const& props_tree,
            std::string_view desired_type, std::string_view actual_type
        ) : m_err_msg("JSON parse error: ")
        {
            m_err_msg += std::format(
                "Expected property `{}` of type `{}`, found type `{}`",
                props_tree.stringify(),
                desired_type, actual_type
            );
        }
        BiliApiParseException(json_parse_out_of_bound_t,
            JsonPropsWalkTree const& props_tree, std::size_t actual_size
        ) : m_err_msg("JSON parse error: ")
        {
            m_err_msg += std::format(
                "Out of bound while accessing `{}` of size {}",
                props_tree.stringify(),
                actual_size
            );
        }
        BiliApiParseException(json_parse_user_defined_t,
            JsonPropsWalkTree const& props_tree, std::string_view msg
        ) : m_err_msg("JSON parse error: ")
        {
            m_err_msg += std::format("`{}`: {}", props_tree.stringify(), msg);
        }
        const char* what() const override { return m_err_msg.c_str(); }
    };

    namespace keys {
        static inline const winrt::BiliUWP::APISignKeys api_tv_1{
            L"4409e2ce8ffd12b8", L"59b43e04ad6965f34319062b478f83dd"
        };
        static inline const winrt::BiliUWP::APISignKeys api_android_1{
            L"1d8b6e7d45233436", L"560c52ccd288fed045859ed18bffd973"
        };
    }

    struct RequestTvQrLoginResult {
        winrt::hstring url;
        winrt::hstring auth_code;
    };
    struct PollTvQrLoginResult {
        ApiCode code;
        uint64_t mid;
        winrt::hstring access_token;
        winrt::hstring refresh_token;
        uint32_t expires_in;
        winrt::BiliUWP::UserCookies user_cookies;
    };
    struct Oauth2RefreshTokenResult {
        winrt::hstring access_token;
        winrt::hstring refresh_token;
        uint32_t expires_in;
        winrt::BiliUWP::UserCookies user_cookies;
    };
    struct RevokeLoginResult {
        // TODO...
    };
    struct MyAccountNavInfoResult {
        bool logged_in;
        bool email_verified;
        winrt::hstring face_url;
        struct {
            uint32_t current_level;
            uint32_t current_min;
            uint32_t current_exp;
            uint32_t next_exp;  // NOTE: 0 represents infinity ("--")
        } level_info;
        uint64_t mid;
        bool mobile_verified;
        double coin_count;
        uint32_t moral;
        struct {
            uint32_t role;
            winrt::hstring title;
            winrt::hstring desc;
            int32_t type;
        } official;
        struct {
            int32_t type;
            winrt::hstring desc;
        } official_verify;
        struct {
            uint64_t pid;
            winrt::hstring name;
            winrt::hstring image;
        } pendant;
        winrt::hstring uname;
        uint64_t vip_due_date;
        bool is_vip;
        int32_t vip_type;
        struct {
            winrt::hstring text;
            winrt::hstring label_theme;
        } vip_label;
        bool vip_avatar_subscript;
        winrt::hstring vip_nickname_color;
        struct {
            double bcoin_balance;
            double coupon_balance;
        } wallet;
        bool has_shop;
        winrt::hstring shop_url;
    };
    struct MyAccountNavStatInfoResult {
        uint32_t following_count;
        uint32_t follower_count;
        uint32_t dynamic_count;
    };
    struct VideoViewInfo_Dimension {
        uint32_t width;
        uint32_t height;
        bool rotate;
    };
    struct VideoViewInfo_DescV2 {
        winrt::hstring raw_text;
        uint32_t type;
        uint64_t biz_id;
    };
    struct VideoViewInfo_Page {
        uint64_t cid;
        uint32_t page;
        winrt::hstring from;
        winrt::hstring part_title;
        uint64_t duration;
        uint64_t external_vid;
        winrt::hstring external_weblink;
        VideoViewInfo_Dimension dimension;
    };
    struct VideoViewInfo_Subtitle {
        uint64_t id;
        winrt::hstring language;
        winrt::hstring language_doc;
        bool locked;
        uint64_t author_mid;
        winrt::hstring subtitle_url;
        struct {
            uint64_t mid;
            winrt::hstring name;
            winrt::hstring sex;
            winrt::hstring face_url;
            winrt::hstring sign;
        } author;
    };
    struct VideoViewInfo_Staff {
        uint64_t mid;
        winrt::hstring title;
        winrt::hstring name;
        winrt::hstring face_url;
        struct {
            uint64_t type;
            bool is_vip;
        } vip;
        struct {
            uint32_t role;
            winrt::hstring title;
            winrt::hstring desc;
            int32_t type;
        } official;
        uint64_t follower_count;
    };
    struct VideoViewInfoResult {
        winrt::hstring bvid;
        uint64_t avid;
        uint32_t videos_count;
        uint64_t tid;
        winrt::hstring tname;
        uint32_t copyright;
        winrt::hstring cover_url;
        winrt::hstring title;
        uint64_t pubdate;
        uint64_t ctime;
        winrt::hstring desc;
        std::vector<VideoViewInfo_DescV2> desc_v2;
        uint32_t state;
        uint32_t duration;
        uint64_t forward_avid;
        uint64_t mission_id;
        winrt::hstring pgc_redirect_url;
        struct {
            bool elec;
            bool download;
            bool movie;
            bool pay;
            bool hd5;
            bool no_reprint;
            bool autoplay;
            bool ugc_pay;
            bool is_stein_gate;
            bool is_cooperation;
        } rights;
        struct {
            uint64_t mid;
            winrt::hstring name;
            winrt::hstring face_url;
        } owner;
        struct {
            uint64_t avid;
            uint64_t view_count;
            uint64_t danmaku_count;
            uint64_t reply_count;
            uint64_t favorite_count;
            uint64_t coin_count;
            uint64_t share_count;
            uint64_t now_rank;
            uint64_t his_rank;  // history_rank
            uint64_t like_count;
            uint64_t dislike_count;
            winrt::hstring evaluation;
            winrt::hstring argue_msg;
        } stat;
        winrt::hstring dynamic_text;
        uint64_t cid_1p;
        VideoViewInfo_Dimension dimension_1p;
        std::vector<VideoViewInfo_Page> pages;
        struct {
            bool allow_submit;
            std::vector<VideoViewInfo_Subtitle> list;
        } subtitle;
        std::vector<VideoViewInfo_Staff> staff;
        struct {
            winrt::hstring url_image_ani_cut;
        } user_garb;
    };
    struct VideoFullInfoResult {
        // TODO...
    };
    struct VideoPlayUrl_DurlPart {
        uint64_t order;
        uint64_t length;
        uint64_t size;
        winrt::hstring ahead;
        winrt::hstring vhead;
        winrt::hstring url;
        std::vector<winrt::hstring> backup_url;
    };
    struct VideoPlayUrl_Dash_Stream {
        uint32_t id;
        winrt::hstring base_url;
        std::vector<winrt::hstring> backup_url;
        uint32_t bandwidth;
        winrt::hstring mime_type;
        winrt::hstring codecs;
        uint32_t width;
        uint32_t height;
        winrt::hstring frame_rate;
        winrt::hstring sar;
        uint32_t start_with_sap;
        struct {
            winrt::hstring initialization;
            winrt::hstring index_range;
        } segment_base;
        uint32_t codecid;
    };
    struct VideoPlayUrl_Dash {
        uint64_t duration;
        double min_buffer_time;
        std::vector<VideoPlayUrl_Dash_Stream> video;
        std::vector<VideoPlayUrl_Dash_Stream> audio;
    };
    struct VideoPlayUrl_SupportFormat {
        uint32_t quality;
        winrt::hstring format;
        winrt::hstring new_description;
        winrt::hstring display_desc;
        winrt::hstring superscript;
        std::vector<winrt::hstring> codecs;
    };
    struct VideoPlayUrlResult {
        winrt::hstring from;
        winrt::hstring result;
        winrt::hstring message;
        uint32_t quality;
        winrt::hstring format;
        uint64_t timelength;    // Unit: ms
        winrt::hstring accept_format;
        std::vector<winrt::hstring> accept_description;
        std::vector<winrt::hstring> accept_quality;
        uint32_t video_codecid;
        winrt::hstring seek_param;
        winrt::hstring seek_type;
        std::vector<VideoPlayUrl_DurlPart> durl;
        std::optional<VideoPlayUrl_Dash> dash;
        std::vector<VideoPlayUrl_SupportFormat> support_formats;
    };

    struct VideoPlayUrlPreferenceParam {
        bool prefer_dash;
        bool prefer_4k;
        bool prefer_hdr;
        bool prefer_av1;
    };

    // TODO: Complete BiliClient
    struct BiliClient {
        BiliClient();

        // Getter & setters
        winrt::hstring get_access_token(void);
        void set_access_token(winrt::hstring const& value);
        winrt::hstring get_refresh_token(void);
        void set_refresh_token(winrt::hstring const& value);
        winrt::BiliUWP::UserCookies get_cookies(void);
        void set_cookies(winrt::BiliUWP::UserCookies const& value);

        // Authentication
        util::winrt::task<RequestTvQrLoginResult> request_tv_qr_login(winrt::guid local_id);
        //   NOTE: When succeeded, self data will be automatically updated
        util::winrt::task<PollTvQrLoginResult> poll_tv_qr_login(
            winrt::hstring auth_code,
            winrt::guid local_id
        );
        //   NOTE: When succeeded, self data will be automatically updated
        util::winrt::task<Oauth2RefreshTokenResult> oauth2_refresh_token(void);
        //   NOTE: When succeeded, self data will be erased
        util::winrt::task<RevokeLoginResult> revoke_login(void);

        // User information
        util::winrt::task<MyAccountNavInfoResult> my_account_nav_info(void);
        util::winrt::task<MyAccountNavStatInfoResult> my_account_nav_stat_info(void);

        // Video information
        util::winrt::task<VideoViewInfoResult> video_view_info(std::variant<uint64_t, winrt::hstring> vid);
        util::winrt::task<VideoFullInfoResult> video_full_info(std::variant<uint64_t, winrt::hstring> vid);
        util::winrt::task<VideoPlayUrlResult> video_play_url(
            std::variant<uint64_t, winrt::hstring> vid, uint64_t cid,
            VideoPlayUrlPreferenceParam prefers
        );

        // Audio information

        // Favourites information

    private:
        winrt::BiliUWP::BiliClientManaged m_bili_client;

        winrt::hstring m_refresh_token;
    };
}
