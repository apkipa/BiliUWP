#include "pch.h"
#include "DebugConsoleWindowPage.h"
#if __has_include("DebugConsoleWindowPage.g.cpp")
#include "DebugConsoleWindowPage.g.cpp"
#endif
#include "DebugConsoleWindowPage_LogViewItem.g.h"
#include "DebugConsoleWindowPage_LogViewItem.g.cpp"

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::UI;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Controls;

SolidColorBrush brush_from_log_level(util::debug::LogLevel level) {
    static auto brush_gray = SolidColorBrush(Colors::Gray());
    static auto brush_magenta = SolidColorBrush(Colors::Magenta());
    static auto brush_cyan = SolidColorBrush(Colors::Cyan());
    static auto brush_orange = SolidColorBrush(Colors::Orange());
    static auto brush_red = SolidColorBrush(Colors::Red());
    switch (level) {
    case util::debug::LogLevel::Trace:      return brush_gray;
    case util::debug::LogLevel::Debug:      return brush_magenta;
    case util::debug::LogLevel::Info:       return brush_cyan;
    case util::debug::LogLevel::Warn:       return brush_orange;
    case util::debug::LogLevel::Error:      return brush_red;
    default:                                return nullptr;
    }
}

namespace winrt::BiliUWP {
    DebugConsoleWindowPage_LogViewItem DebugConsoleWindowPage_MakeLogViewItem(
        std::chrono::system_clock::time_point time,
        util::debug::LogLevel level,
        std::source_location src_loc,
        winrt::hstring content
    ) {
        return winrt::make<winrt::BiliUWP::implementation::DebugConsoleWindowPage_LogViewItem>(
            std::move(time), level, std::move(src_loc), std::move(content));
    }
}

namespace winrt::BiliUWP::implementation {
    wchar_t DebugConsoleWindowPage_LogViewItem::LevelChar() {
        switch (m_level) {
        case util::debug::LogLevel::Trace:  return 'T';
        case util::debug::LogLevel::Debug:  return 'D';
        case util::debug::LogLevel::Info:   return 'I';
        case util::debug::LogLevel::Warn:   return 'W';
        case util::debug::LogLevel::Error:  return 'E';
        default:                            return '?';
        }
    }
    Brush DebugConsoleWindowPage_LogViewItem::ColorBrush() {
        return brush_from_log_level(m_level);
    }
    hstring DebugConsoleWindowPage_LogViewItem::Time() {
        return hstring(std::format(L"[{}]", std::chrono::zoned_time{ std::chrono::current_zone(), m_time }));
    }
    hstring DebugConsoleWindowPage_LogViewItem::SourceLocation() {
        return to_hstring(std::format("[{}:{}:{}]",
            m_src_loc.file_name(), m_src_loc.function_name(), m_src_loc.line()));
    }
    DebugConsoleWindowPage::DebugConsoleWindowPage() {}
    void DebugConsoleWindowPage::LogsList_SelectionChanged(
        IInspectable const& sender,
        SelectionChangedEventArgs const&
    ) {
        auto log_item = sender.as<ListView>().SelectedItem().try_as<DebugConsoleWindowPage_LogViewItem>();
        if (!log_item) { return; }
        auto full_text = std::format(L"{} {} {} {}",
            log_item->Time(), log_item->LevelChar(), log_item->SourceLocation(), log_item->Content());
        util::winrt::set_clipboard_text(hstring{ std::move(full_text) }, true);
    }
    void DebugConsoleWindowPage::AppendLog(winrt::BiliUWP::DebugConsoleWindowPage_LogViewItem log_item) {
        // TODO: Known issue: Closing & reopening debug console raises hresult_wrong_thread
        //       (AppendLog() must be triggered; here LogsList().ScrollIntoView() throws)
        auto logs_list = LogsList();
        logs_list.Items().Append(log_item);
        logs_list.ScrollIntoView(log_item);
    }
    void DebugConsoleWindowPage::ClearLogs(void) {
        LogsList().Items().Clear();
    }
}
