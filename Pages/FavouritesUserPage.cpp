#include "pch.h"
#include "FavouritesUserPage.h"
#if __has_include("FavouritesUserPage.g.cpp")
#include "FavouritesUserPage.g.cpp"
#endif
#include "App.h"

using namespace winrt;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::Foundation;

namespace winrt::BiliUWP::implementation {
    FavouritesUserPage::FavouritesUserPage() {}
    void FavouritesUserPage::OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs const& e) {
        auto tab = ::BiliUWP::App::get()->tab_from_page(*this);
        tab->set_icon(Symbol::OutlineStar);
        tab->set_title(::BiliUWP::App::res_str(L"App/Page/FavouritesUserPage/Title"));
        // TODO: Use App.Page.FavouritesUserPage.MyTitle if page opens self
    }
    void FavouritesUserPage::RefreshItem_Click(
        Windows::Foundation::IInspectable const&, Windows::UI::Xaml::RoutedEventArgs const&
    ) {
        util::debug::log_error(L"Not implemented");
        BiliUWP::SimpleContentDialog cd;
        cd.Title(box_value(L"Not Implemented"));
        cd.CloseButtonText(L"Close");
        auto app = ::BiliUWP::App::get();
        app->tab_from_page(*this)->show_dialog(cd);
    }
}
