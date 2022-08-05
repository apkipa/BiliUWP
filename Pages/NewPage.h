#pragma once

#include "NewPage.g.h"

namespace winrt::BiliUWP::implementation {
    struct NewPage : NewPageT<NewPage> {
        NewPage();

        void OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs const& e);

        void SearchBox_PreviewKeyDown(
            Windows::Foundation::IInspectable const& sender,
            Windows::UI::Xaml::Input::KeyRoutedEventArgs const& e
        );
        void SearchBox_TextChanged(
            Windows::UI::Xaml::Controls::AutoSuggestBox const& sender,
            Windows::UI::Xaml::Controls::AutoSuggestBoxTextChangedEventArgs const& e
        );
        void SearchBox_QuerySubmitted(
            Windows::UI::Xaml::Controls::AutoSuggestBox const& sender,
            Windows::UI::Xaml::Controls::AutoSuggestBoxQuerySubmittedEventArgs const& e
        );

        void Button_MyFavourites_Click(
            Windows::Foundation::IInspectable const& sender,
            Windows::UI::Xaml::RoutedEventArgs const& e
        );
        void Button_Settings_Click(
            Windows::Foundation::IInspectable const& sender,
            Windows::UI::Xaml::RoutedEventArgs const& e
        );
    private:
    };
}

namespace winrt::BiliUWP::factory_implementation {
    struct NewPage : NewPageT<NewPage, implementation::NewPage> {};
}
