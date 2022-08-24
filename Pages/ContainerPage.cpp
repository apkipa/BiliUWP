#include "pch.h"
#include "ContainerPage.h"
#if __has_include("ContainerPage.g.cpp")
#include "ContainerPage.g.cpp"
#endif

using namespace winrt;
using namespace Windows::UI::Xaml;

namespace winrt::BiliUWP::implementation {
    ContainerPage::ContainerPage() {}
    void ContainerPage::OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs const& e) {
        this->ContentPresenter().Content(e.Parameter());
    }
}
