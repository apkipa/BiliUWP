#pragma once
#include "util.hpp"

namespace BiliUWP {
    struct DebugConsoleImpl;
    struct DebugConsole {
        DebugConsole(std::nullptr_t) noexcept : m_pimpl(nullptr) {}
        DebugConsole(std::shared_ptr<DebugConsoleImpl> pimpl = nullptr) noexcept : m_pimpl(pimpl) {}
        DebugConsole(DebugConsole&& other) noexcept : DebugConsole() { swap(*this, other); }
        DebugConsole(DebugConsole const& other) noexcept : m_pimpl(other.m_pimpl) {}
        DebugConsole& operator=(DebugConsole other) noexcept { swap(*this, other); return *this; }

        static util::winrt::task<DebugConsole> CreateAsync(void);

        void ClearLogs(void);
        void AppendLog(
            std::chrono::system_clock::time_point time,
            util::debug::LogLevel level,
            std::source_location src_loc,
            winrt::hstring content
        );
        bool IsAlive(void);
        operator bool(void) { return IsAlive(); }

        friend void swap(DebugConsole& a, DebugConsole& b) noexcept {
            a.m_pimpl.swap(b.m_pimpl);
        }

    private:
        std::shared_ptr<DebugConsoleImpl> m_pimpl;
    };
}
