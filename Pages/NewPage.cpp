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

namespace winrt::BiliUWP::implementation {
    NewPage::NewPage() {
        InitializeComponent();
    }

    void NewPage::OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs const& e) {
        auto tab = ::BiliUWP::App::get()->tab_from_page(*this);
        auto ico_src = Microsoft::UI::Xaml::Controls::SymbolIconSource();
        ico_src.Symbol(Symbol::Placeholder);
        tab->set_icon(ico_src);
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
    }
    void NewPage::Button_Settings_Click(IInspectable const&, RoutedEventArgs const& e) {
        // TODO: Finish NewPage::Button_Settings_Click
    }
}
