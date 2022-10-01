#include "pch.h"
#include "SearchPage.h"
#if __has_include("SearchPage.g.cpp")
#include "SearchPage.g.cpp"
#endif
#include "App.h"

using namespace winrt;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;

namespace winrt::BiliUWP::implementation {
    SearchPage::SearchPage() {}
    void SearchPage::OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs const& e) {
        // TODO...
        auto tab = ::BiliUWP::App::get()->tab_from_page(*this);
        tab->set_icon(static_cast<Symbol>(0xE721));
        tab->set_title(L"SearchPage");

        BiliUWP::SimpleContentDialog cd;
        cd.Title(box_value(L"Not Implemented"));
        cd.CloseButtonText(L"Close");
        tab->show_dialog(cd);
    }
}
