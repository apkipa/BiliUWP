#include "pch.h"
#include "LoginPage.h"
#if __has_include("LoginPage.g.cpp")
#include "LoginPage.g.cpp"
#endif
#include "App.h"
#include "qrcodegen.hpp"

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;

namespace winrt::BiliUWP::implementation {
    using namespace Windows::Foundation;

    LoginPage::LoginPage() :
        m_result(std::make_shared<LoginPageResult>(LoginPageResult::UserCanceled)),
        m_qr_session()
    {
        InitializeComponent();
    }

    void LoginPage::OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs const&) {
        auto tab = ::BiliUWP::App::get()->tab_from_page(*this);
        tab->set_icon(Symbol::Permissions);
        tab->set_title(::BiliUWP::App::res_str(L"App/Common/Login"));
        tab->closed_by_user([this] {
            // Page is closed; stop all pending tasks
            auto cur_async_op = m_cur_async_op;
            if (cur_async_op) {
                cur_async_op.Cancel();
            }
            m_finish_event.set();
        });
    }

    IAsyncOperation<BiliUWP::LoginPageResult> LoginPage::RequestLogin(void) {
        auto cancellation_token = co_await winrt::get_cancellation_token();
        cancellation_token.enable_propagation();

        // Make sure we can still access the result after this has been destroyed
        auto result = m_result;
        co_await m_finish_event;
        co_return *result;
    }
    void LoginPage::LoginMethodsList_ItemClick(IInspectable const&, ItemClickEventArgs const& e) {
        if (e.ClickedItem() == QRLoginItem()) {
            // TODO: QRCode login
            LoginMethodSelectionPane().Visibility(Visibility::Collapsed);
            QRCodeLoginPane().Visibility(Visibility::Visible);
            TokenLoginPane().Visibility(Visibility::Collapsed);
            m_cur_async_op = [this]() -> IAsyncAction {
                // Fetch QRCode (only for the first time) and poll
                auto cancellation_token = co_await winrt::get_cancellation_token();
                cancellation_token.enable_propagation();
                // TODO...
                if (m_qr_session.auth_code == L"") {
                    // Fetch QRCode
                    QRCodeProgRing().IsActive(true);
                    deferred([this] {
                        QRCodeProgRing().IsActive(false);
                    });
                    auto client = ::BiliUWP::App::get()->bili_client();
                    m_qr_session = std::move(co_await client->request_tv_qr_login({}));
                }
                // Poll QRCode
                co_return;
            }();
        }
        else if (e.ClickedItem() == TokenLoginItem()) {
            // TODO: Token login
            LoginMethodSelectionPane().Visibility(Visibility::Collapsed);
            QRCodeLoginPane().Visibility(Visibility::Collapsed);
            TokenLoginPane().Visibility(Visibility::Visible);
        }
        else {
            util::debug::log_error(L"Unable to determine login method");
        }
    }
    void LoginPage::ButtonClick_ReturnToMethodsList(IInspectable const&, RoutedEventArgs const&) {
        if (m_cur_async_op) {
            m_cur_async_op.Cancel();
            m_cur_async_op = nullptr;
        }

        LoginMethodSelectionPane().Visibility(Visibility::Visible);
        QRCodeLoginPane().Visibility(Visibility::Collapsed);
        TokenLoginPane().Visibility(Visibility::Collapsed);
    }
    void LoginPage::QRCodeReload_Click(IInspectable const&, RoutedEventArgs const&) {
        m_cur_async_op = [this]() -> IAsyncAction {
            // Reload QRCode and poll
            auto cancellation_token = co_await winrt::get_cancellation_token();
            cancellation_token.enable_propagation();
            // TODO...
            // Fetch QRCode
            // Poll QRCode
            co_return;
        }();
    }
    void LoginPage::TokenLogin_Click(IInspectable const&, RoutedEventArgs const&) {
        // Populate token and finish the process
        auto cfg_model = ::BiliUWP::App::get()->cfg_model();
        cfg_model.User_AccessToken(TokenLogin_AccessToken().Text());
        cfg_model.User_RefreshToken(TokenLogin_RefreshToken().Text());
        cfg_model.User_Cookies_SESSDATA(TokenLogin_Cookies_SESSDATA().Text());
        cfg_model.User_Cookies_bili_jct(TokenLogin_Cookies_bili_jct().Text());
        cfg_model.User_Cookies_DedeUserID(TokenLogin_Cookies_DedeUserID().Text());
        cfg_model.User_Cookies_DedeUserID__ckMd5(TokenLogin_Cookies_DedeUserID__ckMd5().Text());
        cfg_model.User_Cookies_sid(TokenLogin_Cookies_sid().Text());
        *m_result = LoginPageResult::Ok;
        m_finish_event.set();
        ::BiliUWP::App::get()->signal_login_state_changed();
    }
}
