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

/*
struct ::BiliUWP::DebugConsoleImpl : std::enable_shared_from_this<DebugConsoleImpl> {
    DebugConsoleImpl(CoreApplicationView const& cav) : m_cav(cav) {}
    ~DebugConsoleImpl() { Close(); }
    void Close(void) {
        if (!m_cav) { return; }
        m_cav.Dispatcher().RunAsync(CoreDispatcherPriority::Normal, [] {
            auto lv = Window::Current().Content().as<ListView>();
            lv.Items().Clear();
            Window::Current().Close();
        });
        m_cav = nullptr;
    }
    void ClearLogs(void) {
        m_cav.Dispatcher().RunAsync(CoreDispatcherPriority::Normal, [] {
            auto lv = Window::Current().Content().as<ListView>();
            lv.Items().Clear();
        });
    }
    void AppendLog(
        std::chrono::system_clock::time_point time,
        util::debug::LogLevel level,
        std::source_location src_loc,
        winrt::hstring content
    ) {
        m_cav.Dispatcher().RunAsync(CoreDispatcherPriority::Normal, [=] {
            auto tb_style = Style(xaml_typename<TextBlock>());
            auto tb_style_setter1 = Setter(TextBlock::FontFamilyProperty(), FontFamily(L"Consolas"));
            tb_style.Setters().Append(tb_style_setter1);
            auto lv = Window::Current().Content().as<ListView>();
            auto lvi = ListViewItem();
            lvi.Padding(ThicknessHelper::FromUniformLength(0));
            lvi.MinHeight(0);
            lvi.Height(20);
            auto grid = Grid();
            grid.ColumnSpacing(4);
            auto grid_cd1 = ColumnDefinition();
            grid_cd1.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Auto));
            grid.ColumnDefinitions().Append(grid_cd1);
            auto grid_cd2 = ColumnDefinition();
            grid_cd2.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Auto));
            grid.ColumnDefinitions().Append(grid_cd2);
            auto grid_cd3 = ColumnDefinition();
            grid_cd3.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
            grid.ColumnDefinitions().Append(grid_cd3);
            auto rt0 = Rectangle();
            rt0.VerticalAlignment(VerticalAlignment::Stretch);
            rt0.Width(4);
            rt0.Fill(brush_from_log_level(level));
            grid.Children().Append(rt0);
            auto tb1 = util::winrt::make_text_block(hstring(std::format(
                L"[{}]", std::chrono::zoned_time{ std::chrono::current_zone(), time }
            )));
            Grid::SetColumn(tb1, 1);
            //tb1.Style(tb_style);
            grid.Children().Append(tb1);
            // Clip the text to workaround performance degradation
            std::wstring_view sv{ content.begin(), content.end() };
            auto tb2 = util::winrt::make_text_block(hstring{ sv.substr(0, 280) });
            tb2.TextTrimming(TextTrimming::CharacterEllipsis);
            Grid::SetColumn(tb2, 2);
            //tb2.Style(tb_style);
            grid.Children().Append(tb2);
            lvi.Content(grid);
            lv.Items().Append(lvi);
            lv.ScrollIntoView(lvi);
        });
    }
    bool IsAlive(void) {
        return static_cast<bool>(m_cav);
    }

private:
    CoreApplicationView m_cav;
};
*/

struct ::BiliUWP::DebugConsoleImpl : std::enable_shared_from_this<DebugConsoleImpl> {
    DebugConsoleImpl(winrt::BiliUWP::DebugConsoleWindowPage page) : m_page(std::move(page)) {}
    ~DebugConsoleImpl() { Close(); }
    void Close(void) {
        if (!m_page) { return; }
        m_page.Dispatcher().RunAsync(CoreDispatcherPriority::Normal, [] {
            Window::Current().Close();
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
