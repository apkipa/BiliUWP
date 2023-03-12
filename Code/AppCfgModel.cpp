#include "pch.h"
#include "util.hpp"
#include "AppCfgModel.h"
#include "AppCfgModel.g.cpp"

#define apply_props_list(functor)                                                   \
    functor(ConfigVersion, L"0.1.0");                                               \
    functor(App_UseTabView, true);                                                  \
    functor(App_ShowTabThumbnails, false);                                          \
    functor(App_AlwaysSyncPlayingCfg, true);                                        \
    functor(App_GlobalVolume, 1000);                                                \
    functor(App_UseHRASForVideo, true);                                             \
    functor(App_OverrideSpaceForPlaybackControl, false);                            \
    functor(App_UseCustomVideoPresenter, false);                                    \
    functor(App_PersistClipboardAfterExit, false);                                  \
    functor(App_SimplifyVisualsLevel, 0);                                           \
    functor(App_IsDeveloper, false);                                                \
    functor(App_ShowDetailedStats, false);                                          \
    functor(App_DebugVideoDanmakuControl, false);                                   \
    functor(App_RedactLogs, true);                                                  \
    functor(App_StoreLogs, false);                                                  \
    functor(App_LogLevel, static_cast<uint32_t>(util::debug::LogLevel::Error));     \
    functor(App_ShowDebugConsole, false);                                           \
    functor(App_LocalId, util::winrt::gen_random_guid());                           \
    functor(User_CredentialEffectiveStartTime, 0);                                  \
    functor(User_CredentialEffectiveEndTime, 0);                                    \
    functor(User_ApiKey, L"");                                                      \
    functor(User_ApiKeySec, L"");                                                   \
    functor(User_AccessToken, L"");                                                 \
    functor(User_RefreshToken, L"");                                                \
    functor(User_Cookies_SESSDATA, L"");                                            \
    functor(User_Cookies_bili_jct, L"");                                            \
    functor(User_Cookies_DedeUserID, L"");                                          \
    functor(User_Cookies_DedeUserID__ckMd5, L"");                                   \
    functor(User_Cookies_sid, L"")

namespace winrt::BiliUWP::implementation {
    using namespace Windows::Foundation;
    using namespace Windows::Data::Json;
    using namespace Windows::UI::Xaml::Data;

    // Optimal parameter type for passing values around
    // TODO: Will modern C++ have a better replacement for the ParamT defined below?
    template<typename T>
    using ParamT = std::conditional_t<std::is_fundamental_v<T>, T, T const&>;

    AppCfgModel::AppCfgModel() : m_LocalData(Windows::Storage::ApplicationData::Current().LocalSettings()) {}

    event_token AppCfgModel::PropertyChanged(PropertyChangedEventHandler const& handler) {
        return m_PropertyChanged.add(handler);
    }
    void AppCfgModel::PropertyChanged(event_token const& token) noexcept {
        m_PropertyChanged.remove(token);
    }

    void AppCfgModel::ResetConfig(void) {
        m_LocalData.Values().Clear();
    }

    void AppCfgModel::SetItemBoxed(hstring const& key, IInspectable const& value) {
        // TODO: Add support for array values (Windows::Storage::ApplicationDataCompositeValue)
        m_LocalData.Values().Insert(key, value);
    }
    IInspectable AppCfgModel::TryGetItemBoxed(hstring const& key) {
        // TODO: Add support for array values (Windows::Storage::ApplicationDataCompositeValue)
        return m_LocalData.Values().TryLookup(key);
    }

#define gen_prop_getsetter(key, default_value)                                  \
    using key##_type = decltype(std::declval<BiliUWP::AppCfgModel>().key());    \
    key##_type AppCfgModel::key() {                                             \
        if (auto v = this->TryGetItemBoxed(L"" #key).try_as<key##_type>()) {    \
            return *v;                                                          \
        }                                                                       \
        else {                                                                  \
            this->SetItemBoxed(L"" #key, box_value(default_value));             \
            return default_value;                                               \
        }                                                                       \
    }                                                                           \
    void AppCfgModel::key(ParamT<key##_type> value) {                           \
        if (value == this->key()) { return; }                                   \
        this->SetItemBoxed(L"" #key, box_value(value));                         \
        m_PropertyChanged(*this, PropertyChangedEventArgs{ L"" #key });         \
    }

    apply_props_list(gen_prop_getsetter);

    template<typename T>
    JsonValue value_as_json(ParamT<T> value) = delete;
    template<>
    JsonValue value_as_json<hstring>(ParamT<hstring> value) {
        return JsonValue::CreateStringValue(value);
    }
    template<>
    JsonValue value_as_json<uint32_t>(ParamT<uint32_t> value) {
        return JsonValue::CreateNumberValue(value);
    }
    template<>
    JsonValue value_as_json<uint64_t>(ParamT<uint64_t> value) {
        return JsonValue::CreateNumberValue(static_cast<double>(value));
    }
    template<>
    JsonValue value_as_json<double>(ParamT<double> value) {
        return JsonValue::CreateNumberValue(value);
    }
    template<>
    JsonValue value_as_json<bool>(ParamT<bool> value) {
        return JsonValue::CreateBooleanValue(value);
    }
    template<>
    JsonValue value_as_json<guid>(ParamT<guid> value) {
        return JsonValue::CreateStringValue(util::winrt::to_hstring(value));
    }

    template<typename T>
    T value_from_json(JsonValue const& jv) = delete;
    template<>
    hstring value_from_json(JsonValue const& jv) {
        return jv.GetString();
    }
    template<>
    uint32_t value_from_json(JsonValue const& jv) {
        return static_cast<uint32_t>(jv.GetNumber());
    }
    template<>
    uint64_t value_from_json(JsonValue const& jv) {
        return static_cast<uint64_t>(jv.GetNumber());
    }
    template<>
    double value_from_json(JsonValue const& jv) {
        return jv.GetNumber();
    }
    template<>
    bool value_from_json(JsonValue const& jv) {
        return jv.GetBoolean();
    }
    template<>
    guid value_from_json(JsonValue const& jv) {
        return guid{ jv.GetString() };
    }

    template<typename T>
    T named_value_from_json(JsonObject const& jo, hstring const& key) = delete;
    template<>
    hstring named_value_from_json(JsonObject const& jo, hstring const& key) {
        return jo.GetNamedString(key);
    }
    template<>
    uint32_t named_value_from_json(JsonObject const& jo, hstring const& key) {
        return static_cast<uint32_t>(jo.GetNamedNumber(key));
    }
    template<>
    uint64_t named_value_from_json(JsonObject const& jo, hstring const& key) {
        return static_cast<uint64_t>(jo.GetNamedNumber(key));
    }
    template<>
    double named_value_from_json(JsonObject const& jo, hstring const& key) {
        return jo.GetNamedNumber(key);
    }
    template<>
    bool named_value_from_json(JsonObject const& jo, hstring const& key) {
        return jo.GetNamedBoolean(key);
    }
    template<>
    guid named_value_from_json(JsonObject const& jo, hstring const& key) {
        return guid{ jo.GetNamedString(key) };
    }

    hstring AppCfgModel::SerializeAsString() {
        JsonObject json;

#define gen_prop_serializer(key, default_value)                             \
    json.SetNamedValue(L"" #key, value_as_json<key##_type>(this->key()))

        apply_props_list(gen_prop_serializer);

        return json.ToString();
    }
    void AppCfgModel::DeserializeFromString(hstring const& s) {
        JsonObject jo = JsonObject::Parse(s);

#define gen_prop_deserializer_preflight(key, default_value)                 \
    static_cast<void>(named_value_from_json<key##_type>(jo, L"" #key))
#define gen_prop_deserializer(key, default_value)                           \
    this->key(named_value_from_json<key##_type>(jo, L"" #key))

        // Check whether json has all required fields with correct types
        apply_props_list(gen_prop_deserializer_preflight);
        // Perform actual data update
        apply_props_list(gen_prop_deserializer);
    }
}
