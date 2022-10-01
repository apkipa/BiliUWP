#pragma once
#include "App.xaml.g.h"
#include "Converters.h"
#include "util.hpp"
#include "AppCfgModel.h"
#include "BiliClient.hpp"
#include "DebugConsole.hpp"

namespace BiliUWP {
    struct AppInst;
}

namespace winrt::BiliUWP::implementation {
    struct App : AppT<App> {
        App();
        ~App();

        void OnLaunched(Windows::ApplicationModel::Activation::LaunchActivatedEventArgs const&);
        void OnSuspending(IInspectable const&, Windows::ApplicationModel::SuspendingEventArgs const&);
        void OnNavigationFailed(
            IInspectable const&,
            Windows::UI::Xaml::Navigation::NavigationFailedEventArgs const&
        );

        // TODO: Tab manager, ...
        // TODO: Add prelaunch support by calling
        //       Windows::ApplicationModel::Core::CoreApplication::EnablePrelaunch(true)
        // TODO: Prelaunch: Init all resources, but don't load any pages.
        //                  Defer page loading to user activation.
        // TODO: Pages should be able to serialize state for restoration when being
        //       relaunched, if the user requests to reopen pages.
        // TODO: Support multiple tab views / standalone views / single view
        // TODO: Add ExFrame which supports special popups(such as login prompts) (?)
        // TODO: Add support for Windows::UI::Core::SystemNavigationManager::BackRequested
    private:
        ::BiliUWP::AppInst* m_app_inst;
        bool m_is_fully_launched;
        bool m_need_restore_state;
    };
}

namespace BiliUWP {
    struct AppLoggingProvider;

    // Source: https://devblogs.microsoft.com/oldnewthing/20220721-00/?p=106879
    template<typename T>
    struct require_make_shared : std::enable_shared_from_this<T> {
    protected:
        struct use_the_make_function {
            explicit use_the_make_function() = default;
        };
        template<typename T>
        class has_initializer {
            template<typename U, typename = decltype(std::declval<U>().post_init(use_the_make_function{}))>
            static constexpr bool get_value(int) { return true; }
            template<typename>
            static constexpr bool get_value(...) { return false; }
        public:
            static constexpr bool value = get_value<T>(0);
        };
        template<typename... Args>
        static auto create(Args&&... args) {
            static_assert(std::is_convertible_v<T*, require_make_shared*>,
                "Must derive publicly from require_make_shared");
            auto ptr = std::make_shared<T>(use_the_make_function{}, std::forward<Args>(args)...);
            if constexpr (has_initializer<T>::value) {
                // We introduce post_init() to allow the retrieval of self
                // weak reference during construction
                ptr->post_init(use_the_make_function{});
            }
            return ptr;
        }
    public:
        require_make_shared() = default;
        // Deny copy construction
        require_make_shared(require_make_shared const&) = delete;

        friend class make_helper;
    };

    class make_helper {
    public:
        template<typename T, typename... Types>
        static auto make(Types&&... args) {
            return T::create(std::forward<Types>(args)...);
        }
    };

    // Delegate constructor
    template<typename T, typename U = typename T::element_type, typename... Types>
    T make(Types&&... args) {
        return make_helper::make<U>(std::forward<Types>(args)...);
    }

    namespace implementation {
        // Refers to a app tab (may be virtual)
        struct AppTab;
    }

    using AppTab = std::shared_ptr<implementation::AppTab>;

    struct AppInst {
        // TODO: Init all things except for home page, ... (?)
        AppInst();
        ~AppInst();
        AppInst(AppInst const&) = delete;
        AppInst& operator=(AppInst const&) = delete;

        // Tab management
        //   NOTE: A shim for AppTab::get_from_page
        AppTab tab_from_page(winrt::Windows::UI::Xaml::Controls::Page const& tab_page);
        //   Adds a new tab next to the specified one (or last one if passed nullptr)
        void add_tab(AppTab new_tab, AppTab insert_after = nullptr);
        //   Closes and removes the tab
        void remove_tab(AppTab tab);
        //   Activates the tab and brings it into view; may not work in single-view mode
        //   Returns whether activation is successful
        //   NOTE: If tab is nullptr, operation will be no-op
        bool activate_tab(AppTab tab);

        // Resource management
        //   Gets a string from i18n strings list
        winrt::hstring res_str_raw(winrt::hstring const& res_id);

        // Storage management
        winrt::BiliUWP::AppCfgModel cfg_model(void) { return m_cfg_model; }
        // TODO...
        void clear_app_state_snapshot(...);
        // TODO: Flush local tab order from TabView in case user rearranged tabs
        void save_app_state_snapshot(...);
        void restore_app_state_snapshot(...);
        void export_cfg_as_json(...);
        void import_cfg_from_json(...);

        // Logging
        struct LogDesc {
            std::chrono::system_clock::time_point time;
            util::debug::LogLevel level;
            std::source_location src_loc;
            winrt::hstring content;
        };
        // NOTE: Higher the level, fewer the logs
        void set_log_level(util::debug::LogLevel new_level) { m_cur_log_level = new_level; }
        void log_trace(winrt::hstring str, std::source_location loc) {
            // TODO: Add mutex support
            constexpr auto this_level = util::debug::LogLevel::Trace;
            if (this_level < m_cur_log_level) {
                return;
            }
            this->append_log_entry(
                { std::chrono::system_clock::now(), this_level, std::move(loc), std::move(str) }
            );
        }
        void log_debug(winrt::hstring str, std::source_location loc) {
            constexpr auto this_level = util::debug::LogLevel::Debug;
            if (this_level < m_cur_log_level) {
                return;
            }
            this->append_log_entry(
                { std::chrono::system_clock::now(), this_level, std::move(loc), std::move(str) }
            );
        }
        void log_info(winrt::hstring str, std::source_location loc) {
            constexpr auto this_level = util::debug::LogLevel::Info;
            if (this_level < m_cur_log_level) {
                return;
            }
            this->append_log_entry(
                { std::chrono::system_clock::now(), this_level, std::move(loc), std::move(str) }
            );
        }
        void log_warn(winrt::hstring str, std::source_location loc) {
            constexpr auto this_level = util::debug::LogLevel::Warn;
            if (this_level < m_cur_log_level) {
                return;
            }
            this->append_log_entry(
                { std::chrono::system_clock::now(), this_level, std::move(loc), std::move(str) }
            );
        }
        void log_error(winrt::hstring str, std::source_location loc) {
            constexpr auto this_level = util::debug::LogLevel::Error;
            if (this_level < m_cur_log_level) {
                return;
            }
            this->append_log_entry(
                { std::chrono::system_clock::now(), this_level, std::move(loc), std::move(str) }
            );
        }
        void clear_log(void) { m_app_logs.clear(); };

        // Authentication
        bool is_logged_in(void);
        winrt::Windows::Foundation::IAsyncAction request_login(void);
        winrt::Windows::Foundation::IAsyncAction request_login_blocking(AppTab tab);
        winrt::Windows::Foundation::IAsyncAction request_logout(void);
        template<typename T>
        winrt::event_token login_state_changed(T&& functor) {
            return m_ev_login_state_changed.add(
                [functor = std::move(functor)](winrt::Windows::Foundation::IInspectable const&, bool) {
                    functor();
                }
            );
        }
        void login_state_changed(winrt::event_token et) {
            m_ev_login_state_changed.remove(et);
        }
        void signal_login_state_changed(void) {
            m_ev_login_state_changed(nullptr, false);
        }

        BiliClient* bili_client(void) {
            return m_bili_client;
        }

        DebugConsole& debug_console(void) {
            return m_dbg_con;
        }

    private:
        friend winrt::BiliUWP::implementation::App;

        // Helper functions for internal use
        void add_tab(AppTab new_tab, winrt::Microsoft::UI::Xaml::Controls::TabView const& tab_view);
        void init_current_window(void);
        winrt::Microsoft::UI::Xaml::Controls::TabView parent_of_tab(AppTab tab);
        static void hack_tab_view_item(winrt::Microsoft::UI::Xaml::Controls::TabViewItem const& tvi);
        void append_log_entry(LogDesc log_desc);

        // TODO: Manage resources here
        std::vector<AppTab> m_app_tabs;
        // NOTE: Only used in tab mode for multiple views
        // TODO: Use std::scoped_lock<std::mutex> for TabView (maybe use helper functions?)
        std::vector<winrt::Microsoft::UI::Xaml::Controls::TabView> m_tv;
        // NOTE: Only used in single-view mode; stores all `ContainerPage`s
        // TODO: Maybe we don't need back stack, as we can directly navigate
        //       with state stored in AppTab?
        winrt::Windows::UI::Xaml::Controls::Frame m_glob_frame;
        util::debug::LogLevel m_cur_log_level;
        std::vector<LogDesc> m_app_logs;
        AppLoggingProvider* m_logging_provider;
        winrt::Windows::Storage::StorageFile m_cur_log_file;
        std::jthread m_logging_thread;
        util::sync::mpsc_channel_sender<winrt::hstring> m_logging_tx;
        winrt::Windows::Foundation::Collections::IVector<winrt::hstring> m_logging_file_buf;
        winrt::Windows::ApplicationModel::Resources::ResourceLoader m_res_ldr;
        ::BiliUWP::DebugConsole m_dbg_con;

        // TODO: We should still handle config in separate RTClass in `AppCfgModel.cpp`.
        //       The reason is that all settings should be laid flat in the root space,
        //       accessible from the XAML framework.
        //       For arrays, just use ApplicationDataCompositeValue.
        //       We just need to store a reference to the RTClass.

        // App configurations
        winrt::BiliUWP::AppCfgModel m_cfg_model;

        // Configuration cache (for settings that can't be changed at run time)
        bool m_cfg_app_use_tab_view;
        bool m_cfg_app_store_logs;

        BiliClient* m_bili_client;

        winrt::event<winrt::Windows::Foundation::EventHandler<bool>> m_ev_login_state_changed;
    };

    struct AppLoggingProvider final : util::debug::LoggingProvider {
        AppLoggingProvider(AppInst* app_inst) : m_app_inst(app_inst) {}
        void set_log_level(util::debug::LogLevel new_level) {
            m_app_inst->set_log_level(new_level);
        }
        void log(std::wstring_view str, std::source_location loc) {
            // TODO: Maybe implement AppLoggingProvider::log()
            m_app_inst->log_info(winrt::hstring{ str }, std::move(loc));
        }
        void log_trace(std::wstring_view str, std::source_location loc) {
            m_app_inst->log_trace(winrt::hstring{ str }, std::move(loc));
        }
        void log_debug(std::wstring_view str, std::source_location loc) {
            m_app_inst->log_debug(winrt::hstring{ str }, std::move(loc));
        }
        void log_info(std::wstring_view str, std::source_location loc) {
            m_app_inst->log_info(winrt::hstring{ str }, std::move(loc));
        }
        void log_warn(std::wstring_view str, std::source_location loc) {
            m_app_inst->log_warn(winrt::hstring{ str }, std::move(loc));
        }
        void log_error(std::wstring_view str, std::source_location loc) {
            m_app_inst->log_error(winrt::hstring{ str }, std::move(loc));
        }

    private:
        AppInst* m_app_inst;
    };

    namespace App {
        // TODO: Provide shim APIs
        inline auto get(void) {
            extern AppInst* g_app_inst;
            if (g_app_inst == nullptr) {
                throw std::exception("Attempted to get an invalid app instance");
            }
            return g_app_inst;
        }

        namespace details {
            template<typename... types, size_t... idxs>
            inline winrt::hstring replace_res_str(
                winrt::hstring const& res_str,
                std::index_sequence<idxs...>,
                types const&... params
            ) {
                std::wstring str{ res_str };
                auto replace_fn = [&](auto pattern, auto const& param) {
                    winrt::hstring param_str;
                    if constexpr (std::is_assignable_v<winrt::hstring, decltype(param)>) {
                        param_str = param;
                    }
                    else {
                        param_str = winrt::to_hstring(param);
                    }
                    size_t index = 0;
                    while ((index = str.find(pattern.value, index)) != std::wstring::npos) {
                        // Assuming pattern.value is an array of wchar_t
                        constexpr size_t pattern_len = sizeof(pattern.value) / sizeof(*pattern.value) - 1;
                        str.replace(index, pattern_len, param_str);
                        index += param_str.size();
                    }
                };
                (replace_fn(
                    util::str::concat_wstr(L"{", util::str::unsigned_to_wstr_v<idxs>, L"}"),
                    params
                ), ...);
                return str.c_str();
            }
        }
        template<typename... types>
        inline winrt::hstring res_str(winrt::hstring const& res_id, types const&... params) {
            return details::replace_res_str(
                get()->res_str_raw(res_id),
                std::make_index_sequence<sizeof...(params)>{},
                params...
            );
        }
    }

    struct implementation::AppTab : require_make_shared<AppTab> {
        AppTab(use_the_make_function);
        void post_init(use_the_make_function);
        ~AppTab();
        AppTab(AppTab const&) = delete;
        AppTab& operator=(AppTab const&) = delete;

        winrt::Microsoft::UI::Xaml::Controls::IconSource get_icon();
        void set_icon(winrt::Microsoft::UI::Xaml::Controls::IconSource const& ico_src);
        void set_icon(winrt::Windows::UI::Xaml::Controls::Symbol const& symbol);
        winrt::hstring get_title();
        void set_title(winrt::hstring const& title);
        winrt::Windows::Foundation::IInspectable get_content() { return m_page_frame.Content(); }

        bool navigate(
            winrt::Windows::UI::Xaml::Interop::TypeName const& page_type,
            winrt::Windows::Foundation::IInspectable const& param = nullptr
        );
        bool can_go_back(void);
        void go_back(void);
        bool can_go_forward(void);
        void go_forward(void);

        // NOTE: Cancel the returned async operation to close the dialog
        // NOTE: If the close button text is empty, the button will not be shown
        winrt::Windows::Foundation::IAsyncOperation<winrt::BiliUWP::SimpleContentDialogResult> show_dialog(
            winrt::BiliUWP::SimpleContentDialog dialog
        );

        // A shim for AppInst::activate_tab()
        bool activate(void) {
            if (!m_app_inst) {
                throw winrt::hresult_error(E_FAIL, L"AppTab is not associated with an AppInst");
            }
            return m_app_inst->activate_tab(this->shared_from_this());
        }

        // TODO: Associate AppTab with ui context and update it when tab goes to a new window
        auto ui_context(void) {
            return winrt::resume_foreground(m_root_grid.Dispatcher());
        }

        template<typename T>
        winrt::event_token closed_by_user(T&& functor) {
            return m_ev_closed_by_user.add(
                [functor = std::forward<T>(functor)](winrt::Windows::Foundation::IInspectable const&, bool) {
                    functor();
                }
            );
        }
        void closed_by_user(winrt::event_token et) {
            m_ev_closed_by_user.remove(et);
        }

    private:
        friend winrt::BiliUWP::implementation::App;
        friend AppInst;

        // NOTE: AppInst should be unassociated as soon as it is removed from the instance
        void associate_app_inst(AppInst* app_inst) { m_app_inst = app_inst; }
        auto get_tab_view_item(void) { return m_tab_item; }
        winrt::Windows::UI::Xaml::FrameworkElement get_container(void) { return m_root_grid; }
        bool has_page(winrt::Windows::UI::Xaml::Controls::Page const& page) {
            return m_page_frame.Content() == page;
        }
        static ::BiliUWP::AppTab get_from_page(winrt::Windows::UI::Xaml::Controls::Page const& page) {
            for (auto const& [frame, tab_ptr] : s_frame_tab_map) {
                if (frame.Content() == page) {
                    return tab_ptr->shared_from_this();
                }
            }
            return nullptr;
        }
        void signal_closed_by_user(void) {
            m_ev_closed_by_user(nullptr, false);
        }

        static inline std::map<winrt::Windows::UI::Xaml::Controls::Frame, AppTab*> s_frame_tab_map;

        AppInst* m_app_inst;

        winrt::Microsoft::UI::Xaml::Controls::TabViewItem m_tab_item;
        winrt::Windows::UI::Xaml::Controls::Grid m_root_grid;
        winrt::Windows::UI::Xaml::Controls::Frame m_page_frame;

        winrt::Windows::Foundation::IAsyncOperation<winrt::BiliUWP::SimpleContentDialogResult> m_show_dlg_op;
        util::winrt::async_storage m_cur_async;

        winrt::event<winrt::Windows::Foundation::EventHandler<bool>> m_ev_closed_by_user;
    };
}
