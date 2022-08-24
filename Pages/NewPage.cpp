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

enum class BiliResType {
    Unknown = 0,
    VideoA,             // av<...>
    VideoB,             // bv<...>
    Audio,              // au<...>
    Column,             // cv<...>
    User,               // uid<...>
    FavouritesUser,     // fuid<...>
    FavouritesFolder,   // ffid<...>
    Live,               // live<...>
};

auto extract_nid_from_str(std::wstring_view sv, size_t skip_prefix_count = 0) {
    uint64_t n = 0;
    for (sv.remove_prefix(skip_prefix_count); !sv.empty(); sv.remove_prefix(1)) {
        n = n * 10 + static_cast<decltype(n)>(sv[0] - L'0');
    }
    return n;
};
BiliResType parse_res_type_from_str(winrt::hstring const& str) {
    std::wstring_view str_view = str;
    if (str_view.starts_with(L"av") || str_view.starts_with(L"Av") ||
        str_view.starts_with(L"aV") || str_view.starts_with(L"AV"))
    {
        str_view.remove_prefix(2);
        return !str_view.empty() && util::str::is_str_all_digits(str_view) ?
            BiliResType::VideoA : BiliResType::Unknown;
    }
    else if (str_view.starts_with(L"bv") || str_view.starts_with(L"Bv") ||
        str_view.starts_with(L"bV") || str_view.starts_with(L"BV"))
    {
        // BV ids seem to have a consistent length of 12
        return str_view.size() == 12 ? BiliResType::VideoB : BiliResType::Unknown;
    }
    else if (str_view.starts_with(L"au") || str_view.starts_with(L"Au") ||
        str_view.starts_with(L"aU") || str_view.starts_with(L"AU"))
    {
        str_view.remove_prefix(2);
        return !str_view.empty() && util::str::is_str_all_digits(str_view) ?
            BiliResType::Audio : BiliResType::Unknown;
    }
    else if (str_view.starts_with(L"fuid")) {
        str_view.remove_prefix(4);
        return !str_view.empty() && util::str::is_str_all_digits(str_view) ?
            BiliResType::FavouritesUser : BiliResType::Unknown;
    }
    else if (str_view.starts_with(L"ffid")) {
        str_view.remove_prefix(4);
        return !str_view.empty() && util::str::is_str_all_digits(str_view) ?
            BiliResType::FavouritesFolder : BiliResType::Unknown;
    }
    else {
        // TODO: Finish parse_res_type_from_str
        return BiliResType::Unknown;
    }
}

namespace winrt::BiliUWP::implementation {
    NewPage::NewPage() {}
    void NewPage::InitializeComponent() {
        NewPageT::InitializeComponent();

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
            // Routes event to parent elements (TabView)
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
        auto icon = sender.QueryIcon().as<SymbolIcon>();
        switch (parse_res_type_from_str(sender.Text())) {
        case BiliResType::VideoA:
        case BiliResType::VideoB:
        case BiliResType::Audio:
        case BiliResType::Column:
        case BiliResType::User:
        case BiliResType::FavouritesUser:
        case BiliResType::FavouritesFolder:
        case BiliResType::Live:
            icon.Symbol(Symbol::Forward);
            break;
        default:
            icon.Symbol(Symbol::Find);
            break;
        }
    }
    void NewPage::SearchBox_QuerySubmitted(
        AutoSuggestBox const& sender,
        AutoSuggestBoxQuerySubmittedEventArgs const&
    ) {
        // TODO: Finish NewPage::SearchBox_QuerySubmitted
        auto tab = ::BiliUWP::make<::BiliUWP::AppTab>();
        auto text = sender.Text();

        switch (parse_res_type_from_str(text)) {
        case BiliResType::VideoA:
            tab->navigate(
                xaml_typename<winrt::BiliUWP::MediaPlayPage>(),
                box_value(MediaPlayPageNavParam{
                    MediaPlayPage_MediaType::Video, extract_nid_from_str(text, 2), L""
                })
            );
            break;
        case BiliResType::VideoB:
            tab->navigate(
                xaml_typename<winrt::BiliUWP::MediaPlayPage>(),
                box_value(MediaPlayPageNavParam{ MediaPlayPage_MediaType::Video, 0, text })
            );
            break;
        case BiliResType::Audio:
            tab->navigate(
                xaml_typename<winrt::BiliUWP::MediaPlayPage>(),
                box_value(MediaPlayPageNavParam{
                    MediaPlayPage_MediaType::Audio, extract_nid_from_str(text, 2), L""
                })
            );
            break;
        case BiliResType::Column:
            // TODO: Implement remaining types of resources
            throw hresult_not_implemented();
        case BiliResType::User:
            // TODO: Implement remaining types of resources
            throw hresult_not_implemented();
        case BiliResType::FavouritesUser:
            tab->navigate(
                xaml_typename<winrt::BiliUWP::FavouritesUserPage>(),
                box_value(FavouritesUserPageNavParam{ extract_nid_from_str(text, 4) })
            );
            break;
        case BiliResType::FavouritesFolder:
            // TODO: Implement remaining types of resources
            throw hresult_not_implemented();
        case BiliResType::Live:
            // TODO: Implement remaining types of resources
            throw hresult_not_implemented();
        default:
            // TODO: Navigate to search page
            throw hresult_not_implemented();
        }

        ::BiliUWP::App::get()->add_tab(tab);
        tab->activate();
    }
    void NewPage::Button_MyFavourites_Click(IInspectable const&, RoutedEventArgs const&) {
        auto app = ::BiliUWP::App::get();
        if (!app->is_logged_in()) {
            app->request_login_blocking(app->tab_from_page(*this));
            return;
        }
        auto tab = ::BiliUWP::make<::BiliUWP::AppTab>();
        tab->navigate(
            xaml_typename<winrt::BiliUWP::FavouritesUserPage>(),
            box_value(FavouritesUserPageNavParam{
                extract_nid_from_str(app->cfg_model().User_Cookies_DedeUserID())
            })
        );
        app->add_tab(tab);
        tab->activate();
    }
    void NewPage::Button_Settings_Click(IInspectable const&, RoutedEventArgs const& e) {
        auto tab = ::BiliUWP::make<::BiliUWP::AppTab>();
        tab->navigate(xaml_typename<winrt::BiliUWP::SettingsPage>());
        ::BiliUWP::App::get()->add_tab(tab);
        tab->activate();
    }
}
