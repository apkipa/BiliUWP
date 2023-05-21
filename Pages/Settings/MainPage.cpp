#include "pch.h"
#include "Settings/MainPage.h"
#if __has_include("Settings/MainPage.g.cpp")
#include "Settings/MainPage.g.cpp"
#endif
#include "App.h"

constexpr unsigned ENTER_DEV_MODE_TAP_TIMES = 3;

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::ApplicationModel::DataTransfer;
using namespace Windows::Storage;

namespace winrt {
    namespace muxc = Microsoft::UI::Xaml::Controls;
}

namespace winrt::BiliUWP::Settings::implementation {
    MainPage::MainPage() :
        m_cfg_model(::BiliUWP::App::get()->cfg_model()),
        m_app_dbg_settings(Application::Current().DebugSettings()) {}

    void MainPage::OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs const&) {
        auto tab = ::BiliUWP::App::get()->tab_from_page(*this);
        tab->set_icon(Symbol::Setting);
        tab->set_title(::BiliUWP::App::res_str(L"App/Page/Settings/MainPage/Title"));
        util::winrt::run_when_loaded([this](auto&&) {
            // Link NavigationViewItem's with their corresponding pages
            Nvi_User().Tag(box_value(xaml_typename<Settings::UserPage>()));
            Nvi_Appearance().Tag(box_value(xaml_typename<Settings::AppearancePage>()));
            Nvi_Playing().Tag(box_value(xaml_typename<Settings::PlayingPage>()));
            Nvi_Misc().Tag(box_value(xaml_typename<Settings::MiscPage>()));
            Nvi_Developer().Tag(box_value(xaml_typename<Settings::DeveloperPage>()));
            Nvi_About().Tag(box_value(xaml_typename<Settings::AboutPage>()));

            // Automatically load one item into ContentFrame
            auto main_nav_view = MainNavView();
            auto default_nvi = Nvi_User();
            main_nav_view.SelectedItem(default_nvi);
            this->UpdateNavigationFrame(default_nvi);
        }, this);

        Unloaded([this](auto&&, auto&&) {
            this->UpdateContentFrameTransition(false);
        });
    }
    void MainPage::MainNavView_ItemInvoked(
        IInspectable const&,
        muxc::NavigationViewItemInvokedEventArgs const& e
    ) {
        auto nvi = e.InvokedItemContainer();
        if (nvi.IsSelected()) { return; }
        this->UpdateNavigationFrame(nvi);
    }
    fire_forget_except MainPage::RequestEnableDeveloperMode(void) {
        BiliUWP::SimpleContentDialog cd;
        cd.Title(box_value(::BiliUWP::App::res_str(L"App/Dialog/AskEnableDevMode/Title")));
        cd.Content(box_value(::BiliUWP::App::res_str(L"App/Dialog/AskEnableDevMode/Content")));
        cd.PrimaryButtonText(::BiliUWP::App::res_str(L"App/Common/Yes"));
        cd.CloseButtonText(::BiliUWP::App::res_str(L"App/Common/No"));
        auto show_dialog_op = this->GetAppTab()->show_dialog(cd);
        auto cfg_model = m_cfg_model;
        switch (co_await std::move(show_dialog_op)) {
        case SimpleContentDialogResult::None:
            break;
        case SimpleContentDialogResult::Primary:
            m_cfg_model.App_IsDeveloper(true);
            break;
        }
    }
    void MainPage::UpdateNavigationFrame(muxc::NavigationViewItemBase const& nvi) {
        if (auto tn = nvi.Tag().try_as<Windows::UI::Xaml::Interop::TypeName>()) {
            MainNavViewHeaderTextBlock().Text(nvi.Content().as<hstring>());
            this->UpdateContentFrameTransition(true);
            auto cf = ContentFrame();
            cf.Navigate(*tn, *this);
            cf.BackStack().Clear();
        }
    }
    void MainPage::UpdateContentFrameTransition(bool enable) {
        if (m_cf_transition_enabled == enable) { return; }
        m_cf_transition_enabled = enable;
        auto cf_ct = ContentFrame().ContentTransitions();
        if (enable) {
            cf_ct.Append(ContentFrameNavigationThemeTransition());
        }
        else {
            cf_ct.Clear();
        }
    }
}
