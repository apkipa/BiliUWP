#include "pch.h"
#include "DebugConsole.hpp"
#include "DebugConsoleWindowPage.h"

using namespace winrt;
using namespace Windows::UI;
using namespace Windows::UI::Core;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Media::Animation;
using namespace Windows::UI::Xaml::Shapes;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::ViewManagement;
using namespace Windows::Foundation;
using namespace Windows::ApplicationModel::Core;

struct ::BiliUWP::DebugConsoleImpl : std::enable_shared_from_this<DebugConsoleImpl> {
    DebugConsoleImpl(winrt::BiliUWP::DebugConsoleWindowPage page) : m_page(std::move(page)) {}
    ~DebugConsoleImpl() { Close(); }
    void Close(void) {
        if (!m_page) { return; }
        m_page.Dispatcher().RunAsync(CoreDispatcherPriority::Normal, [] {
            auto window = Window::Current();
            window.Content(nullptr);
            window.Close();
        });
        m_page = nullptr;
    }
    void ClearLogs(void) {
        m_page.Dispatcher().RunAsync(CoreDispatcherPriority::Normal, [page = m_page] {
            page.ClearLogs();
        });
    }
    void AppendLog(
        std::chrono::system_clock::time_point time,
        util::debug::LogLevel level,
        std::source_location src_loc,
        winrt::hstring content
    ) {
        m_page.Dispatcher().RunAsync(CoreDispatcherPriority::Normal,
            [log_item = winrt::BiliUWP::DebugConsoleWindowPage_MakeLogViewItem(
                std::move(time), level, std::move(src_loc), std::move(content)), page = m_page
            ] {
                page.AppendLog(log_item);
            }
        );
    }
    bool IsAlive(void) {
        return static_cast<bool>(m_page);
    }

private:
    winrt::BiliUWP::DebugConsoleWindowPage m_page;
};

namespace BiliUWP {
    void DebugConsole::ClearLogs(void) { m_pimpl->ClearLogs(); }
    void DebugConsole::AppendLog(
        std::chrono::system_clock::time_point time,
        util::debug::LogLevel level,
        std::source_location src_loc,
        winrt::hstring content
    ) {
        m_pimpl->AppendLog(std::move(time), std::move(level), std::move(src_loc), std::move(content));
    }
    bool DebugConsole::IsAlive(void) { return m_pimpl && m_pimpl->IsAlive(); }
    util::winrt::task<DebugConsole> DebugConsole::CreateAsync(void) {
        auto cav = CoreApplication::CreateNewView();
        ApplicationView av = nullptr;
        winrt::BiliUWP::DebugConsoleWindowPage page = nullptr;
        int32_t avid;
        co_await cav.Dispatcher().RunAsync(CoreDispatcherPriority::Normal, [&] {
            av = ApplicationView::GetForCurrentView();
            avid = av.Id();
            av.Title(L"Debug Console");
            auto window = Window::Current();
            auto frame = Frame();
            frame.Navigate(xaml_typename<winrt::BiliUWP::DebugConsoleWindowPage>());
            page = frame.Content().as<winrt::BiliUWP::DebugConsoleWindowPage>();
            window.Content(frame);
            window.Activate();
        });
        co_await ApplicationViewSwitcher::TryShowAsStandaloneAsync(avid);
        auto pimpl = std::make_shared<DebugConsoleImpl>(std::move(page));
        co_await cav.Dispatcher().RunAsync(CoreDispatcherPriority::Normal, [&] {
            av.Consolidated(
                [weak_this = pimpl->weak_from_this()]
                (ApplicationView const&, ApplicationViewConsolidatedEventArgs const&) {
                    if (auto strong_this = weak_this.lock()) {
                        strong_this->Close();
                    }
                }
            );
        });
        co_return pimpl;
    }
}
