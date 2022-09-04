#include "pch.h"
#include "Converters.h"
#include "StringToBitmapImageConverter.g.cpp"
#include "StringToUriConverter.g.cpp"
#include "UInt32ToSelectedIndexConverter.g.cpp"

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
}
