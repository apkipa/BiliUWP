#include "pch.h"
#include "VideoDanmakuControl.h"
#if __has_include("VideoDanmakuControl.g.cpp")
#include "VideoDanmakuControl.g.cpp"
#endif
#include "util.hpp"
#include <d2d1_3.h>
#include <d2d1_3helper.h>
#include <dwrite_3.h>
#include <dxgi1_4.h>
#include <d3d11_1.h>
#include <winrt/Microsoft.Graphics.Canvas.h>
#include <Microsoft.Graphics.Canvas.native.h>
#include <windows.ui.xaml.media.dxinterop.h>

namespace winrt {
    using namespace Windows::Foundation;
    using namespace Windows::UI::Core;
    using namespace Windows::UI::Xaml;
    using namespace Windows::UI::Xaml::Controls;
}
namespace abi {
    using namespace ABI::Microsoft::Graphics::Canvas;
}

namespace winrt::BiliUWP::implementation {
    auto const& get_global_dwrite_factory(void) {
        static auto factory = [] {
            com_ptr<IDWriteFactory3> dwrite_factory;
            check_hresult(DWriteCreateFactory(
                DWRITE_FACTORY_TYPE_SHARED,
                guid_of<decltype(dwrite_factory)>(),
                reinterpret_cast<::IUnknown**>(dwrite_factory.put())
            ));
            return dwrite_factory;
        }();
        return factory;
    }

    unsigned size_dips_to_pixels(float dips, float dpi) {
        auto result = std::llround(dips * dpi / 96);
        if (result == 0 && dips > 0) { result = 1; }
        return static_cast<unsigned>(result);
    }

    void create_composition_swapchain(
        com_ptr<ID2D1Device2> const& d2d1_dev,
        float width, float height, float dpi,
        bool use_transparent_swapchain,
        com_ptr<IDXGISwapChain2>& dxgi_swapchain
    ) {
        com_ptr<IDXGIDevice> dxgi_dev;
        check_hresult(d2d1_dev->GetDxgiDevice(dxgi_dev.put()));
        com_ptr<IDXGIAdapter> dxgi_adapter;
        check_hresult(dxgi_dev->GetAdapter(dxgi_adapter.put()));
        com_ptr<IDXGIFactory2> dxgi_factory;
        dxgi_factory.capture(dxgi_adapter, &IDXGIAdapter::GetParent);
        DXGI_SWAP_CHAIN_DESC1 swap_chain_desc = { 0 };
        swap_chain_desc.Width = size_dips_to_pixels(width, dpi);
        swap_chain_desc.Height = size_dips_to_pixels(height, dpi);
        swap_chain_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        swap_chain_desc.Stereo = false;
        swap_chain_desc.SampleDesc.Count = 1;
        swap_chain_desc.SampleDesc.Quality = 0;
        swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swap_chain_desc.BufferCount = 2;
        swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swap_chain_desc.Flags = 0;
        if (use_transparent_swapchain) {
            swap_chain_desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        }
        com_ptr<IDXGISwapChain1> base_dxgi_swapchain;
        check_hresult(dxgi_factory->CreateSwapChainForComposition(
            dxgi_dev.get(),
            &swap_chain_desc,
            nullptr,
            base_dxgi_swapchain.put()
        ));
        base_dxgi_swapchain.as(dxgi_swapchain);
    }
    void resize_swapchain(
        com_ptr<IDXGISwapChain1> const& dxgi_swapchain,
        float width, float height, float dpi
    ) {
        check_hresult(dxgi_swapchain->ResizeBuffers(
            2,
            size_dips_to_pixels(width, dpi), size_dips_to_pixels(height, dpi),
            DXGI_FORMAT_UNKNOWN,
            0
        ));
    }

    struct VideoDanmakuControl_SharedData : std::enable_shared_from_this<VideoDanmakuControl_SharedData> {
        VideoDanmakuControl_SharedData() : danmaku(make_self<VideoDanmakuCollection>()) {}

        // NOTE: No lock
        auto get_text_format(hstring const& font_family, float font_size) {
            auto& dwrite_factory = get_global_dwrite_factory();
            auto& txt_fmt = txt_fmt_map[{ font_family, font_size }];
            if (!txt_fmt) {
                com_ptr<IDWriteTextFormat> base_txt_fmt;
                check_hresult(dwrite_factory->CreateTextFormat(
                    font_family.c_str(),
                    nullptr,
                    DWRITE_FONT_WEIGHT_REGULAR,
                    DWRITE_FONT_STYLE_NORMAL,
                    DWRITE_FONT_STRETCH_NORMAL,
                    font_size,
                    L"",
                    base_txt_fmt.put()
                ));
                base_txt_fmt.as(txt_fmt);
            }
            return txt_fmt;
        }
        // NOTE: No lock
        auto get_d2d1_device() {
            if (!d2d1_dev) {
                auto shared_device = winrt::Microsoft::Graphics::Canvas::CanvasDevice::GetSharedDevice();
                auto native_device_wrapper = shared_device.as<abi::ICanvasResourceWrapperNative>();
                d2d1_dev.capture(native_device_wrapper, &abi::ICanvasResourceWrapperNative::GetNativeResource,
                    nullptr, 0.0f);
            }
            return d2d1_dev;
        }
        void request_redraw(void) {
            should_redraw.store(true);
            should_redraw.notify_one();
        }

        com_ptr<VideoDanmakuCollection> danmaku{ nullptr };
        std::atomic_bool show_danmaku{ false };
        std::atomic_bool should_redraw{ false };
        IAsyncAction thread_task{ nullptr };
        std::atomic_bool thread_running{ false };
        com_ptr<IDXGISwapChain2> swapchain{ nullptr };
        com_ptr<ID2D1Device2> d2d1_dev{ nullptr };
        com_ptr<ID2D1DeviceContext3> d2d1_dev_ctx{ nullptr };
        winrt::Windows::UI::Xaml::Controls::SwapChainPanel swapchain_panel{ nullptr };
        // NOTE: Data below are protected with mutex
        std::mutex mutex;
        // (FontFamily, FontSize) => TextFormat
        std::map<std::tuple<hstring, float>, com_ptr<IDWriteTextFormat1>> txt_fmt_map;
        bool use_transparent_swapchain{ true };
        bool should_recreate_swapchain{ true };
        bool should_resize_swapchain{ false };
        float swapchain_panel_width, swapchain_panel_height;
        float swapchain_panel_scale_x, swapchain_panel_scale_y;
        bool is_playing{ false };
        bool viewport_occluded{ true };
        uint64_t play_progress{};   // Unit: ms
        std::chrono::steady_clock::time_point last_tp;
        BiliUWP::VideoDanmakuPopulateD3DSurfaceDelegate background_populator{ nullptr };
    };

    struct VideoDanmakuNormalItemContainer {
        VideoDanmakuNormalItemContainer(BiliUWP::VideoDanmakuNormalItem const& in_data) :
            data(in_data) {}
        BiliUWP::VideoDanmakuNormalItem data;
        com_ptr<IDWriteTextFormat1> m_txt_format;
        com_ptr<IDWriteTextLayout3> m_txt_layout;
    };

    VideoDanmakuCollection::VideoDanmakuCollection() {}
    void VideoDanmakuCollection::AddManyNormal(array_view<winrt::BiliUWP::VideoDanmakuNormalItem const> items) {
        std::scoped_lock guard(m_mutex);
        for (auto& item : items) {
            util::container::insert_sorted(m_normal_items, item,
                [](VideoDanmakuNormalItemContainer const& a, VideoDanmakuNormalItemContainer const& b) {
                    return a.data.appear_time < b.data.appear_time;
                }
            );
        }
    }
    uint32_t VideoDanmakuCollection::GetManyNormal(uint32_t startIndex, array_view<winrt::BiliUWP::VideoDanmakuNormalItem> items) {
        std::scoped_lock guard(m_mutex);
        if (startIndex >= m_normal_items.size()) { return 0; }
        uint32_t copy_count = std::min(items.size(), static_cast<uint32_t>(m_normal_items.size() - startIndex));
        auto it_src_start = m_normal_items.begin() + startIndex;
        std::transform(it_src_start, it_src_start + copy_count, items.begin(),
            [](VideoDanmakuNormalItemContainer const& v) { return v.data; }
        );
        return copy_count;
    }
    void VideoDanmakuCollection::RemoveMany(array_view<uint64_t const> itemIds) {
        std::scoped_lock guard(m_mutex);
        for (auto& item_id : itemIds) {
            std::erase_if(m_normal_items,
                [&](VideoDanmakuNormalItemContainer const& v) { return v.data.id == item_id; }
            );
        }
    }
    void VideoDanmakuCollection::UpdateManyVisibility(array_view<winrt::BiliUWP::VideoDanmakuItemWithVisibility const> items) {
        std::scoped_lock guard(m_mutex);
        throw hresult_not_implemented();
    }
    void VideoDanmakuCollection::ClearAll() {
        std::scoped_lock guard(m_mutex);
        m_normal_items.clear();
    }

    VideoDanmakuControl::VideoDanmakuControl() :
        m_shared_data(std::make_shared<VideoDanmakuControl_SharedData>()),
        m_core_window(CoreWindow::GetForCurrentThread()) {}
    void VideoDanmakuControl::InitializeComponent() {
        VideoDanmakuControlT::InitializeComponent();

        using namespace Windows::System::Threading;

        m_shared_data->last_tp = {};
        m_shared_data->swapchain_panel = RootSwapChainPanel();
        // TODO: Remember to lock Win2D CanvasDevice before performing Direct2D operations
        // TODO: Register event handler
        // TODO: Call ID2D1Device::ClearResources when suspending
        m_shared_data->swapchain_panel_width = 1;
        m_shared_data->swapchain_panel_height = 1;
        m_shared_data->swapchain_panel_scale_x = m_shared_data->swapchain_panel.CompositionScaleX();
        m_shared_data->swapchain_panel_scale_y = m_shared_data->swapchain_panel.CompositionScaleY();
        m_shared_data->swapchain_panel.SizeChanged([this](auto&&, SizeChangedEventArgs const& e) {
            auto [width, height] = e.NewSize();
            std::scoped_lock guard(m_shared_data->mutex);
            m_shared_data->swapchain_panel_width = width;
            m_shared_data->swapchain_panel_height = height;
            m_shared_data->should_resize_swapchain = true;
            m_shared_data->request_redraw();
        });
        m_shared_data->swapchain_panel.CompositionScaleChanged([this](SwapChainPanel const& sender, auto&&) {
            std::scoped_lock guard(m_shared_data->mutex);
            m_shared_data->swapchain_panel_scale_x = sender.CompositionScaleX();
            m_shared_data->swapchain_panel_scale_y = sender.CompositionScaleY();
            m_shared_data->should_resize_swapchain = true;
            m_shared_data->request_redraw();
        });
        auto loaded_changed_handler = [this](IInspectable const& sender, RoutedEventArgs const&) {
            m_is_loaded = sender.as<FrameworkElement>().IsLoaded();
            std::scoped_lock guard(m_shared_data->mutex);
            if (m_is_loaded) {
                m_shared_data->viewport_occluded = !(m_is_visible && m_is_core_window_visible);
                if (!m_shared_data->viewport_occluded) {
                    m_shared_data->request_redraw();
                }
            }
            else {
                m_shared_data->viewport_occluded = true;
            }
        };
        m_shared_data->swapchain_panel.Loaded(loaded_changed_handler);
        m_shared_data->swapchain_panel.Unloaded(loaded_changed_handler);
        RegisterPropertyChangedCallback(UIElement::VisibilityProperty(),
            [this](DependencyObject const& sender, DependencyProperty const&) {
                bool is_visible = sender.as<UIElement>().Visibility() == Visibility::Visible;
                m_is_visible = is_visible;
                std::scoped_lock guard(m_shared_data->mutex);
                if (is_visible) {
                    m_shared_data->viewport_occluded = !(m_is_loaded && m_is_core_window_visible);
                    if (!m_shared_data->viewport_occluded) {
                        m_shared_data->request_redraw();
                    }
                }
                else {
                    m_shared_data->viewport_occluded = true;
                }
            }
        );
        m_et_core_window_visibility_changed = m_core_window.VisibilityChanged(
            [this](CoreWindow const&, VisibilityChangedEventArgs const& e) {
                m_is_core_window_visible = e.Visible();
                std::scoped_lock guard(m_shared_data->mutex);
                if (m_is_core_window_visible) {
                    m_shared_data->viewport_occluded = !(m_is_visible && m_is_loaded);
                    if (!m_shared_data->viewport_occluded) {
                        m_shared_data->request_redraw();
                    }
                }
                else {
                    m_shared_data->viewport_occluded = true;
                }
            }
        );
        m_is_loaded = !m_shared_data->swapchain_panel.IsLoaded();
        m_is_visible = Visibility() == Visibility::Visible;
        m_is_core_window_visible = m_core_window.Visible();
        // Start render thread
        m_shared_data->thread_task = ThreadPool::RunAsync([shared_data = m_shared_data](IAsyncAction const& work_item) {
            HRESULT hr;
            util::win32::set_thread_name(L"VideoDanmakuControl Render Thread");
            shared_data->thread_running.wait(false);
            try {
                while (true) {
                    if (work_item.Status() == AsyncStatus::Canceled) { break; }

                    {
                        std::scoped_lock guard(shared_data->mutex);

                        const auto dpi_x = 96 * shared_data->swapchain_panel_scale_x;
                        const auto dpi_y = 96 * shared_data->swapchain_panel_scale_y;
                        const auto vp_width = shared_data->swapchain_panel_width;
                        const auto vp_height = shared_data->swapchain_panel_height;

                        // TODO: Use correct size
                        // Recreate device & swapchain if required
                        bool has_swapchain = static_cast<bool>(shared_data->swapchain);
                        bool recreate_swapchain_required = shared_data->should_recreate_swapchain;
                        bool resize_required = shared_data->should_resize_swapchain;
                        if (!has_swapchain || recreate_swapchain_required || resize_required) {
                            auto d2d1_device = shared_data->get_d2d1_device();
                            if (shared_data->d2d1_dev_ctx) {
                                shared_data->d2d1_dev_ctx->SetTarget(nullptr);
                                shared_data->d2d1_dev_ctx = nullptr;
                            }
                            if (has_swapchain && !recreate_swapchain_required) {
                                // Resize swapchain
                                hr = shared_data->swapchain->ResizeBuffers(
                                    2,
                                    size_dips_to_pixels(shared_data->swapchain_panel_width, dpi_x),
                                    size_dips_to_pixels(shared_data->swapchain_panel_height, dpi_y),
                                    DXGI_FORMAT_UNKNOWN,
                                    0
                                );
                                if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
                                    // Failed to resize; reset resources and restart
                                    shared_data->swapchain = nullptr;
                                    shared_data->d2d1_dev = nullptr;
                                    shared_data->d2d1_dev_ctx = nullptr;
                                    continue;
                                }
                            }
                            else {
                                // Recreate swapchain
                                create_composition_swapchain(
                                    d2d1_device,
                                    shared_data->swapchain_panel_width, shared_data->swapchain_panel_height,
                                    96 * shared_data->swapchain_panel_scale_x,
                                    shared_data->use_transparent_swapchain,
                                    shared_data->swapchain
                                );
                                shared_data->swapchain_panel.Dispatcher().RunAsync(
                                    winrt::CoreDispatcherPriority::Normal,
                                    [shared_data] {
                                        shared_data->swapchain_panel
                                            .as<ISwapChainPanelNative>()
                                            ->SetSwapChain(shared_data->swapchain.get());
                                    }
                                );
                                com_ptr<ID2D1DeviceContext1> d2d1_dev_ctx;
                                check_hresult(d2d1_device->CreateDeviceContext(
                                    D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                    d2d1_dev_ctx.put()
                                ));
                                d2d1_dev_ctx.as(shared_data->d2d1_dev_ctx);
                            }
                            DXGI_MATRIX_3X2_F inverse_scale = { 0 };
                            inverse_scale._11 = 1.0f / shared_data->swapchain_panel_scale_x;
                            inverse_scale._22 = 1.0f / shared_data->swapchain_panel_scale_y;
                            shared_data->swapchain->SetMatrixTransform(&inverse_scale);

                            shared_data->should_recreate_swapchain = false;
                            shared_data->should_resize_swapchain = false;
                            // Reinitialize Direct2D context
                            com_ptr<ID2D1DeviceContext1> d2d1_dev_ctx;
                            check_hresult(d2d1_device->CreateDeviceContext(
                                D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                d2d1_dev_ctx.put()
                            ));
                            d2d1_dev_ctx.as(shared_data->d2d1_dev_ctx);
                            com_ptr<IDXGISurface> dxgi_surface;
                            dxgi_surface.capture(shared_data->swapchain, &IDXGISwapChain1::GetBuffer, 0);
                            D2D1_BITMAP_PROPERTIES1 bmp_props = D2D1::BitmapProperties1(
                                D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                                96 * shared_data->swapchain_panel_scale_x,
                                96 * shared_data->swapchain_panel_scale_y
                            );
                            com_ptr<ID2D1Bitmap1> d2d1_bmp;
                            check_hresult(shared_data->d2d1_dev_ctx->CreateBitmapFromDxgiSurface(
                                dxgi_surface.get(), bmp_props, d2d1_bmp.put()));
                            shared_data->d2d1_dev_ctx->SetDpi(dpi_x, dpi_y);
                            shared_data->d2d1_dev_ctx->SetTarget(d2d1_bmp.get());
                        }

                        // TODO: Draw
                        shared_data->should_redraw.store(false);
                        auto& d2d1_dev_ctx = shared_data->d2d1_dev_ctx;
                        d2d1_dev_ctx->BeginDraw();
                        {
                            std::scoped_lock guard_danmaku(shared_data->danmaku->m_mutex);

                            auto cur_p = [&] {
                                using u64_ms_dur = std::chrono::duration<uint64_t, std::milli>;
                                if (!shared_data->is_playing) { return shared_data->play_progress; }
                                auto dur = std::chrono::steady_clock::now() - shared_data->last_tp;
                                auto dur_ms = std::chrono::round<u64_ms_dur>(dur).count();
                                return shared_data->play_progress + dur_ms;
                            }();

                            //d2d1_dev_ctx->Clear(D2D1::ColorF(0.5f, 0.0f, 0.0f, 0.5f));
                            d2d1_dev_ctx->Clear();
                            com_ptr<ID2D1SolidColorBrush> solid_brush;
                            check_hresult(d2d1_dev_ctx->CreateSolidColorBrush(
                                D2D1::ColorF(D2D1::ColorF::White), solid_brush.put()));
                            float danmaku_scroll_speed = 200;
                            std::vector<uint32_t> slot_occupied_until(10);
                            for (size_t i = 0; i < shared_data->danmaku->m_normal_items.size(); i++) {
                                auto& item = shared_data->danmaku->m_normal_items[i];
                                if (item.data.appear_time > cur_p) { break; }
                                if (!item.m_txt_layout) {
                                    if (!item.m_txt_format) {
                                        item.m_txt_format = shared_data->get_text_format(
                                            L"Microsoft YaHei", item.data.font_size);
                                    }
                                    com_ptr<IDWriteTextLayout> base_txt_layout;
                                    auto& dwrite_factory = get_global_dwrite_factory();
                                    dwrite_factory->CreateTextLayout(
                                        item.data.content.c_str(),
                                        item.data.content.size(),
                                        item.m_txt_format.get(),
                                        std::numeric_limits<float>::max(),
                                        0,
                                        base_txt_layout.put()
                                    );
                                    base_txt_layout.as(item.m_txt_layout);
                                }
                                // TODO: Optimize performance (by caching bitmap?)
                                // TODO: Let danmaku scroll
                                D2D1_POINT_2F item_pt;
                                DWRITE_TEXT_METRICS1 txt_metrics;
                                check_hresult(item.m_txt_layout->GetMetrics(&txt_metrics));
                                const float item_width = txt_metrics.width;
                                const float item_height = txt_metrics.height;
                                item_pt.x = vp_width - (cur_p - item.data.appear_time) * danmaku_scroll_speed / 1000;
                                auto item_end_time = item.data.appear_time +
                                    static_cast<uint32_t>(std::ceilf(item_width * 1000 / danmaku_scroll_speed));
                                auto slot_it = slot_occupied_until.begin(), slot_ie = slot_occupied_until.end();
                                for (; slot_it != slot_ie; slot_it++) {
                                    if (item.data.appear_time >= *slot_it) {
                                        item_pt.y = item_height * (slot_it - slot_occupied_until.begin());
                                        *slot_it = item_end_time;
                                        break;
                                    }
                                }
                                if (slot_it == slot_ie) {
                                    // No spare slots
                                    if (item_pt.x + item_width < 0) {
                                        continue;
                                    }
                                    item_pt.x = 0;
                                    item_pt.y = 0;
                                }
                                if (item_pt.x + item_width < 0) {
                                    continue;
                                }
                                d2d1_dev_ctx->DrawTextLayout(
                                    item_pt,
                                    item.m_txt_layout.get(),
                                    solid_brush.get(),
                                    D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT
                                );
                            }
                            const bool debug_print_counter = true;
                            if constexpr (debug_print_counter) {
                                static auto text_fmt = shared_data->get_text_format(L"Microsoft YaHei", 25);
                                static int counter = 0;
                                counter++;
                                auto buf = std::format(L"n: {}\nt: {}ms", counter, cur_p);
                                d2d1_dev_ctx->DrawText(
                                    buf.c_str(),
                                    buf.size(),
                                    text_fmt.get(),
                                    D2D1::RectF(0.0f, 30.0f, 500.0f, 300.0f),
                                    solid_brush.get(),
                                    D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT
                                );
                            }
                        }
                        d2d1_dev_ctx->EndDraw();
                    }

                    // Final present
                    hr = shared_data->swapchain->Present(1, 0);
                    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
                        // Device has lost
                        shared_data->swapchain = nullptr;
                        shared_data->d2d1_dev = nullptr;
                        shared_data->d2d1_dev_ctx = nullptr;
                    }
                    else { check_hresult(hr); }

                    // TODO: Change this
                    {
                        std::scoped_lock guard(shared_data->mutex);
                        if (shared_data->is_playing && !shared_data->viewport_occluded) {
                            shared_data->should_redraw.store(true);
                        }
                        else {
                            if (!shared_data->should_redraw.load()) {
                                // Trim device resources
                                shared_data->d2d1_dev->ClearResources();
                            }
                        }
                    }

                    shared_data->should_redraw.wait(false);
                }
            }
            catch (...) {
                util::debug::log_error(L"VideoDanmakuControl Render Thread: Caught unhandled exception");
                util::winrt::log_current_exception();
            }
            shared_data->thread_running.store(false);
            util::debug::log_trace(L"VideoDanmakuControl Render Thread stopped");
        }, WorkItemPriority::Normal, WorkItemOptions::TimeSliced);
        m_shared_data->thread_running.store(true);
    }
    VideoDanmakuControl::~VideoDanmakuControl() {
        if (m_shared_data->thread_task) {
            m_shared_data->thread_task.Cancel();
            m_shared_data->request_redraw();
        }
        if (m_et_core_window_visibility_changed) {
            m_core_window.VisibilityChanged(m_et_core_window_visibility_changed);
        }
    }
    BiliUWP::VideoDanmakuCollection VideoDanmakuControl::Danmaku() {
        return *m_shared_data->danmaku;
    }
    void VideoDanmakuControl::IsDanmakuVisible(bool value) {
        m_shared_data->show_danmaku.store(value);
    }
    bool VideoDanmakuControl::IsDanmakuVisible() {
        return m_shared_data->show_danmaku.load();
    }
    bool VideoDanmakuControl::IsRunning() {
        std::scoped_lock guard(m_shared_data->mutex);
        return m_shared_data->is_playing;
    }
    void VideoDanmakuControl::Start() {
        std::scoped_lock guard(m_shared_data->mutex);
        if (m_shared_data->is_playing) { return; }
        m_shared_data->is_playing = true;
        m_shared_data->last_tp = std::chrono::steady_clock::now();
        m_shared_data->request_redraw();
    }
    void VideoDanmakuControl::Pause() {
        std::scoped_lock guard(m_shared_data->mutex);
        if (!m_shared_data->is_playing) { return; }
        m_shared_data->is_playing = false;
        auto cur_tp = std::chrono::steady_clock::now();
        m_shared_data->play_progress += std::chrono::round<std::chrono::milliseconds>(
            cur_tp - m_shared_data->last_tp).count();
        m_shared_data->last_tp = {};
    }
    void VideoDanmakuControl::Stop(bool continueDanmakuRunning) {
        // TODO: continueDanmakuRunning
        std::scoped_lock guard(m_shared_data->mutex);
        m_shared_data->is_playing = false;
        m_shared_data->play_progress = 0;
        m_shared_data->last_tp = {};
    }
    void VideoDanmakuControl::UpdateCurrentTime(Windows::Foundation::TimeSpan const& time) {
        std::scoped_lock guard(m_shared_data->mutex);
        if (m_shared_data->is_playing) {
            m_shared_data->play_progress = std::chrono::round<std::chrono::milliseconds>(time).count();
            m_shared_data->last_tp = std::chrono::steady_clock::now();
        }
        else {
            m_shared_data->play_progress = std::chrono::round<std::chrono::milliseconds>(time).count();
            m_shared_data->request_redraw();
        }
    }
    void VideoDanmakuControl::SetAssociatedMediaTimelineController(Windows::Media::MediaTimelineController const& controller) {
        throw hresult_not_implemented();
    }
    Windows::Media::MediaTimelineController VideoDanmakuControl::GetAssociatedMediaTimelineController() {
        throw hresult_not_implemented();
    }
    void VideoDanmakuControl::SetBackgroundPopulator(BiliUWP::VideoDanmakuPopulateD3DSurfaceDelegate const& handler, bool isProactive) {
        bool previous_transparent = !m_shared_data->background_populator;
        bool current_transparent = !handler;
        std::scoped_lock guard(m_shared_data->mutex);
        m_shared_data->background_populator = handler;
        if (previous_transparent != current_transparent) {
            m_shared_data->use_transparent_swapchain = current_transparent;
            m_shared_data->should_recreate_swapchain = true;
        }
    }
    void VideoDanmakuControl::TriggerBackgroundUpdate() {
        throw hresult_not_implemented();
    }
}
