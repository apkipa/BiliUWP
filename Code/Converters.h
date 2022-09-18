#pragma once

#include "StringToBitmapImageConverter.g.h"
#include "StringToUriConverter.g.h"
#include "UInt32ToSelectedIndexConverter.g.h"
#include "NumberScaleConverter.g.h"


namespace winrt::BiliUWP::implementation {
    struct StringToBitmapImageConverter : StringToBitmapImageConverterT<StringToBitmapImageConverter> {
        StringToBitmapImageConverter() = default;
        Windows::Foundation::IInspectable Convert(Windows::Foundation::IInspectable const& value, Windows::UI::Xaml::Interop::TypeName const& targetType, Windows::Foundation::IInspectable const& parameter, hstring const& language);
        Windows::Foundation::IInspectable ConvertBack(Windows::Foundation::IInspectable const& value, Windows::UI::Xaml::Interop::TypeName const& targetType, Windows::Foundation::IInspectable const& parameter, hstring const& language);
    };
    struct StringToUriConverter : StringToUriConverterT<StringToUriConverter> {
        StringToUriConverter() = default;
        Windows::Foundation::IInspectable Convert(Windows::Foundation::IInspectable const& value, Windows::UI::Xaml::Interop::TypeName const& targetType, Windows::Foundation::IInspectable const& parameter, hstring const& language);
        Windows::Foundation::IInspectable ConvertBack(Windows::Foundation::IInspectable const& value, Windows::UI::Xaml::Interop::TypeName const& targetType, Windows::Foundation::IInspectable const& parameter, hstring const& language);
    };
    struct UInt32ToSelectedIndexConverter : UInt32ToSelectedIndexConverterT<UInt32ToSelectedIndexConverter> {
        UInt32ToSelectedIndexConverter() = default;
        Windows::Foundation::IInspectable Convert(Windows::Foundation::IInspectable const& value, Windows::UI::Xaml::Interop::TypeName const& targetType, Windows::Foundation::IInspectable const& parameter, hstring const& language);
        Windows::Foundation::IInspectable ConvertBack(Windows::Foundation::IInspectable const& value, Windows::UI::Xaml::Interop::TypeName const& targetType, Windows::Foundation::IInspectable const& parameter, hstring const& language);
    };
    struct NumberScaleConverter : NumberScaleConverterT<NumberScaleConverter> {
        NumberScaleConverter() = default;
        Windows::Foundation::IInspectable Convert(Windows::Foundation::IInspectable const& value, Windows::UI::Xaml::Interop::TypeName const& targetType, Windows::Foundation::IInspectable const& parameter, hstring const& language);
        Windows::Foundation::IInspectable ConvertBack(Windows::Foundation::IInspectable const& value, Windows::UI::Xaml::Interop::TypeName const& targetType, Windows::Foundation::IInspectable const& parameter, hstring const& language);
    };
}

namespace winrt::BiliUWP::factory_implementation {
    struct StringToBitmapImageConverter : StringToBitmapImageConverterT<StringToBitmapImageConverter, implementation::StringToBitmapImageConverter> {};
    struct StringToUriConverter : StringToUriConverterT<StringToUriConverter, implementation::StringToUriConverter> {};
    struct UInt32ToSelectedIndexConverter : UInt32ToSelectedIndexConverterT<UInt32ToSelectedIndexConverter, implementation::UInt32ToSelectedIndexConverter> {};
    struct NumberScaleConverter : NumberScaleConverterT<NumberScaleConverter, implementation::NumberScaleConverter> {};
}
