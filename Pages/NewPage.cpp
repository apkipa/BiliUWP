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

        SearchBox().Loaded([](IInspectable const& sender, RoutedEventArgs const&) {
            sender.as<AutoSuggestBox>().Focus(FocusState::Programmatic);
        });
    }
    void NewPage::OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs const& e) {
        auto tab = ::BiliUWP::App::get()->tab_from_page(*this);
        tab->set_icon(Symbol::Placeholder);
        tab->set_title(::BiliUWP::App::res_str(L"App/Page/NewPage/Title"));
    }
    void NewPage::SearchBox_PreviewKeyDown(IInspectable const&, KeyRoutedEventArgs const& e) {
        auto cur_core_window = Window::Current().CoreWindow();
        bool is_ctrl_down = static_cast<bool>(
            cur_core_window.GetKeyState(VirtualKey::Control) & CoreVirtualKeyStates::Down
        );
        if (is_ctrl_down && e.OriginalKey() == VirtualKey::Tab) {
            // This should be a good enough workaround without apparent flaws
            ButtonsPane().IsTabStop(true);
            ButtonsPane().Focus(FocusState::Programmatic);
            Dispatcher().RunAsync(CoreDispatcherPriority::High, [this]() {
                SearchBox().Focus(FocusState::Programmatic);
                ButtonsPane().IsTabStop(false);
            });
        }
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
        if (!app->is_logged_in()) {
            app->request_login_blocking(app->tab_from_page(*this));
            return;
        }
        util::debug::log_error(L"Not implemented");
    }
    void NewPage::Button_Settings_Click(IInspectable const&, RoutedEventArgs const& e) {
        // TODO: Finish NewPage::Button_Settings_Click
        BiliUWP::SimpleContentDialog cd;
        cd.Title(box_value(L"Not Implemented"));
        cd.CloseButtonText(L"Close");
        auto app = ::BiliUWP::App::get();
        app->tab_from_page(*this)->show_dialog(cd);
    }
}
