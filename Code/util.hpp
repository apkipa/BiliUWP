#pragma once

#include <string>
#include <format>
#include <atomic>
#include <shared_mutex>
#include <source_location>

/* TODO:
Currently, we pin cppwinrt to versions before v2.0.221117.1 since we are blocked by
the dependency on winrt::impl::get_awaiter, winrt::impl::notify_awaiter, etc. Remove the
uses in the future to keep cppwinrt up to date.
*/

namespace util {
    namespace misc {
#define CONCAT_2_IMPL(a, b) a ## b
#define CONCAT_2(a, b) CONCAT_2_IMPL(a, b)
#define CONCAT_3_IMPL(a, b, c) a ## b ## c
#define CONCAT_3(a, b, c) CONCAT_3_IMPL(a, b, c)

        // NOTE: Macro expects functors of type void(void)
#define deferred(x) auto CONCAT_2(internal_deffered_, __COUNTER__) = ::util::misc::Defer(x)
        template<typename T>
        struct Defer final {
            Defer(T func) : m_func(std::move(func)) {}
            ~Defer() { std::invoke(m_func); }
        private:
            T m_func;
        };

        template<typename>
        inline constexpr bool always_false_v = false;

        template<typename T, typename>
        using first_type = T;
        template<typename, typename U>
        using second_type = U;
    }

    namespace str {
        std::wstring wstrprintf(_Printf_format_string_ const wchar_t* str, ...);
        constexpr bool is_str_all_digits(std::wstring_view sv) {
            return sv.find_first_not_of(L"0123456789") == std::wstring_view::npos;
        }

        namespace details {
            template<unsigned... digits>
            struct to_wchars {
                static constexpr wchar_t value[] = { static_cast<wchar_t>((L'0' + digits))..., L'\0' };
            };
            template<template<unsigned... digits> typename applier, unsigned remainder, unsigned... digits>
            struct extract_digits : extract_digits<applier, remainder / 10, remainder % 10, digits...> {};
            template<template<unsigned... digits> typename applier, unsigned... digits>
            struct extract_digits<applier, 0, digits...> : applier<digits...> {};
        }
        template<unsigned num>
        struct unsigned_to_wstr : details::extract_digits<details::to_wchars, num> {};
        template<>
        struct unsigned_to_wstr<0> : details::to_wchars<0> {};
        template<unsigned num>
        constexpr auto& unsigned_to_wstr_v = unsigned_to_wstr<num>::value;

        namespace details {
            template<size_t n1, size_t n2>
            struct concat_wstr_2 {
                wchar_t value[n1 + n2 + 1];
                template<size_t... idxs1, size_t... idxs2>
                constexpr concat_wstr_2(
                    const wchar_t* s1, std::index_sequence<idxs1...>,
                    const wchar_t* s2, std::index_sequence<idxs2...>
                ) :
                    value{ s1[idxs1]..., s2[idxs2]..., L'\0' }
                {}
            };
        }

        // NOTE: Due to some limitations, _v version is not provided, and callers
        //       must store the entire return value as constexpr
        template<size_t n1, size_t n2>
        constexpr auto concat_wstr_2(const wchar_t(&s1)[n1], const wchar_t(&s2)[n2]) {
            constexpr auto len1 = n1 - 1;
            constexpr auto len2 = n2 - 1;
            return details::concat_wstr_2<len1, len2>(
                s1, std::make_index_sequence<len1>{},
                s2, std::make_index_sequence<len2>{}
            );
        }

        template<size_t n1, size_t n2, typename... types>
        constexpr auto concat_wstr(const wchar_t(&s1)[n1], const wchar_t(&s2)[n2], types&... args) {
            auto result = concat_wstr_2(s1, s2);
            if constexpr (sizeof...(args) > 0) {
                return concat_wstr(result.value, args...);
            }
            else {
                return result;
            }
        }

        constexpr void write_u8_hex(uint8_t n, wchar_t buf[2]) {
            constexpr wchar_t char_map[16] = {
                L'0', L'1', L'2', L'3',
                L'4', L'5', L'6', L'7',
                L'8', L'9', L'a', L'b',
                L'c', L'd', L'e', L'f',
            };
            buf[0] = char_map[n >> 4];
            buf[1] = char_map[n & 0xf];
        }
        // NOTE: Data is written by convention (big endian)
        constexpr void write_u16_hex(uint16_t n, wchar_t buf[4]) {
            write_u8_hex(n & 0xff, buf + 2);
            write_u8_hex(n >> 8, buf);
        }
        // NOTE: Data is written by convention (big endian)
        constexpr void write_u32_hex(uint32_t n, wchar_t buf[8]) {
            write_u16_hex(n & 0xffff, buf + 4);
            write_u16_hex(n >> 16, buf);
        }
        // NOTE: Data is written by convention (big endian)
        constexpr void write_u64_hex(uint64_t n, wchar_t buf[16]) {
            write_u32_hex(n & 0xffffffff, buf + 8);
            write_u32_hex(n >> 32, buf);
        }
        // NOTE: Data is written in little endian
        constexpr void write_u16_hex_swap(uint16_t n, wchar_t buf[4]) {
            write_u8_hex(n & 0xff, buf);
            write_u8_hex(n >> 8, buf + 2);
        }
        // NOTE: Data is written in little endian
        constexpr void write_u32_hex_swap(uint32_t n, wchar_t buf[8]) {
            write_u16_hex_swap(n & 0xffff, buf);
            write_u16_hex_swap(n >> 16, buf + 4);
        }
        // NOTE: Data is written in little endian
        constexpr void write_u64_hex_swap(uint64_t n, wchar_t buf[16]) {
            write_u32_hex_swap(n & 0xffffffff, buf);
            write_u32_hex_swap(n >> 32, buf + 8);
        }

        inline std::wstring byte_size_to_str(size_t size, double precision = 1) {
            double float_size = static_cast<double>(size);
            const wchar_t* size_postfix;
            uint64_t power_of_size = 0;

            while (float_size >= 1024) {
                float_size /= 1024;
                power_of_size++;
            }
            switch (power_of_size) {
            case 0:     size_postfix = L"B";        break;
            case 1:     size_postfix = L"KiB";      break;
            case 2:     size_postfix = L"MiB";      break;
            case 3:     size_postfix = L"GiB";      break;
            case 4:     size_postfix = L"TiB";      break;
            case 5:     size_postfix = L"PiB";      break;
            case 6:     size_postfix = L"EiB";      break;
            default:    size_postfix = L"<ERROR>";  break;
            }
            return std::format(L"{} {}",
                std::round(float_size * precision) / precision,
                size_postfix
            );
        }
        inline std::wstring bit_size_to_str(size_t size, double precision = 1) {
            double float_size = static_cast<double>(size);
            const wchar_t* size_postfix;
            uint64_t power_of_size = 0;

            while (float_size >= 1000) {
                float_size /= 1000;
                power_of_size++;
            }
            switch (power_of_size) {
            case 0:     size_postfix = L"b";        break;
            case 1:     size_postfix = L"Kb";      break;
            case 2:     size_postfix = L"Mb";      break;
            case 3:     size_postfix = L"Gb";      break;
            case 4:     size_postfix = L"Tb";      break;
            case 5:     size_postfix = L"Pb";      break;
            case 6:     size_postfix = L"Eb";      break;
            default:    size_postfix = L"<ERROR>";  break;
            }
            return std::format(L"{} {}",
                std::round(float_size * precision) / precision,
                size_postfix
            );
        }
    }

    namespace time {
        std::wstring pretty_time(void);
        uint64_t get_secs_since_epoch(void);

        ::winrt::hstring timestamp_to_str(uint64_t seconds);
    }

    namespace num {
        inline constexpr uint32_t rotate_left(uint32_t v, unsigned int offset) {
            return (v << offset) | (v >> ((CHAR_BIT * sizeof v) - offset));
        }
        inline uint64_t gen_global_seqid(void) {
            static uint64_t counter = 0;
            return counter++;
        }
        inline std::optional<double> try_parse_f64(std::string_view sv) {
            auto begin = &*sv.begin();
            auto end = &*sv.end();
            double val;
            auto result = std::from_chars(begin, end, val);
            if (!static_cast<bool>(result.ec) && result.ptr == end) {
                return val;
            }
            return std::nullopt;
        }
        inline std::optional<double> try_parse_f64(std::wstring_view sv) {
            // TODO: Optimize performance by not creating std::wstring
            size_t count;
            try {
                return std::stod(std::wstring{ sv }, &count);
            }
            catch (...) { return std::nullopt; }
        }
        inline std::optional<int64_t> try_parse_i64(std::string_view sv) {
            auto begin = &*sv.begin();
            auto end = &*sv.end();
            int64_t val;
            auto result = std::from_chars(begin, end, val);
            if (!static_cast<bool>(result.ec) && result.ptr == end) {
                return val;
            }
            return std::nullopt;
        }
        inline std::optional<int64_t> try_parse_i64(std::wstring_view sv) {
            static_assert(std::is_same_v<int64_t, long long>, "int64_t != long long");
            // TODO: Optimize performance by not creating std::wstring
            size_t count;
            try {
                return std::stoll(std::wstring{ sv }, &count);
            }
            catch (...) { return std::nullopt; }
        }
        inline std::optional<uint64_t> try_parse_u64(std::string_view sv) {
            auto begin = &*sv.begin();
            auto end = &*sv.end();
            uint64_t val;
            auto result = std::from_chars(begin, end, val);
            if (!static_cast<bool>(result.ec) && result.ptr == end) {
                return val;
            }
            return std::nullopt;
        }
        inline std::optional<uint64_t> try_parse_u64(std::wstring_view sv) {
            static_assert(std::is_same_v<uint64_t, unsigned long long>, "uint64_t != long long");
            // TODO: Optimize performance by not creating std::wstring
            size_t count;
            try {
                return std::stoull(std::wstring{ sv }, &count);
            }
            catch (...) { return std::nullopt; }
        }
    }

    namespace debug {
        enum class LogLevel : unsigned {
            Trace = 0, Debug, Info, Warn, Error,
        };
        class LoggingProvider {
        public:
            virtual void set_log_level(LogLevel new_level) = 0;
            virtual void log(std::wstring_view str, std::source_location const& loc) = 0;
            virtual void log_trace(std::wstring_view str, std::source_location const& loc) = 0;
            virtual void log_debug(std::wstring_view str, std::source_location const& loc) = 0;
            virtual void log_info(std::wstring_view str, std::source_location const& loc) = 0;
            virtual void log_warn(std::wstring_view str, std::source_location const& loc) = 0;
            virtual void log_error(std::wstring_view str, std::source_location const& loc) = 0;
            virtual ~LoggingProvider() {}
        };
        // Pass nullptr to disable logging
        void set_log_provider(LoggingProvider* provider);
        LoggingProvider* get_log_provider(void);
        // TODO: std::source_location shows full file path, maybe change this?
        inline void log_trace(
            std::wstring_view str, std::source_location const& loc = std::source_location::current()
        ) {
            if (auto provider = get_log_provider()) {
                provider->log_trace(str, loc);
            }
        }
        inline void log_debug(
            std::wstring_view str, std::source_location const& loc = std::source_location::current()
        ) {
            if (auto provider = get_log_provider()) {
                provider->log_debug(str, loc);
            }
        }
        inline void log_info(
            std::wstring_view str, std::source_location const& loc = std::source_location::current()
        ) {
            if (auto provider = get_log_provider()) {
                provider->log_info(str, loc);
            }
        }
        inline void log_warn(
            std::wstring_view str, std::source_location const& loc = std::source_location::current()
        ) {
            if (auto provider = get_log_provider()) {
                provider->log_warn(str, loc);
            }
        }
        inline void log_error(
            std::wstring_view str, std::source_location const& loc = std::source_location::current()
        ) {
            if (auto provider = get_log_provider()) {
                provider->log_error(str, loc);
            }
        }

        class RAIIObserver {
        public:
            RAIIObserver(std::source_location const& loc = std::source_location::current()) : m_loc(loc) {
                log_trace(std::format(L"Constructed RAIIObserver at line {}", m_loc.line()), m_loc);
            }
            RAIIObserver(RAIIObserver const& s, std::source_location const& loc = std::source_location::current()) :
                m_loc(loc)
            {
                log_trace(
                    std::format(L"Copied RAIIObserver at line {} from line {}", m_loc.line(), s.m_loc.line()),
                    m_loc
                );
            }
            ~RAIIObserver() {
                log_trace(std::format(L"Destructed RAIIObserver which came from line {}", m_loc.line()));
            }
        private:
            std::source_location m_loc;
        };
    }

    namespace cryptography {
        class Md5 {
        private:
            uint32_t temp_chunk[16];
            uint64_t data_length;
            uint32_t h0, h1, h2, h3;

            void process_chunk(void);
        public:
            Md5();
            ~Md5();

            void initialize(void);
            void finialize(void);

            void add_byte(uint8_t byte);
            void add_string(std::string_view str);
            void add_string(std::wstring_view str);

            std::wstring get_result_as_str(void);
        };
    }

    namespace mem {
        // Source: https://stackoverflow.com/a/21028912
        template <typename T, typename A = std::allocator<T>>
        class default_init_allocator : public A {
            typedef std::allocator_traits<A> a_t;
        public:
            template <typename U> struct rebind {
                using other =
                    default_init_allocator<
                    U, typename a_t::template rebind_alloc<U>>;
            };

            using A::A;

            template <typename U>
            void construct(U* ptr) noexcept(std::is_nothrow_default_constructible<U>::value) {
                ::new(static_cast<void*>(ptr)) U;
            }
            template <typename U, typename...Args>
            void construct(U* ptr, Args&&... args) {
                a_t::construct(static_cast<A&>(*this),
                    ptr, std::forward<Args>(args)...);
            }
        };
    }

    namespace container {
        // TODO: Make monotonic_vector more feature complete like std::vector
        // WARN: All operations changing the content may cause iterators to be invalidated, use with caution
        template<typename T, typename Compare = std::less<typename std::vector<T>::value_type>>
        class monotonic_vector {
        public:
            using Container = std::vector<T>;

            using value_type = typename Container::value_type;
            using allocator_type = typename Container::allocator_type;
            using size_type = typename Container::size_type;
            using difference_type = typename Container::difference_type;
            using reference = typename Container::reference;
            using const_reference = typename Container::const_reference;
            using pointer = typename Container::pointer;
            using const_pointer = typename Container::const_pointer;
            using iterator = typename Container::iterator;
            using const_iterator = typename Container::const_iterator;
            using reverse_iterator = typename Container::reverse_iterator;
            using const_reverse_iterator = typename Container::const_reverse_iterator;

            using value_compare = Compare;

            monotonic_vector() = default;
            explicit monotonic_vector(Compare const& pred) : c(), comp(pred) {}

            // Simple proxy functions
            reference at(size_type pos) { return c.at(pos); }
            const_reference at(size_type pos) const { return c.at(pos); }
            reference operator[](size_type pos) noexcept { return c[pos]; }
            const_reference operator[](size_type pos) const noexcept { return c[pos]; }
            reference front() noexcept { return c.front(); }
            const_reference front() const noexcept { return c.front(); }
            reference back() noexcept { return c.back(); }
            const_reference back() const noexcept { return c.back(); }
            value_type* data() noexcept { return c.data(); }
            const value_type* data() const noexcept { return c.data(); }
            iterator begin() noexcept { return c.begin(); }
            const_iterator begin() const noexcept { return c.begin(); };
            const_iterator cbegin() const noexcept { return c.cbegin(); };
            iterator end() noexcept { return c.end(); }
            const_iterator end() const noexcept { return c.end(); };
            const_iterator cend() const noexcept { return c.cend(); };
            reverse_iterator rbegin() noexcept { return c.rbegin(); }
            const_reverse_iterator rbegin() const noexcept { return c.rbegin(); };
            const_reverse_iterator crbegin() const noexcept { return c.crbegin(); };
            reverse_iterator rend() noexcept { return c.rend(); }
            const_reverse_iterator rend() const noexcept { return c.rend(); };
            const_reverse_iterator crend() const noexcept { return c.crend(); };
            bool empty() const noexcept { return c.empty(); }
            size_type size() const noexcept { return c.size(); }
            size_type max_size() const noexcept { return c.max_size(); }
            void reserve(size_type new_cap) { c.reserve(new_cap); }
            size_type capacity() const noexcept { return c.capacity(); }
            void shrink_to_fit() { c.shrink_to_fit(); }
            void clear() noexcept { c.clear(); }
            iterator erase(iterator pos) { return c.erase(pos); }

            // No push_back(), ...

            // Redesigned proxy functions
            // NOTE: This method requires vector to be ordered; modifying via iterators
            //       can break this promise and cause undefined behavior. To workaround
            //       this issue, call method ensure_ordered() beforehand.
            iterator insert(const T& value) {
                return c.insert(std::upper_bound(c.begin(), c.end(), value, comp), value);
            }
            /*template< class... Args >
            iterator emplace(Args&&... args) {
                auto iter = c.begin();
                auto iter_end = c.end();
                for (; iter != iter_end; iter++) {
                    if (comp(value, *iter)) {
                        break;
                    }
                }
                return c.emplace(iter, std::forward<Args>(args));
            }*/
            // TODO...

            // Extra functions
            void ensure_ordered() {
                std::stable_sort(c.begin(), c.end(), comp);
            }
        private:
            Container c{};
            Compare comp{};
        };
    }

    namespace fs {
        bool create_dir(const wchar_t* path) noexcept;
        bool create_dir_all(const wchar_t* path) noexcept;
        bool path_exists(const wchar_t* path) noexcept;
        bool delete_file(const wchar_t* path) noexcept;
        bool delete_file_if_exists(const wchar_t* path) noexcept;
        bool touch_file(const wchar_t* path) noexcept;
        // NOTE: This function does not guarantee success for paths
        //       across different volumes / file systems
        bool rename_path(const wchar_t* orig_path, const wchar_t* new_path) noexcept;
        // Path must represent a folder; returns UINT64_MAX on failure
        uint64_t calc_folder_size(const wchar_t* path) noexcept;
        bool delete_all_inside_folder(const wchar_t* path) noexcept;
        bool delete_folder(const wchar_t* path) noexcept;
    }

    namespace win32 {
        // TODO...
    }

    namespace winrt {
#define co_safe_capture_val(val)                            \
    auto CONCAT_3(temp_capture_, val, __LINE__){ val };     \
    auto& val{ CONCAT_3(temp_capture_, val, __LINE__) }
#define co_safe_capture_ref(val)                            \
    auto CONCAT_3(temp_capture_, val, __LINE__){ &(val) };  \
    auto& val{ *CONCAT_3(temp_capture_, val, __LINE__) }
#define co_safe_capture(val) co_safe_capture_val(val)

        // WARN: Make sure operations surrounded by the following pair of
        //       macros are idempotent!
#define http_client_safe_invoke_begin do { try {
#define http_client_safe_invoke_end }                                               \
    catch (winrt::hresult_error const& e) {                                         \
        util::debug::log_debug(L"http_client_safe_invoke: Detected exception");     \
        if (e.code() == E_CHANGED_STATE) { continue; }                              \
        throw;                                                                      \
    } break; } while (true)

        // TODO: Check if this function should be placed in util::debug instead
        inline void log_current_exception(std::source_location const& loc = std::source_location::current()) noexcept {
            try { throw; }
            catch (::winrt::hresult_error const& e) {
                auto error_message = e.message();
                util::debug::log_error(std::format(
                    L"Uncaught async exception(hresult_error): 0x{:08x}: {}",
                    static_cast<uint32_t>(e.code()), error_message
                ), loc);
                //if (IsDebuggerPresent()) { __debugbreak(); }
            }
            catch (std::exception const& e) {
                auto error_message = e.what();
                util::debug::log_error(std::format(
                    L"Uncaught async exception(std::exception): {}",
                    ::winrt::to_hstring(error_message)
                ), loc);
                //if (IsDebuggerPresent()) { __debugbreak(); }
            }
            catch (const wchar_t* e) {
                auto error_message = e;
                util::debug::log_error(std::format(
                    L"Uncaught async exception(wchar_t*): {}", error_message
                ), loc);
                //if (IsDebuggerPresent()) { __debugbreak(); }
            }
            catch (...) {
                auto error_message = L"Unknown exception was thrown";
                util::debug::log_error(std::format(
                    L"Uncaught async exception(any): {}", error_message
                ), loc);
                //if (IsDebuggerPresent()) { __debugbreak(); }
            }
        }

        // Same as ::winrt::fire_and_forget, except that it reports unhandled exceptions
        // Source: https://devblogs.microsoft.com/oldnewthing/20190320-00/?p=102345
        struct fire_forget_except {
            struct promise_type {
                fire_forget_except get_return_object() const noexcept { return {}; }
                void return_void() const noexcept {}
                std::suspend_never initial_suspend() const noexcept { return {}; }
                std::suspend_never final_suspend() const noexcept { return {}; }
                void unhandled_exception() noexcept {
                    log_current_exception();
                }
            };
        };

        template<typename Functor, typename T>
        void run_when_loaded(Functor&& functor, T const& elem) {
            if (elem.IsLoaded()) {
                functor(elem);
            }
            else {
                using ::winrt::Windows::UI::Xaml::RoutedEventArgs;
                using ::winrt::Windows::Foundation::IInspectable;
                auto revoke_et = std::make_shared_for_overwrite<::winrt::event_token>();
                *revoke_et = elem.Loaded(
                    [revoke_et, functor = std::forward<Functor>(functor)](
                        IInspectable const& sender, RoutedEventArgs const&)
                    {
                        auto elem = sender.as<T>();
                        elem.Loaded(*revoke_et);
                        functor(std::move(elem));
                    }
                );
            }
        }

        template<typename T>
        inline auto try_unbox_value(::winrt::Windows::Foundation::IInspectable const& value) {
            return value.try_as<T>();
        }

        ::winrt::Windows::UI::Xaml::UIElement get_child_elem(
            ::winrt::Windows::UI::Xaml::UIElement const& elem,
            std::wstring_view name,
            std::wstring_view class_name = L""
        );
        ::winrt::Windows::UI::Xaml::UIElement get_child_elem(
            ::winrt::Windows::UI::Xaml::UIElement const& elem,
            int32_t idx = 0
        );
        template<typename T = ::winrt::Windows::UI::Xaml::UIElement>
        T get_first_descendant(
            ::winrt::Windows::UI::Xaml::UIElement const& elem,
            std::wstring_view name = L"",
            std::wstring_view class_name = L""
        ) {
            using ::winrt::Windows::UI::Xaml::Media::VisualTreeHelper;
            using ::winrt::Windows::UI::Xaml::FrameworkElement;
            using ::winrt::Windows::UI::Xaml::UIElement;
            int32_t child_elem_cnt;
            if (!elem) { return nullptr; }
            child_elem_cnt = VisualTreeHelper::GetChildrenCount(elem);
            for (int32_t i = 0; i < child_elem_cnt; i++) {
                bool is_name_match = false, is_class_match = false;
                auto cur_elem = VisualTreeHelper::GetChild(elem, i).as<UIElement>();
                if (auto target = cur_elem.try_as<T>()) {
                    if (name != L"") {
                        if (auto fe = target.try_as<FrameworkElement>()) {
                            is_name_match = (fe.Name() == name);
                        }
                    }
                    else {
                        is_name_match = true;
                    }
                    if (class_name != L"") {
                        is_class_match = (::winrt::get_class_name(target) == class_name);
                    }
                    else {
                        is_class_match = true;
                    }

                    if (is_name_match && is_class_match) {
                        return target;
                    }
                }
                if (auto target = get_first_descendant<T>(cur_elem, name, class_name)) { return target; }
            }
            return nullptr;
        }
        inline ::winrt::Windows::UI::Xaml::Controls::ScrollViewer get_descendant_scrollviewer(
            ::winrt::Windows::UI::Xaml::UIElement const& elem
        ) {
            return get_first_descendant<::winrt::Windows::UI::Xaml::Controls::ScrollViewer>(elem);
        }

        inline ::winrt::Windows::Foundation::IInspectable get_app_res_as_object(::winrt::hstring key) {
            static auto cur_app_res = ::winrt::Windows::UI::Xaml::Application::Current().Resources();
            return cur_app_res.Lookup(box_value(key));
        }
        template<typename T>
        inline auto get_app_res(::winrt::hstring key) {
            return get_app_res_as_object(key).as<T>();
        }
        template<typename T>
        inline auto try_get_app_res(::winrt::hstring key) {
            return get_app_res_as_object(key).try_as<T>();
        }

        // Path must represent a folder; returns UINT64_MAX on failure
        ::winrt::Windows::Foundation::IAsyncOperation<uint64_t> calc_folder_size(::winrt::hstring path);
        ::winrt::Windows::Foundation::IAsyncOperation<bool> delete_all_inside_folder(::winrt::hstring path);
        ::winrt::Windows::Foundation::IAsyncOperation<bool> delete_folder(::winrt::hstring path);

        ::winrt::guid gen_random_guid(void);
        std::wstring to_wstring(::winrt::guid const& value);
        ::winrt::hstring to_hstring(::winrt::guid const& value);

        inline ::winrt::guid to_guid(::winrt::hstring const& s) {
            return ::winrt::guid{ s };
        }
        inline ::winrt::guid to_guid(std::string_view s) {
            return ::winrt::guid{ s };
        }
        inline ::winrt::guid to_guid(std::wstring_view s) {
            return ::winrt::guid{ s };
        }

        // Source: https://devblogs.microsoft.com/oldnewthing/20210301-00/?p=104914
        struct awaitable_event {
            void set() const noexcept {
                SetEvent(os_handle());
            }
            void reset() const noexcept {
                ResetEvent(os_handle());
            }
            auto operator co_await() const noexcept {
                return ::winrt::resume_on_signal(os_handle());
            }
        private:
            HANDLE os_handle() const noexcept {
                return handle.get();
            }
            ::winrt::handle handle{ ::winrt::check_pointer(CreateEventW(nullptr, true, false, nullptr)) };
        };

        inline bool update_popups_theme(
            ::winrt::Windows::UI::Xaml::XamlRoot const& xaml_root,
            ::winrt::Windows::UI::Xaml::ElementTheme theme
        ) {
            using ::winrt::Windows::UI::Xaml::Media::VisualTreeHelper;
            auto ivec = VisualTreeHelper::GetOpenPopupsForXamlRoot(xaml_root);
            for (auto&& i : ivec) {
                i.RequestedTheme(theme);
            }
        }

        inline void fix_content_dialog_theme(
            ::winrt::Windows::UI::Xaml::Controls::ContentDialog const& cd,
            ::winrt::Windows::UI::Xaml::FrameworkElement const& theme_base
        ) {
            using ::winrt::Windows::UI::Xaml::FrameworkElement;
            using ::winrt::Windows::Foundation::IInspectable;
            using ::winrt::Windows::UI::Xaml::Controls::ContentDialog;
            using ::winrt::Windows::UI::Xaml::Controls::ContentDialogOpenedEventArgs;
            // Prevent cyclic references causing resource leak
            auto revoker = theme_base.ActualThemeChanged(::winrt::auto_revoke,
                [ref = ::winrt::make_weak(cd)](FrameworkElement const& sender, IInspectable const&) {
                    if (auto cd = ref.get()) {
                        cd.RequestedTheme(sender.RequestedTheme());
                    }
                }
            );
            cd.Opened(
                [revoker = std::move(revoker), ref = ::winrt::make_weak(theme_base)]
                (ContentDialog const& sender, ContentDialogOpenedEventArgs const&) {
                    if (auto theme_base = ref.get()) {
                        sender.RequestedTheme(theme_base.ActualTheme());
                    }
                }
            );
        }

        inline void force_focus_element(
            ::winrt::Windows::UI::Xaml::Controls::Control const& elem,
            ::winrt::Windows::UI::Xaml::FocusState state
        ) {
            auto is_tab_stop = elem.IsTabStop();
            if (!is_tab_stop) {
                elem.IsTabStop(true);
            }
            elem.Focus(state);
            if (!is_tab_stop) {
                elem.IsTabStop(false);
            }
        }

        // NOTE: This function discards Ctrl+Tab event for target elements by
        //       temporarily shifting focus to helper element, then restoring
        //       focus quickly
        inline void discard_ctrl_tab_for_elem(
            ::winrt::Windows::UI::Xaml::Controls::Control const& target,
            ::winrt::Windows::UI::Xaml::Controls::Control const& helper_elem
        ) {
            using ::winrt::Windows::Foundation::IInspectable;
            using ::winrt::Windows::UI::Xaml::FocusState;
            using ::winrt::Windows::UI::Xaml::Input::KeyRoutedEventArgs;
            using ::winrt::Windows::UI::Core::CoreVirtualKeyStates;
            using ::winrt::Windows::UI::Core::CoreDispatcherPriority;
            using ::winrt::Windows::System::VirtualKey;
            target.PreviewKeyDown(
                [weak_t = ::winrt::make_weak(target), weak_he = ::winrt::make_weak(helper_elem)]
                (IInspectable const&, KeyRoutedEventArgs const& e) {
                    auto strong_t = weak_t.get();
                    auto strong_he = weak_he.get();
                    if (!strong_t || !strong_he) { return; }
                    auto cur_core_window = ::winrt::Windows::UI::Xaml::Window::Current().CoreWindow();
                    bool is_ctrl_down = static_cast<bool>(
                        cur_core_window.GetKeyState(VirtualKey::Control) & CoreVirtualKeyStates::Down);
                    if (is_ctrl_down && e.OriginalKey() == VirtualKey::Tab) {
                        // Routes event to parent elements
                        // This should be a good enough workaround without apparent flaws
                        auto is_tab_stop = strong_he.IsTabStop();
                        if (!is_tab_stop) { strong_he.IsTabStop(true); }
                        strong_he.Focus(FocusState::Programmatic);
                        cur_core_window.Dispatcher().RunAsync(CoreDispatcherPriority::High, [=]() {
                            strong_t.Focus(FocusState::Programmatic);
                            if (!is_tab_stop) { strong_he.IsTabStop(false); }
                        });
                    }
                }
            );
        }

        inline ::winrt::Windows::Media::Playback::MediaPlaybackSession try_get_media_playback_session(
            ::winrt::Windows::Media::Playback::MediaPlayer const& mp
        ) {
            if (!mp) { return nullptr; }
            if (!mp.Source()) { return nullptr; }
            return mp.PlaybackSession();
        }

        inline bool is_web_link_uri(::winrt::Windows::Foundation::Uri const& uri) {
            auto scheme = uri.SchemeName();
            return scheme == L"https" || scheme == L"http";
        }

        inline auto make_text_block(::winrt::hstring const& text) {
            ::winrt::Windows::UI::Xaml::Controls::TextBlock tb;
            tb.Text(text);
            return tb;
        }
        inline auto make_symbol_icon(::winrt::Windows::UI::Xaml::Controls::Symbol symbol) {
            ::winrt::Windows::UI::Xaml::Controls::SymbolIcon si;
            si.Symbol(symbol);
            return si;
        }

        // Source: https://rudyhuyn.azurewebsites.net/blog/2019/09/25/detect-the-display-mode-of-your-uwp-window/
        enum class AppViewWindowingMode {
            Unknown,
            Windowed,
            Maximized,
            FullScreen,
            FullScreenTabletMode,
            SnappedLeft,
            SnappedRight,
            CompactOverlay,
        };
        bool is_mixed_reality(void);
        AppViewWindowingMode get_cur_view_windowing_mode(void);

        inline constexpr ::winrt::Windows::UI::Color color_from_argb(
            uint8_t a, uint8_t r, uint8_t g, uint8_t b
        ) {
            return { .A = a, .R = r, .G = g, .B = b };
        }
        // cfore.A * cfore + (1 - cfore.A) * cback
        inline constexpr ::winrt::Windows::UI::Color blend_colors_2(
            ::winrt::Windows::UI::Color cfore, ::winrt::Windows::UI::Color cback
        ) {
            return {
                .A = cback.A,
                .R = static_cast<uint8_t>((cfore.A * cfore.R + (255 - cfore.A) * cback.R) / 255),
                .G = static_cast<uint8_t>((cfore.A * cfore.G + (255 - cfore.A) * cback.G) / 255),
                .B = static_cast<uint8_t>((cfore.A * cfore.B + (255 - cfore.A) * cback.B) / 255),
            };
        }

        // Like concurrency::task, but with built-in cancellation support
        template<typename ReturnType = void>
        struct task : ::winrt::enable_await_cancellation {
            using ReturnWrapType = typename std::shared_ptr<ReturnType>;

            task() :
                m_task(), m_cts(concurrency::cancellation_token_source::_FromImpl(nullptr)),
                m_cancellable(nullptr) {}
            task(std::nullptr_t) : task() {}

            operator bool() { return m_task != decltype(m_task){}; }
            auto& operator=(task other) {
                using std::swap;
                swap(m_task, other.m_task);
                swap(m_cts, other.m_cts);
                swap(m_cancellable, other.m_cancellable);
                return *this;
            }

            struct promise_type {
                promise_type() : m_cancellable(std::make_shared<::winrt::cancellable_promise>()) {}
                task get_return_object() {
                    return {
                        concurrency::create_task(m_tce, concurrency::task_options{ m_cts.get_token() }),
                        m_cts,
                        m_cancellable
                    };
                }
                std::suspend_never initial_suspend() const noexcept { return {}; }
                std::suspend_never final_suspend() const noexcept { return {}; }
                void return_value(ReturnType value) {
                    // Use std::shared_ptr to allow for move-only types
                    // and reduce possibly expensive copy costs
                    m_tce.set(std::make_shared<ReturnType>(std::move(value)));
                }
                void unhandled_exception() {
                    m_tce.set_exception(std::current_exception());
                }
                template<typename Expression>
                auto await_transform(Expression&& expression) {
                    if (m_cts.get_token().is_canceled()) {
                        throw ::winrt::hresult_canceled();
                    }
                    return ::winrt::impl::notify_awaiter<Expression>{
                        static_cast<Expression&&>(expression),
                        m_propagate_cancellation ? &*m_cancellable : nullptr
                    };
                }
                ::winrt::impl::cancellation_token<promise_type> await_transform(
                    ::winrt::get_cancellation_token_t
                ) noexcept {
                    return { static_cast<promise_type*>(this) };
                }
                bool enable_cancellation_propagation(bool value) noexcept {
                    return std::exchange(m_propagate_cancellation, value);
                }
                void cancellation_callback(::winrt::delegate<>&& cancel) noexcept {
                    m_cts.get_token().register_callback(cancel);
                }
            private:
                concurrency::task_completion_event<ReturnWrapType> m_tce;
                concurrency::cancellation_token_source m_cts;
                std::shared_ptr<::winrt::cancellable_promise> m_cancellable;
                bool m_propagate_cancellation{ false };
            };

            bool await_ready() const {
                return m_task.is_done();
            }
            void await_suspend(std::coroutine_handle<> resume) const {
                m_task.then(
                    [resume](concurrency::task<ReturnWrapType> const&) { resume(); },
                    concurrency::task_continuation_context::get_current_winrt_context()
                );
            }
            // TODO: msvc is possibly dishonoring the lvalue reference and forcing
            //       a move; double check and report this in the future
            ReturnType& await_resume() const {
                return *m_task.get();
            }
            void enable_cancellation(::winrt::cancellable_promise* promise) {
                promise->set_canceller([](void* context) {
                    auto that = static_cast<task*>(context);
                    that->cancel();
                }, this);
            }
            void cancel(void) {
                // TODO: Mutex protection
                if (!m_cts.get_token().is_canceled()) {
                    m_cancellable->cancel();
                    m_cts.cancel();
                }
            }
            ~task() {
                // Swallow exceptions, if any
                if (!*this) { return; }
                m_task.then([](concurrency::task<ReturnWrapType> const& task) {
                    try { task.wait(); }
                    catch (...) { util::winrt::log_current_exception(); }
                });
            }
        private:
            task(
                concurrency::task<ReturnWrapType> task,
                concurrency::cancellation_token_source cts,
                std::shared_ptr<::winrt::cancellable_promise> cancellable
            ) : m_task(std::move(task)), m_cts(std::move(cts)), m_cancellable(std::move(cancellable)) {}

            concurrency::task<ReturnWrapType> m_task;
            concurrency::cancellation_token_source m_cts;
            std::shared_ptr<::winrt::cancellable_promise> m_cancellable;
        };
        // task<void> specialization
        template<>
        struct task<void> : ::winrt::enable_await_cancellation {
            task() :
                m_task(), m_cts(concurrency::cancellation_token_source::_FromImpl(nullptr)),
                m_cancellable(nullptr) {}
            task(std::nullptr_t) : task() {}

            operator bool() { return m_task != decltype(m_task){}; }
            auto& operator=(task other) {
                using std::swap;
                swap(m_task, other.m_task);
                swap(m_cts, other.m_cts);
                swap(m_cancellable, other.m_cancellable);
                return *this;
            }

            struct promise_type {
                promise_type() : m_cancellable(std::make_shared<::winrt::cancellable_promise>()) {}
                task get_return_object() {
                    return {
                        concurrency::create_task(m_tce, concurrency::task_options{ m_cts.get_token() }),
                        m_cts,
                        m_cancellable
                    };
                }
                std::suspend_never initial_suspend() const noexcept { return {}; }
                std::suspend_never final_suspend() const noexcept { return {}; }
                void return_void() {
                    m_tce.set();
                }
                void unhandled_exception() {
                    m_tce.set_exception(std::current_exception());
                }
                template<typename Expression>
                auto await_transform(Expression&& expression) {
                    if (m_cts.get_token().is_canceled()) {
                        throw ::winrt::hresult_canceled();
                    }
                    return ::winrt::impl::notify_awaiter<Expression>{
                        static_cast<Expression&&>(expression),
                            m_propagate_cancellation ? &*m_cancellable : nullptr
                    };
                }
                ::winrt::impl::cancellation_token<promise_type> await_transform(
                    ::winrt::get_cancellation_token_t
                ) noexcept {
                    return { static_cast<promise_type*>(this) };
                }
                bool enable_cancellation_propagation(bool value) noexcept {
                    return std::exchange(m_propagate_cancellation, value);
                }
                void cancellation_callback(::winrt::delegate<>&& cancel) noexcept {
                    m_cts.get_token().register_callback(cancel);
                }
            private:
                concurrency::task_completion_event<void> m_tce;
                concurrency::cancellation_token_source m_cts;
                std::shared_ptr<::winrt::cancellable_promise> m_cancellable;
                bool m_propagate_cancellation{ false };
            };

            bool await_ready() const {
                return m_task.is_done();
            }
            void await_suspend(std::coroutine_handle<> resume) const {
                m_task.then(
                    [resume](concurrency::task<void> const&) { resume(); },
                    concurrency::task_continuation_context::get_current_winrt_context()
                );
            }
            void await_resume() const {
                m_task.get();
            }
            void enable_cancellation(::winrt::cancellable_promise* promise) {
                promise->set_canceller([](void* context) {
                    auto that = static_cast<task*>(context);
                    that->cancel();
                }, this);
            }
            void cancel(void) {
                // TODO: Mutex protection
                if (!m_cts.get_token().is_canceled()) {
                    m_cancellable->cancel();
                    m_cts.cancel();
                }
            }
            ~task() {
                // Swallow exceptions, if any
                if (!*this) { return; }
                m_task.then([](concurrency::task<void> const& task) {
                    try { task.wait(); }
                    catch (...) { util::winrt::log_current_exception(); }
                });
            }
        private:
            task(
                concurrency::task<void> task,
                concurrency::cancellation_token_source cts,
                std::shared_ptr<::winrt::cancellable_promise> cancellable
            ) : m_task(std::move(task)), m_cts(std::move(cts)), m_cancellable(std::move(cancellable)) {}

            concurrency::task<void> m_task;
            concurrency::cancellation_token_source m_cts;
            std::shared_ptr<::winrt::cancellable_promise> m_cancellable;
        };
        // Like winrt::resume_after, but calling contexts are preserved
        inline ::winrt::Windows::Foundation::IAsyncAction resume_after(
            ::winrt::Windows::Foundation::TimeSpan duration
        ) {
            co_await ::winrt::resume_after(duration);
        }

        inline void set_clipboard_text(::winrt::hstring const& text, bool flush) {
            using namespace ::winrt::Windows::ApplicationModel::DataTransfer;
            auto data_package = DataPackage();
            data_package.RequestedOperation(DataPackageOperation::Copy);
            data_package.SetText(text);
            Clipboard::SetContent(data_package);
            if (flush) {
                Clipboard::Flush();
            }
        }

        inline void cancel_async(::winrt::Windows::Foundation::IAsyncInfo async) {
            if (async) { async.Cancel(); }
        }

        inline bool is_async_running(::winrt::Windows::Foundation::IAsyncInfo async) {
            return async && async.Status() == ::winrt::Windows::Foundation::AsyncStatus::Started;
        }

        // NOTE: Smartly manages the lifetime of async operations
        // NOTE: Exceptions are currently ignored, this may not be desired
        // NOTE: Running async operations are canceled when async_storage destructs
        struct async_storage {
            async_storage() : m_data(std::make_shared<data>()), m_method_lock() {}
            async_storage(async_storage const&) = delete;
            async_storage& operator=(async_storage) = delete;
            ~async_storage() {
                safe_cancel_clear();
            }

            template<typename Functor, typename... Args>
            void cancel_and_run(Functor&& functor, Args&&... args) {
                std::scoped_lock method_call_guard{ m_method_lock };
                safe_cancel_clear();
                auto async = transform_async(
                    std::invoke(std::forward<Functor>(functor), std::forward<Args>(args)...)
                );
                {
                    std::scoped_lock guard{ m_data->lock };
                    m_data->async = async;
                }
                set_completed_handler_for_async(async);
            }
            // NOTE: Returns whether new task is run
            template<typename Functor, typename... Args>
            bool run_if_idle(Functor&& functor, Args&&... args) {
                std::scoped_lock method_call_guard{ m_method_lock };
                {
                    std::scoped_lock guard{ m_data->lock };
                    // If m_data->async is nullptr, we are confident that everything's really idle
                    if (m_data->async) { return false; }
                }
                auto async = std::invoke(std::forward<Functor>(functor), std::forward<Args>(args)...);
                {
                    // SAFETY: Lock is not needed here since there are no possible race conditions
                    m_data->async = async;
                }
                set_completed_handler_for_async(async);
                return true;
            }

            void cancel_running(void) {
                // NOTE: m_method_lock must be locked here, or it may interleave with other methods
                std::scoped_lock method_call_guard{ m_method_lock };
                safe_cancel_clear();
            }

            ::winrt::Windows::Foundation::IAsyncInfo peek_async(void) {
                std::scoped_lock method_call_guard{ m_method_lock };
                std::scoped_lock guard{ m_data->lock };
                return m_data->async;
            }

        private:
            void safe_cancel_clear(void) {
                m_data->lock.lock();
                // SAFETY: noexcept
                auto old_async = std::exchange(m_data->async, nullptr);
                m_data->lock.unlock();
                if (old_async) {
                    old_async.Cancel();
                }
            }

            template<typename T>
            auto transform_async(T&& async) {
                if constexpr (std::is_convertible_v<T, ::winrt::Windows::Foundation::IAsyncInfo>) {
                    return std::forward<T>(async);
                }
                else {
                    // TODO: Maybe optimize performance by not creating a new coroutine
                    return [](T async) -> ::winrt::Windows::Foundation::IAsyncAction {
                        auto cancellation_token = co_await ::winrt::get_cancellation_token();
                        cancellation_token.enable_propagation();
                        co_await async;
                    }(std::forward<T>(async));
                }
            }

            template<typename T>
            void set_completed_handler_for_async(T&& async) {
                struct disconnect_aware_handler {
                    disconnect_aware_handler(std::shared_ptr<data> data) : m_data(std::move(data)) {}
                    ~disconnect_aware_handler() {
                        if (m_data) {
                            (*this)(std::decay_t<T>{}, ::winrt::Windows::Foundation::AsyncStatus::Error);
                        }
                    }
                    void operator()(auto&& sender, ::winrt::Windows::Foundation::AsyncStatus status) {
                        if (status == ::winrt::Windows::Foundation::AsyncStatus::Started) { return; }
                        {
                            std::scoped_lock guard{ m_data->lock };
                            if (sender == m_data->async) {
                                // NOTE: Coroutine is not freed if IAsyncInfo is still alive
                                m_data->async = nullptr;
                            }
                        }
                        m_data = nullptr;
                    }
                private:
                    std::shared_ptr<data> m_data;
                };
                async.Completed(disconnect_aware_handler{ m_data });
            }

            struct data {
                std::mutex lock{};
                ::winrt::Windows::Foundation::IAsyncInfo async{ nullptr };
            };
            std::shared_ptr<data> m_data;
            std::mutex m_method_lock;
        };

        namespace details {
            template<typename T>
            class has_get_weak {
                template<typename U, typename = decltype(std::declval<U>().get_weak())>
                static constexpr bool get_value(int) { return true; }
                template<typename>
                static constexpr bool get_value(...) { return false; }
            public:
                static constexpr bool value = get_value<T>(0);
            };
            template<typename>
            struct is_com_ptr : std::false_type {};
            template<typename T>
            struct is_com_ptr<::winrt::com_ptr<T>> : std::true_type {};
            template<typename T>
            class has_member_operator_co_await {
                template<typename U, typename = decltype(std::declval<U>().operator co_await())>
                static constexpr bool get_value(int) { return true; }
                template<typename>
                static constexpr bool get_value(...) { return false; }
            public:
                static constexpr bool value = get_value<T>(0);
            };
            template<typename T>
            class has_nonmember_operator_co_await {
                template<typename U, typename = decltype(operator co_await(std::declval<U>()))>
                static constexpr bool get_value(int) { return true; }
                template<typename>
                static constexpr bool get_value(...) { return false; }
            public:
                static constexpr bool value = get_value<T>(0);
            };
            /*template<typename T>
            auto awaiter_from_awaitable(T&& awaitable) {
                using OT = std::decay_t<T>;
                if constexpr (has_member_operator_co_await<OT>::value) {
                    return awaitable.operator co_await();
                }
                else if constexpr (has_nonmember_operator_co_await<OT>::value) {
                    return operator co_await(static_cast<OT&&>(awaitable));
                }
                else {
                    struct awaiter_wrapper {
                        awaiter_wrapper(T a) : m_a(a) {}
                        bool await_ready() const {
                            return m_a.await_ready();
                        }
                        auto await_suspend(std::coroutine_handle<> resume) {
                            return m_a.await_suspend(resume);
                        }
                        decltype(auto) await_resume() {
                            return m_a.await_resume();
                        }

                        T m_a;
                    };
                    return awaiter_wrapper(awaitable);
                }
            }*/
        }

        // WARN: Don't use weak_storage directly. Instead, use make_weak_storage.
        template<typename T>
        struct weak_storage {
            weak_storage() : m_weak_ref(nullptr), m_strong_ref(nullptr) {}
            template<
                typename U = ::winrt::impl::com_ref<T> const&,
                typename = std::enable_if_t<std::is_constructible_v<::winrt::weak_ref<T>, U&&>>
            > weak_storage(U&& object) :
                m_weak_ref(::winrt::weak_ref{ std::forward<U>(object) }), m_strong_ref(nullptr) {}
            weak_storage(::winrt::weak_ref<T> weak) : m_weak_ref(std::move(weak)), m_strong_ref(nullptr) {}

            // Obtain a strong reference and pass ownership to caller
            auto get(void) noexcept {
                if (m_strong_ref) { return m_strong_ref; }
                return m_weak_ref.get();
            }
            // Obtain a strong reference and lock ownership until unlocked or destructed
            auto lock(void) noexcept {
                if (!m_strong_ref) { m_strong_ref = this->get(); }
                return m_strong_ref;
            }
            // Release previously locked ownership, if any
            void unlock(void) noexcept {
                m_strong_ref = nullptr;
            }

            // unlock-await-lock; throws hresult_canceled if failed
            template<typename U>
            auto ual(U&& awaitable) {
                struct ual_awaitable : ::winrt::enable_await_cancellation {
                    ual_awaitable() = delete;
                    ual_awaitable(U&& awaitable, weak_storage* that) :
                        m_awaiter(::winrt::impl::get_awaiter(static_cast<U&&>(awaitable))), m_that(that) {}
                    void enable_cancellation(::winrt::cancellable_promise* promise) {
                        if constexpr (std::is_convertible_v<
                            std::remove_reference_t<decltype(m_awaiter)>&, enable_await_cancellation&
                        >) {
                            // NOTE: Cancellation revoking happens in this class, not in the inner awaiter
                            m_awaiter.enable_cancellation(promise);
                        }
                    }
                    bool await_ready() const {
                        return m_awaiter.await_ready();
                    }
                    auto await_suspend(std::coroutine_handle<> resume) {
                        m_that->unlock();
                        return m_awaiter.await_suspend(resume);
                    }
                    decltype(auto) await_resume() {
                        if (!m_that->lock()) {
                            throw ::winrt::hresult_canceled(L"ual interrupted due to invalid weak reference");
                        }
                        return m_awaiter.await_resume();
                    }

                    decltype(::winrt::impl::get_awaiter(std::declval<U&&>())) m_awaiter;
                    weak_storage* m_that;
                };
                return ual_awaitable{ static_cast<U&&>(awaitable), this };
            }

            // WARN: Make sure it is in locked state before using operator->
            auto& operator*() noexcept {
                assert(m_strong_ref != nullptr);
                if constexpr (details::is_com_ptr<decltype(m_strong_ref)>::value) {
                    return m_strong_ref.operator*();
                }
                else {
                    return m_strong_ref;
                }
            }
            // WARN: Make sure it is in locked state before using operator->
            auto operator->() noexcept {
                assert(m_strong_ref != nullptr);
                if constexpr (details::is_com_ptr<decltype(m_strong_ref)>::value) {
                    return m_strong_ref.operator->();
                }
                else {
                    return &m_strong_ref;
                }
            }

        private:
            ::winrt::weak_ref<T> m_weak_ref;
            ::winrt::impl::com_ref<T> m_strong_ref;
        };
        template<typename T>
        weak_storage<::winrt::impl::wrapped_type_t<std::decay_t<T>>> make_weak_storage(T&& object) {
            if constexpr (details::has_get_weak<T>::value) {
                return object.get_weak();
            }
            else {
                return std::forward<T>(object);
            }
        }

        ::winrt::Windows::Foundation::IAsyncOperationWithProgress<
            ::winrt::Windows::Web::Http::IHttpContent, ::winrt::Windows::Web::Http::HttpProgress
        > fetch_partial_http_content(
            ::winrt::Windows::Foundation::Uri const& http_uri,
            ::winrt::Windows::Web::Http::HttpClient const& http_client,
            uint64_t pos, uint64_t size
        );
        ::winrt::Windows::Foundation::IAsyncOperationWithProgress<
            ::winrt::Windows::Storage::Streams::IBuffer, uint64_t
        > fetch_partial_http_as_buffer(
            ::winrt::Windows::Foundation::Uri const& http_uri,
            ::winrt::Windows::Web::Http::HttpClient const& http_client,
            uint64_t pos, uint64_t size
        );

        struct BufferBackedRandomAccessStream :
            ::winrt::implements<BufferBackedRandomAccessStream,
            ::winrt::Windows::Storage::Streams::IRandomAccessStream,
            ::winrt::Windows::Foundation::IClosable,
            ::winrt::Windows::Storage::Streams::IInputStream,
            ::winrt::Windows::Storage::Streams::IOutputStream>
        {
            BufferBackedRandomAccessStream(::winrt::Windows::Storage::Streams::IBuffer const& buffer,
                uint64_t start_pos = 0) : m_buffer(buffer), m_cur_pos(start_pos) {}
            ~BufferBackedRandomAccessStream() { Close(); }
            void Close() {
                std::scoped_lock guard(m_mutex);
                m_buffer = nullptr;
            }
            ::winrt::Windows::Foundation::IAsyncOperationWithProgress<::winrt::Windows::Storage::Streams::IBuffer, uint32_t> ReadAsync(
                ::winrt::Windows::Storage::Streams::IBuffer buffer,
                uint32_t count,
                ::winrt::Windows::Storage::Streams::InputStreamOptions options
            ) {
                std::scoped_lock guard(m_mutex);
                if (!m_buffer) { throw ::winrt::hresult_illegal_method_call(); }

                auto buf_len = m_buffer.Length();
                uint64_t actual_count = m_cur_pos >= buf_len ? 0 : buf_len - m_cur_pos;
                actual_count = std::min(static_cast<uint64_t>(count), actual_count);

                auto progress_token = co_await ::winrt::get_progress_token();
                progress_token(0);
                buffer.Length(static_cast<uint32_t>(actual_count));
                actual_count = std::min(static_cast<uint64_t>(buffer.Length()), actual_count);
                memcpy(
                    static_cast<void*>(buffer.data()),
                    static_cast<void*>(m_buffer.data() + m_cur_pos),
                    actual_count
                );
                m_cur_pos += actual_count;
                progress_token(static_cast<uint32_t>(actual_count));

                co_return buffer;
            }
            ::winrt::Windows::Foundation::IAsyncOperationWithProgress<uint32_t, uint32_t> WriteAsync(
                ::winrt::Windows::Storage::Streams::IBuffer buffer
            ) {
                std::scoped_lock guard(m_mutex);
                if (!m_buffer) { throw ::winrt::hresult_illegal_method_call(); }

                auto buf_cap = m_buffer.Capacity();
                uint64_t actual_count = m_cur_pos >= buf_cap ? 0 : buf_cap - m_cur_pos;
                actual_count = std::min(static_cast<uint64_t>(buffer.Length()), actual_count);

                auto progress_token = co_await ::winrt::get_progress_token();
                progress_token(0);
                if (m_cur_pos + actual_count > m_buffer.Length()) {
                    m_buffer.Length(static_cast<uint32_t>(m_cur_pos + actual_count));
                }
                memcpy(
                    static_cast<void*>(m_buffer.data() + m_cur_pos),
                    static_cast<void*>(buffer.data()),
                    actual_count
                );
                m_cur_pos += actual_count;
                progress_token(static_cast<uint32_t>(actual_count));

                co_return static_cast<uint32_t>(actual_count);
            }
            ::winrt::Windows::Foundation::IAsyncOperation<bool> FlushAsync() {
                std::scoped_lock guard(m_mutex);
                if (!m_buffer) { throw ::winrt::hresult_illegal_method_call(); }
                co_return true;
            }
            uint64_t Size() {
                std::scoped_lock guard(m_mutex);
                if (!m_buffer) { throw ::winrt::hresult_illegal_method_call(); }
                return m_buffer.Length();
            }
            void Size(uint64_t value) {
                std::scoped_lock guard(m_mutex);
                if (!m_buffer) { throw ::winrt::hresult_illegal_method_call(); }
                if (value > m_buffer.Capacity()) {
                    throw ::winrt::hresult_error(E_FAIL, L"IBuffer capacity exceeded");
                }
                m_buffer.Length(static_cast<uint32_t>(value));
            }
            ::winrt::Windows::Storage::Streams::IInputStream GetInputStreamAt(uint64_t position) {
                std::scoped_lock guard(m_mutex);
                if (!m_buffer) { throw ::winrt::hresult_illegal_method_call(); }
                return ::winrt::make<BufferBackedRandomAccessStream>(m_buffer, position);
            }
            ::winrt::Windows::Storage::Streams::IOutputStream GetOutputStreamAt(uint64_t position) {
                std::scoped_lock guard(m_mutex);
                if (!m_buffer) { throw ::winrt::hresult_illegal_method_call(); }
                return ::winrt::make<BufferBackedRandomAccessStream>(m_buffer, position);
            }
            uint64_t Position() {
                std::scoped_lock guard(m_mutex);
                if (!m_buffer) { throw ::winrt::hresult_illegal_method_call(); }
                return m_cur_pos;
            }
            void Seek(uint64_t position) {
                std::scoped_lock guard(m_mutex);
                if (!m_buffer) { throw ::winrt::hresult_illegal_method_call(); }
                m_cur_pos = position;
            }
            ::winrt::Windows::Storage::Streams::IRandomAccessStream CloneStream() {
                std::scoped_lock guard(m_mutex);
                if (!m_buffer) { throw ::winrt::hresult_illegal_method_call(); }
                return ::winrt::make<BufferBackedRandomAccessStream>(m_buffer);
            }
            bool CanRead() { return true; }
            bool CanWrite() { return true; }

        private:
            std::mutex m_mutex;
            ::winrt::Windows::Storage::Streams::IBuffer m_buffer;
            uint64_t m_cur_pos;
        };

        void persist_textbox_cc_clipboard(::winrt::Windows::UI::Xaml::Controls::TextBox const& tb);
        void persist_autosuggestbox_clipboard(::winrt::Windows::UI::Xaml::Controls::AutoSuggestBox const& ctrl);

        ::winrt::Windows::Storage::Streams::IRandomAccessStream string_to_utf8_stream(::winrt::hstring const& s);

        // A simple wrapper mutex around OS's synchronization primitives, with async support
        // NOTE: It is safe to destroy a locked mutex!
        // TODO: Maybe optimize async waiting performance
        // TODO: Rewrite using srwlock to avoid UB
        struct mutex {
            mutex() {}
            void lock(void) { m_mutex.lock(); }
            ::winrt::Windows::Foundation::IAsyncAction lock_async(void) {
                // Fast path for light contention
                for (size_t i = 0; i < MAX_SPIN_COUNT; i++) {
                    if (try_lock()) { co_return; }
                }
                co_await ::winrt::resume_background();
                m_mutex.lock();
            }
            bool try_lock(void) { return m_mutex.try_lock(); }
            template<class Rep, class Period>
            bool try_lock_for(const std::chrono::duration<Rep, Period>& timeout_duration) {
                return m_mutex.try_lock_for(timeout_duration);
            }
            template<class Rep, class Period>
            ::winrt::Windows::Foundation::IAsyncOperation<bool> try_lock_for_async(
                const std::chrono::duration<Rep, Period>& timeout_duration
            ) {
                // Fast path for light contention
                if (try_lock()) { co_return true; }
                co_await ::winrt::resume_background();
                co_return m_mutex.try_lock_for(timeout_duration);
            }
            template<class Clock, class Duration>
            bool try_lock_until(const std::chrono::time_point<Clock, Duration>& timeout_time) {
                return m_mutex.try_lock_until(timeout_time);
            }
            template<class Clock, class Duration>
            ::winrt::Windows::Foundation::IAsyncOperation<bool> try_lock_until_async(
                const std::chrono::time_point<Clock, Duration>& timeout_time
            ) {
                // Fast path for light contention
                if (try_lock()) { co_return true; }
                co_await ::winrt::resume_background();
                co_return m_mutex.try_lock_until(timeout_time);
            }
            void unlock(void) { m_mutex.unlock(); }
            void lock_shared(void) { m_mutex.lock_shared(); }
            ::winrt::Windows::Foundation::IAsyncAction lock_shared_async(void) {
                // Fast path for light contention
                for (size_t i = 0; i < MAX_SPIN_COUNT; i++) {
                    if (try_lock_shared()) { co_return; }
                }
                co_await ::winrt::resume_background();
                m_mutex.lock_shared();
            }
            bool try_lock_shared(void) { return m_mutex.try_lock_shared(); }
            template<class Rep, class Period>
            bool try_lock_shared_for(const std::chrono::duration<Rep, Period>& timeout_duration) {
                return m_mutex.try_lock_shared_for(timeout_duration);
            }
            template<class Rep, class Period>
            ::winrt::Windows::Foundation::IAsyncOperation<bool> try_lock_shared_for_async(
                const std::chrono::duration<Rep, Period>& timeout_duration
            ) {
                // Fast path for light contention
                if (try_lock_shared()) { co_return true; }
                co_await ::winrt::resume_background();
                co_return m_mutex.try_lock_shared_for(timeout_duration);
            }
            template<class Clock, class Duration>
            bool try_lock_shared_until(const std::chrono::time_point<Clock, Duration>& timeout_time) {
                return m_mutex.try_lock_shared_until(timeout_time);
            }
            template<class Clock, class Duration>
            ::winrt::Windows::Foundation::IAsyncOperation<bool> try_lock_shared_until_async(
                const std::chrono::time_point<Clock, Duration>& timeout_time
            ) {
                // Fast path for light contention
                if (try_lock_shared()) { co_return true; }
                co_await ::winrt::resume_background();
                co_return m_mutex.try_lock_shared_until(timeout_time);
            }
            void unlock_shared(void) { m_mutex.unlock_shared(); }
        private:
            static constexpr size_t MAX_SPIN_COUNT = 40;
            std::shared_timed_mutex m_mutex;
        };

        // NOTE: winrt::deferrable_event_args with support for multiple awaiters
        template<typename D>
        struct deferrable_event_args {
            ::winrt::Windows::Foundation::Deferral GetDeferral() {
                ::winrt::slim_lock_guard guard(m_lock);

                if (!m_co_handles.empty()) {
                    // Cannot ask for deferral after the event handler returned.
                    throw ::winrt::hresult_illegal_method_call(L"Getting deferral outside the event handler is disallowed");
                }

                ::winrt::Windows::Foundation::Deferral deferral{ {static_cast<D&>(*this).get_strong(), &deferrable_event_args::one_deferral_completed } };
                ++m_outstanding_deferrals;
                return deferral;
            }

            [[nodiscard]] ::winrt::Windows::Foundation::IAsyncAction wait_for_deferrals() {
                struct awaitable : std::suspend_always {
                    bool await_suspend(coroutine_handle handle) {
                        return m_deferrable.await_suspend(handle);
                    }

                    deferrable_event_args& m_deferrable;
                };

                co_await awaitable{ {}, *this };
            }

        private:
            using coroutine_handle = std::coroutine_handle<>;

            void one_deferral_completed() {
                std::vector<coroutine_handle> resume;
                {
                    ::winrt::slim_lock_guard guard(m_lock);

                    if (m_outstanding_deferrals <= 0) {
                        throw ::winrt::hresult_illegal_method_call();
                    }

                    if (--m_outstanding_deferrals == 0) {
                        resume.push_back(nullptr);
                        resume.swap(m_co_handles);
                    }
                }

                if (!resume.empty() && resume[0]) {
                    for (auto i : resume) {
                        ::winrt::impl::resume_background(i);
                    }
                }
            }

            bool await_suspend(coroutine_handle handle) noexcept {
                ::winrt::slim_lock_guard guard(m_lock);
                m_co_handles.push_back(handle);
                return m_outstanding_deferrals > 0;
            }

            ::winrt::slim_mutex m_lock;
            int32_t m_outstanding_deferrals = 0;
            std::vector<coroutine_handle> m_co_handles;
        };

        // A simple in-memory stream which supports IRandomAccessStream
        namespace details { struct InMemoryStreamImpl; }
        struct InMemoryStream {
            InMemoryStream();
            void size(size_t value) const;
            size_t size() const;
            void expand_on_overflow(bool value) const;
            bool expand_on_overflow() const;
            size_t read_at(void* buf, size_t pos, size_t count) const;
            size_t write_at(const void* buf, size_t pos, size_t count) const;
            ::winrt::Windows::Storage::Streams::IRandomAccessStream as_random_access_stream() const;
        private:
            std::shared_ptr<details::InMemoryStreamImpl> m_impl;
        };
    }

    namespace sync {
        // UNLIKELY TODO: Implement unbounded mpsc channel with linked list
        // TODO: Implement mutex-based mpmc channel

        // TODO: Maybe place size_t_msb to a better place?
        static constexpr size_t size_t_msb = ~(std::numeric_limits<size_t>::max() >> 1);
        // NOTE: These masks are for heads & tails
        static constexpr size_t disconnected_mask = size_t_msb;
        // Sufficient to avoid ABA problems
        static constexpr size_t turn_around_mask = size_t_msb >> 1;
        static constexpr size_t value_mask = ~(disconnected_mask | turn_around_mask);
        // NOTE: To close either sender or receiver, simply make them empty
        // NOTE: mpsc_channel is a low-performace channel and provides strong order guarantee
        // WARN: If multiple senders race, latecomers must wait until firstcomers finish
        // WARN: NEVER access receivers across threads
        // TODO: Fallback to std::atomic_flag version if atomic operations are not lock-free
        // TODO: mpsc_channel may have issues; write unit tests
        static_assert(
            std::atomic<size_t>::is_always_lock_free,
            "size_t operations are not lock-free; use mutex version instead"
        );
#pragma warning(push)
#pragma warning(disable: 4324)
        template<typename T>
        struct mpsc_channel_shared_ring_buffer {
            static_assert(
                std::is_nothrow_move_constructible_v<T> && std::is_nothrow_destructible_v<T>,
                "mpsc_channel requires non-throwing types to work correctly. "
                "Consider wrapping the type in a std::shared_ptr."
            );

            char* const buffer;
            const size_t capacity;
            // Use paired head & tail to ensure integrity
            alignas(std::hardware_destructive_interference_size)
                std::atomic<size_t> head1, tail1;   // Preallocated range
            alignas(std::hardware_destructive_interference_size)
                std::atomic<size_t> head2, tail2;   // Actual range
            std::atomic<size_t> sender_count, receiver_count;

            // TODO: Maybe use std::construct_at & std::destroy_at
            mpsc_channel_shared_ring_buffer(size_t n) :
                buffer{ new(operator new(n * sizeof(T), std::align_val_t(alignof(T)))) char[n * sizeof(T)] },
                capacity(n), head1(0), tail1(0), head2(0), tail2(0),
                sender_count(0), receiver_count(0) {}
            ~mpsc_channel_shared_ring_buffer() {
                auto head = head2.load(), tail = tail2.load();
                auto items_count = tail - head;
                head %= capacity;
                for (size_t i = 0; i < items_count; i++) {
                    if (head >= capacity) {
                        head = 0;
                    }
                    std::launder(reinterpret_cast<T*>(buffer + head * sizeof(T)))->~T();
                    head++;
                }
                operator delete(
                    reinterpret_cast<void*>(buffer),
                    capacity * sizeof(T),
                    std::align_val_t(alignof(T))
                );
            }

            void disconnect_and_notify(void) noexcept {
                head2.fetch_or(disconnected_mask);
                tail2.fetch_or(disconnected_mask);
                head2.notify_all();
                tail2.notify_all();
            }
            char* get_raw_slot_ptr(size_t slot) noexcept {
                //return buffer + (slot % capacity) * sizeof(T);
                return buffer + (slot & (capacity - 1)) * sizeof(T);
            }
            T* get_slot_ptr(size_t slot) noexcept {
                return std::launder(reinterpret_cast<T*>(get_raw_slot_ptr(slot)));
            }
        };
#pragma warning(pop)
        template<typename T>
        struct mpsc_channel_sender {
            mpsc_channel_sender() noexcept : m_shared(nullptr) {}
            mpsc_channel_sender(std::shared_ptr<mpsc_channel_shared_ring_buffer<T>> shared) noexcept :
                m_shared(std::move(shared))
            {
                m_shared->sender_count.fetch_add(1);
            }
            mpsc_channel_sender(mpsc_channel_sender const& other) noexcept : m_shared(other.m_shared) {
                m_shared->sender_count.fetch_add(1);
            }
            mpsc_channel_sender(mpsc_channel_sender&& other) noexcept :
                m_shared(std::move(other.m_shared)) {}
            mpsc_channel_sender& operator=(mpsc_channel_sender other) noexcept {
                m_shared.swap(other.m_shared);
                return *this;
            }
            ~mpsc_channel_sender() {
                if (!m_shared) { return; }
                if (m_shared->sender_count.fetch_sub(1) == 1) {
                    m_shared->disconnect_and_notify();
                }
            }
            // If send failed, this means there are no available receivers
            // WARN: send() will be blocked if backpressure occurs or firstcomers are sending
            // WARN: Do NOT call send on empty senders
            bool send(T value) const noexcept {
                // Acquire a slot for writing
                auto slot = m_shared->tail1.fetch_add(1);
                // Wait until slot is empty
                auto cur_real_head = m_shared->head2.load();
                // NOTE: If cur_real_head has disconnected_mask set, the result is guaranteed
                //       to be larger than capacity (because of unsigned integer modulo rule)
                while (((slot - cur_real_head) & ~turn_around_mask) >= m_shared->capacity) {
                    if (cur_real_head & disconnected_mask) {
                        // Rollback
                        m_shared->tail1.fetch_sub(1);
                        return false;
                    }
                    m_shared->head2.wait(cur_real_head);
                    cur_real_head = m_shared->head2.load();
                }
                // Wait until firstcomers complete
                // NOTE: Make the wait section as small as possible
                auto cur_real_tail = m_shared->tail2.load();
                while (((slot - cur_real_tail) & ~turn_around_mask) > 0) {
                    if (cur_real_tail & disconnected_mask) {
                        // Rollback
                        m_shared->tail1.fetch_sub(1);
                        return false;
                    }
                    m_shared->tail2.wait(cur_real_tail);
                    cur_real_tail = m_shared->tail2.load();
                }
                // Start actual writing operation
                new(m_shared->get_raw_slot_ptr(slot)) T(std::move(value));
                // Mark completion of writing
                m_shared->tail2.fetch_add(1);
                m_shared->tail2.notify_all();

                return true;
            }
            // NOTE: If send failed, try_send() will return the value passed in;
            //       otherwise returns std::nullopt
            // WARN: While try_send() does not wait when buffer is full, it will
            //       still wait for firstcomers
            std::optional<T> try_send(T value) const noexcept {
                // Try to acquire a slot
                size_t slot;
                do {
                    // Get a slot for future writing
                    slot = m_shared->tail1.load();
                    // Make sure the slot is empty
                    if (((slot - m_shared->head2.load()) & ~turn_around_mask) >= m_shared->capacity) {
                        return std::optional<T>{ std::move(value) };
                    }
                    // Acquire the slot via CAS
                } while (!m_shared->tail1.compare_exchange_weak(slot, slot + 1));
                // Wait until firstcomers complete
                auto cur_real_tail = m_shared->tail2.load();
                while (((slot - cur_real_tail) & ~turn_around_mask) > 0) {
                    if (cur_real_tail & disconnected_mask) {
                        // Rollback
                        m_shared->tail1.fetch_sub(1);
                        return false;
                    }
                    m_shared->tail2.wait(cur_real_tail);
                    cur_real_tail = m_shared->tail2.load();
                }
                // Start actual writing operation
                new(m_shared->get_raw_slot_ptr(slot)) T(std::move(value));
                // Mark completion of writing
                m_shared->tail2.fetch_add(1);
                m_shared->tail2.notify_all();

                return std::nullopt;
            }

            bool is_disconnected(void) const noexcept {
                return m_shared->receiver_count.load() == 0;
            }

        private:
            std::shared_ptr<mpsc_channel_shared_ring_buffer<T>> m_shared;
        };
        template<typename T>
        struct mpsc_channel_receiver {
            mpsc_channel_receiver() noexcept : m_shared(nullptr) {}
            mpsc_channel_receiver(std::shared_ptr<mpsc_channel_shared_ring_buffer<T>> shared) noexcept :
                m_shared(std::move(shared))
            {
                m_shared->receiver_count.fetch_add(1);
            }
            mpsc_channel_receiver(mpsc_channel_receiver const&) = delete;
            mpsc_channel_receiver(mpsc_channel_receiver&& other) noexcept :
                m_shared(std::move(other.m_shared)) {}
            mpsc_channel_receiver& operator=(mpsc_channel_receiver other) noexcept {
                m_shared.swap(other.m_shared);
                return *this;
            }
            ~mpsc_channel_receiver() {
                if (!m_shared) { return; }
                if (m_shared->receiver_count.fetch_sub(1) == 1) {
                    m_shared->disconnect_and_notify();
                }
            }
            // NOTE: If there are no data in buffer, recv will block the thread.
            //       If all senders are gone, recv will return std::nullopt.
            // WARN: Do NOT call recv on empty receivers
            std::optional<T> recv(void) const noexcept {
                // Acquire a slot for reading
                auto slot = m_shared->head1.fetch_add(1);
                // Wait until slot has data
                // NOTE: If slot has data, readers will unconditionally continue, which
                //       differs from senders' behaviour
                auto cur_real_tail = m_shared->tail2.load();
                while ((cur_real_tail & ~disconnected_mask) <= slot) {
                    if (cur_real_tail & disconnected_mask) {
                        // Rollback
                        m_shared->head1.fetch_sub(1);
                        return std::nullopt;
                    }
                    m_shared->tail2.wait(cur_real_tail);
                    cur_real_tail = m_shared->tail2.load();
                }
                // Start actual reading operation
                auto slot_ptr = m_shared->get_slot_ptr(slot);
                std::optional<T> result{ std::move(*slot_ptr) };
                slot_ptr->~T();
                // Mark completion of reading
                m_shared->head2.fetch_add(1);
                m_shared->head2.notify_all();

                // Extra: if slot has turn_around_mask set, perform a turn around
                if (slot & turn_around_mask) {
                    // Turning around should be transparent; no need no notify
                    m_shared->head1.fetch_and(~turn_around_mask);
                    m_shared->head2.fetch_and(~turn_around_mask);
                    m_shared->tail1.fetch_and(~turn_around_mask);
                    m_shared->tail2.fetch_and(~turn_around_mask);
                }

                return result;
            }
            std::optional<T> try_recv(void) const noexcept {
                // Try to acquire a slot
                size_t slot;
                do {
                    // Get a slot for future reading
                    slot = m_shared->head1.load();
                    // Make sure the slot has data
                    auto cur_real_tail = m_shared->tail2.load();
                    if ((cur_real_tail & ~disconnected_mask) <= slot) {
                        return std::nullopt;
                    }
                    // Acquire the slot via CAS
                } while (!m_shared->head1.compare_exchange_weak(slot, slot + 1));
                // Start actual reading operation
                auto slot_ptr = m_shared->get_slot_ptr(slot);
                std::optional<T> result{ std::move(*slot_ptr) };
                slot_ptr->~T();
                // Mark completion of reading
                m_shared->head2.fetch_add(1);
                m_shared->head2.notify_all();

                // Extra: if slot has turn_around_mask set, perform a turn around
                if (slot & turn_around_mask) {
                    // Turning around should be transparent; no need no notify
                    m_shared->head1.fetch_and(~turn_around_mask);
                    m_shared->head2.fetch_and(~turn_around_mask);
                    m_shared->tail1.fetch_and(~turn_around_mask);
                    m_shared->tail2.fetch_and(~turn_around_mask);
                }

                return result;
            }

            bool is_disconnected(void) const noexcept {
                return m_shared->sender_count.load() == 0;
            }

        private:
            std::shared_ptr<mpsc_channel_shared_ring_buffer<T>> m_shared;
        };
        // NOTE: Currently, we don't support too large capacity
        template<typename T>
        inline std::pair<mpsc_channel_sender<T>, mpsc_channel_receiver<T>> mpsc_channel_bounded(size_t n) {
            if (n == 0) {
                // Default value
                n = std::max(0xffff / sizeof(T), std::size_t{ 1 });
            }
            // Make capacity power of 2 for simple turn around
            n--;
            n |= n >> 1;
            n |= n >> 2;
            n |= n >> 4;
            if constexpr (sizeof(size_t) > 1) {
                n |= n >> 8;
            }
            if constexpr (sizeof(size_t) > 2) {
                n |= n >> 16;
            }
            if constexpr (sizeof(size_t) > 4) {
                n |= n >> 32;
            }
            n++;
            if (n >= std::min(turn_around_mask - 1, std::size_t{ 0x1fffffff })) {
                throw std::invalid_argument("Capacity too large");
            }
            auto shared = std::make_shared<mpsc_channel_shared_ring_buffer<T>>(n);
            return { shared, shared };
        }
    }
}

// Preludes
using util::winrt::fire_forget_except;
//using util::winrt::co_exlog;
