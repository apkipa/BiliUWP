#include "pch.h"
#include "util.hpp"

#include <cstdarg>

namespace util {
    namespace str {
        std::wstring wstrprintf(_Printf_format_string_ const wchar_t* str, ...) {
            wchar_t buf[1024];
            std::va_list arg;
            va_start(arg, str);
            int len = std::vswprintf(buf, sizeof buf / sizeof * buf, str, arg);
            va_end(arg);
            if (len < 0) {
                // Maybe the result string is too long, try to allocate a larger buffer and try again
                constexpr auto max_buf_size = static_cast<size_t>(1024) * 1024 * 4;
                std::wstring str_buf;
                // NOTE: Method reserve is not applicable, as writing to reserved space
                //       invokes undefined behavior
                str_buf.resize(max_buf_size);
                va_start(arg, str);
                len = std::vswprintf(&str_buf[0], max_buf_size, str, arg);
                va_end(arg);
                if (len < 0) {
                    throw std::bad_alloc();
                }
                str_buf.resize(len);
                return str_buf;
            }
            return { buf, static_cast<size_t>(len) };
        }
    }

    namespace time {
        template <typename T>
        inline auto to_ms(const std::chrono::time_point<T>& tp) {
            using namespace std::chrono;

            auto dur = tp.time_since_epoch();
            return duration_cast<milliseconds>(dur).count();
        }

        std::wstring pretty_time(void) {
            auto tp = std::chrono::system_clock::now();
            std::time_t current_time = std::chrono::system_clock::to_time_t(tp);

            std::tm time_info;
            localtime_s(&time_info, &current_time);

            char buffer[128];
            size_t string_size = strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &time_info);
            unsigned int ms = to_ms(tp) % 1000;
            string_size += static_cast<size_t>(std::snprintf(
                buffer + string_size,
                sizeof(buffer) - string_size,
                ".%03u",
                ms
            ));

            return { buffer, buffer + string_size };
        }

        uint64_t get_secs_since_epoch(void) {
            auto cur_t = std::chrono::system_clock::now();
            auto res = std::chrono::duration_cast<std::chrono::seconds>(cur_t.time_since_epoch()).count();
            return static_cast<uint64_t>(res);
        }
    }

    namespace debug {
        static LoggingProvider* g_log_provider;
        void set_log_provider(LoggingProvider* provider) {
            g_log_provider = provider;
        }
        LoggingProvider* get_log_provider(void) {
            return g_log_provider;
        }
    }

    namespace cryptography {
        namespace Impl_Md5 {
            static constexpr uint32_t r[64] = {
                7, 12, 17, 22,  7, 12, 17, 22,  7, 12, 17, 22,  7, 12, 17, 22,
                5,  9, 14, 20,  5,  9, 14, 20,  5,  9, 14, 20,  5,  9, 14, 20,
                4, 11, 16, 23,  4, 11, 16, 23,  4, 11, 16, 23,  4, 11, 16, 23,
                6, 10, 15, 21,  6, 10, 15, 21,  6, 10, 15, 21,  6, 10, 15, 21,
            };
            static constexpr uint32_t k[64] = {
                0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
                0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
                0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
                0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
                0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
                0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
                0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
                0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
            };
            static constexpr unsigned int S11 = 7;
            static constexpr unsigned int S12 = 12;
            static constexpr unsigned int S13 = 17;
            static constexpr unsigned int S14 = 22;
            static constexpr unsigned int S21 = 5;
            static constexpr unsigned int S22 = 9;
            static constexpr unsigned int S23 = 14;
            static constexpr unsigned int S24 = 20;
            static constexpr unsigned int S31 = 4;
            static constexpr unsigned int S32 = 11;
            static constexpr unsigned int S33 = 16;
            static constexpr unsigned int S34 = 23;
            static constexpr unsigned int S41 = 6;
            static constexpr unsigned int S42 = 10;
            static constexpr unsigned int S43 = 15;
            static constexpr unsigned int S44 = 21;
            static constexpr uint32_t F(uint32_t x, uint32_t y, uint32_t z) {
                return x & y | ~x & z;
            }
            static constexpr uint32_t G(uint32_t x, uint32_t y, uint32_t z) {
                return x & z | y & ~z;
            }
            static constexpr uint32_t H(uint32_t x, uint32_t y, uint32_t z) {
                return x ^ y ^ z;
            }
            static constexpr uint32_t I(uint32_t x, uint32_t y, uint32_t z) {
                return y ^ (x | ~z);
            }
            static constexpr void FF(uint32_t& a, uint32_t b, uint32_t c, uint32_t d, uint32_t x, uint32_t s, uint32_t ac) {
                a = num::rotate_left(a + F(b, c, d) + x + ac, s) + b;
            }
            static constexpr void GG(uint32_t& a, uint32_t b, uint32_t c, uint32_t d, uint32_t x, uint32_t s, uint32_t ac) {
                a = num::rotate_left(a + G(b, c, d) + x + ac, s) + b;
            }
            static constexpr void HH(uint32_t& a, uint32_t b, uint32_t c, uint32_t d, uint32_t x, uint32_t s, uint32_t ac) {
                a = num::rotate_left(a + H(b, c, d) + x + ac, s) + b;
            }
            static constexpr void II(uint32_t& a, uint32_t b, uint32_t c, uint32_t d, uint32_t x, uint32_t s, uint32_t ac) {
                a = num::rotate_left(a + I(b, c, d) + x + ac, s) + b;
            }
        }

        // Class Md5 {
        void Md5::process_chunk(void) {
            using namespace Impl_Md5;

            auto& x = this->temp_chunk;
            uint32_t a = this->h0, b = this->h1, c = this->h2, d = this->h3;
            // Round 1
            FF(a, b, c, d, x[0], S11, 0xd76aa478);  // 1
            FF(d, a, b, c, x[1], S12, 0xe8c7b756);  // 2
            FF(c, d, a, b, x[2], S13, 0x242070db);  // 3
            FF(b, c, d, a, x[3], S14, 0xc1bdceee);  // 4
            FF(a, b, c, d, x[4], S11, 0xf57c0faf);  // 5
            FF(d, a, b, c, x[5], S12, 0x4787c62a);  // 6
            FF(c, d, a, b, x[6], S13, 0xa8304613);  // 7
            FF(b, c, d, a, x[7], S14, 0xfd469501);  // 8
            FF(a, b, c, d, x[8], S11, 0x698098d8);  // 9
            FF(d, a, b, c, x[9], S12, 0x8b44f7af);  // 10
            FF(c, d, a, b, x[10], S13, 0xffff5bb1); // 11
            FF(b, c, d, a, x[11], S14, 0x895cd7be); // 12
            FF(a, b, c, d, x[12], S11, 0x6b901122); // 13
            FF(d, a, b, c, x[13], S12, 0xfd987193); // 14
            FF(c, d, a, b, x[14], S13, 0xa679438e); // 15
            FF(b, c, d, a, x[15], S14, 0x49b40821); // 16
            // Round 2
            GG(a, b, c, d, x[1], S21, 0xf61e2562);  // 17
            GG(d, a, b, c, x[6], S22, 0xc040b340);  // 18
            GG(c, d, a, b, x[11], S23, 0x265e5a51); // 19
            GG(b, c, d, a, x[0], S24, 0xe9b6c7aa);  // 20
            GG(a, b, c, d, x[5], S21, 0xd62f105d);  // 21
            GG(d, a, b, c, x[10], S22, 0x02441453); // 22
            GG(c, d, a, b, x[15], S23, 0xd8a1e681); // 23
            GG(b, c, d, a, x[4], S24, 0xe7d3fbc8);  // 24
            GG(a, b, c, d, x[9], S21, 0x21e1cde6);  // 25
            GG(d, a, b, c, x[14], S22, 0xc33707d6); // 26
            GG(c, d, a, b, x[3], S23, 0xf4d50d87);  // 27
            GG(b, c, d, a, x[8], S24, 0x455a14ed);  // 28
            GG(a, b, c, d, x[13], S21, 0xa9e3e905); // 29
            GG(d, a, b, c, x[2], S22, 0xfcefa3f8);  // 30
            GG(c, d, a, b, x[7], S23, 0x676f02d9);  // 31
            GG(b, c, d, a, x[12], S24, 0x8d2a4c8a); // 32
            // Round 3
            HH(a, b, c, d, x[5], S31, 0xfffa3942);  // 33
            HH(d, a, b, c, x[8], S32, 0x8771f681);  // 34
            HH(c, d, a, b, x[11], S33, 0x6d9d6122); // 35
            HH(b, c, d, a, x[14], S34, 0xfde5380c); // 36
            HH(a, b, c, d, x[1], S31, 0xa4beea44);  // 37
            HH(d, a, b, c, x[4], S32, 0x4bdecfa9);  // 38
            HH(c, d, a, b, x[7], S33, 0xf6bb4b60);  // 39
            HH(b, c, d, a, x[10], S34, 0xbebfbc70); // 40
            HH(a, b, c, d, x[13], S31, 0x289b7ec6); // 41
            HH(d, a, b, c, x[0], S32, 0xeaa127fa);  // 42
            HH(c, d, a, b, x[3], S33, 0xd4ef3085);  // 43
            HH(b, c, d, a, x[6], S34, 0x04881d05);  // 44
            HH(a, b, c, d, x[9], S31, 0xd9d4d039);  // 45
            HH(d, a, b, c, x[12], S32, 0xe6db99e5); // 46
            HH(c, d, a, b, x[15], S33, 0x1fa27cf8); // 47
            HH(b, c, d, a, x[2], S34, 0xc4ac5665);  // 48
            // Round 4
            II(a, b, c, d, x[0], S41, 0xf4292244);  // 49
            II(d, a, b, c, x[7], S42, 0x432aff97);  // 50
            II(c, d, a, b, x[14], S43, 0xab9423a7); // 51
            II(b, c, d, a, x[5], S44, 0xfc93a039);  // 52
            II(a, b, c, d, x[12], S41, 0x655b59c3); // 53
            II(d, a, b, c, x[3], S42, 0x8f0ccc92);  // 54
            II(c, d, a, b, x[10], S43, 0xffeff47d); // 55
            II(b, c, d, a, x[1], S44, 0x85845dd1);  // 56
            II(a, b, c, d, x[8], S41, 0x6fa87e4f);  // 57
            II(d, a, b, c, x[15], S42, 0xfe2ce6e0); // 58
            II(c, d, a, b, x[6], S43, 0xa3014314);  // 59
            II(b, c, d, a, x[13], S44, 0x4e0811a1); // 60
            II(a, b, c, d, x[4], S41, 0xf7537e82);  // 61
            II(d, a, b, c, x[11], S42, 0xbd3af235); // 62
            II(c, d, a, b, x[2], S43, 0x2ad7d2bb);  // 63
            II(b, c, d, a, x[9], S44, 0xeb86d391);  // 64
            // Final
            this->h0 += a;
            this->h1 += b;
            this->h2 += c;
            this->h3 += d;
        }
        Md5::Md5() {
            this->initialize();
        }
        Md5::~Md5() = default;

        void Md5::initialize(void) {
            this->h0 = 0x67452301;
            this->h1 = 0xefcdab89;
            this->h2 = 0x98badcfe;
            this->h3 = 0x10325476;
            for (auto& i : this->temp_chunk) {
                i = 0;
            }
            this->data_length = 0;
        }
        void Md5::finialize(void) {
            uint64_t data_length_copy = this->data_length * 8;
            add_byte(0x80);
            while (this->data_length % 64 != 56) {
                add_byte(0);
            }
            for (size_t i = 0; i < 64 / 8; i++) {
                add_byte(data_length_copy & 0xff);
                data_length_copy >>= 8;
            }
        }
        void Md5::add_byte(uint8_t byte) {
            size_t cur_pos = (this->data_length % 64) / 4;
            this->temp_chunk[cur_pos] = (this->temp_chunk[cur_pos] >> 8) | (byte << 24);
            this->data_length++;
            if (this->data_length % 64 == 0) {
                this->process_chunk();
                for (auto& i : this->temp_chunk) {
                    i = 0;
                }
            }
        }
        void Md5::add_string(std::string_view str) {
            for (auto i : str) {
                add_byte(i);
            }
        }
        void Md5::add_string(std::wstring_view str) {
            // Assuming str only contains ASCII characters
            for (auto i : str) {
                add_byte(i & 0xff);
            }
        }

        std::wstring Md5::get_result_as_str(void) {
            wchar_t buf[32];
            util::str::write_u32_hex_swap(this->h0, buf + 0);
            util::str::write_u32_hex_swap(this->h1, buf + 8);
            util::str::write_u32_hex_swap(this->h2, buf + 16);
            util::str::write_u32_hex_swap(this->h3, buf + 24);
            return { buf, std::size(buf) };
        }
        // }; // End class Md5
    }

    namespace container {
        // TODO...
    }

    namespace fs {
        bool create_dir(const wchar_t* path) noexcept {
            if (CreateDirectoryW(path, nullptr)) {
                return true;
            }
            return GetLastError() == ERROR_ALREADY_EXISTS;
        }
        bool path_exists(const wchar_t* path) noexcept {
            return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
        }
        bool delete_file(const wchar_t* path) noexcept {
            return DeleteFileW(path) != 0;
        }
        bool rename_path(const wchar_t* orig_path, const wchar_t* new_path) noexcept {
            return MoveFileExW(orig_path, new_path, 0);
        }
        uint64_t calc_folder_size(const wchar_t* path) noexcept {
            if (!path || *path == L'\0') {
                return std::numeric_limits<uint64_t>::max();
            }

            // '<PATH>' + '\\' + '<FILENAME>'
            auto orig_path_len = wcslen(path);
            auto buf = new(std::nothrow) wchar_t[orig_path_len + MAX_PATH + 1];
            if (!buf) {
                return std::numeric_limits<uint64_t>::max();
            }
            memcpy(buf, path, sizeof(wchar_t) * orig_path_len);
            auto buf_cur = buf + orig_path_len;
            wcscpy(buf_cur, L"\\*");
            buf_cur++;

            // Just use the good old Win32 APIs
            WIN32_FIND_DATAW find_data;
            auto find_handle = FindFirstFileExW(
                buf,
                FindExInfoBasic,
                &find_data,
                FindExSearchNameMatch,
                nullptr,
                0
            );
            if (find_handle == INVALID_HANDLE_VALUE) {
                delete[] buf;
                return std::numeric_limits<uint64_t>::max();
            }

            uint64_t size = 0;
            do {
                if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    if (wcscmp(find_data.cFileName, L".") == 0 || wcscmp(find_data.cFileName, L"..") == 0) {
                        continue;
                    }
                    wcscpy(buf_cur, find_data.cFileName);
                    size += calc_folder_size(buf);
                }
                else {
                    size += (static_cast<uint64_t>(find_data.nFileSizeHigh) << 32) + find_data.nFileSizeLow;
                }
            } while (FindNextFileW(find_handle, &find_data) != 0);

            FindClose(find_handle);

            delete[] buf;
            return size;
        }
        bool delete_all_inside_folder(const wchar_t* path) noexcept {
            if (!path || *path == L'\0') {
                return false;
            }

            // '<PATH>' + '\\' + '<FILENAME>'
            auto orig_path_len = wcslen(path);
            auto buf = new(std::nothrow) wchar_t[orig_path_len + MAX_PATH + 1];
            if (!buf) {
                return false;
            }
            memcpy(buf, path, sizeof(wchar_t) * orig_path_len);
            auto buf_cur = buf + orig_path_len;
            wcscpy(buf_cur, L"\\*");
            buf_cur++;

            // Just use the good old Win32 APIs
            WIN32_FIND_DATAW find_data;
            auto find_handle = FindFirstFileExW(
                buf,
                FindExInfoBasic,
                &find_data,
                FindExSearchNameMatch,
                nullptr,
                0
            );
            if (find_handle == INVALID_HANDLE_VALUE) {
                delete[] buf;
                return false;
            }

            bool succeeded = true;
            do {
                if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    if (wcscmp(find_data.cFileName, L".") == 0 || wcscmp(find_data.cFileName, L"..") == 0) {
                        continue;
                    }
                    wcscpy(buf_cur, find_data.cFileName);
                    if (!delete_folder(buf)) {
                        succeeded = false;
                    }
                }
                else {
                    wcscpy(buf_cur, find_data.cFileName);
                    if (!delete_file(buf)) {
                        succeeded = false;
                    }
                }
            } while (FindNextFileW(find_handle, &find_data) != 0);

            FindClose(find_handle);

            delete[] buf;
            return succeeded;
        }
        bool delete_folder(const wchar_t* path) noexcept {
            return delete_all_inside_folder(path) && RemoveDirectoryW(path) != 0;
        }
    }

    namespace win32 {
        // TODO...
    }

    namespace winrt {
        ::winrt::Windows::UI::Xaml::UIElement get_child_elem(
            ::winrt::Windows::UI::Xaml::UIElement const& elem,
            std::wstring_view name,
            std::wstring_view class_name
        ) {
            using namespace ::winrt::Windows::UI::Xaml;
            using namespace ::winrt::Windows::UI::Xaml::Media;
            int32_t child_elem_cnt;
            if (!elem) {
                return nullptr;
            }
            child_elem_cnt = VisualTreeHelper::GetChildrenCount(elem);
            for (int32_t i = 0; i < child_elem_cnt; i++) {
                bool is_name_match = false, is_class_match = false;
                UIElement cur_elem = VisualTreeHelper::GetChild(elem, i).as<UIElement>();

                if (name != L"") {
                    if (auto fe = cur_elem.try_as<FrameworkElement>()) {
                        is_name_match = (fe.Name() == name);
                    }
                }
                else {
                    is_name_match = true;
                }
                if (class_name != L"") {
                    is_class_match = (::winrt::get_class_name(cur_elem) == class_name);
                }
                else {
                    is_class_match = true;
                }

                if (is_name_match && is_class_match) {
                    return cur_elem;
                }
            }
            return nullptr;
        }
        ::winrt::Windows::UI::Xaml::UIElement get_child_elem(
            ::winrt::Windows::UI::Xaml::UIElement const& elem,
            int32_t idx
        ) {
            using namespace ::winrt::Windows::UI::Xaml;
            using namespace ::winrt::Windows::UI::Xaml::Media;
            if (!elem) {
                return nullptr;
            }
            return VisualTreeHelper::GetChild(elem, idx).as<UIElement>();
        }

        ::winrt::Windows::Foundation::IAsyncOperation<uint64_t> calc_folder_size(::winrt::hstring path) {
            if (path == L"") {
                co_return std::numeric_limits<uint64_t>::max();
            }
            // Don't block current thread
            co_await ::winrt::resume_background();
            co_return fs::calc_folder_size(path.c_str());
        }
        ::winrt::Windows::Foundation::IAsyncOperation<bool> delete_all_inside_folder(::winrt::hstring path) {
            if (path == L"") {
                co_return false;
            }
            // Don't block current thread
            co_await ::winrt::resume_background();
            co_return fs::delete_all_inside_folder(path.c_str());
        }
        ::winrt::Windows::Foundation::IAsyncOperation<bool> delete_folder(::winrt::hstring path) {
            if (path == L"") {
                co_return false;
            }
            // Don't block current thread
            co_await ::winrt::resume_background();
            co_return fs::delete_folder(path.c_str());
        }

        ::winrt::guid gen_random_guid(void) {
            return ::winrt::Windows::Foundation::GuidHelper::CreateNewGuid();
        }

        constexpr void write_guid_to_buf(::winrt::guid const& value, wchar_t buf[36]) {
            util::str::write_u32_hex(value.Data1, buf);
            buf[8] = L'-';
            util::str::write_u16_hex(value.Data2, buf + 9);
            buf[13] = L'-';
            util::str::write_u16_hex(value.Data3, buf + 14);
            buf[18] = L'-';
            util::str::write_u8_hex(value.Data4[0], buf + 19);
            util::str::write_u8_hex(value.Data4[1], buf + 21);
            buf[23] = L'-';
            util::str::write_u8_hex(value.Data4[2], buf + 24);
            util::str::write_u8_hex(value.Data4[3], buf + 26);
            util::str::write_u8_hex(value.Data4[4], buf + 28);
            util::str::write_u8_hex(value.Data4[5], buf + 30);
            util::str::write_u8_hex(value.Data4[6], buf + 32);
            util::str::write_u8_hex(value.Data4[7], buf + 34);
        }

        std::wstring to_wstring(::winrt::guid const& value) {
            /*
            wchar_t buffer[36 + 1];
            swprintf_s(buffer, L"%08x-%04hx-%04hx-%02hhx%02hhx-%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx",
                value.Data1, value.Data2, value.Data3, value.Data4[0], value.Data4[1],
                value.Data4[2], value.Data4[3], value.Data4[4], value.Data4[5], value.Data4[6], value.Data4[7]
            );
            return buffer;
            */
            wchar_t buffer[36];
            write_guid_to_buf(value, buffer);
            return { buffer, std::size(buffer) };
        }
        ::winrt::hstring to_hstring(::winrt::guid const& value) {
            wchar_t buffer[36];
            write_guid_to_buf(value, buffer);
            return { buffer, static_cast<::winrt::hstring::size_type>(std::size(buffer)) };
        }

        bool is_mixed_reality(void) {
            using namespace ::winrt::Windows::Foundation::Metadata;
            using namespace ::winrt::Windows::ApplicationModel::Preview::Holographic;
            try {
                return ApiInformation::IsTypePresent(
                    ::winrt::name_of<HolographicApplicationPreview>()
                ) && ApiInformation::IsMethodPresent(
                    ::winrt::name_of<HolographicApplicationPreview>(),
                    L"IsCurrentViewPresentedOnHolographicDisplay"
                ) && HolographicApplicationPreview::IsCurrentViewPresentedOnHolographicDisplay();
            }
            catch (...) {}
            return false;
        }

        AppViewWindowingMode get_cur_view_windowing_mode(void) {
            using namespace ::winrt::Windows::Foundation::Metadata;
            using namespace ::winrt::Windows::UI::ViewManagement;
            using namespace ::winrt::Windows::ApplicationModel::LockScreen;

            auto app_view = ApplicationView::GetForCurrentView();
            auto dev_family = ::winrt::Windows::System::Profile::AnalyticsInfo::VersionInfo().DeviceFamily();
            if (dev_family == L"Windows.Desktop") {
                if (ApiInformation::IsEnumNamedValuePresent(
                    L"Windows.UI.ViewManagement.ApplicationViewMode",
                    L"CompactOverlay"
                ) && app_view.ViewMode() == ApplicationViewMode::CompactOverlay)
                {
                    return AppViewWindowingMode::CompactOverlay;
                }
                if (LockApplicationHost::GetForCurrentView()) {
                    // When an application is in kiosk mode, ApplicationView.IsFullScreenMode will return
                    // false even if the application is in fact displayed full screen. We need to check
                    // manually if an application is in kiosk mode and force the result to FullScreen.
                    return AppViewWindowingMode::FullScreen;
                }
                if (is_mixed_reality()) {
                    return AppViewWindowingMode::Windowed;
                }
                if (app_view.IsFullScreenMode()) {
                    return AppViewWindowingMode::FullScreen;
                }
                switch (UIViewSettings::GetForCurrentView().UserInteractionMode()) {
                case UserInteractionMode::Mouse:
                    return app_view.IsFullScreen() ?
                        AppViewWindowingMode::Maximized :
                        AppViewWindowingMode::Windowed;
                case UserInteractionMode::Touch:
                    if (app_view.AdjacentToLeftDisplayEdge()) {
                        return app_view.AdjacentToRightDisplayEdge() ?
                            AppViewWindowingMode::FullScreenTabletMode :
                            AppViewWindowingMode::SnappedLeft;
                    }
                    else {
                        return AppViewWindowingMode::SnappedRight;
                    }
                default:
                    return AppViewWindowingMode::Unknown;
                }
            }
            else if (dev_family == L"Windows.Mobile") {
                if (UIViewSettings::GetForCurrentView().UserInteractionMode() == UserInteractionMode::Mouse) {
                    return app_view.IsFullScreenMode() ?
                        AppViewWindowingMode::Maximized :
                        AppViewWindowingMode::Windowed;
                }
                else {
                    return AppViewWindowingMode::FullScreen;
                }
            }
            else if (dev_family == L"Windows.Holographic") {
                return AppViewWindowingMode::Windowed;
            }
            else if (dev_family == L"Windows.Xbox" || dev_family == L"Windows.IoT") {
                return AppViewWindowingMode::FullScreen;
            }
            else if (dev_family == L"Windows.Team") {
                if (app_view.AdjacentToLeftDisplayEdge()) {
                    return app_view.AdjacentToRightDisplayEdge() ?
                        AppViewWindowingMode::FullScreenTabletMode :
                        AppViewWindowingMode::SnappedLeft;
                }
                else {
                    return AppViewWindowingMode::SnappedRight;
                }
            }
            else {
                return AppViewWindowingMode::Unknown;
            }
        }

        void persist_textbox_copying_handler(
            ::winrt::Windows::UI::Xaml::Controls::TextBox const& sender,
            ::winrt::Windows::UI::Xaml::Controls::TextControlCopyingToClipboardEventArgs const& e
        ) {
            using namespace ::winrt::Windows::ApplicationModel::DataTransfer;
            e.Handled(true);
            auto data_package = DataPackage();
            data_package.RequestedOperation(DataPackageOperation::Copy);
            data_package.SetText(sender.SelectedText());
            Clipboard::SetContent(data_package);
            Clipboard::Flush();
        }
        void persist_textbox_cutting_handler(
            ::winrt::Windows::UI::Xaml::Controls::TextBox const& sender,
            ::winrt::Windows::UI::Xaml::Controls::TextControlCuttingToClipboardEventArgs const& e
        ) {
            using namespace ::winrt::Windows::ApplicationModel::DataTransfer;
            e.Handled(true);
            auto data_package = DataPackage();
            data_package.RequestedOperation(DataPackageOperation::Move);
            data_package.SetText(sender.SelectedText());
            sender.SelectedText(L"");
            Clipboard::SetContent(data_package);
            Clipboard::Flush();
        }
        void persist_textbox_cc_clipboard(::winrt::Windows::UI::Xaml::Controls::TextBox const& tb) {
            tb.CopyingToClipboard(persist_textbox_copying_handler);
            tb.CuttingToClipboard(persist_textbox_cutting_handler);
        }
        void persist_autosuggestbox_clipboard(::winrt::Windows::UI::Xaml::Controls::AutoSuggestBox const& ctrl) {
            using ::winrt::Windows::Foundation::IInspectable;
            using ::winrt::Windows::UI::Xaml::RoutedEventArgs;
            using ::winrt::Windows::UI::Xaml::Controls::AutoSuggestBox;
            auto run_fn = [](::winrt::Windows::UI::Xaml::Controls::AutoSuggestBox const& ctrl) {
                auto elem = get_child_elem(get_child_elem(ctrl, L"LayoutRoot"), L"TextBox");
                persist_textbox_cc_clipboard(elem.as<::winrt::Windows::UI::Xaml::Controls::TextBox>());
            };
            if (ctrl.IsLoaded()) {
                run_fn(ctrl);
            }
            else {
                auto revoke_et = std::make_shared_for_overwrite<::winrt::event_token>();
                *revoke_et = ctrl.Loaded([=](IInspectable const& sender, RoutedEventArgs const&) {
                    auto ctrl = sender.as<AutoSuggestBox>();
                    ctrl.Loaded(*revoke_et);
                    run_fn(ctrl);
                });
            }
        }
    }
}
