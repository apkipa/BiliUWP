#include "pch.h"
#include "App.h"
#include "BiliClient.hpp"
#include "json.h"

namespace BiliUWP {
    bool try_check_api_code(ApiCode code) {
        return code == ApiCode::Success;
    }
    bool try_check_api_code(int32_t code) {
        return try_check_api_code(static_cast<ApiCode>(code));
    }
    bool try_check_api_code(double code) {
        return try_check_api_code(static_cast<ApiCode>(code));
    }
    void check_api_code(ApiCode code) {
        if (!try_check_api_code(code)) {
            throw BiliApiUpstreamException(code);
        }
    }
    void check_api_code(int32_t code) {
        check_api_code(static_cast<ApiCode>(code));
    }
    void check_api_code(double code) {
        check_api_code(static_cast<ApiCode>(code));
    }
    void check_json_code(json::JsonObject const& jo) {
        check_api_code(jo.at(L"code").get_value<int32_t>());
    }
    void check_json_code(winrt::Windows::Data::Json::JsonObject const& jo) {
        auto code = static_cast<ApiCode>(jo.GetNamedNumber(L"code"));
        if (!try_check_api_code(code)) {
            if (auto jv = jo.TryLookup(L"message")) {
                throw BiliApiUpstreamException(code, winrt::to_string(jv.GetString()));
            }
        }
    }

    struct JsonObjectVisitor;
    struct JsonArrayVisitor;
    struct JsonValueVisitor;

    struct JsonVisitorHelper {
        struct BadFnParamType;
        template<typename Functor>
        static auto get_fn_param_helper(Functor func, int, int, int) ->
            util::misc::second_type<decltype(func(std::declval<JsonObjectVisitor>())), JsonObjectVisitor>;
        template<typename Functor>
        static auto get_fn_param_helper(Functor func, int, int, ...) ->
            util::misc::second_type<decltype(func(std::declval<JsonArrayVisitor>())), JsonArrayVisitor>;
        template<typename Functor>
        static auto get_fn_param_helper(Functor func, int, ...) ->
            util::misc::second_type<decltype(func(std::declval<JsonValueVisitor>())), JsonValueVisitor>;
        template<typename Functor>
        static auto get_fn_param_helper(Functor func, ...) -> BadFnParamType;
        static constexpr std::string_view stringify(winrt::Windows::Data::Json::JsonValueType jvt) {
            using winrt::Windows::Data::Json::JsonValueType;
            switch (jvt) {
            case JsonValueType::Array:          return "array";
            case JsonValueType::Boolean:        return "boolean";
            case JsonValueType::Null:           return "null";
            case JsonValueType::Number:         return "number";
            case JsonValueType::Object:         return "object";
            case JsonValueType::String:         return "string";
            default:                            return "<unknown>";
            }
        }
    };

    struct JsonValueVisitor {
        JsonValueVisitor(
            winrt::Windows::Data::Json::IJsonValue jv,
            JsonPropsWalkTree& props_walk
        ) : m_jv(std::move(jv)), m_props_walk(props_walk) {}

        JsonPropsWalkTree& get_props_walk_tree(void) { return m_props_walk; }

        bool is_null(void) {
            return m_jv.ValueType() == winrt::Windows::Data::Json::JsonValueType::Null;
        }
        void expect_null(void) {
            expect_jv_type(m_jv, winrt::Windows::Data::Json::JsonValueType::Null);
        }

        template<typename T>
        std::optional<T> try_as(void) = delete;
        template<>
        std::optional<std::wstring> try_as(void) {
            if (m_jv.ValueType() != winrt::Windows::Data::Json::JsonValueType::String) {
                return std::nullopt;
            }
            return std::wstring{ m_jv.GetString() };
        }
        template<>
        std::optional<winrt::hstring> try_as(void) {
            if (m_jv.ValueType() != winrt::Windows::Data::Json::JsonValueType::String) {
                return std::nullopt;
            }
            return m_jv.GetString();
        }
        template<>
        std::optional<bool> try_as(void) {
            if (m_jv.ValueType() != winrt::Windows::Data::Json::JsonValueType::Boolean) {
                return std::nullopt;
            }
            return m_jv.GetBoolean();
        }
        template<>
        std::optional<double> try_as(void) {
            if (m_jv.ValueType() != winrt::Windows::Data::Json::JsonValueType::Number) {
                return std::nullopt;
            }
            return m_jv.GetNumber();
        }
        template<> std::optional<uint8_t> try_as(void) { return try_as_integer<uint8_t>(); }
        template<> std::optional<int8_t> try_as(void) { return try_as_integer<int8_t>(); }
        template<> std::optional<uint16_t> try_as(void) { return try_as_integer<uint16_t>(); }
        template<> std::optional<int16_t> try_as(void) { return try_as_integer<int16_t>(); }
        template<> std::optional<uint32_t> try_as(void) { return try_as_integer<uint32_t>(); }
        template<> std::optional<int32_t> try_as(void) { return try_as_integer<int32_t>(); }
        template<> std::optional<uint64_t> try_as(void) { return try_as_integer<uint64_t>(); }
        template<> std::optional<int64_t> try_as(void) { return try_as_integer<int64_t>(); }
        template<>
        std::optional<JsonObjectVisitor> try_as(void);
        template<>
        std::optional<JsonArrayVisitor> try_as(void);

        template<typename T>
        T as(void) = delete;
        template<>
        std::wstring as(void) {
            expect_jv_type(m_jv, winrt::Windows::Data::Json::JsonValueType::String);
            return std::wstring{ m_jv.GetString() };
        }
        template<>
        winrt::hstring as(void) {
            expect_jv_type(m_jv, winrt::Windows::Data::Json::JsonValueType::String);
            return m_jv.GetString();
        }
        template<>
        bool as(void) {
            expect_jv_type(m_jv, winrt::Windows::Data::Json::JsonValueType::Boolean);
            return m_jv.GetBoolean();
        }
        template<>
        double as(void) {
            expect_jv_type(m_jv, winrt::Windows::Data::Json::JsonValueType::Number);
            return m_jv.GetNumber();
        }
        template<> uint8_t as(void) { return as_integer<uint8_t>(); }
        template<> int8_t as(void) { return as_integer<int8_t>(); }
        template<> uint16_t as(void) { return as_integer<uint16_t>(); }
        template<> int16_t as(void) { return as_integer<int16_t>(); }
        template<> uint32_t as(void) { return as_integer<uint32_t>(); }
        template<> int32_t as(void) { return as_integer<int32_t>(); }
        template<> uint64_t as(void) { return as_integer<uint64_t>(); }
        template<> int64_t as(void) { return as_integer<int64_t>(); }
        template<>
        JsonObjectVisitor as(void);
        template<>
        JsonArrayVisitor as(void);

        template<typename T>
        void populate(T& dst) {
            dst = this->as<T>();
        }
        template<typename T>
        void populate(std::vector<T>& dst) {
            this->as<JsonArrayVisitor>().scope_populate([&](T& dst, JsonValueVisitor jvv) {
                dst = jvv.as<T>();
            }, dst);
        }
        template<typename T>
        void populate(std::optional<T>& dst) {
            if (auto v = this->try_as<T>()) {
                dst = std::move(*v);
            }
            else {
                dst = std::nullopt;
            }
        }

    private:
        void expect_jv_type(
            winrt::Windows::Data::Json::IJsonValue const& jv,
            winrt::Windows::Data::Json::JsonValueType expected_type
        ) {
            auto jvt = jv.ValueType();
            if (jvt != expected_type) {
                throw BiliApiParseException(BiliApiParseException::json_parse_wrong_type,
                    m_props_walk,
                    JsonVisitorHelper::stringify(expected_type),
                    JsonVisitorHelper::stringify(jvt)
                );
            }
        }
        template<typename T>
        std::optional<T> try_as_integer(void) {
            expect_jv_type(m_jv, winrt::Windows::Data::Json::JsonValueType::Number);
            auto number = m_jv.GetNumber();
            if (std::trunc(number) != number) {
                return std::nullopt;
            }
            errno = 0;
            long long integer{ std::llround(number) };
            if (auto cur_errno = errno; cur_errno == EDOM || cur_errno == ERANGE) {
                return std::nullopt;
            }
            if constexpr (std::numeric_limits<T>::is_signed) {
                if (integer < std::numeric_limits<T>::min() || integer > std::numeric_limits<T>::max()) {
                    return std::nullopt;
                }
            }
            else {
                if (integer < 0 || static_cast<unsigned long long>(integer) > std::numeric_limits<T>::max()) {
                    return std::nullopt;
                }
            }
            return std::optional{ static_cast<T>(integer) };
        }
        template<typename T>
        T as_integer(void) {
            expect_jv_type(m_jv, winrt::Windows::Data::Json::JsonValueType::Number);
            auto number = m_jv.GetNumber();
            if (std::trunc(number) != number) {
                throw BiliApiParseException(BiliApiParseException::json_parse_wrong_type,
                    m_props_walk,
                    "integer",
                    JsonVisitorHelper::stringify(winrt::Windows::Data::Json::JsonValueType::Number)
                );
            }
            errno = 0;
            long long integer{ std::llround(number) };
            if (auto cur_errno = errno; cur_errno == EDOM || cur_errno == ERANGE) {
                throw BiliApiParseException(BiliApiParseException::json_parse_out_of_range,
                    m_props_walk
                );
            }
            if constexpr (std::numeric_limits<T>::is_signed) {
                if (integer < std::numeric_limits<T>::min() || integer > std::numeric_limits<T>::max()) {
                    throw BiliApiParseException(BiliApiParseException::json_parse_out_of_range, m_props_walk);
                }
            }
            else {
                if (integer < 0 || static_cast<unsigned long long>(integer) > std::numeric_limits<T>::max()) {
                    throw BiliApiParseException(BiliApiParseException::json_parse_out_of_range, m_props_walk);
                }
            }
            return static_cast<T>(integer);
        }

        winrt::Windows::Data::Json::IJsonValue m_jv;
        JsonPropsWalkTree& m_props_walk;
    };

    struct JsonObjectVisitor {
        JsonObjectVisitor(
            winrt::Windows::Data::Json::JsonObject jo,
            JsonPropsWalkTree& props_walk
        ) : m_jo(std::move(jo)), m_props_walk(props_walk) {}

        JsonPropsWalkTree& get_props_walk_tree(void) { return m_props_walk; }

        bool has_key(std::string_view key) {
            return m_jo.HasKey(winrt::to_hstring(key));
        }
        template<typename Functor>
        auto scope(Functor&& func, std::string_view key) {
            using winrt::Windows::Data::Json::JsonValueType;
            m_props_walk.push(key);
            deferred([this] {
                m_props_walk.pop();
            });
            auto jv = expect_jo_lookup(m_jo, key);
            using ParamType = decltype(JsonVisitorHelper::get_fn_param_helper(func, 0, 0, 0));
            if constexpr (std::is_same_v<ParamType, JsonObjectVisitor>) {
                expect_jv_type(jv, JsonValueType::Object);
                return func(JsonObjectVisitor{ jv.GetObject(), m_props_walk });
            }
            else if constexpr (std::is_same_v<ParamType, JsonArrayVisitor>) {
                expect_jv_type(jv, JsonValueType::Array);
                return func(JsonArrayVisitor{ jv.GetArray(), m_props_walk });
            }
            else if constexpr (std::is_same_v<ParamType, JsonValueVisitor>) {
                return func(JsonValueVisitor{ std::move(jv), m_props_walk });
            }
            else {
                static_assert(util::misc::always_false_v<ParamType>, "Incorrect parameter type for functor");
            }
        }
        template<typename Functor>
        auto scope_enumerate(Functor&& func) {
            // TODO: Maybe improve performance for scope_enumerate
            for (auto&& kv : m_jo) {
                this->scope(std::bind(func, kv.Key(), std::placeholders::_1), winrt::to_string(kv.Key()));
            }
        }
        template<typename T>
        void populate_inner(T& dst, JsonValueVisitor jvv) {
            jvv.populate(dst);
        }
        template<typename T>
        void populate(T& dst, std::string_view key) {
            m_props_walk.push(key);
            deferred([this] {
                m_props_walk.pop();
            });
            populate_inner(dst, { expect_jo_lookup(m_jo, key), m_props_walk });
        }
        // NOTE: std::nullopt will be stored only when the property does not exist
        template<typename T>
        void populate(std::optional<T>& dst, std::string_view key) {
            if (auto jv = m_jo.TryLookup(winrt::to_hstring(key))) {
                T temp;
                m_props_walk.push(key);
                deferred([this] {
                    m_props_walk.pop();
                });
                JsonValueVisitor{ jv, m_props_walk }.populate(temp);
                dst = std::move(temp);
            }
            else {
                dst = std::nullopt;
            }
        }

    private:
        winrt::Windows::Data::Json::IJsonValue expect_jo_lookup(
            winrt::Windows::Data::Json::JsonObject const& jo,
            std::string_view key
        ) {
            auto jv = jo.TryLookup(winrt::to_hstring(key));
            if (!jv) {
                throw BiliApiParseException(BiliApiParseException::json_parse_no_prop, m_props_walk);
            }
            return jv;
        }
        void expect_jv_type(
            winrt::Windows::Data::Json::IJsonValue const& jv,
            winrt::Windows::Data::Json::JsonValueType expected_type
        ) {
            auto jvt = jv.ValueType();
            if (jvt != expected_type) {
                throw BiliApiParseException(BiliApiParseException::json_parse_wrong_type,
                    m_props_walk,
                    JsonVisitorHelper::stringify(expected_type),
                    JsonVisitorHelper::stringify(jvt)
                );
            }
        }

        winrt::Windows::Data::Json::JsonObject m_jo;
        JsonPropsWalkTree& m_props_walk;
    };
    struct JsonArrayVisitor {
        JsonArrayVisitor(
            winrt::Windows::Data::Json::JsonArray ja,
            JsonPropsWalkTree& props_walk
        ) : m_ja(std::move(ja)), m_props_walk(props_walk) {}

        JsonPropsWalkTree& get_props_walk_tree(void) { return m_props_walk; }

        size_t size(void) {
            return static_cast<size_t>(m_ja.Size());
        }
        template<typename Functor>
        auto scope(Functor&& func, size_t idx) {
            using winrt::Windows::Data::Json::JsonValueType;
            m_props_walk.push(idx);
            deferred([this] {
                m_props_walk.pop();
            });
            auto jv = expect_ja_get(m_ja, idx);
            using ParamType = decltype(JsonVisitorHelper::get_fn_param_helper(func, 0, 0, 0));
            if constexpr (std::is_same_v<ParamType, JsonObjectVisitor>) {
                expect_jv_type(jv, JsonValueType::Object);
                return func(JsonObjectVisitor{ jv.GetObject(), m_props_walk });
            }
            else if constexpr (std::is_same_v<ParamType, JsonArrayVisitor>) {
                expect_jv_type(jv, JsonValueType::Array);
                return func(JsonArrayVisitor{ jv.GetArray(), m_props_walk });
            }
            else if constexpr (std::is_same_v<ParamType, JsonValueVisitor>) {
                return func(JsonValueVisitor{ std::move(jv), m_props_walk });
            }
            else {
                static_assert(util::misc::always_false_v<ParamType>, "Incorrect parameter type for functor");
            }
        }
        template<typename T, typename Functor>
        auto scope_populate(Functor&& func, std::vector<T>& container) {
            // TODO: Maybe improve performance for scope_populate
            auto size = m_ja.Size();
            container.clear();
            container.resize(size);
            for (decltype(size) i = 0; i < size; i++) {
                this->scope(std::bind(func, std::ref(container[i]), std::placeholders::_1), i);
            }
        }
        template<typename Functor>
        auto scope_enumerate(Functor&& func) {
            // TODO: Maybe improve performance for scope_enumerate
            auto size = m_ja.Size();
            for (decltype(size) i = 0; i < size; i++) {
                this->scope(std::bind(func, static_cast<size_t>(i), std::placeholders::_1), i);
            }
        }
        template<typename T>
        void populate_inner(T& dst, JsonValueVisitor jvv) {
            jvv.populate(dst);
        }
        // NOTE: populate is just an assignment inside scope
        template<typename T>
        void populate(T& dst, size_t idx) {
            m_props_walk.push(idx);
            deferred([this] {
                m_props_walk.pop();
            });
            populate_inner(dst, { expect_ja_get(m_ja, idx), m_props_walk });
        }

    private:
        winrt::Windows::Data::Json::IJsonValue expect_ja_get(
            winrt::Windows::Data::Json::JsonArray const& ja,
            size_t idx
        ) {
            auto size = static_cast<size_t>(ja.Size());
            if (idx >= size) {
                throw BiliApiParseException(BiliApiParseException::json_parse_out_of_bound,
                    m_props_walk,
                    size
                );
            }
            return ja.GetAt(static_cast<uint32_t>(idx));
        }
        void expect_jv_type(
            winrt::Windows::Data::Json::IJsonValue const& jv,
            winrt::Windows::Data::Json::JsonValueType expected_type
        ) {
            auto jvt = jv.ValueType();
            if (jvt != expected_type) {
                throw BiliApiParseException(BiliApiParseException::json_parse_wrong_type,
                    m_props_walk,
                    JsonVisitorHelper::stringify(expected_type),
                    JsonVisitorHelper::stringify(jvt)
                );
            }
        }

        winrt::Windows::Data::Json::JsonArray m_ja;
        JsonPropsWalkTree& m_props_walk;
    };

    template<>
    std::optional<JsonObjectVisitor> JsonValueVisitor::try_as(void) {
        if (m_jv.ValueType() != winrt::Windows::Data::Json::JsonValueType::Object) {
            return std::nullopt;
        }
        return JsonObjectVisitor{ m_jv.GetObject(), m_props_walk };
    }
    template<>
    std::optional<JsonArrayVisitor> JsonValueVisitor::try_as(void) {
        if (m_jv.ValueType() != winrt::Windows::Data::Json::JsonValueType::Array) {
            return std::nullopt;
        }
        return JsonArrayVisitor{ m_jv.GetArray(), m_props_walk };
    }
    template<>
    JsonObjectVisitor JsonValueVisitor::as(void) {
        expect_jv_type(m_jv, winrt::Windows::Data::Json::JsonValueType::Object);
        return { m_jv.GetObject(), m_props_walk };
    }
    template<>
    JsonArrayVisitor JsonValueVisitor::as(void) {
        expect_jv_type(m_jv, winrt::Windows::Data::Json::JsonValueType::Array);
        return { m_jv.GetArray(), m_props_walk };
    }
}

namespace BiliUWP {
    // NOTE: User-defined as / try_as conversions start here
    // WARN: Injected functions should only depend on public interfaces

    // User-defined adapters for member function scope
    // Usage: visitor.scope(adapter{ params }, key);
    namespace adapter {
        struct assign_num_0_1_to_bool {
            assign_num_0_1_to_bool(bool& dst) : m_dst(dst) {}
            void operator()(JsonValueVisitor jvv) {
                auto value = jvv.as<double>();
                if (value == 0) {
                    m_dst = false;
                }
                else if (value == 1) {
                    m_dst = true;
                }
                else {
                    throw BiliApiParseException(BiliApiParseException::json_parse_user_defined,
                        jvv.get_props_walk_tree(), "Only 0 and 1 are expected values"
                    );
                }
            }
        private:
            bool& m_dst;
        };
        // NOTE: -1 => false, 0 => true
        struct assign_num_neg1_0_to_bool {
            assign_num_neg1_0_to_bool(bool& dst) : m_dst(dst) {}
            void operator()(JsonValueVisitor jvv) {
                auto value = jvv.as<double>();
                if (value == -1) {
                    m_dst = false;
                }
                else if (value == 0) {
                    m_dst = true;
                }
                else {
                    throw BiliApiParseException(BiliApiParseException::json_parse_user_defined,
                        jvv.get_props_walk_tree(), "Only -1 and 0 are expected values"
                    );
                }
            }
        private:
            bool& m_dst;
        };
        struct assign_num_to_bool {
            assign_num_to_bool(bool& dst) : m_dst(dst) {}
            void operator()(JsonValueVisitor jvv) {
                m_dst = static_cast<bool>(jvv.as<double>());
            }
        private:
            bool& m_dst;
        };
        // NOTE: A convenience helper for calling populate inside scope_populate
        template<typename T>
        struct assign_vec {
            assign_vec(std::vector<T>& vec) : m_vec(vec) {}
            void operator()(JsonArrayVisitor jav) {
                jav.scope_populate([&](T& dst, JsonValueVisitor jvv) {
                    dst = jvv.as<T>();
                }, m_vec);
            }
        private:
            std::vector<T>& m_vec;
        };
        // NOTE: Same as assign_vec, except that null is interpreted as an empty array
        template<typename T>
        struct assign_vec_or_null_as_empty {
            assign_vec_or_null_as_empty(std::vector<T>& vec) : m_vec(vec) {}
            void operator()(JsonValueVisitor jav) {
                if (jav.is_null()) {
                    m_vec.clear();
                }
                else {
                    jav.as<JsonArrayVisitor>().scope_populate([&](T& dst, JsonValueVisitor jvv) {
                        dst = jvv.as<T>();
                    }, m_vec);
                }
            }
        private:
            std::vector<T>& m_vec;
        };
        // NOTE: It always expects that property exists
        template<typename T>
        struct assign_value_or_null_to_optional {
            assign_value_or_null_to_optional(std::optional<T>& opt) : m_opt(opt) {}
            void operator()(JsonValueVisitor jav) {
                if (jav.is_null()) {
                    m_opt = std::nullopt;
                }
                else {
                    T temp;
                    jav.populate(temp);
                    m_opt = std::move(temp);
                }
            }
        private:
            std::optional<T>& m_opt;
        };
        template<typename T, std::enable_if_t<std::is_enum_v<T>, int> = 0>
        struct assign_num_to_enum {
            assign_num_to_enum(T& dst) : m_dst(dst) {}
            void operator()(JsonValueVisitor jvv) {
                m_dst = static_cast<T>(jvv.as<std::underlying_type_t<T>>());
            }
        private:
            T& m_dst;
        };
    }

    // User-defined as / try_as conversions
    template<>
    VideoViewInfo_Dimension JsonValueVisitor::as(void) {
        VideoViewInfo_Dimension result;
        auto jov = this->as<JsonObjectVisitor>();
        jov.populate(result.width, "width");
        jov.populate(result.height, "height");
        jov.scope(adapter::assign_num_0_1_to_bool{ result.rotate }, "rotate");
        return result;
    }
    template<>
    VideoViewInfo_DescV2 JsonValueVisitor::as(void) {
        VideoViewInfo_DescV2 result;
        auto jov = this->as<JsonObjectVisitor>();
        jov.populate(result.raw_text, "raw_text");
        jov.populate(result.type, "type");
        jov.populate(result.biz_id, "biz_id");
        return result;
    }
    template<>
    VideoViewInfo_Page JsonValueVisitor::as(void) {
        VideoViewInfo_Page result;
        auto jov = this->as<JsonObjectVisitor>();
        jov.populate(result.cid, "cid");
        jov.populate(result.page, "page");
        jov.populate(result.from, "from");
        jov.populate(result.part_title, "part");
        jov.populate(result.duration, "duration");
        jov.populate(result.external_vid, "vid");
        jov.populate(result.external_weblink, "weblink");
        jov.populate(result.dimension, "dimension");
        return result;
    }
    template<>
    VideoViewInfo_Subtitle JsonValueVisitor::as(void) {
        VideoViewInfo_Subtitle result;
        auto jov = this->as<JsonObjectVisitor>();
        jov.populate(result.id, "id");
        jov.populate(result.language, "lan");
        jov.populate(result.language_doc, "lan_doc");
        jov.populate(result.locked, "is_lock");
        jov.populate(result.subtitle_url, "subtitle_url");
        jov.scope([&](JsonObjectVisitor jov) {
            jov.populate(result.author.mid, "mid");
            jov.populate(result.author.name, "name");
            jov.populate(result.author.sex, "sex");
            jov.populate(result.author.face_url, "face");
            jov.populate(result.author.sign, "sign");
        }, "author");
        return result;
    }
    template<>
    VideoViewInfo_Staff JsonValueVisitor::as(void) {
        VideoViewInfo_Staff result;
        auto jov = this->as<JsonObjectVisitor>();
        jov.populate(result.mid, "mid");
        jov.populate(result.title, "title");
        jov.populate(result.name, "name");
        jov.populate(result.face_url, "face");
        jov.scope([&](JsonObjectVisitor jov) {
            jov.populate(result.vip.type, "type");
            jov.scope(adapter::assign_num_0_1_to_bool{ result.vip.is_vip }, "status");
        }, "vip");
        jov.scope([&](JsonObjectVisitor jov) {
            jov.populate(result.official.role, "role");
            jov.populate(result.official.title, "title");
            jov.populate(result.official.desc, "desc");
            jov.populate(result.official.type, "type");
        }, "official");
        jov.populate(result.follower_count, "follower");
        return result;
    }
    template<>
    VideoInfoV2_Subtitle JsonValueVisitor::as(void) {
        VideoInfoV2_Subtitle result;
        auto jov = this->as<JsonObjectVisitor>();
        jov.populate(result.id, "id");
        jov.populate(result.language, "lan");
        jov.populate(result.language_doc, "lan_doc");
        jov.populate(result.locked, "is_lock");
        jov.populate(result.subtitle_url, "subtitle_url");
        if (result.subtitle_url.starts_with(L"//")) {
            result.subtitle_url = L"https:" + result.subtitle_url;
        }
        return result;
    }
    template<>
    VideoPlayUrl_DurlPart JsonValueVisitor::as(void) {
        VideoPlayUrl_DurlPart result;
        auto jov = this->as<JsonObjectVisitor>();
        jov.populate(result.order, "order");
        jov.populate(result.length, "length");
        jov.populate(result.size, "size");
        jov.populate(result.ahead, "ahead");
        jov.populate(result.vhead, "vhead");
        jov.populate(result.url, "url");
        jov.scope(adapter::assign_vec_or_null_as_empty{ result.backup_url }, "backup_url");
        return result;
    }
    template<>
    VideoPlayUrl_Dash_Stream JsonValueVisitor::as(void) {
        VideoPlayUrl_Dash_Stream result;
        auto jov = this->as<JsonObjectVisitor>();
        jov.populate(result.id, "id");
        jov.populate(result.base_url, "base_url");
        jov.scope(adapter::assign_vec_or_null_as_empty{ result.backup_url }, "backup_url");
        jov.populate(result.bandwidth, "bandwidth");
        jov.populate(result.mime_type, "mime_type");
        jov.populate(result.codecs, "codecs");
        jov.populate(result.width, "width");
        jov.populate(result.height, "height");
        jov.populate(result.frame_rate, "frame_rate");
        jov.populate(result.sar, "sar");
        jov.populate(result.start_with_sap, "start_with_sap");
        jov.scope([&](JsonObjectVisitor jov) {
            jov.populate(result.segment_base.initialization, "initialization");
            jov.populate(result.segment_base.index_range, "index_range");
        }, "segment_base");
        jov.populate(result.codecid, "codecid");
        return result;
    }
    template<>
    VideoPlayUrl_Dash JsonValueVisitor::as(void) {
        VideoPlayUrl_Dash result;
        auto jov = this->as<JsonObjectVisitor>();
        jov.populate(result.duration, "duration");
        jov.populate(result.min_buffer_time, "min_buffer_time");
        jov.populate(result.video, "video");
        jov.scope(adapter::assign_vec_or_null_as_empty{ result.audio }, "audio");
        return result;
    }
    template<>
    VideoPlayUrl_SupportFormat JsonValueVisitor::as(void) {
        VideoPlayUrl_SupportFormat result;
        auto jov = this->as<JsonObjectVisitor>();
        jov.populate(result.quality, "quality");
        jov.populate(result.format, "format");
        jov.populate(result.new_description, "new_description");
        jov.populate(result.display_desc, "display_desc");
        jov.populate(result.superscript, "superscript");
        jov.scope(adapter::assign_value_or_null_to_optional{ result.codecs }, "codecs");
        return result;
    }
    template<>
    AudioPlayUrl_Quality JsonValueVisitor::as(void) {
        AudioPlayUrl_Quality result;
        auto jov = this->as<JsonObjectVisitor>();
        jov.scope(adapter::assign_num_to_enum{ result.type }, "type");
        jov.populate(result.desc, "desc");
        jov.populate(result.size, "size");
        jov.populate(result.bps, "bps");
        jov.populate(result.tag, "tag");
        jov.scope(adapter::assign_num_0_1_to_bool{ result.require_membership }, "require");
        jov.populate(result.require_membership_desc, "requiredesc");
        return result;
    }
    template<>
    UserFavFoldersList_Folder JsonValueVisitor::as(void) {
        UserFavFoldersList_Folder result;
        auto jov = this->as<JsonObjectVisitor>();
        jov.populate(result.id, "id");
        jov.populate(result.fid, "fid");
        jov.populate(result.mid, "mid");
        jov.populate(result.attr, "attr");
        jov.populate(result.title, "title");
        jov.populate(result.cover_url, "cover");
        jov.populate(result.cover_type, "cover_type");
        jov.populate(result.intro, "intro");
        jov.populate(result.ctime, "ctime");
        jov.populate(result.mtime, "mtime");
        jov.scope(adapter::assign_num_0_1_to_bool{ result.fav_has_item }, "fav_state");
        jov.populate(result.media_count, "media_count");
        return result;
    }
    template<>
    UserFavFoldersListAll_Folder JsonValueVisitor::as(void) {
        UserFavFoldersListAll_Folder result;
        auto jov = this->as<JsonObjectVisitor>();
        jov.populate(result.id, "id");
        jov.populate(result.fid, "fid");
        jov.populate(result.mid, "mid");
        jov.populate(result.attr, "attr");
        jov.populate(result.title, "title");
        jov.scope(adapter::assign_num_0_1_to_bool{ result.fav_has_item }, "fav_state");
        jov.populate(result.media_count, "media_count");
        return result;
    }
    template<>
    FavFolderResList_Media JsonValueVisitor::as(void) {
        FavFolderResList_Media result;
        auto jov = this->as<JsonObjectVisitor>();
        jov.populate(result.nid, "id");
        jov.scope(adapter::assign_num_to_enum{ result.type }, "type");
        jov.populate(result.title, "title");
        jov.populate(result.cover_url, "cover");
        jov.populate(result.intro, "intro");
        jov.populate(result.page_count, "page");
        jov.populate(result.duration, "duration");
        jov.scope([&](JsonObjectVisitor jov) {
            jov.populate(result.upper.mid, "mid");
            jov.populate(result.upper.name, "name");
            jov.populate(result.upper.face_url, "face");
        }, "upper");
        jov.populate(result.attr, "attr");
        jov.scope([&](JsonObjectVisitor jov) {
            jov.populate(result.cnt_info.favourite_count, "collect");
            jov.populate(result.cnt_info.play_count, "play");
            jov.populate(result.cnt_info.danmaku_count, "danmaku");
        }, "cnt_info");
        jov.populate(result.res_link, "link");
        jov.populate(result.ctime, "ctime");
        jov.populate(result.pubtime, "pubtime");
        jov.populate(result.fav_time, "fav_time");
        jov.populate(result.bvid, "bvid");
        return result;
    }
    template<>
    UserSpaceInfo_FansMedal_Medal JsonValueVisitor::as(void) {
        UserSpaceInfo_FansMedal_Medal result;
        auto jov = this->as<JsonObjectVisitor>();
        jov.populate(result.uid, "uid");
        jov.populate(result.target_uid, "target_id");
        jov.populate(result.medal_id, "medal_id");
        jov.populate(result.level, "level");
        jov.populate(result.medal_name, "medal_name");
        jov.populate(result.medal_color, "medal_color");
        jov.populate(result.cur_intimacy, "intimacy");
        jov.populate(result.next_intimacy, "next_intimacy");
        jov.populate(result.intimacy_day_limit, "day_limit");
        jov.populate(result.medal_color_start, "medal_color_start");
        jov.populate(result.medal_color_end, "medal_color_end");
        jov.populate(result.medal_color_border, "medal_color_border");
        jov.scope(
            adapter::assign_num_0_1_to_bool{ result.is_lighted }, "is_lighted");
        jov.populate(result.light_status, "light_status");
        jov.populate(result.wearing_status, "wearing_status");
        jov.populate(result.score, "score");
        return result;
    }
    template<>
    UserSpaceInfo_LiveRoom JsonValueVisitor::as(void) {
        UserSpaceInfo_LiveRoom result;
        auto jov = this->as<JsonObjectVisitor>();
        jov.populate(result.room_status, "roomStatus");
        jov.populate(result.live_status, "liveStatus");
        jov.populate(result.room_url, "url");
        jov.populate(result.room_title, "title");
        jov.populate(result.room_cover_url, "cover");
        jov.scope([&](JsonObjectVisitor jov) {
            jov.populate(result.watched_show.is_switched, "switch");
            jov.populate(result.watched_show.total_watched_users, "num");
            jov.populate(result.watched_show.text_small, "text_small");
            jov.populate(result.watched_show.text_large, "text_large");
            jov.populate(result.watched_show.icon_url, "icon");
            jov.populate(result.watched_show.icon_location, "icon_location");
            jov.populate(result.watched_show.icon_web_url, "icon_web");
        }, "watched_show");
        jov.populate(result.room_id, "roomid");
        jov.scope(adapter::assign_num_0_1_to_bool{ result.is_rounding }, "roundStatus");
        jov.populate(result.broadcast_type, "broadcast_type");
        return result;
    }
    template<>
    UserSpaceInfo_School JsonValueVisitor::as(void) {
        UserSpaceInfo_School result;
        auto jov = this->as<JsonObjectVisitor>();
        jov.populate(result.name, "name");
        return result;
    }
    template<>
    UserSpacePublishedVideos_Video JsonValueVisitor::as(void) {
        UserSpacePublishedVideos_Video result;
        auto jov = this->as<JsonObjectVisitor>();
        jov.populate(result.avid, "aid");
        jov.populate(result.author, "author");
        jov.populate(result.bvid, "bvid");
        jov.populate(result.comment_count, "comment");
        jov.populate(result.copyright, "copyright");
        jov.populate(result.publish_time, "created");
        jov.populate(result.description, "description");
        jov.scope(adapter::assign_num_0_1_to_bool{ result.is_pay }, "is_pay");
        jov.scope(adapter::assign_num_0_1_to_bool{ result.is_union_video }, "is_union_video");
        jov.populate(result.length_str, "length");
        jov.populate(result.mid, "mid");
        jov.populate(result.cover_url, "pic");
        jov.scope([&](JsonValueVisitor jvv) {
            if (jvv.try_as<winrt::hstring>() == L"--") {
                result.play_count = std::nullopt;
            }
            else {
                jvv.populate(result.play_count);
            }
        }, "play");
        //jov.populate(result.play_count, "play");
        jov.populate(result.review, "review");
        jov.populate(result.subtitle, "subtitle");
        jov.populate(result.title, "title");
        jov.populate(result.tid, "typeid");
        jov.populate(result.danmaku_count, "video_review");
        return result;
    }
    template<>
    UserSpacePublishedVideos_EpisodicButton JsonValueVisitor::as(void) {
        UserSpacePublishedVideos_EpisodicButton result;
        auto jov = this->as<JsonObjectVisitor>();
        jov.populate(result.text, "text");
        jov.populate(result.uri, "uri");
        return result;
    }
    template<>
    UserSpacePublishedAudios_Audio JsonValueVisitor::as(void) {
        UserSpacePublishedAudios_Audio result;
        auto jov = this->as<JsonObjectVisitor>();
        jov.populate(result.id, "id");
        jov.populate(result.mid, "uid");
        jov.populate(result.uname, "uname");
        jov.populate(result.title, "title");
        jov.populate(result.cover_url, "cover");
        jov.populate(result.duration, "duration");
        jov.populate(result.coin_count, "coin_num");
        jov.populate(result.passtime, "passtime");
        jov.populate(result.ctime, "ctime");
        jov.scope([&](JsonObjectVisitor jov) {
            jov.populate(result.statistics.sid, "sid");
            jov.populate(result.statistics.play_count, "play");
            jov.populate(result.statistics.favourite_count, "collect");
            jov.populate(result.statistics.comment_count, "comment");
            jov.populate(result.statistics.share_count, "share");
        }, "statistic");
        return result;
    }

    BiliClient::BiliClient() :
        m_bili_client(winrt::BiliUWP::BiliClientManaged()), m_refresh_token() {}

    winrt::hstring BiliClient::get_access_token(void) {
        return m_bili_client.data_access_token();
    }
    void BiliClient::set_access_token(winrt::hstring const& value) {
        m_bili_client.data_access_token(value);
    }
    winrt::hstring BiliClient::get_refresh_token(void) {
        return m_refresh_token;
    }
    void BiliClient::set_refresh_token(winrt::hstring const& value) {
        m_refresh_token = value;
    }
    winrt::BiliUWP::UserCookies BiliClient::get_cookies(void) {
        return m_bili_client.data_cookies();
    }
    void BiliClient::set_cookies(winrt::BiliUWP::UserCookies const& value) {
        m_bili_client.data_cookies(value);
    }
    winrt::BiliUWP::APISignKeys BiliClient::get_api_sign_keys(void) {
        return m_api_sign_keys;
    }
    void BiliClient::set_api_sign_keys(winrt::BiliUWP::APISignKeys const& value) {
        m_api_sign_keys = value;
    }

    // Authentication
    util::winrt::task<RequestTvQrLoginResult> BiliClient::request_tv_qr_login(winrt::guid local_id) {
        RequestTvQrLoginResult result;

        auto cancellation_token = co_await winrt::get_cancellation_token();
        cancellation_token.enable_propagation();

        auto jo = co_await m_bili_client.api_passport_x_passport_tv_login_qrcode_auth_code(
            keys::api_tv_1, local_id
        );
        util::debug::log_trace(std::format(L"Parsing JSON: {}", jo.Stringify()));
        check_json_code(jo);
        JsonPropsWalkTree json_props_walk;
        JsonObjectVisitor jov{ std::move(jo), json_props_walk };
        jov.scope([&](JsonObjectVisitor jov) {
            jov.populate(result.url, "url");
            jov.populate(result.auth_code, "auth_code");
        }, "data");

        co_return result;
    }
    util::winrt::task<PollTvQrLoginResult> BiliClient::poll_tv_qr_login(
        winrt::hstring auth_code,
        winrt::guid local_id
    ) {
        PollTvQrLoginResult result{};

        auto cancellation_token = co_await winrt::get_cancellation_token();
        cancellation_token.enable_propagation();

        auto jo = co_await m_bili_client.api_passport_x_passport_tv_login_qrcode_poll(
            keys::api_tv_1, auth_code, local_id
        );
        util::debug::log_trace(std::format(L"Parsing JSON: {}", jo.Stringify()));
        JsonPropsWalkTree json_props_walk;
        JsonObjectVisitor jov{ std::move(jo), json_props_walk };
        double code;
        jov.populate(code, "code");
        result.code = static_cast<ApiCode>(code);
        switch (result.code) {
        case ApiCode::TvQrLogin_QrExpired:
        case ApiCode::TvQrLogin_QrNotConfirmed:
            // These codes should not be treated as errors
            break;
        default:
            check_api_code(result.code);
        }
        if (result.code == ApiCode::Success) {
            jov.scope([&](JsonObjectVisitor jov) {
                jov.populate(result.mid, "mid");
                jov.populate(result.access_token, "access_token");
                jov.populate(result.refresh_token, "refresh_token");
                jov.populate(result.expires_in, "expires_in");
                jov.scope([&](JsonObjectVisitor jov) {
                    jov.scope([&](JsonArrayVisitor jav) {
                        jav.scope_enumerate([&](size_t, JsonObjectVisitor jov) {
                            winrt::hstring name;
                            jov.populate(name, "name");
                            if (name == L"SESSDATA") {
                                jov.populate(result.user_cookies.SESSDATA, "value");
                            }
                            else if (name == L"bili_jct") {
                                jov.populate(result.user_cookies.bili_jct, "value");
                            }
                            else if (name == L"DedeUserID") {
                                jov.populate(result.user_cookies.DedeUserID, "value");
                            }
                            else if (name == L"DedeUserID__ckMd5") {
                                jov.populate(result.user_cookies.DedeUserID__ckMd5, "value");
                            }
                            else if (name == L"sid") {
                                jov.populate(result.user_cookies.sid, "value");
                            }
                        });
                    }, "cookies");
                }, "cookie_info");
            }, "data");

            // Update self data
            this->set_access_token(result.access_token);
            this->set_refresh_token(result.refresh_token);
            this->set_cookies(result.user_cookies);
            this->set_api_sign_keys(keys::api_tv_1);
        }

        co_return result;
    }
    util::winrt::task<Oauth2RefreshTokenResult> BiliClient::oauth2_refresh_token(void) {
        Oauth2RefreshTokenResult result;

        auto cancellation_token = co_await winrt::get_cancellation_token();
        cancellation_token.enable_propagation();

        auto jo = co_await m_bili_client.api_passport_api_v2_oauth2_refresh_token(
            m_api_sign_keys, m_refresh_token
        );
        util::debug::log_trace(std::format(L"Parsing JSON: {}", jo.Stringify()));
        check_json_code(jo);
        JsonPropsWalkTree json_props_walk;
        JsonObjectVisitor jov{ std::move(jo), json_props_walk };
        jov.scope([&](JsonObjectVisitor jov) {
            jov.scope([&](JsonObjectVisitor jov) {
                jov.populate(result.mid, "mid");
                jov.populate(result.access_token, "access_token");
                jov.populate(result.refresh_token, "refresh_token");
                jov.populate(result.expires_in, "expires_in");
            }, "token_info");
            jov.scope([&](JsonObjectVisitor jov) {
                jov.scope([&](JsonArrayVisitor jav) {
                    jav.scope_enumerate([&](size_t, JsonObjectVisitor jov) {
                        winrt::hstring name;
                        jov.populate(name, "name");
                        if (name == L"SESSDATA") {
                            jov.populate(result.user_cookies.SESSDATA, "value");
                        }
                        else if (name == L"bili_jct") {
                            jov.populate(result.user_cookies.bili_jct, "value");
                        }
                        else if (name == L"DedeUserID") {
                            jov.populate(result.user_cookies.DedeUserID, "value");
                        }
                        else if (name == L"DedeUserID__ckMd5") {
                            jov.populate(result.user_cookies.DedeUserID__ckMd5, "value");
                        }
                        else if (name == L"sid") {
                            jov.populate(result.user_cookies.sid, "value");
                        }
                    });
                }, "cookies");
            }, "cookie_info");
        }, "data");

        // Update self data
        this->set_access_token(result.access_token);
        this->set_refresh_token(result.refresh_token);
        this->set_cookies(result.user_cookies);

        co_return result;
    }
    util::winrt::task<RevokeLoginResult> BiliClient::revoke_login(void) {
        // TODO: Fix BiliClient::revoke_login
        RevokeLoginResult result;

        auto cancellation_token = co_await winrt::get_cancellation_token();
        cancellation_token.enable_propagation();

        auto jo = co_await m_bili_client.api_passport_x_passport_login_revoke(keys::api_android_1);
        util::debug::log_trace(std::format(L"Parsing JSON: {}", jo.Stringify()));
        check_json_code(jo);
        JsonPropsWalkTree json_props_walk;
        JsonObjectVisitor jov{ std::move(jo), json_props_walk };
        // TODO: Breakpoint here to verify actual JSON response

        // Update self data
        this->set_access_token(L"");
        this->set_refresh_token(L"");
        this->set_cookies({});

        co_return result;
    }

    // User information
    util::winrt::task<MyAccountNavInfoResult> BiliClient::my_account_nav_info(void) {
        MyAccountNavInfoResult result;

        auto cancellation_token = co_await winrt::get_cancellation_token();
        cancellation_token.enable_propagation();

        auto jo = co_await m_bili_client.api_api_x_web_interface_nav();
        util::debug::log_trace(std::format(L"Parsing JSON: {}", jo.Stringify()));
        check_json_code(jo);
        JsonPropsWalkTree json_props_walk;
        JsonObjectVisitor jov{ std::move(jo), json_props_walk };
        jov.scope([&](JsonObjectVisitor jov) {
            jov.populate(result.logged_in, "isLogin");
            jov.scope(adapter::assign_num_0_1_to_bool{ result.email_verified }, "email_verified");
            //jov.populate(result.email_verified, "email_verified");
            jov.populate(result.face_url, "face");
            jov.scope([&](JsonObjectVisitor jov) {
                jov.populate(result.level_info.current_level, "current_level");
                jov.populate(result.level_info.current_min, "current_min");
                jov.populate(result.level_info.current_exp, "current_exp");
                jov.populate(result.level_info.next_exp, "next_exp");
            }, "level_info");
            jov.populate(result.mid, "mid");
            // TODO: Use assign_num_0_1_to_bool for mobile_verified
            jov.populate(result.mobile_verified, "mobile_verified");
            jov.populate(result.coin_count, "money");
            jov.populate(result.moral, "moral");
            jov.scope([&](JsonObjectVisitor jov) {
                jov.populate(result.official.role, "role");
                jov.populate(result.official.title, "title");
                jov.populate(result.official.desc, "desc");
                jov.populate(result.official.type, "type");
            }, "official");
            jov.scope([&](JsonObjectVisitor jov) {
                jov.populate(result.official_verify.type, "type");
                jov.populate(result.official_verify.desc, "desc");
            }, "officialVerify");
            jov.scope([&](JsonObjectVisitor jov) {
                jov.populate(result.pendant.pid, "pid");
                jov.populate(result.pendant.name, "name");
                jov.populate(result.pendant.image, "image");
            }, "pendant");
            jov.populate(result.uname, "uname");
            jov.populate(result.vip_due_date, "vipDueDate");
            // TODO: Use assign_num_0_1_to_bool for vipStatus
            jov.populate(result.is_vip, "vipStatus");
            jov.populate(result.vip_type, "vipType");
            jov.scope([&](JsonObjectVisitor jov) {
                jov.populate(result.vip_label.text, "text");
                jov.populate(result.vip_label.label_theme, "label_theme");
            }, "vip_label");
            // TODO: Use assign_num_0_1_to_bool for vip_avatar_subscript
            jov.populate(result.vip_avatar_subscript, "vip_avatar_subscript");
            jov.populate(result.vip_nickname_color, "vip_nickname_color");
            jov.scope([&](JsonObjectVisitor jov) {
                jov.populate(result.wallet.bcoin_balance, "bcoin_balance");
                jov.populate(result.wallet.coupon_balance, "coupon_balance");
            }, "wallet");
            jov.populate(result.has_shop, "has_shop");
            jov.populate(result.shop_url, "shop_url");
        }, "data");

        co_return result;
    }
    util::winrt::task<MyAccountNavStatInfoResult> BiliClient::my_account_nav_stat_info(void) {
        MyAccountNavStatInfoResult result;

        auto cancellation_token = co_await winrt::get_cancellation_token();
        cancellation_token.enable_propagation();

        auto jo = co_await m_bili_client.api_api_x_web_interface_nav_stat();
        util::debug::log_trace(std::format(L"Parsing JSON: {}", jo.Stringify()));
        check_json_code(jo);
        JsonPropsWalkTree json_props_walk;
        JsonObjectVisitor jov{ std::move(jo), json_props_walk };
        jov.scope([&](JsonObjectVisitor jov) {
            jov.populate(result.following_count, "following");
            jov.populate(result.follower_count, "follower");
            jov.populate(result.dynamic_count, "dynamic");
        }, "data");

        co_return result;
    }
    util::winrt::task<UserCardInfoResult> BiliClient::user_card_info(uint64_t mid) {
        UserCardInfoResult result;

        auto cancellation_token = co_await winrt::get_cancellation_token();
        cancellation_token.enable_propagation();

        auto jo = co_await m_bili_client.api_api_x_web_interface_card(mid, true);
        util::debug::log_trace(std::format(L"Parsing JSON: {}", jo.Stringify()));
        check_json_code(jo);
        JsonPropsWalkTree json_props_walk;
        JsonObjectVisitor jov{ std::move(jo), json_props_walk };
        jov.scope([&](JsonObjectVisitor jov) {
            jov.scope([&](JsonObjectVisitor jov) {
                jov.populate(result.card.mid, "mid");
                jov.populate(result.card.name, "name");
                jov.populate(result.card.sex, "sex");
                jov.populate(result.card.face_url, "face");
                jov.populate(result.card.follower_count, "fans");
                jov.populate(result.card.following_count, "friend");
                jov.scope([&](JsonObjectVisitor jov) {
                    jov.populate(result.card.level_info.current_level, "current_level");
                }, "level_info");
                jov.scope([&](JsonObjectVisitor jov) {
                    jov.populate(result.card.pendant.pid, "pid");
                    jov.populate(result.card.pendant.name, "name");
                    jov.populate(result.card.pendant.image_url, "image");
                }, "pendant");
                jov.scope([&](JsonObjectVisitor jov) {
                    jov.populate(result.card.nameplate.nid, "nid");
                    jov.populate(result.card.nameplate.name, "name");
                    jov.populate(result.card.nameplate.image_url, "image");
                    jov.populate(result.card.nameplate.image_small_url, "image_small");
                    jov.populate(result.card.nameplate.level, "level");
                    jov.populate(result.card.nameplate.condition, "condition");
                }, "nameplate");
                jov.scope([&](JsonObjectVisitor jov) {
                    jov.populate(result.card.official.role, "role");
                    jov.populate(result.card.official.title, "title");
                    jov.populate(result.card.official.desc, "desc");
                    jov.populate(result.card.official.type, "type");
                }, "Official");
                jov.scope([&](JsonObjectVisitor jov) {
                    jov.populate(result.card.official_verify.type, "type");
                    jov.populate(result.card.official_verify.desc, "desc");
                }, "official_verify");
                jov.scope([&](JsonObjectVisitor jov) {
                    jov.populate(result.card.vip.type, "vipType");
                    jov.scope(adapter::assign_num_0_1_to_bool{ result.card.vip.is_vip }, "vipStatus");
                }, "vip");
            }, "card");
            jov.scope([&](JsonObjectVisitor jov) {
                jov.populate(result.space.small_img_url, "s_img");
                jov.populate(result.space.large_img_url, "l_img");
            }, "space");
            jov.populate(result.is_following, "following");
            jov.populate(result.posts_count, "archive_count");
            jov.populate(result.like_count, "like_num");
        }, "data");

        co_return result;
    }
    util::winrt::task<UserSpaceInfoResult> BiliClient::user_space_info(uint64_t mid) {
        UserSpaceInfoResult result;

        auto cancellation_token = co_await winrt::get_cancellation_token();
        cancellation_token.enable_propagation();

        auto jo = co_await m_bili_client.api_api_x_space_acc_info(mid);
        util::debug::log_trace(std::format(L"Parsing JSON: {}", jo.Stringify()));
        check_json_code(jo);
        JsonPropsWalkTree json_props_walk;
        JsonObjectVisitor jov{ std::move(jo), json_props_walk };
        jov.scope([&](JsonObjectVisitor jov) {
            jov.populate(result.mid, "mid");
            jov.populate(result.name, "name");
            jov.populate(result.sex, "sex");
            jov.populate(result.face_url, "face");
            jov.scope(adapter::assign_num_0_1_to_bool{ result.is_face_nft }, "face_nft");
            jov.populate(result.sign, "sign");
            jov.populate(result.level, "level");
            jov.scope(adapter::assign_num_0_1_to_bool{ result.is_silenced }, "silence");
            jov.populate(result.coin_count, "coins");
            jov.populate(result.has_fans_badge, "fans_badge");
            jov.scope([&](JsonObjectVisitor jov) {
                jov.populate(result.fans_medal.show, "show");
                jov.populate(result.fans_medal.wear, "wear");
                jov.scope(adapter::assign_value_or_null_to_optional{ result.fans_medal.medal }, "medal");
            }, "fans_medal");
            jov.scope([&](JsonObjectVisitor jov) {
                jov.populate(result.official.role, "role");
                jov.populate(result.official.title, "title");
                jov.populate(result.official.desc, "desc");
                jov.populate(result.official.type, "type");
            }, "official");
            jov.scope([&](JsonObjectVisitor jov) {
                jov.populate(result.vip.type, "type");
                jov.scope(adapter::assign_num_0_1_to_bool{ result.vip.is_vip }, "status");
                jov.populate(result.vip.due_date, "due_date");
                jov.populate(result.vip.vip_pay_type, "vip_pay_type");
                jov.scope([&](JsonObjectVisitor jov) {
                    jov.populate(result.vip.label.path, "path");
                    jov.populate(result.vip.label.text, "text");
                    jov.populate(result.vip.label.label_theme, "label_theme");
                    jov.populate(result.vip.label.text_color, "text_color");
                    jov.populate(result.vip.label.bg_style, "bg_style");
                    jov.populate(result.vip.label.bg_color, "bg_color");
                    jov.populate(result.vip.label.border_color, "border_color");
                }, "label");
                jov.scope(
                    adapter::assign_num_0_1_to_bool{ result.vip.show_avatar_subscript }, "avatar_subscript");
                jov.populate(result.vip.nickname_color, "nickname_color");
                jov.populate(result.vip.role, "role");
                jov.populate(result.vip.avatar_subscript_url, "avatar_subscript_url");
            }, "vip");
            jov.scope([&](JsonObjectVisitor jov) {
                jov.populate(result.pendant.pid, "pid");
                jov.populate(result.pendant.name, "name");
                jov.populate(result.pendant.image_url, "image");
                jov.populate(result.pendant.expire, "expire");
                jov.populate(result.pendant.image_enhance, "image_enhance");
                jov.populate(result.pendant.image_enhance_frame, "image_enhance_frame");
            }, "pendant");
            jov.scope([&](JsonObjectVisitor jov) {
                jov.populate(result.nameplate.nid, "nid");
                jov.populate(result.nameplate.name, "name");
                jov.populate(result.nameplate.image_url, "image");
                jov.populate(result.nameplate.image_small_url, "image_small");
                jov.populate(result.nameplate.level, "level");
                jov.populate(result.nameplate.condition, "condition");
            }, "nameplate");
            jov.scope([&](JsonObjectVisitor jov) {
                jov.populate(result.user_honour_info.mid, "mid");
                jov.scope(adapter::assign_value_or_null_to_optional{ result.user_honour_info.colour }, "colour");
                jov.scope(adapter::assign_vec_or_null_as_empty{ result.user_honour_info.tags }, "tags");
            }, "user_honour_info");
            jov.populate(result.is_followed, "is_followed");
            jov.populate(result.top_photo_url, "top_photo");
            jov.scope([&](JsonObjectVisitor jov) {
                if (!jov.has_key("id")) {
                    result.sys_notice = std::nullopt;
                    return;
                }
                UserSpaceInfo_SysNotice result_sn;
                jov.populate(result_sn.id, "id");
                jov.populate(result_sn.content, "content");
                jov.populate(result_sn.url, "url");
                jov.populate(result_sn.notice_type, "notice_type");
                jov.populate(result_sn.icon, "icon");
                jov.populate(result_sn.text_color, "text_color");
                jov.populate(result_sn.bg_color, "bg_color");
                result.sys_notice = std::move(result_sn);
            }, "sys_notice");
            jov.scope(adapter::assign_value_or_null_to_optional{ result.live_room }, "live_room");
            jov.populate(result.birthday, "birthday");
            jov.scope(adapter::assign_value_or_null_to_optional{ result.school }, "school");
            jov.scope([&](JsonObjectVisitor jov) {
                jov.populate(result.profession.name, "name");
                jov.populate(result.profession.department, "department");
                jov.populate(result.profession.title, "title");
                jov.scope(adapter::assign_num_0_1_to_bool{ result.profession.is_show }, "is_show");
            }, "profession");
            jov.scope([&](JsonObjectVisitor jov) {
                jov.populate(result.series.user_upgrade_status, "user_upgrade_status");
                jov.populate(result.series.show_upgrade_window, "show_upgrade_window");
            }, "series");
            jov.scope(adapter::assign_num_0_1_to_bool{ result.is_senior_member }, "is_senior_member");
        }, "data");

        co_return result;
    }
    util::winrt::task<UserSpaceUpStatInfoResult> BiliClient::user_space_upstat_info(uint64_t mid) {
        UserSpaceUpStatInfoResult result;

        auto cancellation_token = co_await winrt::get_cancellation_token();
        cancellation_token.enable_propagation();

        auto jo = co_await m_bili_client.api_api_x_space_upstat(mid);
        util::debug::log_trace(std::format(L"Parsing JSON: {}", jo.Stringify()));
        check_json_code(jo);
        JsonPropsWalkTree json_props_walk;
        JsonObjectVisitor jov{ std::move(jo), json_props_walk };
        jov.scope([&](JsonObjectVisitor jov) {
            jov.scope([&](JsonObjectVisitor jov) {
                jov.populate(result.archive.view_count, "view");
            }, "archive");
            jov.scope([&](JsonObjectVisitor jov) {
                jov.populate(result.article.view_count, "view");
            }, "article");
            jov.populate(result.like_count, "likes");
        }, "data");

        co_return result;
    }
    util::winrt::task<UserSpacePublishedVideosResult> BiliClient::user_space_published_videos(
        uint64_t mid, PageParam page, winrt::hstring search_keyword, UserPublishedVideosOrderParam order
    ) {
        using winrt::BiliUWP::ApiParam_SpaceArcSearchOrder;

        UserSpacePublishedVideosResult result;

        auto cancellation_token = co_await winrt::get_cancellation_token();
        cancellation_token.enable_propagation();

        ApiParam_SpaceArcSearchOrder param_order;
        switch (order) {
        case UserPublishedVideosOrderParam::ByPublishTime:
            param_order = ApiParam_SpaceArcSearchOrder::ByPublishTime;
            break;
        case UserPublishedVideosOrderParam::ByClickCount:
            param_order = ApiParam_SpaceArcSearchOrder::ByClickCount;
            break;
        case UserPublishedVideosOrderParam::ByFavouriteCount:
            param_order = ApiParam_SpaceArcSearchOrder::ByFavouriteCount;
            break;
        default:
            throw winrt::hresult_invalid_argument();
        }
        auto jo = co_await m_bili_client.api_api_x_space_arc_search(
            mid, winrt::BiliUWP::ApiParam_Page{ page.n, page.size }, search_keyword, 0, param_order
        );
        util::debug::log_trace(std::format(L"Parsing JSON: {}", jo.Stringify()));
        check_json_code(jo);
        JsonPropsWalkTree json_props_walk;
        JsonObjectVisitor jov{ std::move(jo), json_props_walk };
        jov.scope([&](JsonObjectVisitor jov) {
            jov.scope([&](JsonObjectVisitor jov) {
                jov.scope([&](JsonObjectVisitor jov) {
                    jov.scope_enumerate([&](winrt::hstring const& key, JsonObjectVisitor jov) {
                        auto num_key = util::num::try_parse_u64(key);
                        if (!num_key) {
                            throw BiliApiParseException(BiliApiParseException::json_parse_user_defined,
                                jov.get_props_walk_tree(), "Object does not reside in u64 key"
                            );
                        }
                        UserSpacePublishedVideos_Type result_t;
                        jov.populate(result_t.count, "count");
                        jov.populate(result_t.type_name, "name");
                        jov.populate(result_t.tid, "tid");
                        result.list.tlist[*num_key] = std::move(result_t);
                    });
                }, "tlist");
                jov.populate(result.list.vlist, "vlist");
            }, "list");
            jov.scope([&](JsonObjectVisitor jov) {
                jov.populate(result.page.count, "count");
                jov.populate(result.page.pn, "pn");
                jov.populate(result.page.ps, "ps");
            }, "page");
            jov.populate(result.episodic_button, "episodic_button");
        }, "data");

        co_return result;
    }
    util::winrt::task<UserSpacePublishedAudiosResult> BiliClient::user_space_published_audios(
        uint64_t mid, PageParam page, UserPublishedAudiosOrderParam order
    ) {
        using winrt::BiliUWP::ApiParam_AudioMusicServiceWebSongUpperOrder;

        UserSpacePublishedAudiosResult result;

        auto cancellation_token = co_await winrt::get_cancellation_token();
        cancellation_token.enable_propagation();

        ApiParam_AudioMusicServiceWebSongUpperOrder param_order;
        switch (order) {
        case UserPublishedAudiosOrderParam::ByPublishTime:
            param_order = ApiParam_AudioMusicServiceWebSongUpperOrder::ByPublishTime;
            break;
        case UserPublishedAudiosOrderParam::ByPlayCount:
            param_order = ApiParam_AudioMusicServiceWebSongUpperOrder::ByPlayCount;
            break;
        case UserPublishedAudiosOrderParam::ByFavouriteCount:
            param_order = ApiParam_AudioMusicServiceWebSongUpperOrder::ByFavouriteCount;
            break;
        default:
            throw winrt::hresult_invalid_argument();
        }
        auto jo = co_await m_bili_client.api_api_audio_music_service_web_song_upper(
            mid, winrt::BiliUWP::ApiParam_Page{ page.n, page.size }, param_order
        );
        util::debug::log_trace(std::format(L"Parsing JSON: {}", jo.Stringify()));
        check_json_code(jo);
        JsonPropsWalkTree json_props_walk;
        JsonObjectVisitor jov{ std::move(jo), json_props_walk };
        jov.scope([&](JsonObjectVisitor jov) {
            jov.populate(result.cur_page, "curPage");
            jov.populate(result.page_count, "pageCount");
            jov.populate(result.page_size, "pageSize");
            jov.populate(result.total_size, "totalSize");
            jov.scope(adapter::assign_vec_or_null_as_empty{ result.data }, "data");
        }, "data");

        co_return result;
    }

    // Video information
    util::winrt::task<VideoViewInfoResult> BiliClient::video_view_info(
        std::variant<uint64_t, winrt::hstring> vid
    ) {
        VideoViewInfoResult result;
        uint64_t avid = 0;
        winrt::hstring bvid = L"";

        auto cancellation_token = co_await winrt::get_cancellation_token();
        cancellation_token.enable_propagation();

        std::visit([&](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, uint64_t>) {
                avid = arg;
            }
            else if constexpr (std::is_same_v<T, winrt::hstring>) {
                bvid = arg;
            }
            else {
                static_assert(util::misc::always_false_v<T>, "Unknown video id type");
            }
        }, vid);

        auto jo = co_await m_bili_client.api_api_x_web_interface_view(avid, bvid);
        util::debug::log_trace(std::format(L"Parsing JSON: {}", jo.Stringify()));
        check_json_code(jo);
        JsonPropsWalkTree json_props_walk;
        JsonObjectVisitor jov{ std::move(jo), json_props_walk };
        jov.scope([&](JsonObjectVisitor jov) {
            jov.populate(result.bvid, "bvid");
            jov.populate(result.avid, "aid");
            jov.populate(result.videos_count, "videos");
            jov.populate(result.tid, "tid");
            jov.populate(result.tname, "tname");
            jov.populate(result.copyright, "copyright");
            jov.populate(result.cover_url, "pic");
            jov.populate(result.title, "title");
            jov.populate(result.pubdate, "pubdate");
            jov.populate(result.ctime, "ctime");
            jov.populate(result.desc, "desc");
            jov.scope(adapter::assign_vec_or_null_as_empty{ result.desc_v2 }, "desc_v2");
            jov.populate(result.state, "state");
            jov.populate(result.duration, "duration");
            jov.populate(result.forward_avid, "forward");
            jov.populate(result.mission_id, "mission_id");
            jov.populate(result.pgc_redirect_url, "redirect_url");
            jov.scope([&](JsonObjectVisitor jov) {
                jov.scope(adapter::assign_num_0_1_to_bool{ result.rights.elec }, "elec");
                jov.scope(adapter::assign_num_0_1_to_bool{ result.rights.download }, "download");
                jov.scope(adapter::assign_num_0_1_to_bool{ result.rights.movie }, "movie");
                jov.scope(adapter::assign_num_0_1_to_bool{ result.rights.pay }, "pay");
                jov.scope(adapter::assign_num_0_1_to_bool{ result.rights.hd5 }, "hd5");
                jov.scope(adapter::assign_num_0_1_to_bool{ result.rights.no_reprint }, "no_reprint");
                jov.scope(adapter::assign_num_0_1_to_bool{ result.rights.autoplay }, "autoplay");
                jov.scope(adapter::assign_num_0_1_to_bool{ result.rights.ugc_pay }, "ugc_pay");
                jov.scope(adapter::assign_num_0_1_to_bool{ result.rights.is_stein_gate }, "is_stein_gate");
                jov.scope(adapter::assign_num_0_1_to_bool{ result.rights.is_cooperation }, "is_cooperation");
            }, "rights");
            jov.scope([&](JsonObjectVisitor jov) {
                jov.populate(result.owner.mid, "mid");
                jov.populate(result.owner.name, "name");
                jov.populate(result.owner.face_url, "face");
            }, "owner");
            jov.scope([&](JsonObjectVisitor jov) {
                jov.populate(result.stat.avid, "aid");
                jov.scope([&](JsonValueVisitor jvv) {
                    if (jvv.as<int64_t>() < 0) {
                        result.stat.view_count = std::nullopt;
                    }
                    else {
                        jvv.populate(result.stat.view_count);
                    }
                }, "view");
                //jov.populate(result.stat.view_count, "view");
                jov.populate(result.stat.danmaku_count, "danmaku");
                jov.populate(result.stat.reply_count, "reply");
                jov.populate(result.stat.favorite_count, "favorite");
                jov.populate(result.stat.coin_count, "coin");
                jov.populate(result.stat.share_count, "share");
                jov.populate(result.stat.now_rank, "now_rank");
                jov.populate(result.stat.his_rank, "his_rank");
                jov.populate(result.stat.like_count, "like");
                jov.populate(result.stat.dislike_count, "dislike");
                jov.populate(result.stat.evaluation, "evaluation");
                jov.populate(result.stat.argue_msg, "argue_msg");
            }, "stat");
            jov.populate(result.dynamic_text, "dynamic");
            jov.populate(result.cid_1p, "cid");
            jov.populate(result.dimension_1p, "dimension");
            jov.populate(result.pages, "pages");
            jov.scope([&](JsonObjectVisitor jov) {
                jov.populate(result.subtitle.allow_submit, "allow_submit");
                jov.populate(result.subtitle.list, "list");
            }, "subtitle");
            jov.populate(result.staff, "staff");
            jov.scope([&](JsonObjectVisitor jov) {
                jov.populate(result.user_garb.url_image_ani_cut, "url_image_ani_cut");
            }, "user_garb");
        }, "data");

        co_return result;
    }
    util::winrt::task<VideoFullInfoResult> BiliClient::video_full_info(
        std::variant<uint64_t, winrt::hstring> vid
    ) {
        throw winrt::hresult_not_implemented();
    }
    util::winrt::task<VideoInfoV2Result> BiliClient::video_info_v2(
        std::variant<uint64_t, winrt::hstring> vid, uint64_t cid
    ) {
        VideoInfoV2Result result;
        uint64_t avid = 0;
        winrt::hstring bvid = L"";

        auto cancellation_token = co_await winrt::get_cancellation_token();
        cancellation_token.enable_propagation();

        std::visit([&](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, uint64_t>) {
                avid = arg;
            }
            else if constexpr (std::is_same_v<T, winrt::hstring>) {
                bvid = arg;
            }
            else {
                static_assert(util::misc::always_false_v<T>, "Unknown video id type");
            }
        }, vid);

        auto jo = co_await m_bili_client.api_api_x_player_v2(avid, bvid, cid);
        util::debug::log_trace(std::format(L"Parsing JSON: {}", jo.Stringify()));
        check_json_code(jo);
        JsonPropsWalkTree json_props_walk;
        JsonObjectVisitor jov{ std::move(jo), json_props_walk };
        jov.scope([&](JsonObjectVisitor jov) {
            jov.populate(result.avid, "aid");
            jov.populate(result.bvid, "bvid");
            jov.populate(result.cid, "cid");
            jov.populate(result.page_no, "page_no");
            jov.populate(result.online_count, "online_count");
            jov.scope([&](JsonObjectVisitor jov) {
                jov.populate(result.subtitle.allow_submit, "allow_submit");
                jov.populate(result.subtitle.list, "subtitles");
            }, "subtitle");
        }, "data");

        co_return result;
    }
    util::winrt::task<VideoPlayUrlResult> BiliClient::video_play_url(
        std::variant<uint64_t, winrt::hstring> vid, uint64_t cid,
        VideoPlayUrlPreferenceParam prefers
    ) {
        VideoPlayUrlResult result;
        uint64_t avid = 0;
        winrt::hstring bvid = L"";
        winrt::BiliUWP::ApiParam_VideoPlayUrlPreference api_prefers;

        auto cancellation_token = co_await winrt::get_cancellation_token();
        cancellation_token.enable_propagation();

        std::visit([&](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, uint64_t>) {
                avid = arg;
            }
            else if constexpr (std::is_same_v<T, winrt::hstring>) {
                bvid = arg;
            }
            else {
                static_assert(util::misc::always_false_v<T>, "Unknown video id type");
            }
        }, vid);
        api_prefers.prefer_dash = prefers.prefer_dash;
        api_prefers.prefer_hdr = prefers.prefer_hdr;
        api_prefers.prefer_4k = prefers.prefer_4k;
        api_prefers.prefer_dolby = prefers.prefer_dolby;
        api_prefers.prefer_8k = prefers.prefer_8k;
        api_prefers.prefer_av1 = prefers.prefer_av1;

        auto jo = co_await m_bili_client.api_api_x_player_playurl(avid, bvid, cid, api_prefers);
        util::debug::log_trace(std::format(L"Parsing JSON: {}", jo.Stringify()));
        check_json_code(jo);
        JsonPropsWalkTree json_props_walk;
        JsonObjectVisitor jov{ std::move(jo), json_props_walk };
        jov.scope([&](JsonObjectVisitor jov) {
            jov.populate(result.from, "from");
            jov.populate(result.result, "result");
            jov.populate(result.message, "message");
            jov.populate(result.quality, "quality");
            jov.populate(result.format, "format");
            jov.populate(result.timelength, "timelength");
            jov.populate(result.accept_format, "accept_format");
            jov.populate(result.accept_description, "accept_description");
            jov.populate(result.accept_quality, "accept_quality");
            jov.populate(result.video_codecid, "video_codecid");
            jov.populate(result.seek_param, "seek_param");
            jov.populate(result.seek_type, "seek_type");
            jov.populate(result.durl, "durl");
            jov.populate(result.dash, "dash");
            jov.populate(result.support_formats, "support_formats");
        }, "data");

        co_return result;
    }
    util::winrt::task<VideoShotInfoResult> BiliClient::video_shot_info(
        std::variant<uint64_t, winrt::hstring> vid, uint64_t cid,
        bool load_indices
    ) {
        VideoShotInfoResult result;
        uint64_t avid = 0;
        winrt::hstring bvid = L"";
        winrt::BiliUWP::ApiParam_VideoPlayUrlPreference api_prefers;

        auto cancellation_token = co_await winrt::get_cancellation_token();
        cancellation_token.enable_propagation();

        std::visit([&](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, uint64_t>) {
                avid = arg;
            }
            else if constexpr (std::is_same_v<T, winrt::hstring>) {
                bvid = arg;
            }
            else {
                static_assert(util::misc::always_false_v<T>, "Unknown video id type");
            }
        }, vid);

        auto jo = co_await m_bili_client.api_api_x_player_videoshot(avid, bvid, cid, load_indices);
        util::debug::log_trace(std::format(L"Parsing JSON: {}", jo.Stringify()));
        check_json_code(jo);
        JsonPropsWalkTree json_props_walk;
        JsonObjectVisitor jov{ std::move(jo), json_props_walk };
        jov.scope([&](JsonObjectVisitor jov) {
            jov.populate(result.pvdata_url, "pvdata");
            jov.populate(result.img_x_len, "img_x_len");
            jov.populate(result.img_y_len, "img_y_len");
            jov.populate(result.img_x_size, "img_x_size");
            jov.populate(result.img_y_size, "img_y_size");
            jov.populate(result.images_url, "image");
            jov.populate(result.indices, "index");
        }, "data");

        co_return result;
    }

    // Audio information
    util::winrt::task<AudioBasicInfoResult> BiliClient::audio_basic_info(uint64_t auid) {
        AudioBasicInfoResult result;

        auto cancellation_token = co_await winrt::get_cancellation_token();
        cancellation_token.enable_propagation();

        auto jo = co_await m_bili_client.api_www_audio_music_service_c_web_song_info(auid);
        util::debug::log_trace(std::format(L"Parsing JSON: {}", jo.Stringify()));
        check_json_code(jo);
        JsonPropsWalkTree json_props_walk;
        JsonObjectVisitor jov{ std::move(jo), json_props_walk };
        jov.scope([&](JsonObjectVisitor jov) {
            jov.populate(result.auid, "id");
            jov.populate(result.uid, "uid");
            jov.populate(result.uname, "uname");
            jov.populate(result.author, "author");
            jov.populate(result.audio_title, "title");
            jov.populate(result.cover_url, "cover");
            jov.populate(result.audio_intro, "intro");
            jov.populate(result.audio_lyric, "lyric");
            jov.populate(result.duration, "duration");
            jov.populate(result.pubtime, "passtime");
            jov.populate(result.cur_query_time, "curtime");
            jov.populate(result.linked_avid, "aid");
            jov.populate(result.linked_bvid, "bvid");
            jov.populate(result.linked_cid, "cid");
            jov.scope([&](JsonObjectVisitor jov) {
                jov.populate(result.statistic.auid, "sid");
                jov.populate(result.statistic.play_count, "play");
                jov.populate(result.statistic.favourite_count, "collect");
                jov.populate(result.statistic.comment_count, "comment");
                jov.populate(result.statistic.share_count, "share");
            }, "statistic");
            jov.scope([&](JsonObjectVisitor jov) {
                jov.populate(result.vip_info.vip_type, "type");
                jov.scope(adapter::assign_num_0_1_to_bool{ result.vip_info.is_vip }, "status");
                jov.populate(result.vip_info.vip_due_date, "due_date");
            }, "vipInfo");
            jov.populate(result.favs_with_collected, "collectIds");
            jov.populate(result.coin_count, "coin_num");
        }, "data");

        co_return result;
    }
    util::winrt::task<AudioPlayUrlResult> BiliClient::audio_play_url(
        uint64_t auid, AudioQualityParam quality
    ) {
        AudioPlayUrlResult result;

        auto cancellation_token = co_await winrt::get_cancellation_token();
        cancellation_token.enable_propagation();

        auto jo = co_await m_bili_client.api_api_audio_music_service_c_url(
            auid, static_cast<uint32_t>(quality)
        );
        util::debug::log_trace(std::format(L"Parsing JSON: {}", jo.Stringify()));
        check_json_code(jo);
        JsonPropsWalkTree json_props_walk;
        JsonObjectVisitor jov{ std::move(jo), json_props_walk };
        jov.scope([&](JsonObjectVisitor jov) {
            jov.populate(result.auid, "sid");
            jov.scope(adapter::assign_num_to_enum{ result.type }, "type");
            jov.populate(result.expires_in, "timeout");
            jov.populate(result.stream_size, "size");
            jov.populate(result.urls, "cdns");
            jov.populate(result.qualities, "qualities");
            jov.populate(result.audio_title, "title");
            jov.populate(result.cover_url, "cover");
        }, "data");

        co_return result;
    }

    // Favourites information
    util::winrt::task<UserFavFoldersListResult> BiliClient::user_fav_folders_list(
        uint64_t mid, PageParam page, std::optional<FavItemLookupParam> item_to_find
    ) {
        UserFavFoldersListResult result;

        auto cancellation_token = co_await winrt::get_cancellation_token();
        cancellation_token.enable_propagation();

        auto jo = co_await m_bili_client.api_api_x_v3_fav_folder_created_list(
            mid, winrt::BiliUWP::ApiParam_Page{ page.n, page.size },
            item_to_find.transform([](FavItemLookupParam v) {
                return winrt::BiliUWP::ApiParam_FavItemLookup{
                    v.nid, static_cast<winrt::BiliUWP::ApiData_ResType>(v.type)
                };
            })
        );
        util::debug::log_trace(std::format(L"Parsing JSON: {}", jo.Stringify()));
        check_json_code(jo);
        JsonPropsWalkTree json_props_walk;
        JsonObjectVisitor jov{ std::move(jo), json_props_walk };
        jov.scope([&](JsonObjectVisitor jov) {
            jov.populate(result.count, "count");
            jov.populate(result.list, "list");
            jov.populate(result.has_more, "has_more");
        }, "data");

        co_return result;
    }
    util::winrt::task<UserFavFoldersListAllResult> BiliClient::user_fav_folders_list_all(
        uint64_t mid, std::optional<FavItemLookupParam> item_to_find
    ) {
        UserFavFoldersListAllResult result;

        auto cancellation_token = co_await winrt::get_cancellation_token();
        cancellation_token.enable_propagation();

        auto jo = co_await m_bili_client.api_api_x_v3_fav_folder_created_list_all(
            mid, item_to_find.transform([](FavItemLookupParam v) {
                return winrt::BiliUWP::ApiParam_FavItemLookup{
                    v.nid, static_cast<winrt::BiliUWP::ApiData_ResType>(v.type)
                };
            })
        );
        util::debug::log_trace(std::format(L"Parsing JSON: {}", jo.Stringify()));
        check_json_code(jo);
        JsonPropsWalkTree json_props_walk;
        JsonObjectVisitor jov{ std::move(jo), json_props_walk };
        jov.scope([&](JsonObjectVisitor jov) {
            jov.populate(result.count, "count");
            jov.populate(result.list, "list");
        }, "data");

        co_return result;
    }
    util::winrt::task<FavFolderResListResult> BiliClient::fav_folder_res_list(
        uint64_t folder_id, PageParam page, winrt::hstring search_keyword, FavResSortOrderParam order
    ) {
        using winrt::BiliUWP::ApiParam_FavResSortOrder;

        FavFolderResListResult result;

        auto cancellation_token = co_await winrt::get_cancellation_token();
        cancellation_token.enable_propagation();

        ApiParam_FavResSortOrder param_order;
        switch (order) {
        case FavResSortOrderParam::ByFavouriteTime:
            param_order = ApiParam_FavResSortOrder::ByFavouriteTime;
            break;
        case FavResSortOrderParam::ByViewCount:
            param_order = ApiParam_FavResSortOrder::ByViewCount;
            break;
        case FavResSortOrderParam::ByPublishTime:
            param_order = ApiParam_FavResSortOrder::ByPublishTime;
            break;
        default:
            throw winrt::hresult_invalid_argument();
        }
        auto jo = co_await m_bili_client.api_api_x_v3_fav_resource_list(
            folder_id, winrt::BiliUWP::ApiParam_Page{ page.n, page.size }, search_keyword, param_order
        );
        util::debug::log_trace(std::format(L"Parsing JSON: {}", jo.Stringify()));
        check_json_code(jo);
        JsonPropsWalkTree json_props_walk;
        JsonObjectVisitor jov{ std::move(jo), json_props_walk };
        jov.scope([&](JsonObjectVisitor jov) {
            jov.scope([&](JsonObjectVisitor jov) {
                jov.populate(result.info.id, "id");
                jov.populate(result.info.fid, "fid");
                jov.populate(result.info.mid, "mid");
                jov.populate(result.info.attr, "attr");
                jov.populate(result.info.title, "title");
                jov.populate(result.info.cover_url, "cover");
                jov.scope([&](JsonObjectVisitor jov) {
                    jov.populate(result.info.upper.mid, "mid");
                    jov.populate(result.info.upper.name, "name");
                    jov.populate(result.info.upper.face_url, "face");
                    jov.populate(result.info.upper.followed, "followed");
                    jov.populate(result.info.upper.vip_type, "vip_type");
                    jov.scope(adapter::assign_num_0_1_to_bool{ result.info.upper.is_vip }, "vip_statue");
                }, "upper");
                jov.populate(result.info.cover_type, "cover_type");
                jov.scope([&](JsonObjectVisitor jov) {
                    jov.populate(result.info.cnt_info.favourite_count, "collect");
                    jov.populate(result.info.cnt_info.play_count, "play");
                    jov.populate(result.info.cnt_info.like_count, "thumb_up");
                    jov.populate(result.info.cnt_info.share_count, "share");
                }, "cnt_info");
                jov.populate(result.info.type, "type");
                jov.populate(result.info.intro, "intro");
                jov.populate(result.info.ctime, "ctime");
                jov.populate(result.info.mtime, "mtime");
                jov.populate(result.info.state, "state");
                jov.scope(adapter::assign_num_0_1_to_bool{ result.info.fav_has_item }, "fav_state");
                jov.scope(adapter::assign_num_0_1_to_bool{ result.info.is_liked }, "like_state");
                jov.populate(result.info.media_count, "media_count");
            }, "info");
            jov.scope(adapter::assign_vec_or_null_as_empty{ result.media_list }, "medias");
            jov.populate(result.has_more, "has_more");
        }, "data");

        co_return result;
    }
}
