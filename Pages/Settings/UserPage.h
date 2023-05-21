#pragma once

#include "Settings/UserPage.g.h"
#include "Settings/MainPage.h"

namespace winrt::BiliUWP::Settings::implementation {
    struct UserPage : UserPageT<UserPage> {
        UserPage();

        void OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs const&);
        fire_forget_except LoginButton_Click(
            Windows::Foundation::IInspectable const&,
            Windows::UI::Xaml::RoutedEventArgs const&
        );
        fire_forget_except LogoutButton_Click(
            Windows::Foundation::IInspectable const&,
            Windows::UI::Xaml::RoutedEventArgs const&
        );

        util::winrt::simple_var_accessor<bool> IsLoggedIn;
        bool IsLoggedOut() { return !IsLoggedIn(); }
        util::winrt::simple_var_accessor<hstring> UserNameTextBlockText;
        util::winrt::simple_var_accessor<Windows::Foundation::Uri> UserPictureImageUri{ nullptr };

    private:
        MainPage* m_main_page{};

        void UpdateUI();

        util::winrt::async_storage m_async;
    };
}

namespace winrt::BiliUWP::Settings::factory_implementation {
    struct UserPage : UserPageT<UserPage, implementation::UserPage> {};
}
