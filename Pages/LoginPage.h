#pragma once
#include "LoginPage.g.h"

namespace winrt::BiliUWP::implementation {
    struct LoginPage : LoginPageT<LoginPage> {
        LoginPage();

        Windows::Foundation::IAsyncOperation<BiliUWP::LoginPageResult> RequestLogin(void);
    };
}

namespace winrt::BiliUWP::factory_implementation {
    struct LoginPage : LoginPageT<LoginPage, implementation::LoginPage> {};
}
