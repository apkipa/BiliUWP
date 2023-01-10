#include "pch.h"
#include "util.hpp"
#include "App.h"
#include "MainPage.h"
#include "SimpleContentDialog.h"
#include <shared_mutex>
#include <queue>
#include <regex>

using namespace winrt;
using namespace Windows::ApplicationModel;
using namespace Windows::ApplicationModel::Activation;
using namespace Windows::Foundation;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Navigation;
//using namespace BiliUWP;
//using namespace BiliUWP::implementation;

namespace BiliUWP::App {
    AppInst* g_app_inst = nullptr;
}

namespace BiliUWP {
    AppInst::AppInst() :
        m_app_tabs(), m_tv(), m_glob_frame(nullptr), m_cur_log_level(util::debug::LogLevel::Info),
        m_app_logs(), m_logging_provider(new AppLoggingProvider(this)), m_cur_log_file(nullptr),
        m_logging_thread(), m_logging_tx(), m_logging_file_buf(nullptr),
        m_res_ldr(Windows::ApplicationModel::Resources::ResourceLoader::GetForViewIndependentUse()),
        m_cfg_model(winrt::BiliUWP::AppCfgModel()),
        m_cfg_app_use_tab_view(m_cfg_model.App_UseTabView()),
        m_cfg_app_show_tab_thumbnails(m_cfg_model.App_ShowTabThumbnails()),
        m_cfg_app_store_logs(m_cfg_model.App_StoreLogs()),
        m_bili_client(new BiliClient())
    {
        using namespace Windows::UI::Xaml::Data;
        // TODO: Pre-init
        // TODO: Check if mode is tab or single-view
        // TODO: Initialize absent setting items with default value
        util::debug::set_log_provider(m_logging_provider);

        auto update_log_level_fn = [this] {
            m_cur_log_level = static_cast<util::debug::LogLevel>(m_cfg_model.App_LogLevel());
            if (m_cur_log_level > util::debug::LogLevel::Error) {
                m_cur_log_level = util::debug::LogLevel::Error;
            }
        };
        update_log_level_fn();
        auto update_bili_client_fn = [this] {
            m_bili_client->set_api_sign_keys({ m_cfg_model.User_ApiKey(), m_cfg_model.User_ApiKeySec() });
            m_bili_client->set_access_token(m_cfg_model.User_AccessToken());
            m_bili_client->set_refresh_token(m_cfg_model.User_RefreshToken());
            winrt::BiliUWP::UserCookies user_cookies;
            user_cookies.SESSDATA = m_cfg_model.User_Cookies_SESSDATA();
            user_cookies.bili_jct = m_cfg_model.User_Cookies_bili_jct();
            user_cookies.DedeUserID = m_cfg_model.User_Cookies_DedeUserID();
            user_cookies.DedeUserID__ckMd5 = m_cfg_model.User_Cookies_DedeUserID__ckMd5();
            user_cookies.sid = m_cfg_model.User_Cookies_sid();
            m_bili_client->set_cookies(user_cookies);
        };
        // Also set handlers to log originated `hresult_error`s
        winrt_throw_hresult_handler = [](
            uint32_t line, const char* file, const char* func, void* ret_addr, hresult herr) noexcept
        {
            util::debug::log_warn(to_hstring(std::format(
                "Originated hresult 0x{:08x} in {}:{}:{} (addr={})",
                static_cast<uint32_t>(herr), file, func, line, ret_addr
            )));
        };
        m_cfg_model.PropertyChanged([=](IInspectable const&, PropertyChangedEventArgs const& e) {
            auto prop_name = e.PropertyName();
            if (prop_name == L"User_ApiKey" || prop_name == L"User_ApiKeySec") {
                m_bili_client->set_api_sign_keys({ m_cfg_model.User_ApiKey(), m_cfg_model.User_ApiKeySec() });
            }
            else if (prop_name == L"User_AccessToken") {
                m_bili_client->set_access_token(m_cfg_model.User_AccessToken());
            }
            else if (prop_name == L"User_RefreshToken") {
                m_bili_client->set_refresh_token(m_cfg_model.User_RefreshToken());
            }
            else if (prop_name == L"User_Cookies_SESSDATA" ||
                prop_name == L"User_Cookies_bili_jct" ||
                prop_name == L"User_Cookies_DedeUserID" ||
                prop_name == L"User_Cookies_DedeUserID__ckMd5" ||
                prop_name == L"User_Cookies_sid")
            {
                winrt::BiliUWP::UserCookies user_cookies;
                user_cookies.SESSDATA = m_cfg_model.User_Cookies_SESSDATA();
                user_cookies.bili_jct = m_cfg_model.User_Cookies_bili_jct();
                user_cookies.DedeUserID = m_cfg_model.User_Cookies_DedeUserID();
                user_cookies.DedeUserID__ckMd5 = m_cfg_model.User_Cookies_DedeUserID__ckMd5();
                user_cookies.sid = m_cfg_model.User_Cookies_sid();
                m_bili_client->set_cookies(user_cookies);
            }
            else if (prop_name == L"App_LogLevel") {
                update_log_level_fn();
            }
        });
        update_bili_client_fn();
    }
    AppInst::~AppInst() {
        delete m_bili_client;
        // Notify the logging thread to stop running, if any
        m_logging_tx = {};
        util::debug::set_log_provider(nullptr);
        delete m_logging_provider;
    }
    AppTab AppInst::tab_from_page(Windows::UI::Xaml::Controls::Page const& tab_page) {
        // TODO: mutex lock or helper member function
        if (!m_cfg_app_use_tab_view) {
            throw hresult_not_implemented();
        }
        return implementation::AppTab::get_from_page(tab_page);
    }
    void AppInst::add_tab(AppTab new_tab, AppTab insert_after) {
        // TODO: mutex lock or helper member function
        if (!m_cfg_app_use_tab_view) {
            throw hresult_not_implemented();
        }
        if (!new_tab) {
            return;
        }
        auto tvi = new_tab->get_tab_view_item();
        hack_tab_view_item(tvi);
        if (auto tv = this->parent_of_tab(insert_after)) {
            // Tab is in a window
            uint32_t idx;
            if (tv.TabItems().IndexOf(insert_after->get_tab_view_item(), idx)) {
                tv.TabItems().InsertAt(idx + 1, tvi);
            }
            else {
                tv.TabItems().Append(tvi);
            }
        }
        else {
            // Tab is not in a window or is null; insert into default window
            // TODO: Maybe track view active state and insert into an active one?
            // TODO: Or store relationship of TabView and ApplicationView and use GetCurrentView
            m_tv[0].TabItems().Append(tvi);
        }
        m_app_tabs.push_back(new_tab);
        new_tab->associate_app_inst(this);
    }
    void AppInst::add_tab(AppTab new_tab, Microsoft::UI::Xaml::Controls::TabView const& tab_view) {
        // TODO: mutex lock or helper member function
        if (!m_cfg_app_use_tab_view) {
            throw hresult_not_implemented();
        }
        if (!new_tab) { return; }
        if (!tab_view) { return; }
        auto tvi = new_tab->get_tab_view_item();
        hack_tab_view_item(tvi);
        tab_view.TabItems().Append(tvi);
        m_app_tabs.push_back(new_tab);
        new_tab->associate_app_inst(this);
    }
    void AppInst::remove_tab(AppTab tab) {
        using namespace Windows::UI::ViewManagement;

        // TODO: mutex lock or helper member function
        if (!m_cfg_app_use_tab_view) {
            throw hresult_not_implemented();
        }
        // TODO: Add tab closing callback
        if (!tab) {
            return;
        }
        tab->associate_app_inst(nullptr);
        auto tv = this->parent_of_tab(tab);
        uint32_t idx;
        if (tv) {
            // MSVC reports false positive C4701 for idx, so we split if checks
            if (tv.TabItems().IndexOf(tab->get_tab_view_item(), idx)) {
                if (tv.TabItems().Size() == 1) {
                    // Close view instead of removing tab from TabView
                    // TODO: Don't close tab, so OnSuspending can save tab state (? should just save here?)
                    // TODO: Use a kv map to perform accurate reverse lookup (TabView -> ApplicationView)
                    ApplicationView::GetForCurrentView().TryConsolidateAsync();
                    // ???
                    if (m_tv.size() > 1) {
                        // Remove TabView from m_tv
                        m_tv.erase(std::find(m_tv.begin(), m_tv.end(), tv));
                    }
                    return;
                }
                tv.TabItems().RemoveAt(idx);
                // Hack: Update tab items layout
                tv.TabWidthMode(tv.TabWidthMode());
            }
        }
        if (auto it = std::find(m_app_tabs.begin(), m_app_tabs.end(), tab); it != m_app_tabs.end()) {
            m_app_tabs.erase(it);
        }
        // TODO: Close entire window (view) if no more tabs exist
        // TODO: Save current tab if this is the only window
        /*if (tv && tv.TabItems().Size() == 0) {
            ApplicationView::GetForCurrentView().TryConsolidateAsync();
        }*/
    }
    bool AppInst::activate_tab(AppTab tab) {
        // TODO: mutex lock or helper member function
        // TODO: Perform perfect scrolling when tab is activated
        if (!m_cfg_app_use_tab_view) {
            throw hresult_not_implemented();
        }
        if (!tab) {
            return false;
        }
        auto tab_item = tab->get_tab_view_item();
        util::winrt::run_when_loaded([=](Microsoft::UI::Xaml::Controls::TabViewItem const& tvi) {
            if (auto tv = this->parent_of_tab(tab)) {
                tv.SelectedItem(tvi);
            }
        }, tab_item);
        return true;
    }
    hstring AppInst::res_str_raw(hstring const& res_id) {
        return m_res_ldr.GetString(res_id);
    }
    // NOTE: Code should rely on credential-related events instead of the result of this action
    Windows::Foundation::IAsyncAction AppInst::request_login(void) {
        using LoginTaskType = concurrency::task<winrt::BiliUWP::LoginPageResult>;
        // TODO: Move static variables to AppInst
        static struct {
            AppTab login_tab;
            LoginTaskType login_op;
            std::function<void()> cancel_login_op_fn;
            std::shared_mutex mutex_lock;
        } s_state;

        auto cancellation_token = co_await winrt::get_cancellation_token();
        cancellation_token.callback([] {
            std::shared_lock lg(s_state.mutex_lock);
            if (s_state.cancel_login_op_fn) {
                s_state.cancel_login_op_fn();
            }
        });

        // NOTE: LoginPage is in charge of both updating credentials and broadcasting events
        if (std::unique_lock lg(s_state.mutex_lock); s_state.login_op == LoginTaskType{}) {
            // No pending login tasks, start a new one
            s_state.login_tab = ::BiliUWP::make<AppTab>();
            s_state.login_tab->navigate(winrt::xaml_typename<winrt::BiliUWP::LoginPage>());
            this->add_tab(s_state.login_tab);
            s_state.login_tab->activate();
            s_state.login_op = [&]() -> concurrency::task<winrt::BiliUWP::LoginPageResult> {
                // SAFETY: Coroutine is eagerly driven and captured variables
                //         are not used after any suspension points
                auto login_op = s_state.login_tab->get_content().as<winrt::BiliUWP::LoginPage>().RequestLogin();
                s_state.cancel_login_op_fn = [=] {
                    login_op.Cancel();
                };
                co_return co_await login_op;
            }();
        }
        deferred([&] {
            if (std::unique_lock lg(s_state.mutex_lock); s_state.login_op != LoginTaskType{}) {
                // Clean up existing states
                this->remove_tab(s_state.login_tab);
                s_state.login_tab = {};
                s_state.login_op = {};
                s_state.cancel_login_op_fn = {};
            }
        });
        if (std::shared_lock lg(s_state.mutex_lock); s_state.login_op != LoginTaskType{}) {
            // Just wait for the existing task to complete, discarding the results
            try {
                co_await s_state.login_op;
            }
            catch (...) {}
        }
    }
    bool AppInst::is_logged_in(void) {
        return m_cfg_model.User_AccessToken() != L"";
    }
    winrt::Windows::Foundation::IAsyncAction AppInst::request_login_blocking(AppTab tab) {
        auto cancellation_token = co_await winrt::get_cancellation_token();
        cancellation_token.enable_propagation();

        auto login_dlg = winrt::BiliUWP::SimpleContentDialog();
        login_dlg.Title(box_value(App::res_str(L"App/Dialog/FinishLogin/Title")));
        login_dlg.Content(box_value(App::res_str(L"App/Dialog/FinishLogin/Content")));
        login_dlg.CloseButtonText(App::res_str(L"App/Common/Cancel"));

        auto login_op = this->request_login();
        auto show_dlg_op = [&]() -> IAsyncAction { co_await tab->show_dialog(login_dlg); }();

        deferred([&] {
            login_op.Cancel();
            login_dlg.Hide();
        });
        // We don't release tab before awaiting on purpose (login page should be preserved
        // even when tab is closed)
        co_await winrt::when_any(login_op, show_dlg_op);
    }
    Windows::Foundation::IAsyncAction AppInst::request_logout(void) {
        co_await m_bili_client->revoke_login();
        m_cfg_model.User_ApiKey(L"");
        m_cfg_model.User_ApiKeySec(L"");
        m_cfg_model.User_AccessToken(L"");
        m_cfg_model.User_RefreshToken(L"");
        m_cfg_model.User_Cookies_SESSDATA(L"");
        m_cfg_model.User_Cookies_bili_jct(L"");
        m_cfg_model.User_Cookies_DedeUserID(L"");
        m_cfg_model.User_Cookies_DedeUserID__ckMd5(L"");
        m_cfg_model.User_Cookies_sid(L"");
        this->signal_login_state_changed();
    }
    Microsoft::UI::Xaml::Controls::TabView AppInst::parent_of_tab(AppTab tab) {
        // TODO: mutex lock or helper member function
        if (!tab) {
            return nullptr;
        }
        if (m_cfg_app_use_tab_view) {
            auto tab_item = tab->get_tab_view_item();
            for (auto& i : m_tv) {
                uint32_t idx;
                if (i.TabItems().IndexOf(tab_item, idx)) {
                    return i;
                }
            }
            return nullptr;
        }
        else {
            return nullptr;
        }
    }
    void AppInst::hack_tab_view_item(Microsoft::UI::Xaml::Controls::TabViewItem const& tvi) {
        using Microsoft::UI::Xaml::Controls::TabViewItem;
        util::winrt::run_when_loaded([](Microsoft::UI::Xaml::Controls::TabViewItem const& tvi) {
            auto elem = util::winrt::get_child_elem(tvi, L"LayoutRoot");
            elem = util::winrt::get_child_elem(elem, L"TabContainer");
            ToolTipService::SetToolTip(
                util::winrt::get_child_elem(elem, L"CloseButton"),
                box_value(App::res_str(L"App/Override/SR_TabViewCloseButtonTooltipWithKA"))
            );
        }, tvi);
    }
    void AppInst::append_log_entry(LogDesc log_desc) {
        static auto str_from_logdesc_fn = [](LogDesc const& ld) {
            wchar_t level_char;
            switch (ld.level) {
            case util::debug::LogLevel::Trace:  level_char = 'T';   break;
            case util::debug::LogLevel::Debug:  level_char = 'D';   break;
            case util::debug::LogLevel::Info:   level_char = 'I';   break;
            case util::debug::LogLevel::Warn:   level_char = 'W';   break;
            case util::debug::LogLevel::Error:  level_char = 'E';   break;
            default:                            level_char = '?';   break;
            }
            return hstring(std::format(
                L"[{}] {} [{}] {}",
                std::chrono::zoned_time{ std::chrono::current_zone(), ld.time },
                level_char,
                to_hstring(std::format("{}:{}:{}",
                    ld.src_loc.file_name(), ld.src_loc.function_name(), ld.src_loc.line())),
                ld.content
            ));
        };
        if (m_cfg_model.App_RedactLogs()) {
            // TODO: Maybe optimize redaction regexes
            std::wstring result{ log_desc.content };
            // Url query params redaction
            static std::wregex url_query_re(
                L"((access|refresh)_(key|token)=)\\w+",
                std::regex::optimize
            );
            result = std::regex_replace(result, url_query_re, L"$1<REDACTED>");
            // TODO: Redact token_info
            // TODO: Redact cookie_info
            log_desc.content = result;
        }
        if (m_cfg_app_store_logs) {
            // TODO: This function might have a slim chance of data race, maybe fix it
            auto send_log_fn = [this](hstring log_str) {
                return m_logging_thread.joinable() && m_logging_tx.send(log_str);
            };
            [&](hstring log_str) {
                // Optimistic check to reduce mutex overhead
                if (send_log_fn(log_str)) { return; }
                // Logging thread has long gone or not spawned; spawn a new one
                // TODO: Get receiver back to recover lost logs if thread died abruptly
                // Contend with thread creation
                static std::mutex s_spawn_lock;
                std::scoped_lock guard(s_spawn_lock);
                // Make sure we need to create the thread
                if (send_log_fn(log_str)) { return; }
                // Spawn the logging thread
                auto [tx, rx] = util::sync::mpsc_channel_bounded<hstring>(16);
                m_logging_tx = std::move(tx);
                m_logging_thread = std::jthread(
                    [this](util::sync::mpsc_channel_receiver<hstring> rx) {
                        using namespace Windows::Storage;
                        while (auto result = rx.recv()) {
                            // Open the log file if it hasn't been opened
                            if (!m_cur_log_file) {
                                auto local_folder = ApplicationData::Current().LocalFolder();
                                auto logs_folder = local_folder.CreateFolderAsync(
                                    L"logs", CreationCollisionOption::OpenIfExists
                                ).get();
                                try {
                                    // Rename old log if it exists
                                    auto old_log = logs_folder.GetFileAsync(L"latest.log").get();
                                    auto props = old_log.GetBasicPropertiesAsync().get();
                                    old_log.RenameAsync(std::format(
                                        L"{:%F}.log",
                                        std::chrono::zoned_time{
                                            std::chrono::current_zone(),
                                            winrt::clock::to_sys(props.DateModified())
                                        }
                                    ), NameCollisionOption::GenerateUniqueName).get();
                                }
                                catch (hresult_error const&) {}
                                m_cur_log_file = logs_folder.CreateFileAsync(
                                    L"latest.log", CreationCollisionOption::ReplaceExisting
                                ).get();
                            }
                            // Write log to file
                            try {
                                FileIO::AppendLinesAsync(m_cur_log_file, { std::move(*result) }).get();
                            }
                            catch (...) {
                                // We cannot go further; clean up and halt this thread
                                m_cur_log_file = nullptr;
                                util::winrt::log_current_exception();
                                return;
                            }
                        }
                    }
                , std::move(rx));
                // Send the log string again
                m_logging_tx.send(std::move(log_str));
            }(str_from_logdesc_fn(log_desc));
        }
        if (m_dbg_con) {
            m_dbg_con.AppendLog(log_desc.time, log_desc.level, log_desc.src_loc, log_desc.content);
        }
        std::unique_lock log_guard(m_mutex_app_logs);
        m_app_logs.push_back(std::move(log_desc));
    }
    void AppInst::init_current_window(void) {
        using namespace Windows::UI;
        using namespace Windows::UI::Core;
        using namespace Windows::UI::ViewManagement;
        using namespace Windows::UI::Xaml::Media;
        using namespace Windows::UI::Xaml::Input;
        using namespace Windows::ApplicationModel::Core;

        using util::winrt::blend_colors_2;
        using util::winrt::color_from_argb;

        auto window = Window::Current();
        auto av_title_bar = ApplicationView::GetForCurrentView().TitleBar();
        auto cav_title_bar = CoreApplication::GetCurrentView().TitleBar();

        Color bg_normal_clr, bg_hover_clr, bg_pressed_clr;
        if (m_cfg_app_use_tab_view) {
            bg_normal_clr = Colors::Transparent();
            bg_hover_clr = Windows::UI::ColorHelper::FromArgb(0x19, 0, 0, 0);
            bg_pressed_clr = Windows::UI::ColorHelper::FromArgb(0x33, 0, 0, 0);
        }
        else {
            bg_normal_clr = Colors::SkyBlue();
            bg_hover_clr = blend_colors_2(color_from_argb(0x19, 0, 0, 0), bg_normal_clr);
            bg_pressed_clr = blend_colors_2(color_from_argb(0x33, 0, 0, 0), bg_normal_clr);
        }
        av_title_bar.ButtonBackgroundColor(bg_normal_clr);
        av_title_bar.ButtonInactiveBackgroundColor(bg_normal_clr);
        av_title_bar.ButtonHoverBackgroundColor(bg_hover_clr);
        av_title_bar.ButtonPressedBackgroundColor(bg_pressed_clr);
        // TODO: Change with system theme
        bg_normal_clr = Colors::SkyBlue();
        av_title_bar.InactiveBackgroundColor(bg_normal_clr);
        av_title_bar.BackgroundColor(bg_normal_clr);

        if (m_cfg_app_use_tab_view) {
            using namespace Microsoft::UI::Xaml::Controls;

            cav_title_bar.ExtendViewIntoTitleBar(true);

            auto tab_view = TabView();
            tab_view.KeyboardAcceleratorPlacementMode(KeyboardAcceleratorPlacementMode::Hidden);
            tab_view.VerticalAlignment(VerticalAlignment::Stretch);
            auto header_textblock = TextBlock();
            header_textblock.Text(L"BiliUWP");
            header_textblock.VerticalAlignment(VerticalAlignment::Center);
            header_textblock.TextLineBounds(TextLineBounds::Tight);
            header_textblock.IsHitTestVisible(false);
            header_textblock.FontSize(13);  // System title size
            header_textblock.Margin(ThicknessHelper::FromLengths(12, 0, 6, 0));
            tab_view.TabStripHeader(header_textblock);
            tab_view.Background(nullptr);
            auto footer_grid = Grid();
            tab_view.TabStripFooter(footer_grid);
            tab_view.TabCloseRequested(
                [this](TabView const&, TabViewTabCloseRequestedEventArgs const& e) {
                    // TODO: Improve tab close logic
                    for (auto& i : m_app_tabs) {
                        if (e.Tab() == i->get_tab_view_item()) {
                            i->signal_closed_by_user();
                            this->remove_tab(i);
                            return;
                        }
                    }
                }
            );
            tab_view.AddTabButtonClick([this](TabView const& sender, IInspectable const&) {
                auto new_tab = ::BiliUWP::make<AppTab>();
                new_tab->navigate(xaml_typename<winrt::BiliUWP::NewPage>());
                this->add_tab(new_tab, sender);
                new_tab->activate();
            });

            {   // Add keyboard accelerators for TabView
                using namespace Windows::UI::Xaml::Input;
                using namespace Windows::System;

                auto key_acc_ctrl_t = KeyboardAccelerator();
                key_acc_ctrl_t.Key(VirtualKey::T);
                key_acc_ctrl_t.Modifiers(VirtualKeyModifiers::Control);
                key_acc_ctrl_t.Invoked(
                    [this, weak_tv = winrt::make_weak(tab_view)]
                    (KeyboardAccelerator const&, KeyboardAcceleratorInvokedEventArgs const& e) {
                        e.Handled(true);
                        auto new_tab = ::BiliUWP::make<AppTab>();
                        new_tab->navigate(xaml_typename<winrt::BiliUWP::NewPage>());
                        this->add_tab(new_tab, weak_tv.get());
                        new_tab->activate();
                    }
                );
                tab_view.KeyboardAccelerators().Append(key_acc_ctrl_t);
#if false
                // WARN: This method does NOT respect tab request close, and should be avoided
                auto key_acc_ctrl_w = KeyboardAccelerator();
                key_acc_ctrl_w.Key(VirtualKey::W);
                key_acc_ctrl_w.Modifiers(VirtualKeyModifiers::Control);
                key_acc_ctrl_w.Invoked(
                    [root_tabview](KeyboardAccelerator const&, KeyboardAcceleratorInvokedEventArgs const& e) {
                        e.Handled(true);
                        auto tab_items = root_tabview.TabItems();
                        auto idx = root_tabview.SelectedIndex();
                        if (tab_items.GetAt(idx).as<TabViewItem>().IsClosable()) {
                            tab_items.RemoveAt(idx);
                            // Hack: Update tab items layout
                            root_tabview.TabWidthMode(root_tabview.TabWidthMode());
                        }
                    }
                );
                root_tabview.KeyboardAccelerators().Append(key_acc_ctrl_w);
#else
                {   // Hack keyboard accelerators that comes with TabView
                    // NOTE: Close button text hacking is handled somewhere else
                    auto ka = tab_view.KeyboardAccelerators();
                    auto ka_cnt = ka.Size();
                    for (decltype(ka_cnt) i = 0; i < ka_cnt; i++) {
                        auto ka_item = ka.GetAt(i);
                        if (ka_item.Key() == VirtualKey::F4) {
                            // Transform Ctrl+F4 into Ctrl+W
                            ka_item.Key(VirtualKey::W);
                            ka_item.ScopeOwner(nullptr);
                        }
                        else if (ka_item.Key() == VirtualKey::Tab) {
                            ka_item.ScopeOwner(nullptr);
                        }
                    }
                }
#endif
            }

            auto update_titlebar_fn = [=](bool is_active) {
                header_textblock.Opacity(is_active ? 1 : 0.4);
            };
            //update_titlebar_appearance_fn(true);
            window.Activated(
                [update_fn = std::move(update_titlebar_fn)]
                (IInspectable const&, WindowActivatedEventArgs const& e) {
                    update_fn(e.WindowActivationState() != CoreWindowActivationState::Deactivated);
                }
            );
            // UNLIKELY TODO: SystemOverlayLeftInset (header_textblock can act as padding)
            cav_title_bar.LayoutMetricsChanged(
                [=](CoreApplicationViewTitleBar const& sender, IInspectable const&) {
                    footer_grid.Width(sender.SystemOverlayRightInset());
                }
            );

            auto root_grid = Grid();
            auto background_grid = Grid();
            background_grid.Background(SolidColorBrush(bg_normal_clr));
            // TODO: Dynamically detect tab container height in the future
            background_grid.Padding(ThicknessHelper::FromLengths(0, 40, 0, 0));
            root_grid.Children().Append(background_grid);
            root_grid.Children().Append(tab_view);

            if (m_cfg_app_show_tab_thumbnails) {
                // Generate thumbnails for tabs
                auto capture_grid = Grid();
                capture_grid.Background(SolidColorBrush(Colors::Transparent()));
                background_grid.Children().Append(capture_grid);
                tab_view.SelectionChanged(
                    [=](IInspectable const&, SelectionChangedEventArgs const& e) -> fire_forget_except {
                        co_safe_capture(capture_grid);
                        auto removed_items = e.RemovedItems();
                        auto added_items = e.AddedItems();
                        if (removed_items.Size() > 0) {
                            auto old_tvi = removed_items.GetAt(0).as<TabViewItem>();
                            StackPanel sp_old;
                            sp_old.Orientation(Orientation::Vertical);
                            auto cp_old = ContentPresenter();
                            cp_old.Padding(util::winrt::get_app_res<Thickness>(L"ToolTipBorderThemePadding"));
                            cp_old.Content(old_tvi.Header());
                            sp_old.Children().Append(cp_old);
                            Windows::UI::Xaml::Media::Imaging::RenderTargetBitmap rtb;
                            auto content_old = old_tvi.Content().as<FrameworkElement>();
                            old_tvi.Content(nullptr);
                            capture_grid.Children().Append(content_old);
                            co_await rtb.RenderAsync(capture_grid);
                            capture_grid.Children().Clear();
                            old_tvi.Content(content_old);
                            auto brd = Border();
                            auto brd_cr = util::winrt::get_app_res<CornerRadius>(L"ControlCornerRadius");
                            brd_cr.TopLeft = brd_cr.TopRight = 0;
                            brd.CornerRadius(brd_cr);
                            auto img = Image();
                            img.Source(rtb);
                            brd.Child(img);
                            sp_old.Children().Append(brd);
                            auto tooltip = ToolTip();
                            tooltip.Padding(ThicknessHelper::FromUniformLength(0));
                            tooltip.Content(sp_old);
                            ToolTipService::SetToolTip(old_tvi, tooltip);
                        }
                        if (added_items.Size() > 0) {
                            auto new_tvi = added_items.GetAt(0).as<TabViewItem>();
                            ToolTipService::SetToolTip(new_tvi, new_tvi.Header());
                        }
                    }
                );
            }

            // TODO: Track system theme with ActualThemeChanged and change title & background color
            window.SetTitleBar(background_grid);
            window.Content(root_grid);

            // TODO: mutex lock or helper member function
            m_tv.push_back(tab_view);

            // TODO: Clean up code
            tab_view.Loaded([=](IInspectable const& sender, RoutedEventArgs const&) {
                using util::winrt::get_child_elem;

                FrameworkElement tv_tvlw{ nullptr };
                Button tv_ab{ nullptr };

                bool succeeded = false;
                deferred([&] {
                    if (!succeeded) {
                        util::debug::log_error(L"App: Unable to get elements from TabView for hacking");
                    }
                });

                /*
                * Element tree:
                * [TabView] -> [Grid] -> TabContainerGrid [Grid] -> TabListView [TabViewListView]
                * -> [Border] -> ScrollViewer [ScrollViewer] -> Root [Border] -> [Grid]
                * -> ScrollContentPresenter [ScrollContentPresenter]
                * -> TabsItemsPresenter [ItemsPresenter] -> [ItemsStackPanel]
                */
                UIElement elem = get_child_elem(sender.as<UIElement>());
                elem = get_child_elem(elem, L"TabContainerGrid");
                // Hack ShadowReceiver
                if (m_cfg_model.App_SimplifyVisualsLevel() >= 0.5) {
                    // Remove shadow effect to reduce GPU usage
                    auto shadow_recv_grid = get_child_elem(elem, L"ShadowReceiver").as<Grid>();
                    shadow_recv_grid.Visibility(Visibility::Collapsed);
                }
                // Hack AddButton
                tv_ab = get_child_elem(elem, L"AddButton").as<Button>();
                // TODO: In WinUI Version2 controls, we cannot get tv_ab. Try making the code more robust.
                if (true && tv_ab) {
                    // Make add button unfocusable
                    tv_ab.IsTabStop(false);
                }
                // Hack TabViewListView (& ...)
                elem = get_child_elem(elem, L"TabListView");
                tv_tvlw = elem.as<FrameworkElement>();
                if (true) {
                    // Improve non-media fullscreen experience by making TabView compact
                    // TODO: Maybe detect maximize event and let user choose whether to compact TabView
                    //       under such circumstance
                    auto should_use_compact_fn = [] {
                        switch (util::winrt::get_cur_view_windowing_mode()) {
                        case util::winrt::AppViewWindowingMode::Maximized:
                        case util::winrt::AppViewWindowingMode::FullScreen:
                        case util::winrt::AppViewWindowingMode::FullScreenTabletMode:
                            return true;
                        default:
                            return false;
                        }
                    };
                    bool use_compact = should_use_compact_fn();
                    auto change_state_fn = [=](bool compact_frame) {
                        if (compact_frame) {
                            // Some negative space are useful for system, so a little space is reserved
                            tv_tvlw.Margin(ThicknessHelper::FromLengths(0, 2, 0, 0));
                            if (tv_ab) {
                                tv_ab.Margin(ThicknessHelper::FromLengths(0, 1, 0, 0));
                            }
                        }
                        else {
                            tv_tvlw.Margin(ThicknessHelper::FromLengths(0, 8, 0, 0));
                            if (tv_ab) {
                                tv_ab.Margin(ThicknessHelper::FromLengths(0, 7, 0, 0));
                            }
                        }
                    };
                    change_state_fn(use_compact);
                    window.SizeChanged(
                        [change_state_fn = std::move(change_state_fn), should_use_compact_fn, use_compact]
                        (IInspectable const&, WindowSizeChangedEventArgs const&) {
                            static bool last_use_compact = use_compact;
                            bool cur_use_compact = should_use_compact_fn();
                            if (last_use_compact == cur_use_compact) {
                                return;
                            }
                            change_state_fn(cur_use_compact);
                            last_use_compact = cur_use_compact;
                        }
                    );
                }
                // Hack Border
                if (false) {
                    UIElement cur_elem = get_child_elem(elem);
                    cur_elem = get_child_elem(cur_elem, L"ScrollViewer");
                    cur_elem = get_child_elem(cur_elem, L"Root");
                    auto tv_rb = cur_elem.as<Border>();
                    // NOTE: Next line causes tab dragging instability, so it is left out with
                    //       a bit of imperfect negative space
                    tv_rb.Background(nullptr);
                }

                succeeded = true;
            });
        }
        else {
            cav_title_bar.ExtendViewIntoTitleBar(false);

            // TODO: Create a global frame which navigates via ContainerPage
            m_glob_frame = Frame();

            window.Content(m_glob_frame);
        }
    }

    implementation::AppTab::AppTab(use_the_make_function) :
        m_app_inst(nullptr), m_tab_item(), m_root_grid(), m_page_frame(),
        m_show_dlg_op(nullptr)
    {
        m_tab_item.Content(m_root_grid);
        m_root_grid.Children().Append(m_page_frame);

        // SAFETY: Destructors don't throw
        s_frame_tab_map.emplace(m_page_frame, this);
    }
    void implementation::AppTab::post_init(use_the_make_function) {
        using namespace Windows::UI::Xaml::Controls::Primitives;
        using Microsoft::UI::Xaml::Controls::CommandBarFlyout;      // Buggy
        //using Windows::UI::Xaml::Controls::CommandBarFlyout;      // Not buggy

        auto str_back = App::res_str(L"App/Common/Back");
        auto str_forward = App::res_str(L"App/Common/Forward");
        auto cmd_bar_fo = CommandBarFlyout();
        auto btn_back = AppBarButton();
        btn_back.Icon(SymbolIcon(Symbol::Back));
        btn_back.Label(str_back);
        ToolTipService::SetToolTip(btn_back, box_value(str_back));
        cmd_bar_fo.PrimaryCommands().Append(btn_back);
        auto btn_forward = AppBarButton();
        btn_forward.Icon(SymbolIcon(Symbol::Forward));
        btn_forward.Label(str_forward);
        ToolTipService::SetToolTip(btn_forward, box_value(str_forward));
        cmd_bar_fo.PrimaryCommands().Append(btn_forward);
        auto update_menu_fn = [weak_cmd_bar_fo = weak_ref(cmd_bar_fo),
            weak_btn_back = weak_ref(btn_back), weak_btn_forward = weak_ref(btn_forward)
        ](::BiliUWP::AppTab that) {
            // TODO: Maybe we should support stuff like IIntraNavigation for
            //       things like `in-app WebView` Page instances
            // Primary commands
            weak_btn_back.get().IsEnabled(that->can_go_back());
            weak_btn_forward.get().IsEnabled(that->can_go_forward());
            // Secondary commands
            // TODO: Maybe optimize performance by avoiding recreation of secondary commands
            that->m_cur_async.cancel_and_run([](CommandBarFlyout cmd_bar_fo, AppTab* that) -> IAsyncAction {
                auto cancellation_token = co_await get_cancellation_token();
                cancellation_token.enable_propagation();

                auto secondary_cmds = cmd_bar_fo.SecondaryCommands();
                secondary_cmds.Clear();
                if (auto bili_res = that->m_page_frame.Content().try_as<winrt::BiliUWP::IBiliResource>()) {
                    // Page is resource; check for individual items
                    constexpr size_t max_retries = 600;
                    size_t cur_retries = 0;
                    if (!bili_res.IsBiliResReady()) {
                        using namespace std::chrono_literals;
                        auto btn_waiting_for_res = AppBarButton();
                        btn_waiting_for_res.Label(App::res_str(L"App/Menu/AppTab/WaitingForRes/Text"));
                        btn_waiting_for_res.IsEnabled(false);
                        secondary_cmds.Append(btn_waiting_for_res);
                        co_await 100ms;
                        while (!bili_res.IsBiliResReady()) {
                            co_await 100ms;
                            cur_retries++;
                            if (cur_retries >= max_retries) {
                                co_await cmd_bar_fo.Dispatcher();
                                secondary_cmds.Clear();
                                auto btn_wait_for_res_timeout = AppBarButton();
                                btn_wait_for_res_timeout.Label(
                                    App::res_str(L"App/Menu/AppTab/WaitForResTimeout/Text")
                                );
                                btn_wait_for_res_timeout.IsEnabled(false);
                                secondary_cmds.Append(btn_wait_for_res_timeout);
                                co_return;
                            }
                        }
                        co_await cmd_bar_fo.Dispatcher();
                        secondary_cmds.Clear();
                    }
                    auto add_copy_res_btn_fn = [&](hstring const& res_str, hstring const& res_id) {
                        if (res_str == L"") { return; }
                        auto btn_res = AppBarButton();
                        btn_res.Label(App::res_str(res_id));
                        btn_res.Click([=](IInspectable const&, RoutedEventArgs const&) {
                            util::winrt::set_clipboard_text(res_str, true);
                        });
                        secondary_cmds.Append(btn_res);
                    };
                    add_copy_res_btn_fn(bili_res.BiliResId(), L"App/Menu/AppTab/CopyResId/Text");
                    add_copy_res_btn_fn(bili_res.BiliResUrl(), L"App/Menu/AppTab/CopyResLink/Text");
                    add_copy_res_btn_fn(bili_res.BiliResId2(), L"App/Menu/AppTab/CopyAlternativeResId/Text");
                    add_copy_res_btn_fn(bili_res.BiliResUrl2(), L"App/Menu/AppTab/CopyAlternativeResLink/Text");
                    if (secondary_cmds.Size() == 0) {
                        auto btn_no_info_from_res = AppBarButton();
                        btn_no_info_from_res.Label(App::res_str(L"App/Menu/AppTab/NoInfoFromRes/Text"));
                        btn_no_info_from_res.IsEnabled(false);
                        secondary_cmds.Append(btn_no_info_from_res);
                    }
                }
                else {
                    // Page is not resource; do nothing
                }
            }, weak_cmd_bar_fo.get(), &*that);
        };
        btn_back.Click([=, weak_this = weak_from_this()](IInspectable const&, RoutedEventArgs const&) {
            auto strong_this = weak_this.lock();
            if (!strong_this) { return; }
            strong_this->go_back();
            update_menu_fn(std::move(strong_this));
        });
        btn_forward.Click([=, weak_this = weak_from_this()](IInspectable const&, RoutedEventArgs const&) {
            auto strong_this = weak_this.lock();
            if (!strong_this) { return; }
            strong_this->go_forward();
            update_menu_fn(std::move(strong_this));
        });
        cmd_bar_fo.Placement(FlyoutPlacementMode::RightEdgeAlignedTop);
        cmd_bar_fo.Opening([=, weak_this = weak_from_this()](IInspectable const&, IInspectable const&) {
            auto strong_this = weak_this.lock();
            if (!strong_this) { return; }
            update_menu_fn(std::move(strong_this));
        });
        cmd_bar_fo.Closing([weak_this = weak_from_this()](IInspectable const& sender, IInspectable const&) {
            auto strong_this = weak_this.lock();
            if (!strong_this) { return; }
            strong_this->m_cur_async.cancel_running();
        });
        // TODO: Maybe (?) workaround CommandBarFlyout's buggy shadow?
        //cmd_bar_fo.AlwaysExpanded(true);
        m_tab_item.ContextFlyout(cmd_bar_fo);
    }
    implementation::AppTab::~AppTab() {
        s_frame_tab_map.erase(m_page_frame);
        util::winrt::cancel_async(m_show_dlg_op);
    }
    Microsoft::UI::Xaml::Controls::IconSource implementation::AppTab::get_icon() {
        return m_tab_item.IconSource();
    }
    void implementation::AppTab::set_icon(Microsoft::UI::Xaml::Controls::IconSource const& ico_src) {
        m_tab_item.IconSource(ico_src);
    }
    void implementation::AppTab::set_icon(Windows::UI::Xaml::Controls::Symbol const& symbol) {
        auto ico_src = Microsoft::UI::Xaml::Controls::SymbolIconSource();
        ico_src.Symbol(symbol);
        m_tab_item.IconSource(ico_src);
    }
    hstring implementation::AppTab::get_title() {
        return m_tab_item.Header().try_as<hstring>().value_or(L"");
    }
    void implementation::AppTab::set_title(winrt::hstring const& title) {
        m_tab_item.Header(box_value(title));
    }
    bool implementation::AppTab::navigate(
        Windows::UI::Xaml::Interop::TypeName const& page_type,
        Windows::Foundation::IInspectable const& param
    ) {
        return m_page_frame.Navigate(page_type, param);
    }
    bool implementation::AppTab::can_go_back(void) {
        return m_page_frame.CanGoBack();
    }
    void implementation::AppTab::go_back(void) {
        m_page_frame.GoBack();
    }
    bool implementation::AppTab::can_go_forward(void) {
        return m_page_frame.CanGoForward();
    }
    void implementation::AppTab::go_forward(void) {
        m_page_frame.GoForward();
    }
    IAsyncOperation<winrt::BiliUWP::SimpleContentDialogResult> implementation::AppTab::show_dialog(
        winrt::BiliUWP::SimpleContentDialog dialog
    ) {
        auto cancellation_token = co_await winrt::get_cancellation_token();
        cancellation_token.enable_propagation();

        if (m_show_dlg_op) {
            throw hresult_error(E_FAIL, L"Cannot show more than one dialog in a single AppTab");
        }

        m_root_grid.Children().Append(dialog);
        m_show_dlg_op = dialog.ShowAsync();
        deferred([weak_this = weak_from_this()] {
            auto strong_this = weak_this.lock();
            if (!strong_this) { return; }
            strong_this->m_root_grid.Children().RemoveAtEnd();
            strong_this->m_show_dlg_op = nullptr;
        });
        auto result = co_await m_show_dlg_op;
        co_return result;
    }
}

/// <summary>
/// Initializes the singleton application object.  This is the first line of authored code
/// executed, and as such is the logical equivalent of main() or WinMain().
/// </summary>
winrt::BiliUWP::implementation::App::App() :
    m_app_inst(nullptr),
    m_is_fully_launched(false), m_need_restore_state(false)
{
    InitializeComponent();
    Suspending({ this, &App::OnSuspending });

    UnhandledException([this](IInspectable const&, UnhandledExceptionEventArgs const& e) {
        auto error_message = e.Message();
        util::debug::log_error(std::format(
            L"Uncaught exception in application: 0x{:08x}: {}",
            static_cast<uint32_t>(e.Exception()), error_message
        ));
        if (IsDebuggerPresent()) {
            __debugbreak();
        }
        e.Handled(true);
    });

    /*
#if defined _DEBUG && !defined DISABLE_XAML_GENERATED_BREAK_ON_UNHANDLED_EXCEPTION
    UnhandledException([this](IInspectable const&, UnhandledExceptionEventArgs const& e) {
        auto errorMessage = e.Message();
        if (IsDebuggerPresent()) {
            __debugbreak();
        }
    });
#endif
    */
}

winrt::BiliUWP::implementation::App::~App() {
    if (m_app_inst) {
        delete m_app_inst;
    }
}

/// <summary>
/// Invoked when the application is launched normally by the end user.  Other entry points
/// will be used such as when the application is launched to open a specific file.
/// </summary>
/// <param name="e">Details about the launch request and process.</param>
// TODO: Remove the old method
/*
void winrt::BiliUWP::implementation::App::OnLaunched(LaunchActivatedEventArgs const& e) {
    Frame rootFrame{ nullptr };
    auto content = Window::Current().Content();
    if (content) {
        rootFrame = content.try_as<Frame>();
    }

    // Do not repeat app initialization when the Window already has content,
    // just ensure that the window is active
    if (rootFrame == nullptr) {
        // Create a Frame to act as the navigation context and associate it with
        // a SuspensionManager key
        rootFrame = Frame();

        rootFrame.NavigationFailed({ this, &App::OnNavigationFailed });

        if (e.PreviousExecutionState() == ApplicationExecutionState::Terminated) {
            // Restore the saved session state only when appropriate, scheduling the
            // final launch steps after the restore is complete
        }

        if (e.PrelaunchActivated() == false) {
            if (rootFrame.Content() == nullptr) {
                // When the navigation stack isn't restored navigate to the first page,
                // configuring the new page by passing required information as a navigation
                // parameter
                rootFrame.Navigate(xaml_typename<BiliUWP::MainPage>(), box_value(e.Arguments()));
            }
            // Place the frame in the current Window
            Window::Current().Content(rootFrame);
            // Ensure the current window is active
            Window::Current().Activate();
        }
    }
    else {
        if (e.PrelaunchActivated() == false) {
            if (rootFrame.Content() == nullptr) {
                // When the navigation stack isn't restored navigate to the first page,
                // configuring the new page by passing required information as a navigation
                // parameter
                rootFrame.Navigate(xaml_typename<BiliUWP::MainPage>(), box_value(e.Arguments()));
            }
            // Ensure the current window is active
            Window::Current().Activate();
        }
    }
}*/
void winrt::BiliUWP::implementation::App::OnLaunched(LaunchActivatedEventArgs const& e) {
    auto cur_window = Window::Current();

    // Do not repeat app initialization when the Window already has content,
    // just ensure that the window is active
    if (!m_is_fully_launched) {
        // Pre-init first
        if (!m_app_inst) {
            ::BiliUWP::App::g_app_inst = m_app_inst = new ::BiliUWP::AppInst();
        }
        if (!cur_window.Content()) {
            m_app_inst->init_current_window();
        }

        if (e.PreviousExecutionState() == ApplicationExecutionState::Terminated) {
            // Restore the saved session state only when appropriate, scheduling the
            // final launch steps after the restore is complete
            m_need_restore_state = true;
        }
        /*  TODO: If user configured app to restore state, also set the flag
        if (user_requested_restore) {
            m_need_restore_state = true;
        }*/
        // TODO: Explicitly opt-in prelaunch if user allows by calling
        //       Windows.ApplicationModel.Core.CoreApplication.EnablePrelaunch(true);

        // Check if is going to full-init
        if (e.PrelaunchActivated() == false) {
            if (m_need_restore_state) {
                // TODO: Restore app state
            }
            else {
                // TODO: Use default action: create home page tab
                auto home_tab = ::BiliUWP::make<::BiliUWP::AppTab>();
                auto ico_src = Microsoft::UI::Xaml::Controls::SymbolIconSource();
                ico_src.Symbol(Symbol::Home);
                home_tab->set_icon(ico_src);
                home_tab->set_title(L"IMPLEMENT THIS");
                auto prog_ring = ProgressRing();
                prog_ring.IsActive(true);
                prog_ring.Width(60);
                prog_ring.Height(60);
                home_tab->navigate(xaml_typename<ContainerPage>(), prog_ring);
                m_app_inst->add_tab(home_tab);
                home_tab->activate();
            }

            cur_window.Activate();

            m_is_fully_launched = true;

            if (m_app_inst->cfg_model().App_ShowDebugConsole()) {
                m_app_inst->enable_debug_console(true, true);
            }
        }
    }
    else {
        if (e.PrelaunchActivated() == false) {
            cur_window.Activate();
        }
    }
}

/// <summary>
/// Invoked when application execution is being suspended.  Application state is saved
/// without knowing whether the application will be terminated or resumed with the contents
/// of memory still intact.
/// </summary>
/// <param name="sender">The source of the suspend request.</param>
/// <param name="e">Details about the suspend request.</param>
void winrt::BiliUWP::implementation::App::OnSuspending(
    IInspectable const& sender,
    SuspendingEventArgs const& e
) {
    // Save application state and stop any background activity
    // TODO: Notify all activities to save states
    (void)sender;
    (void)e;

    util::debug::log_trace(L"Suspending application");
}

/// <summary>
/// Invoked when Navigation to a certain page fails
/// </summary>
/// <param name="sender">The Frame which failed navigation</param>
/// <param name="e">Details about the navigation failure</param>
void winrt::BiliUWP::implementation::App::OnNavigationFailed(
    IInspectable const&,
    NavigationFailedEventArgs const& e
) {
    // TODO: Remove this function?
    throw hresult_error(E_FAIL, hstring(L"Failed to load Page ") + e.SourcePageType().Name);
}
