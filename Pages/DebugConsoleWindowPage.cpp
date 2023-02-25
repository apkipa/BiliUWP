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
    thread_local auto brush_gray = SolidColorBrush(Colors::Gray());
    thread_local auto brush_magenta = SolidColorBrush(Colors::Magenta());
    thread_local auto brush_cyan = SolidColorBrush(Colors::Cyan());
    thread_local auto brush_orange = SolidColorBrush(Colors::Orange());
    thread_local auto brush_red = SolidColorBrush(Colors::Red());
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
        std::string_view full_path = m_src_loc.file_name();
        auto short_path = full_path.substr(full_path.find("BiliUWP") + 7);
        return to_hstring(std::format("[{}:{}:{}]",
            short_path, m_src_loc.function_name(), m_src_loc.line()));
    }
    DebugConsoleWindowPage::DebugConsoleWindowPage() {
        using namespace std::chrono_literals;
        m_timer_scroll_to_bottom.Interval(30ms);
        m_timer_scroll_to_bottom.Tick([weak_this = get_weak()](IInspectable const&, IInspectable const&) {
            auto that = weak_this.get();
            if (!that) { return; }
            if (!that->m_should_scroll) {
                that->m_timer_scroll_to_bottom.Stop();
            }
            that->m_should_scroll = false;
            auto logs_list = that->LogsList();
            auto logs_list_items = logs_list.Items();
            logs_list.ScrollIntoView(logs_list_items.GetAt(logs_list_items.Size() - 1));
        });
    }
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
        auto logs_list = LogsList();
        auto logs_list_items = logs_list.Items();
        // TODO: Use a better strategy for handling massive logs
        if (logs_list_items.Size() >= 500) {
            // Discard old logs
            for (size_t i = 0; i < 200; i++) {
                logs_list_items.RemoveAt(0);
            }
        }
        logs_list_items.Append(log_item);
        if (!m_timer_scroll_to_bottom.IsEnabled()) {
            m_timer_scroll_to_bottom.Start();
        }
        m_should_scroll = true;
    }
    void DebugConsoleWindowPage::ClearLogs(void) {
        LogsList().Items().Clear();
    }
}
