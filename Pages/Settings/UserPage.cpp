#include "pch.h"
#include "Settings/UserPage.h"
#if __has_include("Settings/UserPage.g.cpp")
#include "Settings/UserPage.g.cpp"
#endif

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Navigation;

namespace winrt::BiliUWP::Settings::implementation {
    UserPage::UserPage() {
        ::BiliUWP::App::get()->login_state_changed({ this, &UserPage::UpdateUI });
    }
    void UserPage::OnNavigatedTo(NavigationEventArgs const& e) {
        m_main_page = e.Parameter().as<MainPage>().get();

        this->UpdateUI();
    }
    fire_forget_except UserPage::LoginButton_Click(IInspectable const&, RoutedEventArgs const&) {
        co_await ::BiliUWP::App::get()->request_login_blocking(m_main_page->GetAppTab());
    }
    fire_forget_except UserPage::LogoutButton_Click(IInspectable const&, RoutedEventArgs const&) {
        co_await ::BiliUWP::App::get()->request_logout();
    }
    void UserPage::UpdateUI() {
        auto app = ::BiliUWP::App::get();
        if (app->is_logged_in()) {
            this->IsLoggedIn(true);
            m_async.cancel_and_run([](UserPage* that) -> IAsyncAction {
                auto cancellation_token = co_await get_cancellation_token();
                cancellation_token.enable_propagation();

                auto ws = util::winrt::make_weak_storage(*that);

                auto client = ::BiliUWP::App::get()->bili_client();
                auto result = co_await ws.ual(client->my_account_nav_info());

                that->UserNameTextBlockText(result.uname);
                that->UserPictureImageUri(Uri(result.face_url));

                that->Bindings->Update();
            }, this);

            Bindings->Update();
        }
        else {
            this->IsLoggedIn(false);
            this->UserNameTextBlockText(::BiliUWP::App::res_str(L"App/Common/NotLoggedIn"));
            this->UserPictureImageUri(nullptr);
        }
    }
}
