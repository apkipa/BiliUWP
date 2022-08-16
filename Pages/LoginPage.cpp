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
            LoginMethodSelectionPane().Visibility(Visibility::Collapsed);
            QRCodeLoginPane().Visibility(Visibility::Visible);
            TokenLoginPane().Visibility(Visibility::Collapsed);
            m_cur_async_op = [](LoginPage* that) -> IAsyncAction {
                using namespace std::chrono_literals;
                // Fetch QRCode (only for the first time) and poll
                auto cancellation_token = co_await winrt::get_cancellation_token();
                cancellation_token.enable_propagation();
                try {
                    if (that->m_qr_session.auth_code == L"") {
                        // Update QRCode
                        co_await that->UpdateQRCcodeImage();
                    }
                    // Return early if the process has already failed
                    if (that->QRCodeReload().Visibility() == Visibility::Visible ||
                        that->QRCodeFailed().Visibility() == Visibility::Visible)
                    {
                        co_return;
                    }
                    // Poll QRCode
                    for (;; co_await 3s) {
                        co_await that->Dispatcher();
                        switch (co_await that->PollQRCcodeStatus()) {
                        case QRCodePollResult::Expired:
                            that->QRCodeReload().Visibility(Visibility::Visible);
                            co_return;
                        case QRCodePollResult::Success:
                            *that->m_result = LoginPageResult::Ok;
                            that->m_finish_event.set();
                            ::BiliUWP::App::get()->signal_login_state_changed();
                            co_return;
                        case QRCodePollResult::Continue:
                            break;
                        default:
                            throw hresult_error(E_FAIL, L"Unexpected QRCodePollResult");
                        }
                    }
                }
                catch (hresult_canceled const&) {
                    co_return;
                }
                catch (hresult_error const& e) {
                    that->QRCodeFailed().Visibility(Visibility::Visible);
                    util::debug::log_error(e.message());
                }
                catch (::BiliUWP::BiliApiException const& e) {
                    that->QRCodeFailed().Visibility(Visibility::Visible);
                    util::debug::log_error(winrt::to_hstring(e.what()));
                }
                catch (...) {
                    that->QRCodeFailed().Visibility(Visibility::Visible);
                    util::debug::log_error(L"Unknown error occurred");
                }
            }(this);
        }
        else if (e.ClickedItem() == TokenLoginItem()) {
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
    void LoginPage::QRCodeReloadButton_Click(IInspectable const&, RoutedEventArgs const&) {
        m_cur_async_op = [](LoginPage* that) -> IAsyncAction {
            using namespace std::chrono_literals;
            // Reload QRCode and poll
            auto cancellation_token = co_await winrt::get_cancellation_token();
            cancellation_token.enable_propagation();
            // Update QRCode
            that->QRCodeFailed().Visibility(Visibility::Collapsed);
            that->QRCodeReload().Visibility(Visibility::Collapsed);
            try {
                co_await that->UpdateQRCcodeImage();
                for (;; co_await 3s) {
                    co_await that->Dispatcher();
                    switch (co_await that->PollQRCcodeStatus()) {
                    case QRCodePollResult::Expired:
                        that->QRCodeReload().Visibility(Visibility::Visible);
                        co_return;
                    case QRCodePollResult::Success:
                        *that->m_result = LoginPageResult::Ok;
                        that->m_finish_event.set();
                        ::BiliUWP::App::get()->signal_login_state_changed();
                        co_return;
                    case QRCodePollResult::Continue:
                        break;
                    default:
                        throw hresult_error(E_FAIL, L"Unexpected QRCodePollResult");
                    }
                }
            }
            catch (hresult_canceled const&) {
                co_return;
            }
            catch (hresult_error const& e) {
                that->QRCodeFailed().Visibility(Visibility::Visible);
                util::debug::log_error(e.message());
            }
            catch (::BiliUWP::BiliApiException const& e) {
                that->QRCodeFailed().Visibility(Visibility::Visible);
                util::debug::log_error(winrt::to_hstring(e.what()));
            }
            catch (...) {
                that->QRCodeFailed().Visibility(Visibility::Visible);
                util::debug::log_error(L"Unknown error occurred");
            }
        }(this);
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
    util::winrt::task<> LoginPage::UpdateQRCcodeImage(void) {
        using namespace Windows::Storage::Streams;
        using namespace Windows::Graphics::Display;
        using namespace Windows::Security::Cryptography;

        const int qrcode_border_size = 2;
        const float zoom_factor = DisplayInformation::GetForCurrentView().LogicalDpi() / 96;

        auto cancellation_token = co_await winrt::get_cancellation_token();
        cancellation_token.enable_propagation();

        auto qrcode_image = this->QRCodeImage();
        qrcode_image.Source(nullptr);
        QRCodeProgRing().IsActive(true);
        deferred([weak_this = this->get_weak()] {
            auto strong_this = weak_this.get();
            if (!strong_this) {
                return;
            }
            strong_this->QRCodeProgRing().IsActive(false);
        });

        auto client = ::BiliUWP::App::get()->bili_client();
        auto req_result = std::move(co_await client->request_tv_qr_login({}));
        auto qrcode = qrcodegen::QrCode::encodeText(req_result.url.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
        auto qrcode_svg = qrcodegen::toSvgString(qrcode, qrcode_border_size);

        const int qrcode_size = (qrcode.getSize() + qrcode_border_size) * 5;

        auto mem_stream = InMemoryRandomAccessStream();
        co_await mem_stream.WriteAsync(
            CryptographicBuffer::ConvertStringToBinary(qrcode_svg, BinaryStringEncoding::Utf8)
        );
        mem_stream.Seek(0);

        auto svg_img_src = Windows::UI::Xaml::Media::Imaging::SvgImageSource();
        qrcode_image.Width(qrcode_size / zoom_factor);
        qrcode_image.Height(qrcode_size / zoom_factor);
        // TODO: Check if image is actually opened
        /*qrcode_image.ImageOpened([this](IInspectable const&, RoutedEventArgs const&) {
            QRCodeProgRing().IsActive(false);
        });*/
        qrcode_image.Source(svg_img_src);
        co_await svg_img_src.SetSourceAsync(mem_stream);

        m_qr_session = std::move(req_result);
    }
    util::winrt::task<QRCodePollResult> LoginPage::PollQRCcodeStatus(void) {
        auto cancellation_token = co_await winrt::get_cancellation_token();
        cancellation_token.enable_propagation();

        auto client = ::BiliUWP::App::get()->bili_client();
        auto result = std::move(co_await client->poll_tv_qr_login(m_qr_session.auth_code, {}));
        switch (result.code) {
        case ::BiliUWP::ApiCode::TvQrLogin_QrNotConfirmed:
            co_return QRCodePollResult::Continue;
        case ::BiliUWP::ApiCode::TvQrLogin_QrExpired:
            co_return QRCodePollResult::Expired;
        case ::BiliUWP::ApiCode::Success:
        {
            util::debug::log_trace(std::format(L"Got tokens which expire in {}", result.expires_in));
            auto cfg_model = ::BiliUWP::App::get()->cfg_model();
            cfg_model.User_AccessToken(result.access_token);
            cfg_model.User_RefreshToken(result.refresh_token);
            cfg_model.User_Cookies_SESSDATA(result.user_cookies.SESSDATA);
            cfg_model.User_Cookies_bili_jct(result.user_cookies.bili_jct);
            cfg_model.User_Cookies_DedeUserID(result.user_cookies.DedeUserID);
            cfg_model.User_Cookies_DedeUserID__ckMd5(result.user_cookies.DedeUserID__ckMd5);
            cfg_model.User_Cookies_sid(result.user_cookies.sid);
            co_return QRCodePollResult::Success;
        }
        default:
            throw hresult_error(E_FAIL,
                std::format(L"Got unknown ApiCode {}", util::misc::enum_to_int(result.code))
            );
        }
    }
}
