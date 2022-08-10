#pragma once

#include <string>
#include <format>
#include <source_location>

namespace util {
    namespace misc {
#define CONCAT_2_IMPL(a, b) a ## b
#define CONCAT_2(a, b) CONCAT_2_IMPL(a, b)
#define CONCAT_3_IMPL(a, b, c) a ## b ## c
#define CONCAT_3(a, b, c) CONCAT_3_IMPL(a, b, c)

#define deferred(x) auto CONCAT_2(internal_deffered_, __COUNTER__) = ::util::misc::Defer(x)
        struct Defer {
            Defer(std::function<void(void)> pFunc) : func(std::move(pFunc)) {};
            std::function<void(void)> func;
            virtual ~Defer() {
                func();
            }
        };

        template<typename T, std::enable_if_t<std::is_enum_v<T>, int> = 0>
        constexpr std::underlying_type_t<T> enum_to_int(T const& value) {
            return static_cast<std::underlying_type_t<T>>(value);
        }

        template<typename>
        inline constexpr bool always_false_v = false;

        template<typename, typename U>
        using discard_first_type = U;
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

        // NOTE: Due to some limitation, _v version is not provided, and callers
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
    }

    namespace time {
        std::wstring pretty_time(void);
        uint64_t get_secs_since_epoch(void);
        //std::wstring get_secs_since_epoch_str(void);
    }

    namespace num {
        constexpr uint32_t rotate_left(uint32_t v, unsigned int offset) {
            return (v << offset) | (v >> ((CHAR_BIT * sizeof v) - offset));
        }
    }

    namespace debug {
        enum class LogLevel : unsigned {
            Trace = 0, Debug, Info, Warn, Error,
        };
        class LoggingProvider {
        public:
            virtual void set_log_level(LogLevel new_level) = 0;
            virtual void log(std::wstring_view str, std::source_location loc) = 0;
            virtual void log_trace(std::wstring_view str, std::source_location loc) = 0;
            virtual void log_debug(std::wstring_view str, std::source_location loc) = 0;
            virtual void log_info(std::wstring_view str, std::source_location loc) = 0;
            virtual void log_warn(std::wstring_view str, std::source_location loc) = 0;
            virtual void log_error(std::wstring_view str, std::source_location loc) = 0;
            virtual ~LoggingProvider() {}
        };
        // Pass nullptr to disable logging
        void set_log_provider(LoggingProvider* provider);
        LoggingProvider* get_log_provider(void);
        // TODO: std::source_location shows full file path, maybe change this?
        inline void log_trace(
            std::wstring_view str, std::source_location loc = std::source_location::current()
        ) {
            if (auto provider = get_log_provider()) {
                provider->log_trace(str, std::move(loc));
            }
        }
        inline void log_debug(
            std::wstring_view str, std::source_location loc = std::source_location::current()
        ) {
            if (auto provider = get_log_provider()) {
                provider->log_debug(str, std::move(loc));
            }
        }
        inline void log_info(
            std::wstring_view str, std::source_location loc = std::source_location::current()
        ) {
            if (auto provider = get_log_provider()) {
                provider->log_info(str, std::move(loc));
            }
        }
        inline void log_warn(
            std::wstring_view str, std::source_location loc = std::source_location::current()
        ) {
            if (auto provider = get_log_provider()) {
                provider->log_warn(str, std::move(loc));
            }
        }
        inline void log_error(
            std::wstring_view str, std::source_location loc = std::source_location::current()
        ) {
            if (auto provider = get_log_provider()) {
                provider->log_error(str, std::move(loc));
            }
        }

        class RAIIObserver {
        public:
            RAIIObserver(std::source_location loc = std::source_location::current()) : m_loc(std::move(loc)) {
                //this->line = (unsigned)location.line();
                log_trace(std::format(L"Constructed RAIIObserver at line {}", m_loc.line()), m_loc);
            }
            RAIIObserver(RAIIObserver const& s, std::source_location loc = std::source_location::current()) :
                m_loc(std::move(loc))
            {
                //this->line = (unsigned)location.line();
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
        bool path_exists(const wchar_t* path) noexcept;
        bool delete_file(const wchar_t* path) noexcept;
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

        // Same as ::winrt::fire_and_forget, except that it reports unhandled exceptions
        // Source: https://devblogs.microsoft.com/oldnewthing/20190320-00/?p=102345
        struct fire_forget_except {
            struct promise_type {
                fire_forget_except get_return_object() const noexcept { return {}; }
                void return_void() const noexcept {}
                std::suspend_never initial_suspend() const noexcept { return {}; }
                std::suspend_never final_suspend() const noexcept { return {}; }
                void unhandled_exception() noexcept {
                    try { throw; }
                    catch (::winrt::hresult_error const& e) {
                        auto error_message = e.message();
                        util::debug::log_error(
                            std::format(L"Uncaught async exception(hresult_error): {}", error_message)
                        );
                        if (IsDebuggerPresent()) {
                            __debugbreak();
                        }
                    }
                    catch (std::exception const& e) {
                        auto error_message = e.what();
                        // %S: Microsoft-C++ specific
                        util::debug::log_error(
                            util::str::wstrprintf(L"Uncaught async exception(std::exception): %S", error_message)
                        );
                        if (IsDebuggerPresent()) {
                            __debugbreak();
                        }
                    }
                    catch (const wchar_t* e) {
                        auto error_message = e;
                        util::debug::log_error(
                            std::format(L"Uncaught async exception(wchar_t*): {}", error_message)
                        );
                        if (IsDebuggerPresent()) {
                            __debugbreak();
                        }
                    }
                    catch (...) {
                        auto error_message = L"Unknown exception was thrown";
                        util::debug::log_error(
                            std::format(L"Uncaught async exception(any): {}", error_message)
                        );
                        if (IsDebuggerPresent()) {
                            __debugbreak();
                        }
                    }
                }
            };
        };

        ::winrt::Windows::UI::Xaml::UIElement get_child_elem(
            ::winrt::Windows::UI::Xaml::UIElement const& elem,
            std::wstring_view name,
            std::wstring_view class_name = L""
        );
        ::winrt::Windows::UI::Xaml::UIElement get_child_elem(
            ::winrt::Windows::UI::Xaml::UIElement const& elem,
            int32_t idx = 0
        );

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
            ::winrt::handle handle{ ::winrt::check_pointer(CreateEvent(nullptr, true, false, nullptr)) };
        };

        inline bool update_popups_theme(
            ::winrt::Windows::UI::Xaml::XamlRoot const& xaml_root,
            ::winrt::Windows::UI::Xaml::ElementTheme theme
        ) {
            auto ivec = ::winrt::Windows::UI::Xaml::Media::VisualTreeHelper::GetOpenPopupsForXamlRoot(
                xaml_root
            );
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
                    // TODO: Fix only std::exception is usable (?)
                    m_tce.set_exception(std::current_exception());
                }
                template <typename Expression>
                auto await_transform(Expression&& expression) {
                    if (m_cts.get_token().is_canceled()) {
                        throw ::winrt::hresult_canceled();
                    }
                    return ::winrt::impl::notify_awaiter<Expression> {
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
            private:
                concurrency::task_completion_event<ReturnWrapType> m_tce;
                concurrency::cancellation_token_source m_cts;
                std::shared_ptr<::winrt::cancellable_promise> m_cancellable;
                bool m_propagate_cancellation{ false };
            };
            bool await_ready() const {
                return m_task.is_done();
            }
            void await_suspend(std::coroutine_handle<> resume) {
                m_task.then(
                    [resume](concurrency::task<ReturnWrapType> const&) { resume(); },
                    concurrency::task_continuation_context::get_current_winrt_context()
                );
            }
            ReturnType& await_resume() {
                return *m_task.get();
            }
            void enable_cancellation(::winrt::cancellable_promise* promise) {
                promise->set_canceller([](void* context) {
                    auto that = static_cast<task*>(context);
                    that->m_cts.cancel();
                }, this);
            }
            void cancel(void) {
                m_cancellable->cancel();
                m_cts.cancel();
            }
            ~task() {
                // Swallow exceptions, if any
                m_task.then([](concurrency::task<ReturnWrapType> const& task) {
                    try { task.wait(); }
                    catch (...) {}
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
                    // TODO: Fix only std::exception is usable (?)
                    m_tce.set_exception(std::current_exception());
                }
                template <typename Expression>
                auto await_transform(Expression&& expression) {
                    if (m_cts.get_token().is_canceled()) {
                        throw ::winrt::hresult_canceled();
                    }
                    return ::winrt::impl::notify_awaiter<Expression> {
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
            private:
                concurrency::task_completion_event<void> m_tce;
                concurrency::cancellation_token_source m_cts;
                std::shared_ptr<::winrt::cancellable_promise> m_cancellable;
                bool m_propagate_cancellation{ false };
            };
            bool await_ready() const {
                return m_task.is_done();
            }
            void await_suspend(std::coroutine_handle<> resume) {
                m_task.then(
                    [resume](concurrency::task<void> const&) { resume(); },
                    concurrency::task_continuation_context::get_current_winrt_context()
                );
            }
            void await_resume() {
                m_task.get();
            }
            void enable_cancellation(::winrt::cancellable_promise* promise) {
                promise->set_canceller([](void* context) {
                    auto that = static_cast<task*>(context);
                    that->m_cts.cancel();
                    }, this);
            }
            void cancel(void) {
                m_cancellable->cancel();
                m_cts.cancel();
            }
            ~task() {
                // Swallow exceptions, if any
                m_task.then([](concurrency::task<void> const& task) {
                    try { task.wait(); }
                    catch (...) {}
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
    }
}

// Preludes
using util::winrt::fire_forget_except;
