#pragma once
#include "BiliClientManaged.h"
#include "util.hpp"
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
        ApiCode m_api_code;
        std::string m_server_msg;
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
        BiliApiUpstreamException(ApiCode api_code) :
            m_api_code(api_code), m_server_msg(), m_err_msg("Server error")
        {
            m_err_msg += std::format("[{}: {}]",
                std::to_underlying(api_code), code_to_msg(api_code)
            );
        }
        BiliApiUpstreamException(ApiCode api_code, std::string_view server_msg) :
            m_api_code(api_code), m_server_msg(server_msg), m_err_msg("Server error")
        {
            m_err_msg += std::format("[{}: {}]: {}",
                std::to_underlying(api_code), code_to_msg(api_code), server_msg
            );
        }
        const char* what() const override { return m_err_msg.c_str(); }
        ApiCode get_api_code(void) const { return m_api_code; }
        std::string_view get_server_msg(void) const { return m_server_msg; }
    };
    class BiliApiParseException : public BiliApiException {
        std::string m_err_msg;
    public:
        static inline struct json_parse_no_prop_t {} json_parse_no_prop;
        static inline struct json_parse_wrong_type_t {} json_parse_wrong_type;
        static inline struct json_parse_out_of_bound_t {} json_parse_out_of_bound;
        static inline struct json_parse_out_of_range_t {} json_parse_out_of_range;
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
        BiliApiParseException(json_parse_out_of_range_t,
            JsonPropsWalkTree const& props_tree
        ) : m_err_msg("JSON parse error: ")
        {
            m_err_msg += std::format(
                "Value of `{}` does not fit into requested native type",
                props_tree.stringify()
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
        // For logging in
        static inline const winrt::BiliUWP::APISignKeys api_android_2{
            L"bca7e84c2d947ac6", L"60698ba2f68e01ce44738920a0ffe768"
        };
    }

    // Public types
    enum class ResItemType : uint32_t {
        Video = 2,
        Audio = 12,
        VideosCollection = 21,
    };
    enum class AudioQuality : int32_t {
        TrialClip = -1,
        BitRate128k = 0,
        BitRate192k,
        BitRate320k,
        Lossless,
    };

    // Result types (and their children types)
    // NOTE: Some fields are omitted (and may be reintroduced in the future)
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
        uint64_t mid;
        winrt::hstring access_token;
        winrt::hstring refresh_token;
        uint32_t expires_in;
        winrt::BiliUWP::UserCookies user_cookies;
    };
    struct RevokeLoginResult {
        // TODO: Finish RevokeLoginResult
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
    struct UserCardInfoResult {
        struct {
            winrt::hstring mid;
            winrt::hstring name;
            winrt::hstring sex;
            winrt::hstring face_url;
            uint64_t follower_count;
            uint64_t following_count;
            struct {
                // Level: 0~6
                uint32_t current_level;
            } level_info;
            struct {
                uint64_t pid;
                winrt::hstring name;
                winrt::hstring image_url;
            } pendant;
            struct {
                uint64_t nid;
                winrt::hstring name;
                winrt::hstring image_url;
                winrt::hstring image_small_url;
                winrt::hstring level;
                winrt::hstring condition;
            } nameplate;
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
                // TODO: Maybe extend this and other related items
                uint32_t type;
                bool is_vip;
            } vip;
        } card;
        struct {
            winrt::hstring small_img_url;
            winrt::hstring large_img_url;
        } space;
        bool is_following;
        uint64_t posts_count;
        uint64_t like_count;
    };
    struct UserSpaceInfo_FansMedal_Medal {
        uint64_t uid;
        uint64_t target_uid;
        uint64_t medal_id;
        uint32_t level;
        winrt::hstring medal_name;
        uint32_t medal_color;
        uint64_t cur_intimacy;
        uint64_t next_intimacy;
        uint64_t intimacy_day_limit;
        uint32_t medal_color_start;
        uint32_t medal_color_end;
        uint32_t medal_color_border;
        bool is_lighted;
        uint32_t light_status;
        uint32_t wearing_status;
        uint64_t score;
    };
    struct UserSpaceInfo_SysNotice {
        uint32_t id;
        winrt::hstring content;
        winrt::hstring url;
        uint32_t notice_type;
        winrt::hstring icon;
        winrt::hstring text_color;
        winrt::hstring bg_color;
    };
    struct UserSpaceInfo_LiveRoom {
        uint32_t room_status;
        uint32_t live_status;
        winrt::hstring room_url;
        winrt::hstring room_title;
        winrt::hstring room_cover_url;
        struct {
            // switch: ?
            bool is_switched;
            uint64_t total_watched_users;
            winrt::hstring text_small;
            winrt::hstring text_large;
            winrt::hstring icon_url;
            winrt::hstring icon_location;
            winrt::hstring icon_web_url;
        } watched_show;
        uint64_t room_id;
        bool is_rounding;
        uint32_t broadcast_type;
    };
    struct UserSpaceInfo_School {
        winrt::hstring name;
    };
    struct UserSpaceInfoResult {
        uint64_t mid;
        winrt::hstring name;
        winrt::hstring sex;
        winrt::hstring face_url;
        bool is_face_nft;
        winrt::hstring sign;
        uint32_t level;
        bool is_silenced;
        uint64_t coin_count;
        bool has_fans_badge;
        struct {
            bool show;
            bool wear;
            std::optional<UserSpaceInfo_FansMedal_Medal> medal;
        } fans_medal;
        struct {
            uint32_t role;
            winrt::hstring title;
            winrt::hstring desc;
            int32_t type;
        } official;
        struct {
            uint32_t type;
            bool is_vip;
            uint64_t due_date;
            uint32_t vip_pay_type;
            struct {
                winrt::hstring path;
                winrt::hstring text;
                winrt::hstring label_theme;
                winrt::hstring text_color;
                uint32_t bg_style;
                winrt::hstring bg_color;
                winrt::hstring border_color;
            } label;
            bool show_avatar_subscript;
            winrt::hstring nickname_color;
            uint32_t role;
            winrt::hstring avatar_subscript_url;
        } vip;
        struct {
            uint64_t pid;
            winrt::hstring name;
            winrt::hstring image_url;
            uint64_t expire;
            winrt::hstring image_enhance;
            winrt::hstring image_enhance_frame;
        } pendant;
        struct {
            uint64_t nid;
            winrt::hstring name;
            winrt::hstring image_url;
            winrt::hstring image_small_url;
            winrt::hstring level;
            winrt::hstring condition;
        } nameplate;
        struct {
            uint64_t mid;
            std::optional<winrt::hstring> colour;
            std::vector<winrt::hstring> tags;
        } user_honour_info;
        bool is_followed;
        winrt::hstring top_photo_url;
        std::optional<UserSpaceInfo_SysNotice> sys_notice;
        std::optional<UserSpaceInfo_LiveRoom> live_room;
        winrt::hstring birthday;
        std::optional<UserSpaceInfo_School> school;
        struct {
            winrt::hstring name;
            winrt::hstring department;
            winrt::hstring title;
            bool is_show;
        } profession;
        struct {
            uint32_t user_upgrade_status;
            bool show_upgrade_window;
        } series;
        bool is_senior_member;
    };
    struct UserSpaceUpStatInfoResult {
        struct {
            uint64_t view_count;
        } archive;
        struct {
            uint64_t view_count;
        } article;
        uint64_t like_count;
    };
    struct UserSpacePublishedVideos_Type {
        uint64_t count;
        winrt::hstring type_name;
        uint64_t tid;
    };
    struct UserSpacePublishedVideos_Video {
        uint64_t avid;
        winrt::hstring author;
        winrt::hstring bvid;
        uint64_t comment_count;
        winrt::hstring copyright;
        uint64_t publish_time;
        winrt::hstring description;
        bool is_pay;
        bool is_union_video;
        winrt::hstring length_str;
        uint64_t mid;
        winrt::hstring cover_url;
        std::optional<uint64_t> play_count;
        uint64_t review;
        winrt::hstring subtitle;
        winrt::hstring title;
        uint64_t tid;
        uint64_t danmaku_count;
    };
    struct UserSpacePublishedVideos_EpisodicButton {
        winrt::hstring text;
        winrt::hstring uri;
    };
    struct UserSpacePublishedVideosResult {
        struct {
            // NOTE: type_id -> Type
            std::map<uint64_t, UserSpacePublishedVideos_Type> tlist;
            std::vector<UserSpacePublishedVideos_Video> vlist;
        } list;
        struct {
            uint64_t count;
            uint64_t pn;
            uint64_t ps;
        } page;
        std::optional<UserSpacePublishedVideos_EpisodicButton> episodic_button;
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
        winrt::hstring external_vid;
        winrt::hstring external_weblink;
        VideoViewInfo_Dimension dimension;
    };
    struct VideoViewInfo_Subtitle {
        uint64_t id;
        winrt::hstring language;
        winrt::hstring language_doc;
        bool locked;
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
            uint32_t type;
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
        std::optional<uint64_t> forward_avid;
        std::optional<uint64_t> mission_id;
        std::optional<winrt::hstring> pgc_redirect_url;
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
            std::optional<uint64_t> view_count;
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
        std::optional<std::vector<VideoViewInfo_Staff>> staff;
        struct {
            winrt::hstring url_image_ani_cut;
        } user_garb;
    };
    struct VideoFullInfoResult {
        // TODO: Finish VideoFullInfoResult
    };
    struct VideoInfoV2_Subtitle {
        uint64_t id;
        winrt::hstring language;
        winrt::hstring language_doc;
        bool locked;
        winrt::hstring subtitle_url;
    };
    struct VideoInfoV2Result {
        uint64_t avid;
        winrt::hstring bvid;
        uint64_t cid;
        uint64_t page_no;
        uint64_t online_count;
        struct {
            bool allow_submit;
            std::vector<VideoInfoV2_Subtitle> list;
        } subtitle;
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
        std::optional<std::vector<winrt::hstring>> codecs;
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
        std::vector<uint32_t> accept_quality;
        uint32_t video_codecid;
        winrt::hstring seek_param;
        winrt::hstring seek_type;
        std::optional<std::vector<VideoPlayUrl_DurlPart>> durl;
        std::optional<VideoPlayUrl_Dash> dash;
        std::vector<VideoPlayUrl_SupportFormat> support_formats;
    };
    struct VideoShotInfoResult {
        // NOTE: Target url is an array of u16 storing image indices
        // NOTE: You must manually handle the wrapping of u16 when
        //       reading the bin file
        winrt::hstring pvdata_url;
        uint64_t img_x_len;
        uint64_t img_y_len;
        uint64_t img_x_size;
        uint64_t img_y_size;
        std::vector<winrt::hstring> images_url;
        std::vector<uint64_t> indices;
    };
    struct AudioBasicInfoResult {
        uint64_t auid;
        uint64_t uid;
        winrt::hstring uname;
        winrt::hstring author;
        winrt::hstring audio_title;
        winrt::hstring cover_url;
        winrt::hstring audio_intro;
        winrt::hstring audio_lyric;
        uint32_t duration;
        uint64_t pubtime;
        uint64_t cur_query_time;
        uint64_t linked_avid;           // 0 if does not exist
        winrt::hstring linked_bvid;     // L"" if does not exist
        uint64_t linked_cid;            // 0 if does not exist
        struct {
            uint64_t auid;
            uint64_t play_count;
            uint64_t favourite_count;
            uint64_t comment_count;
            uint64_t share_count;
        } statistic;
        struct {
            int32_t vip_type;
            bool is_vip;
            uint64_t vip_due_date;
        } vip_info;
        std::vector<uint64_t> favs_with_collected;
        uint64_t coin_count;
    };
    struct AudioPlayUrl_Quality {
        AudioQuality type;
        winrt::hstring desc;
        uint64_t size;
        winrt::hstring bps;
        winrt::hstring tag;
        bool require_membership;
        winrt::hstring require_membership_desc;
    };
    struct AudioPlayUrlResult {
        uint64_t auid;
        AudioQuality type;
        uint64_t expires_in;    // Unit: seconds
        uint64_t stream_size;
        std::vector<winrt::hstring> urls;
        std::vector<AudioPlayUrl_Quality> qualities;
        winrt::hstring audio_title;
        winrt::hstring cover_url;
    };
    struct UserFavFoldersList_Folder {
        uint64_t id;
        uint64_t fid;
        uint64_t mid;
        uint32_t attr;
        winrt::hstring title;
        winrt::hstring cover_url;
        uint32_t cover_type;
        winrt::hstring intro;
        uint64_t ctime;
        uint64_t mtime;
        bool fav_has_item;
        uint64_t media_count;
    };
    struct UserFavFoldersListResult {
        uint64_t count;
        std::vector<UserFavFoldersList_Folder> list;
        bool has_more;
    };
    struct UserFavFoldersListAll_Folder {
        uint64_t id;
        uint64_t fid;
        uint64_t mid;
        uint32_t attr;
        winrt::hstring title;
        bool fav_has_item;
        uint64_t media_count;
    };
    struct UserFavFoldersListAllResult {
        uint64_t count;
        std::vector<UserFavFoldersListAll_Folder> list;
    };
    struct FavFolderResList_Media {
        uint64_t nid;
        ResItemType type;
        winrt::hstring title;
        winrt::hstring cover_url;
        winrt::hstring intro;
        uint64_t page_count;
        uint64_t duration;
        struct {
            uint64_t mid;
            winrt::hstring name;
            winrt::hstring face_url;
        } upper;
        uint32_t attr;
        struct {
            uint64_t favourite_count;
            uint64_t play_count;
            uint64_t danmaku_count;
        } cnt_info;
        winrt::hstring res_link;
        uint64_t ctime;
        uint64_t pubtime;
        uint64_t fav_time;
        winrt::hstring bvid;
    };
    struct FavFolderResListResult {
        struct {
            uint64_t id;
            uint64_t fid;
            uint64_t mid;
            uint32_t attr;
            winrt::hstring title;
            winrt::hstring cover_url;
            struct {
                uint64_t mid;
                winrt::hstring name;
                winrt::hstring face_url;
                bool followed;
                int32_t vip_type;
                bool is_vip;
            } upper;
            uint32_t cover_type;
            struct {
                uint64_t favourite_count;
                uint64_t play_count;
                uint64_t like_count;
                uint64_t share_count;
            } cnt_info;
            uint32_t type;
            winrt::hstring intro;
            uint64_t ctime;
            uint64_t mtime;
            uint32_t state;
            bool fav_has_item;
            bool is_liked;
            uint64_t media_count;
        } info;
        std::vector<FavFolderResList_Media> media_list;
        bool has_more;
    };

    // Parameter types
    struct VideoPlayUrlPreferenceParam {
        bool prefer_dash;
        bool prefer_hdr;
        bool prefer_4k;
        bool prefer_dolby;
        bool prefer_8k;
        bool prefer_av1;
    };
    using AudioQualityParam = AudioQuality;
    struct PageParam {
        uint32_t n;     // Starts from 1
        uint32_t size;  // Recommended value is 20
    };
    enum class UserPublishedVideosOrderParam {
        ByPublishTime,
        ByClickCount,
        ByFavouriteCount,
    };
    struct FavItemLookupParam {
        uint64_t nid;
        ResItemType type;
    };
    enum class FavResSortOrderParam {
        ByFavouriteTime,
        ByViewCount,
        ByPublishTime,
    };

    struct BiliClient {
        BiliClient();

        // Getter & setters
        winrt::hstring get_access_token(void);
        void set_access_token(winrt::hstring const& value);
        winrt::hstring get_refresh_token(void);
        void set_refresh_token(winrt::hstring const& value);
        winrt::BiliUWP::UserCookies get_cookies(void);
        void set_cookies(winrt::BiliUWP::UserCookies const& value);
        winrt::BiliUWP::APISignKeys get_api_sign_keys(void);
        void set_api_sign_keys(winrt::BiliUWP::APISignKeys const& value);

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
        util::winrt::task<UserCardInfoResult> user_card_info(uint64_t mid);
        util::winrt::task<UserSpaceInfoResult> user_space_info(uint64_t mid);
        util::winrt::task<UserSpaceUpStatInfoResult> user_space_upstat_info(uint64_t mid);
        util::winrt::task<UserSpacePublishedVideosResult> user_space_published_videos(
            uint64_t mid, PageParam page, winrt::hstring search_keyword, UserPublishedVideosOrderParam order
        );

        // Video information
        util::winrt::task<VideoViewInfoResult> video_view_info(std::variant<uint64_t, winrt::hstring> vid);
        util::winrt::task<VideoFullInfoResult> video_full_info(std::variant<uint64_t, winrt::hstring> vid);
        util::winrt::task<VideoInfoV2Result> video_info_v2(
            std::variant<uint64_t, winrt::hstring> vid, uint64_t cid
        );
        util::winrt::task<VideoPlayUrlResult> video_play_url(
            std::variant<uint64_t, winrt::hstring> vid, uint64_t cid,
            VideoPlayUrlPreferenceParam prefers
        );
        util::winrt::task<VideoShotInfoResult> video_shot_info(
            std::variant<uint64_t, winrt::hstring> vid, uint64_t cid,
            bool load_indices
        );

        // Audio information
        util::winrt::task<AudioBasicInfoResult> audio_basic_info(uint64_t auid);
        util::winrt::task<AudioPlayUrlResult> audio_play_url(uint64_t auid, AudioQualityParam quality);

        // Favourites information
        util::winrt::task<UserFavFoldersListResult> user_fav_folders_list(
            uint64_t mid, PageParam page, std::optional<FavItemLookupParam> item_to_find
        );
        util::winrt::task<UserFavFoldersListAllResult> user_fav_folders_list_all(
            uint64_t mid, std::optional<FavItemLookupParam> item_to_find
        );
        util::winrt::task<FavFolderResListResult> fav_folder_res_list(
            uint64_t folder_id, PageParam page, winrt::hstring search_keyword, FavResSortOrderParam order
        );

    private:
        winrt::BiliUWP::BiliClientManaged m_bili_client;
        winrt::BiliUWP::APISignKeys m_api_sign_keys;
        winrt::hstring m_refresh_token;
    };
}
