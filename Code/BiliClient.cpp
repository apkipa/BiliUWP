#include "pch.h"
#include "App.h"
#include "BiliClient.hpp"
#include "json.h"

namespace BiliUWP {
    bool try_check_api_code(ApiCode code) {
        return code >= ApiCode::Success;
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
            util::misc::discard_first_type<decltype(func(std::declval<JsonObjectVisitor>())), JsonObjectVisitor>;
        template<typename Functor>
        static auto get_fn_param_helper(Functor func, int, int, ...) ->
            util::misc::discard_first_type<decltype(func(std::declval<JsonArrayVisitor>())), JsonArrayVisitor>;
        template<typename Functor>
        static auto get_fn_param_helper(Functor func, int, ...) ->
            util::misc::discard_first_type<decltype(func(std::declval<JsonValueVisitor>())), JsonValueVisitor>;
        template<typename Functor>
        static auto get_fn_param_helper(Functor func, ...)->BadFnParamType;
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
        template<>
        std::optional<uint32_t> try_as(void) {
            if (m_jv.ValueType() != winrt::Windows::Data::Json::JsonValueType::Number) {
                return std::nullopt;
            }
            return static_cast<uint32_t>(m_jv.GetNumber());
        }
        template<>
        std::optional<int32_t> try_as(void) {
            if (m_jv.ValueType() != winrt::Windows::Data::Json::JsonValueType::Number) {
                return std::nullopt;
            }
            return static_cast<int32_t>(m_jv.GetNumber());
        }
        template<>
        std::optional<uint64_t> try_as(void) {
            if (m_jv.ValueType() != winrt::Windows::Data::Json::JsonValueType::Number) {
                return std::nullopt;
            }
            return static_cast<uint64_t>(m_jv.GetNumber());
        }
        template<>
        std::optional<int64_t> try_as(void) {
            if (m_jv.ValueType() != winrt::Windows::Data::Json::JsonValueType::Number) {
                return std::nullopt;
            }
            return static_cast<int64_t>(m_jv.GetNumber());
        }
        template<>
        std::optional<std::nullptr_t> try_as(void) {
            if (m_jv.ValueType() != winrt::Windows::Data::Json::JsonValueType::Null) {
                return std::nullopt;
            }
            return nullptr;
        }
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
        template<>
        uint32_t as(void) {
            return static_cast<uint32_t>(this->as<double>());
        }
        template<>
        int32_t as(void) {
            return static_cast<int32_t>(this->as<double>());
        }
        template<>
        uint64_t as(void) {
            return static_cast<uint64_t>(this->as<double>());
        }
        template<>
        int64_t as(void) {
            return static_cast<int64_t>(this->as<double>());
        }
        template<>
        std::nullptr_t as(void) {
            expect_jv_type(m_jv, winrt::Windows::Data::Json::JsonValueType::Null);
            return nullptr;
        }
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
        /*
        template<typename T>
        void populate_inner(T& dst, std::string_view key) = delete;
        template<>
        void populate_inner(double& dst, std::string_view key) {
            auto jv = expect_jo_lookup(m_jo, key);
            expect_jv_type(jv, winrt::Windows::Data::Json::JsonValueType::Number);
            dst = jv.GetNumber();
        }
        template<>
        void populate_inner(winrt::hstring& dst, std::string_view key) {
            auto jv = expect_jo_lookup(m_jo, key);
            expect_jv_type(jv, winrt::Windows::Data::Json::JsonValueType::String);
            dst = jv.GetString();
        }
        template<>
        void populate_inner(std::wstring& dst, std::string_view key) {
            auto jv = expect_jo_lookup(m_jo, key);
            expect_jv_type(jv, winrt::Windows::Data::Json::JsonValueType::String);
            dst = jv.GetString();
        }
        template<>
        void populate_inner(bool& dst, std::string_view key) {
            auto jv = expect_jo_lookup(m_jo, key);
            expect_jv_type(jv, winrt::Windows::Data::Json::JsonValueType::Boolean);
            dst = jv.GetBoolean();
        }
        template<>
        void populate_inner(uint32_t& dst, std::string_view key) {
            double temp;
            populate_inner(temp, key);
            dst = static_cast<uint32_t>(temp);
        }
        template<>
        void populate_inner(int32_t& dst, std::string_view key) {
            double temp;
            populate_inner(temp, key);
            dst = static_cast<int32_t>(temp);
        }
        template<>
        void populate_inner(uint64_t& dst, std::string_view key) {
            double temp;
            populate_inner(temp, key);
            dst = static_cast<uint64_t>(temp);
        }
        template<>
        void populate_inner(int64_t& dst, std::string_view key) {
            double temp;
            populate_inner(temp, key);
            dst = static_cast<int64_t>(temp);
        }
        template<typename T>
        void populate(T& dst, std::string_view key) {
            m_props_walk.push(key);
            deferred([this] {
                m_props_walk.pop();
            });
            populate_inner(dst, key);
        }*/
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
                this->scope(std::bind(func, container[i], std::placeholders::_1), i);
            }
        }
        /*
        template<typename T>
        void populate_inner(T& dst, size_t idx) = delete;
        template<>
        void populate_inner(double& dst, size_t idx) {
            auto jv = expect_ja_get(m_ja, idx);
            expect_jv_type(jv, winrt::Windows::Data::Json::JsonValueType::Number);
            dst = jv.GetNumber();
        }
        template<>
        void populate_inner(winrt::hstring& dst, size_t idx) {
            auto jv = expect_ja_get(m_ja, idx);
            expect_jv_type(jv, winrt::Windows::Data::Json::JsonValueType::String);
            dst = jv.GetString();
        }
        template<>
        void populate_inner(std::wstring& dst, size_t idx) {
            auto jv = expect_ja_get(m_ja, idx);
            expect_jv_type(jv, winrt::Windows::Data::Json::JsonValueType::String);
            dst = jv.GetString();
        }
        template<>
        void populate_inner(bool& dst, size_t idx) {
            auto jv = expect_ja_get(m_ja, idx);
            expect_jv_type(jv, winrt::Windows::Data::Json::JsonValueType::Boolean);
            dst = jv.GetBoolean();
        }
        template<>
        void populate_inner(uint32_t& dst, size_t idx) {
            double temp;
            populate_inner(temp, idx);
            dst = static_cast<uint32_t>(temp);
        }
        template<>
        void populate_inner(int32_t& dst, size_t idx) {
            double temp;
            populate_inner(temp, idx);
            dst = static_cast<int32_t>(temp);
        }
        template<>
        void populate_inner(uint64_t& dst, size_t idx) {
            double temp;
            populate_inner(temp, idx);
            dst = static_cast<uint64_t>(temp);
        }
        template<>
        void populate_inner(int64_t& dst, size_t idx) {
            double temp;
            populate_inner(temp, idx);
            dst = static_cast<int64_t>(temp);
        }
        // NOTE: populate is just an assignment inside scope
        template<typename T>
        void populate(T& dst, size_t idx) {
            m_props_walk.push(idx);
            deferred([this] {
                m_props_walk.pop();
            });
            populate_inner(dst, idx);
        }
        */
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
    // NOTE: User-defined as / try_as conversions starts here
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
        jov.populate(result.author_mid, "author_mid");
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
    VideoPlayUrl_DurlPart JsonValueVisitor::as(void) {
        VideoPlayUrl_DurlPart result;
        auto jov = this->as<JsonObjectVisitor>();
        jov.populate(result.order, "order");
        jov.populate(result.length, "length");
        jov.populate(result.size, "size");
        jov.populate(result.ahead, "ahead");
        jov.populate(result.vhead, "vhead");
        jov.populate(result.url, "url");
        jov.populate(result.backup_url, "backup_url");
        return result;
    }
    template<>
    VideoPlayUrl_Dash_Stream JsonValueVisitor::as(void) {
        VideoPlayUrl_Dash_Stream result;
        auto jov = this->as<JsonObjectVisitor>();
        jov.populate(result.id, "id");
        jov.populate(result.base_url, "base_url");
        jov.populate(result.backup_url, "backup_url");
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
        jov.populate(result.audio, "audio");
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
        jov.populate(result.codecs, "codecs");
        return result;
    }

    BiliClient::BiliClient() :
        m_bili_client(winrt::make<winrt::BiliUWP::implementation::BiliClientManaged>()),
        m_refresh_token()
    {
        //m_bili_client.data_user_agent(L"...");
    }

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
        jov.populate(result.url, "url");
        jov.populate(result.auth_code, "auth_code");

        co_return result;
    }
    util::winrt::task<PollTvQrLoginResult> BiliClient::poll_tv_qr_login(
        winrt::hstring auth_code,
        winrt::guid local_id
    ) {
        PollTvQrLoginResult result;

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
        check_api_code(code);
        result.code = static_cast<ApiCode>(code);
        if (result.code == ApiCode::Success) {
            jov.scope([&](JsonObjectVisitor jov) {
                jov.populate(result.mid, "mid");
                jov.populate(result.access_token, "access_token");
                jov.populate(result.refresh_token, "refresh_token");
                jov.populate(result.expires_in, "expires_in");
                jov.scope([&](JsonObjectVisitor jov) {
                    jov.populate(result.user_cookies.SESSDATA, "SESSDATA");
                    jov.populate(result.user_cookies.bili_jct, "bili_jct");
                    jov.populate(result.user_cookies.DedeUserID, "DedeUserID");
                    jov.populate(result.user_cookies.DedeUserID__ckMd5, "DedeUserID__ckMd5");
                    jov.populate(result.user_cookies.sid, "sid");
                }, "cookie_info");
            }, "data");

            // Update self data
            this->set_access_token(result.access_token);
            this->set_refresh_token(result.refresh_token);
            this->set_cookies(result.user_cookies);
        }

        co_return result;
    }
    util::winrt::task<Oauth2RefreshTokenResult> BiliClient::oauth2_refresh_token(void) {
        Oauth2RefreshTokenResult result;

        auto cancellation_token = co_await winrt::get_cancellation_token();
        cancellation_token.enable_propagation();

        auto jo = co_await m_bili_client.api_passport_api_v2_oauth2_refresh_token(
            keys::api_android_1, m_refresh_token
        );
        util::debug::log_trace(std::format(L"Parsing JSON: {}", jo.Stringify()));
        check_json_code(jo);
        JsonPropsWalkTree json_props_walk;
        JsonObjectVisitor jov{ std::move(jo), json_props_walk };
        // TODO: Breakpoint here to verify actual JSON response
        jov.scope([&](JsonObjectVisitor jov) {
            jov.scope([&](JsonObjectVisitor jov) {
                jov.populate(result.access_token, "access_token");
                jov.populate(result.refresh_token, "refresh_token");
            }, "token_info");
            jov.populate(result.expires_in, "expires_in");
            jov.scope([&](JsonObjectVisitor jov) {
                jov.populate(result.user_cookies.SESSDATA, "SESSDATA");
                jov.populate(result.user_cookies.bili_jct, "bili_jct");
                jov.populate(result.user_cookies.DedeUserID, "DedeUserID");
                jov.populate(result.user_cookies.DedeUserID__ckMd5, "DedeUserID__ckMd5");
                jov.populate(result.user_cookies.sid, "sid");
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

        auto jo = co_await m_bili_client.api_app_x_web_interface_nav();
        util::debug::log_trace(std::format(L"Parsing JSON: {}", jo.Stringify()));
        check_json_code(jo);
        JsonPropsWalkTree json_props_walk;
        JsonObjectVisitor jov{ std::move(jo), json_props_walk };
        jov.scope([&](JsonObjectVisitor jov) {
            jov.populate(result.logged_in, "isLogin");
            /*jov.scope([&](JsonValueVisitor jvv) {
                switch (jvv.as<int64_t>()) {
                case 0:     result.email_verified = false;  break;
                case 1:     result.email_verified = true;   break;
                default:
                    throw BiliApiParseException(BiliApiParseException::json_parse_user_defined,
                        json_props_walk, "Only 0 and 1 are expected values"
                    );
                }
            }, "email_verified");*/
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

        auto jo = co_await m_bili_client.api_app_x_web_interface_nav_stat();
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
            //jov.scope(adapter::assign_vec{ result.desc_v2 }, "desc_v2");
            jov.populate(result.desc_v2, "desc_v2");
            jov.populate(result.state, "state");
            jov.populate(result.duration, "duration");
            jov.populate(result.forward_avid, "forward");
            jov.populate(result.mission_id, "mission_id");
            jov.populate(result.pgc_redirect_url, "redirect_url");
            jov.scope([&](JsonObjectVisitor jov) {
                jov.populate(result.rights.elec, "elec");
                jov.populate(result.rights.download, "download");
                jov.populate(result.rights.movie, "movie");
                jov.populate(result.rights.pay, "pay");
                jov.populate(result.rights.hd5, "hd5");
                jov.populate(result.rights.no_reprint, "no_reprint");
                jov.populate(result.rights.autoplay, "autoplay");
                jov.populate(result.rights.ugc_pay, "ugc_pay");
                jov.populate(result.rights.is_stein_gate, "is_stein_gate");
                jov.populate(result.rights.is_cooperation, "is_cooperation");
            }, "rights");
            jov.scope([&](JsonObjectVisitor jov) {
                jov.populate(result.owner.mid, "mid");
                jov.populate(result.owner.name, "name");
                jov.populate(result.owner.face_url, "face");
            }, "owner");
            jov.scope([&](JsonObjectVisitor jov) {
                jov.populate(result.stat.avid, "aid");
                jov.populate(result.stat.view_count, "view");
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
        api_prefers.prefer_4k = prefers.prefer_4k;
        api_prefers.prefer_hdr = prefers.prefer_hdr;
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

    // Audio information

    // Favourites information
}
