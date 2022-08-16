#pragma once
#include "LoginPage.g.h"
#include "util.hpp"
#include "BiliClient.hpp"

namespace winrt::BiliUWP::implementation {
    struct LoginPage : LoginPageT<LoginPage> {
        LoginPage();

        void OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs const&);

        Windows::Foundation::IAsyncOperation<BiliUWP::LoginPageResult> RequestLogin(void);

        void LoginMethodsList_ItemClick(
            Windows::Foundation::IInspectable const&,
            Windows::UI::Xaml::Controls::ItemClickEventArgs const& e
        );
        void ButtonClick_ReturnToMethodsList(
            Windows::Foundation::IInspectable const&,
            Windows::UI::Xaml::RoutedEventArgs const&
        );
        void QRCodeReloadButton_Click(
            Windows::Foundation::IInspectable const&,
            Windows::UI::Xaml::RoutedEventArgs const&
        );
        void TokenLogin_Click(
            Windows::Foundation::IInspectable const&,
            Windows::UI::Xaml::RoutedEventArgs const&
        );

        static void final_release(std::unique_ptr<LoginPage> ptr) noexcept {
            // Page is closed; stop all pending tasks
            auto cur_async_op = ptr->m_cur_async_op;
            ptr->m_finish_event.set();
            if (cur_async_op) {
                cur_async_op.Cancel();
            }
        }

    private:
        util::winrt::task<> UpdateQRCcodeImage(void);
        util::winrt::task<QRCodePollResult> PollQRCcodeStatus(void);

        util::winrt::awaitable_event m_finish_event;

        std::shared_ptr<LoginPageResult> m_result;
        ::BiliUWP::RequestTvQrLoginResult m_qr_session;
        Windows::Foundation::IAsyncAction m_cur_async_op;
    };
}

namespace winrt::BiliUWP::factory_implementation {
    struct LoginPage : LoginPageT<LoginPage, implementation::LoginPage> {};
}
