#pragma once

#include "DebugConsoleWindowPage.g.h"
#include "DebugConsoleWindowPage_LogViewItem.g.h"
#include "util.hpp"

namespace winrt::BiliUWP {
    DebugConsoleWindowPage_LogViewItem DebugConsoleWindowPage_MakeLogViewItem(
        std::chrono::system_clock::time_point time,
        util::debug::LogLevel level,
        std::source_location src_loc,
        winrt::hstring content
    );
}

namespace winrt::BiliUWP::implementation {
    struct DebugConsoleWindowPage_LogViewItem : DebugConsoleWindowPage_LogViewItemT<DebugConsoleWindowPage_LogViewItem> {
        DebugConsoleWindowPage_LogViewItem(
            std::chrono::system_clock::time_point time,
            util::debug::LogLevel level,
            std::source_location src_loc,
            winrt::hstring content
        ) : m_time(std::move(time)), m_level(level), m_src_loc(std::move(src_loc)), m_content(std::move(content)) {}

        wchar_t LevelChar();

        Windows::UI::Xaml::Media::Brush ColorBrush();
        hstring Time();
        hstring SourceLocation();
        hstring Content() { return m_content; }
        hstring ContentShortened() {
            // Clip the text to workaround performance degradation
            return hstring(std::wstring_view(m_content).substr(0, 280));
        }

    private:
        std::chrono::system_clock::time_point m_time;
        util::debug::LogLevel m_level;
        std::source_location m_src_loc;
        hstring m_content;
    };
    struct DebugConsoleWindowPage : DebugConsoleWindowPageT<DebugConsoleWindowPage> {
        DebugConsoleWindowPage();

        void LogsList_SelectionChanged(
            Windows::Foundation::IInspectable const& sender,
            Windows::UI::Xaml::Controls::SelectionChangedEventArgs const&
        );

        void AppendLog(winrt::BiliUWP::DebugConsoleWindowPage_LogViewItem log_item);
        void ClearLogs(void);

    private:
        Windows::UI::Xaml::DispatcherTimer m_timer_scroll_to_bottom;
        bool m_should_scroll = false;
    };
}

namespace winrt::BiliUWP::factory_implementation {
    struct DebugConsoleWindowPage : DebugConsoleWindowPageT<DebugConsoleWindowPage, implementation::DebugConsoleWindowPage> {};
}
