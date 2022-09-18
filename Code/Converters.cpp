#include "pch.h"
#include "Converters.h"
#include "StringToBitmapImageConverter.g.cpp"
#include "StringToUriConverter.g.cpp"
#include "UInt32ToSelectedIndexConverter.g.cpp"
#include "NumberScaleConverter.g.cpp"

// NOTE: Currently, authoring MarkupExtension is not supported (?)

#include "util.hpp"

using namespace winrt;
using namespace Windows::Foundation;

// TODO: Maybe we should remove StringToBitmapImageConverter
namespace winrt::BiliUWP::implementation {
    hstring try_unbox_valid_hstring(IInspectable const& value) {
        if (auto str = util::winrt::try_unbox_value<hstring>(value)) {
            return *str;
        }
        return {};
    }
    hstring try_replace_res_str(hstring const& str) {
        /*
        constexpr std::wstring_view res_prefix = L"RES@";
        if (str.starts_with(res_prefix)) {
            std::wstring_view str_res_id = str;
            str_res_id.remove_prefix(res_prefix.size());
            if (str_res_id == L"PlaceholderImgUrl") {
                return L"https://s1.hdslb.com/bfs/static/jinkela/space/assets/playlistbg.png";
            }
            return {};
        }
        else {
            return str;
        }
        */
        return str;
    }

    IInspectable StringToBitmapImageConverter::Convert(IInspectable const& value, Windows::UI::Xaml::Interop::TypeName const& targetType, IInspectable const& parameter, hstring const& language) {
        hstring str_url{};
        Uri uri{ nullptr };
        if (str_url == L"") {
            str_url = try_unbox_valid_hstring(value);
        }
        if (str_url == L"") {
            str_url = try_replace_res_str(try_unbox_valid_hstring(parameter));
        }
        if (str_url != L"") {
            uri = Uri(str_url);
        }
        return uri ? Windows::UI::Xaml::Media::Imaging::BitmapImage(uri) : nullptr;
    }
    IInspectable StringToBitmapImageConverter::ConvertBack(IInspectable const& value, Windows::UI::Xaml::Interop::TypeName const& targetType, IInspectable const& parameter, hstring const& language) {
        throw hresult_not_implemented();
    }

    IInspectable StringToUriConverter::Convert(IInspectable const& value, Windows::UI::Xaml::Interop::TypeName const& targetType, IInspectable const& parameter, hstring const& language) {
        hstring str_url{};
        Uri uri{ nullptr };
        if (str_url == L"") {
            str_url = try_unbox_valid_hstring(value);
        }
        if (str_url == L"") {
            str_url = try_replace_res_str(try_unbox_valid_hstring(parameter));
        }
        if (str_url != L"") {
            uri = Uri(str_url);
        }
        return uri;
    }
    IInspectable StringToUriConverter::ConvertBack(IInspectable const& value, Windows::UI::Xaml::Interop::TypeName const& targetType, IInspectable const& parameter, hstring const& language) {
        throw hresult_not_implemented();
    }

    IInspectable UInt32ToSelectedIndexConverter::Convert(IInspectable const& value, Windows::UI::Xaml::Interop::TypeName const& targetType, IInspectable const& parameter, hstring const& language) {
        if (auto opt = value.try_as<uint32_t>()) {
            return box_value(static_cast<int32_t>(*opt));
        }
        return value;
    }
    IInspectable UInt32ToSelectedIndexConverter::ConvertBack(IInspectable const& value, Windows::UI::Xaml::Interop::TypeName const& targetType, IInspectable const& parameter, hstring const& language) {
        if (auto opt = value.try_as<int32_t>()) {
            uint32_t val;
            if (*opt < 0) {
                if (parameter) { return parameter; }
                // Default fallback value is index 0
                val = 0;
            }
            else {
                val = static_cast<uint32_t>(*opt);
            }
            return box_value(val);
        }
        return value;
    }

    IInspectable NumberScaleConverter::Convert(IInspectable const& value, Windows::UI::Xaml::Interop::TypeName const& targetType, IInspectable const& parameter, hstring const& language) {
        auto try_unbox_num_fn = [](IInspectable const& value) {
            auto try_gen_unbox_int_fn_fn = [&]<typename T>() {
                return [&] {
                    return value.try_as<T>().transform([](T v) { return static_cast<double>(v); });
                };
            };
            return value.try_as<double>().or_else([&] {
                return value.try_as<float>().transform([](float v) { return static_cast<double>(v); });
            }).or_else([&] {
                // TODO: Maybe get rid of string type in XAML?
                return value.try_as<hstring>().and_then([](hstring v) { return util::num::try_parse_f64(v); });
            })
                .or_else(try_gen_unbox_int_fn_fn.template operator()<uint64_t>())
                .or_else(try_gen_unbox_int_fn_fn.template operator()<int64_t>())
                .or_else(try_gen_unbox_int_fn_fn.template operator()<uint32_t>())
                .or_else(try_gen_unbox_int_fn_fn.template operator()<int32_t>());
        };

        if (auto opt = try_unbox_num_fn(value)) {
            auto box_int_fn = [&]<typename T>() {
                auto result = *opt * try_unbox_num_fn(parameter).value_or(1);
                return box_value(static_cast<T>(std::llround(result)));
            };

            if (targetType == winrt::xaml_typename<double>()) {
                auto result = *opt * try_unbox_num_fn(parameter).value_or(1);
                return box_value(result);
            }
            if (targetType == winrt::xaml_typename<float>()) {
                auto result = *opt * try_unbox_num_fn(parameter).value_or(1);
                return box_value(static_cast<float>(result));
            }
            if (targetType == winrt::xaml_typename<uint64_t>()) { return box_int_fn.template operator()<uint64_t>(); }
            if (targetType == winrt::xaml_typename<int64_t>()) { return box_int_fn.template operator()<int64_t>(); }
            if (targetType == winrt::xaml_typename<uint32_t>()) { return box_int_fn.template operator()<uint32_t>(); }
            if (targetType == winrt::xaml_typename<int32_t>()) { return box_int_fn.template operator()<int32_t>(); }
        }
        return value;
    }
    IInspectable NumberScaleConverter::ConvertBack(IInspectable const& value, Windows::UI::Xaml::Interop::TypeName const& targetType, IInspectable const& parameter, hstring const& language) {
        auto try_unbox_num_fn = [](IInspectable const& value) {
            auto try_gen_unbox_int_fn_fn = [&]<typename T>() {
                return [&] {
                    return value.try_as<T>().transform([](T v) { return static_cast<double>(v); });
                };
            };
            return value.try_as<double>().or_else([&] {
                return value.try_as<float>().transform([](float v) { return static_cast<double>(v); });
            }).or_else([&] {
                // TODO: Maybe get rid of string type in XAML?
                return value.try_as<hstring>().and_then([](hstring v) { return util::num::try_parse_f64(v); });
            })
                .or_else(try_gen_unbox_int_fn_fn.template operator()<uint64_t>())
                .or_else(try_gen_unbox_int_fn_fn.template operator()<int64_t>())
                .or_else(try_gen_unbox_int_fn_fn.template operator()<uint32_t>())
                .or_else(try_gen_unbox_int_fn_fn.template operator()<int32_t>());
        };

        if (auto opt = try_unbox_num_fn(value)) {
            auto box_int_fn = [&]<typename T>() {
                auto result = *opt / try_unbox_num_fn(parameter).value_or(1);
                return box_value(static_cast<T>(std::llround(result)));
            };

            if (targetType == winrt::xaml_typename<double>()) {
                auto result = *opt / try_unbox_num_fn(parameter).value_or(1);
                return box_value(result);
            }
            if (targetType == winrt::xaml_typename<float>()) {
                auto result = *opt / try_unbox_num_fn(parameter).value_or(1);
                return box_value(static_cast<float>(result));
            }
            if (targetType == winrt::xaml_typename<uint64_t>()) { return box_int_fn.template operator()<uint64_t>(); }
            if (targetType == winrt::xaml_typename<int64_t>()) { return box_int_fn.template operator()<int64_t>(); }
            if (targetType == winrt::xaml_typename<uint32_t>()) { return box_int_fn.template operator()<uint32_t>(); }
            if (targetType == winrt::xaml_typename<int32_t>()) { return box_int_fn.template operator()<int32_t>(); }
        }
        return value;
    }
}
