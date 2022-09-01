#pragma once

#include "StringToBitmapImageConverter.g.h"
#include "StringToUriImageConverter.g.h"
#include "UInt32ToSelectedIndexConverter.g.h"

namespace winrt::BiliUWP::implementation {
    struct StringToBitmapImageConverter : StringToBitmapImageConverterT<StringToBitmapImageConverter> {
        StringToBitmapImageConverter() = default;
        Windows::Foundation::IInspectable Convert(Windows::Foundation::IInspectable const& value, Windows::UI::Xaml::Interop::TypeName const& targetType, Windows::Foundation::IInspectable const& parameter, hstring const& language);
        Windows::Foundation::IInspectable ConvertBack(Windows::Foundation::IInspectable const& value, Windows::UI::Xaml::Interop::TypeName const& targetType, Windows::Foundation::IInspectable const& parameter, hstring const& language);
    };
    struct StringToUriImageConverter : StringToUriImageConverterT<StringToUriImageConverter> {
        StringToUriImageConverter() = default;
        Windows::Foundation::IInspectable Convert(Windows::Foundation::IInspectable const& value, Windows::UI::Xaml::Interop::TypeName const& targetType, Windows::Foundation::IInspectable const& parameter, hstring const& language);
        Windows::Foundation::IInspectable ConvertBack(Windows::Foundation::IInspectable const& value, Windows::UI::Xaml::Interop::TypeName const& targetType, Windows::Foundation::IInspectable const& parameter, hstring const& language);
    };
    struct UInt32ToSelectedIndexConverter : UInt32ToSelectedIndexConverterT<UInt32ToSelectedIndexConverter> {
        UInt32ToSelectedIndexConverter() = default;
        Windows::Foundation::IInspectable Convert(Windows::Foundation::IInspectable const& value, Windows::UI::Xaml::Interop::TypeName const& targetType, Windows::Foundation::IInspectable const& parameter, hstring const& language);
        Windows::Foundation::IInspectable ConvertBack(Windows::Foundation::IInspectable const& value, Windows::UI::Xaml::Interop::TypeName const& targetType, Windows::Foundation::IInspectable const& parameter, hstring const& language);
    };
}

namespace winrt::BiliUWP::factory_implementation {
    struct StringToBitmapImageConverter : StringToBitmapImageConverterT<StringToBitmapImageConverter, implementation::StringToBitmapImageConverter> {};
    struct StringToUriImageConverter : StringToUriImageConverterT<StringToUriImageConverter, implementation::StringToUriImageConverter> {};
    struct UInt32ToSelectedIndexConverter : UInt32ToSelectedIndexConverterT<UInt32ToSelectedIndexConverter, implementation::UInt32ToSelectedIndexConverter> {};
}
