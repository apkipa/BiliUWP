#include "pch.h"
#include "LoginPage.h"
#if __has_include("LoginPage.g.cpp")
#include "LoginPage.g.cpp"
#endif

using namespace winrt;
using namespace Windows::UI::Xaml;

namespace winrt::BiliUWP::implementation {
    using namespace Windows::Foundation;

    LoginPage::LoginPage() {
        InitializeComponent();
    }

    IAsyncOperation<BiliUWP::LoginPageResult> LoginPage::RequestLogin(void) {
        throw hresult_not_implemented();
    }
}
