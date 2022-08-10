#include "pch.h"
#include "NewPage.h"
#if __has_include("NewPage.g.cpp")
#include "NewPage.g.cpp"
#endif
#include "App.h"

using namespace winrt;
using namespace Windows::System;
using namespace Windows::UI::Core;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Controls;

using namespace ::BiliUWP::App;

namespace winrt::BiliUWP::implementation {
    NewPage::NewPage() {
        InitializeComponent();
    }

    void NewPage::OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs const& e) {
        auto tab = ::BiliUWP::App::get()->tab_from_page(*this);
        tab->set_icon(Symbol::Placeholder);
        tab->set_title(hstring{ std::format(L"NEW PAGE ({})", (void*)&*tab) });
    }

    void NewPage::SearchBox_PreviewKeyDown(IInspectable const&, KeyRoutedEventArgs const& e) {
        // TODO: Finish NewPage::SearchBox_PreviewKeyDown
    }
    void NewPage::SearchBox_TextChanged(
        AutoSuggestBox const& sender,
        AutoSuggestBoxTextChangedEventArgs const&
    ) {
        // TODO: Finish NewPage::SearchBox_TextChanged
    }
    void NewPage::SearchBox_QuerySubmitted(
        AutoSuggestBox const& sender,
        AutoSuggestBoxQuerySubmittedEventArgs const&
    ) {
        // TODO: Finish NewPage::SearchBox_QuerySubmitted
    }

    void NewPage::Button_MyFavourites_Click(IInspectable const&, RoutedEventArgs const&) {
        // TODO: Finish NewPage::Button_MyFavourites_Click
        auto app = ::BiliUWP::App::get();
        //auto tab = app->tab_from_page(*this);
        /*
        SimpleContentDialog dialog;
        //dialog.Title(box_value(L"错误"));
        dialog.Content(box_value(L"请登录后重试。"));
        dialog.CloseButtonText(L"确定");
        tab->show_dialog(dialog);
        [](auto dialog) -> fire_forget_except {
            using namespace std::chrono_literals;
            co_await 3s;
            dialog.Hide();
        }(dialog);
        */
        if (!app->is_logged_in()) {
            app->request_login_blocking(app->tab_from_page(*this));
            return;
        }
    }
    void NewPage::Button_Settings_Click(IInspectable const&, RoutedEventArgs const& e) {
        // TODO: Finish NewPage::Button_Settings_Click
    }
}
