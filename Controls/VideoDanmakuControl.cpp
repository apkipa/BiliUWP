#include "pch.h"
#include "VideoDanmakuControl.h"
#if __has_include("VideoDanmakuControl.g.cpp")
#include "VideoDanmakuControl.g.cpp"
#endif
#include "util.hpp"
#include <numeric>
#include <d2d1_3.h>
#include <d2d1_3helper.h>
#include <dwrite_3.h>
#include <dxgi1_4.h>
#include <d3d11_1.h>
#include <winrt/Microsoft.Graphics.Canvas.h>
#include <Microsoft.Graphics.Canvas.native.h>
#include <windows.ui.xaml.media.dxinterop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

// NOTE: Some code are adapted from Win2D

// TODO: Maybe investigate ICompositorInterop::CreateCompositionSurfaceForSwapChain?

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

    // TODO: Remove this
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

    // TODO: Empty string is not supported
    com_ptr<ID2D1GeometryGroup> create_mono_text_geometry(
        com_ptr<ID2D1Factory3> const& d2d1_factory,
        com_ptr<IDWriteTextLayout3> const& txt_layout
    ) {
        struct DWriteText2GeometryRenderer : implements<DWriteText2GeometryRenderer, IDWriteTextRenderer> {
            DWriteText2GeometryRenderer(ID2D1Factory3* d2d1_factory) : m_d2d1_factory(d2d1_factory) {}
            ~DWriteText2GeometryRenderer() {
                ClearStoredGeometries();
            }

            void BeginRender() {
                ClearStoredGeometries();
            }
            com_ptr<ID2D1GeometryGroup> EndRender() {
                HRESULT hr;
                com_ptr<ID2D1GeometryGroup> geometry_group;
                hr = m_d2d1_factory->CreateGeometryGroup(
                    D2D1_FILL_MODE_ALTERNATE,
                    m_geometries.data(),
                    static_cast<UINT32>(m_geometries.size()),
                    geometry_group.put()
                );
                ClearStoredGeometries();
                check_hresult(hr);
                return geometry_group;
            }

            // IDWritePixelSnapping
            IFACEMETHODIMP IsPixelSnappingDisabled(
                void*,
                BOOL* isDisabled
            ) noexcept {
                *isDisabled = TRUE;
                return S_OK;
            }
            IFACEMETHODIMP GetCurrentTransform(
                void*,
                DWRITE_MATRIX* transform
            ) noexcept {
                constexpr DWRITE_MATRIX mat_identity{
                    1.f, 0.f,
                    0.f, 1.f,
                    0.f, 0.f,
                };
                *transform = mat_identity;
                return S_OK;
            }
            IFACEMETHODIMP GetPixelsPerDip(
                void*,
                FLOAT* pixelsPerDip
            ) noexcept {
                *pixelsPerDip = 1;
                return S_OK;
            }

            // IDWriteTextRenderer
            IFACEMETHODIMP DrawGlyphRun(
                void* clientDrawingContext,
                FLOAT baselineOriginX,
                FLOAT baselineOriginY,
                DWRITE_MEASURING_MODE measuringMode,
                DWRITE_GLYPH_RUN const* glyphRun,
                DWRITE_GLYPH_RUN_DESCRIPTION const* glyphRunDescription,
                IUnknown* clientDrawingEffect
            ) noexcept {
                static_cast<void>(
                    clientDrawingContext, measuringMode, glyphRunDescription, clientDrawingEffect);
                return util::winrt::exception_boundary([&] {
                    static auto text_analyzer = [] {
                        auto& dwrite_factory = get_global_dwrite_factory();
                        com_ptr<IDWriteTextAnalyzer> txt_analyzer;
                        check_hresult(dwrite_factory->CreateTextAnalyzer(txt_analyzer.put()));
                        return txt_analyzer.as<IDWriteTextAnalyzer1>();
                    }();
                    com_ptr<ID2D1PathGeometry> path_geometry;
                    check_hresult(m_d2d1_factory->CreatePathGeometry(
                        path_geometry.put()
                    ));
                    com_ptr<ID2D1GeometrySink> geometry_sink;
                    check_hresult(path_geometry->Open(geometry_sink.put()));
                    check_hresult(glyphRun->fontFace->GetGlyphRunOutline(
                        glyphRun->fontEmSize,
                        glyphRun->glyphIndices,
                        glyphRun->glyphAdvances,
                        glyphRun->glyphOffsets,
                        glyphRun->glyphCount,
                        glyphRun->isSideways,
                        glyphRun->bidiLevel % 2,
                        geometry_sink.get()
                    ));
                    check_hresult(geometry_sink->Close());
                    DWRITE_MATRIX transform_matrix;
                    check_hresult(text_analyzer->GetGlyphOrientationTransform(
                        DWRITE_GLYPH_ORIENTATION_ANGLE_0_DEGREES,
                        glyphRun->isSideways,
                        &transform_matrix
                    ));
                    transform_matrix.dx = baselineOriginX;
                    transform_matrix.dy = baselineOriginY;
                    D2D1_MATRIX_3X2_F d2d1_mat{
                        transform_matrix.m11, transform_matrix.m12,
                        transform_matrix.m21, transform_matrix.m22,
                        transform_matrix.dx, transform_matrix.dy,
                    };
                    com_ptr<ID2D1TransformedGeometry> transformed_geometry;
                    check_hresult(m_d2d1_factory->CreateTransformedGeometry(
                        path_geometry.get(),
                        d2d1_mat,
                        transformed_geometry.put()
                    ));
                    m_geometries.push_back(transformed_geometry.get());
                    transformed_geometry.detach();
                });
            }
            IFACEMETHODIMP DrawUnderline(
                void* clientDrawingContext,
                FLOAT baselineOriginX,
                FLOAT baselineOriginY,
                DWRITE_UNDERLINE const* underline,
                IUnknown* clientDrawingEffect
            ) noexcept {
                static_cast<void>(
                    clientDrawingContext,
                    baselineOriginX,
                    baselineOriginY,
                    underline,
                    clientDrawingEffect);
                return E_NOTIMPL;
            }
            IFACEMETHODIMP DrawStrikethrough(
                void* clientDrawingContext,
                FLOAT baselineOriginX,
                FLOAT baselineOriginY,
                DWRITE_STRIKETHROUGH const* strikethrough,
                IUnknown* clientDrawingEffect
            ) noexcept {
                static_cast<void>(
                    clientDrawingContext,
                    baselineOriginX,
                    baselineOriginY,
                    strikethrough,
                    clientDrawingEffect);
                return E_NOTIMPL;
            }
            IFACEMETHODIMP DrawInlineObject(
                void*,
                FLOAT originX,
                FLOAT originY,
                IDWriteInlineObject* inlineObject,
                BOOL isSideways,
                BOOL isRightToLeft,
                IUnknown* brush
            ) noexcept {
                static_cast<void>(
                    originX,
                    originY,
                    inlineObject,
                    isSideways,
                    isRightToLeft,
                    brush);
                return E_NOTIMPL;
            }

        private:
            ID2D1Factory3* const m_d2d1_factory;
            std::vector<ID2D1Geometry*> m_geometries;

            void ClearStoredGeometries(void) {
                for (auto i : m_geometries) {
                    i->Release();
                }
                m_geometries.clear();
            }
        };
        auto renderer = make_self<DWriteText2GeometryRenderer>(d2d1_factory.get());
        renderer->BeginRender();
        check_hresult(txt_layout->Draw(nullptr, renderer.get(), 0, 0));
        return renderer->EndRender();
    }

    com_ptr<ID2D1Bitmap> create_text_bitmap(
        ID2D1Factory3* const d2d1_factory,
        ID2D1DeviceContext3* const d2d1_dev_ctx,
        ID2D1Brush* const fill_brush,
        ID2D1Brush* const stroke_brush,     // Optional
        float stroke_width,
        bool enable_color_font,
        bool snap_to_pixel
    ) {
        // TODO: create_text_bitmap
        throw hresult_not_implemented();
    }

    struct VideoDanmakuControl_SharedData : std::enable_shared_from_this<VideoDanmakuControl_SharedData> {
        VideoDanmakuControl_SharedData() : danmaku(make_self<VideoDanmakuCollection>()) {}

        bool request_redraw_nolock(bool force_redraw = false) {
            if (force_redraw || !viewport_occluded) {
                should_redraw.store(true);
                should_redraw.notify_one();
                return true;
            }
            return false;
        }
        bool request_redraw(bool force_redraw = false) {
            std::scoped_lock guard(mutex);
            return request_redraw_nolock(force_redraw);
        }

        com_ptr<VideoDanmakuCollection> danmaku{ nullptr };
        std::atomic_bool enable_debug_output{ false };
        std::atomic_bool show_danmaku{ false };
        std::atomic_bool should_redraw{ false };
        IAsyncAction thread_task{ nullptr };
        std::atomic_bool thread_running{ false };
        winrt::Windows::UI::Xaml::Controls::SwapChainPanel swapchain_panel{ nullptr };
        // NOTE: Data below are protected with mutex
        std::mutex mutex;
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
        bool use_proactive_render_mode{ true };
        bool background_populator_ready{ false };
    };

    struct VideoDanmakuNormalItemContainer {
        VideoDanmakuNormalItemContainer(BiliUWP::VideoDanmakuNormalItem const& in_data) :
            data(in_data), cache_valid(false) {}
        BiliUWP::VideoDanmakuNormalItem data;
        bool cache_valid;
        float width, height;
    };

    struct VideoDanmakuNormalItemContainer_AppearTimePred {
        bool operator()(VideoDanmakuNormalItemContainer const& a, VideoDanmakuNormalItemContainer const& b) {
            return a.data.appear_time < b.data.appear_time;
        }
        bool operator()(VideoDanmakuNormalItemContainer const& a, uint64_t const& b) {
            return a.data.appear_time < b;
        }
    };

    VideoDanmakuCollection::VideoDanmakuCollection() {}
    void VideoDanmakuCollection::AddManyNormal(array_view<winrt::BiliUWP::VideoDanmakuNormalItem const> items) {
        std::scoped_lock guard(m_mutex);
        for (auto& item : items) {
            util::container::insert_sorted(m_normal_items, item,
                VideoDanmakuNormalItemContainer_AppearTimePred{});
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
        // TODO: Maybe completely remove reference to Win2D by managing internal shared device
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
            m_shared_data->request_redraw_nolock();
        });
        m_shared_data->swapchain_panel.CompositionScaleChanged([this](SwapChainPanel const& sender, auto&&) {
            std::scoped_lock guard(m_shared_data->mutex);
            m_shared_data->swapchain_panel_scale_x = sender.CompositionScaleX();
            m_shared_data->swapchain_panel_scale_y = sender.CompositionScaleY();
            m_shared_data->should_resize_swapchain = true;
            m_shared_data->request_redraw_nolock();
        });
        auto loaded_changed_handler = [this](IInspectable const& sender, RoutedEventArgs const&) {
            m_is_loaded = sender.as<FrameworkElement>().IsLoaded();
            std::scoped_lock guard(m_shared_data->mutex);
            if (m_is_loaded) {
                m_shared_data->viewport_occluded = !(m_is_visible && m_is_core_window_visible);
                m_shared_data->request_redraw_nolock();
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
                    m_shared_data->request_redraw_nolock();
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
                    m_shared_data->request_redraw_nolock();
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
            util::win32::set_thread_name(L"VideoDanmakuControl Render Thread");

            // Wait for initialization to complete
            shared_data->thread_running.wait(false);

            struct {
                enum class SlotType {
                    SlotScroll = 1,
                    SlotBottom,
                    SlotTop,
                };
                struct SlotInfo {
                    uint64_t occupied_until;
                    float height;
                };
                struct DanmakuItem {
                    com_ptr<ID2D1Bitmap> cache_bmp;
                    D2D1_POINT_2F cache_bmp_offset;
                    D2D1_SIZE_U cache_bmp_pixel_size;
                    SlotType slot_type;
                    uint32_t slot_i;
                    float width, height;
                    uint64_t appear_t;
                };

                void reset() {
                    this->swapchain = nullptr;
                    this->d2d1_factory = nullptr;
                    this->d2d1_dev = nullptr;
                    this->d2d1_dev_ctx = nullptr;
                    this->swapchain_d3d_surface = nullptr;
                    this->swapchain_dxgi_surface = nullptr;
                    this->vp_width = this->vp_height = 0;
                    this->panel_scale_x = this->panel_scale_y = 0;
                    this->last_update_t = 0;
                    this->active_dm_items.clear();
                    this->scroll_slots.clear();
                    this->bottom_slots.clear();
                    this->top_slots.clear();
                }
                void init_device_from_win2d_shared(
                    float width, float height,
                    float panel_scale_x, float panel_scale_y,
                    bool use_transparent_swapchain
                ) {
                    // TODO: Employ proper Win2D lock
                    auto shared_dev = Microsoft::Graphics::Canvas::CanvasDevice::GetSharedDevice();
                    auto native_dev_wrapper = shared_dev.as<abi::ICanvasResourceWrapperNative>();
                    this->d2d1_dev.capture(
                        native_dev_wrapper, &abi::ICanvasResourceWrapperNative::GetNativeResource,
                        nullptr, 0.0f);
                    init_device_inner(
                        width, height,
                        panel_scale_x, panel_scale_y,
                        use_transparent_swapchain
                    );
                }
                bool try_resize_view(
                    float width, float height,
                    float panel_scale_x, float panel_scale_y
                ) noexcept {
                    const auto dpi_x = 96 * this->panel_scale_x;
                    const auto dpi_y = 96 * this->panel_scale_y;
                    this->d2d1_dev_ctx->SetTarget(nullptr);
                    this->swapchain_d3d_surface = nullptr;
                    this->swapchain_dxgi_surface = nullptr;
                    HRESULT hr;
                    hr = this->swapchain->ResizeBuffers(
                        0,
                        size_dips_to_pixels(width, dpi_x),
                        size_dips_to_pixels(height, dpi_y),
                        DXGI_FORMAT_UNKNOWN,
                        0
                    );
                    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
                        // Failed to resize
                        return false;
                    }
                    if (!try_update_device_context_target(dpi_x, dpi_y)) {
                        return false;
                    }
                    // TODO: Clear size dependent data
                    /*this->last_update_t = 0;
                    this->active_dm_items.clear();
                    this->scroll_slots.clear();
                    this->bottom_slots.clear();
                    this->top_slots.clear();*/
                    // Set remaining data
                    this->vp_width = width;
                    this->vp_height = height;
                    this->panel_scale_x = panel_scale_x;
                    this->panel_scale_y = panel_scale_y;
                    return true;
                }
                void update_swap_chain_panel(SwapChainPanel const& panel) {
                    panel.Dispatcher().RunAsync(CoreDispatcherPriority::Normal,
                        [swapchain = this->swapchain, panel] {
                            panel.as<ISwapChainPanelNative>()->SetSwapChain(swapchain.get());
                        }
                    );
                }

                void update_active_danmaku(
                    VideoDanmakuCollection* danmaku_col,
                    uint64_t cur_p
                ) {
                    if (cur_p == last_update_t) { return; }
                    if (cur_p < last_update_t) {
                        // Start from zero
                        this->last_update_t = 0;
                        this->active_dm_items.clear();
                        this->scroll_slots.clear();
                        this->bottom_slots.clear();
                        this->top_slots.clear();
                        this->processed_dm_items_cnt = 0;
                    }
                    // Purge danmaku that are no longer visible
                    auto is_scroll_danmaku_out_fn = [&](uint64_t appear_t, float item_width) {
                        auto past_dis = (cur_p - appear_t) * this->danmaku_scroll_speed / 1000;
                        return this->vp_width - past_dis + item_width < 0;
                    };
                    std::erase_if(this->active_dm_items, [&](DanmakuItem const& item) {
                        if (item.slot_type == SlotType::SlotScroll) {
                            /*auto past_dis = (cur_p - item.appear_t) * danmaku_scroll_speed / 1000;
                            return vp_width - past_dis + item.width < 0;*/
                            return is_scroll_danmaku_out_fn(item.appear_t, item.width);
                        }
                        else {
                            return cur_p - item.appear_t > this->danmaku_still_duration;
                        }
                    });
                    // Scan danmaku within time range [last_update_t, cur_p)
                    auto it = std::lower_bound(
                        danmaku_col->m_normal_items.begin(), danmaku_col->m_normal_items.end(),
                        last_update_t, VideoDanmakuNormalItemContainer_AppearTimePred{});
                    auto ie = std::lower_bound(
                        danmaku_col->m_normal_items.begin(), danmaku_col->m_normal_items.end(),
                        cur_p, VideoDanmakuNormalItemContainer_AppearTimePred{});
                    for (; it != ie; it++) {
                        // Cache danmaku layout
                        if (!it->cache_valid) {
                            auto txt_layout = create_text_layout(
                                it->data.content,
                                L"Microsoft YaHei",
                                it->data.font_size,
                                DWRITE_FONT_WEIGHT_BOLD
                            );
                            DWRITE_TEXT_METRICS1 txt_metrics;
                            check_hresult(txt_layout->GetMetrics(&txt_metrics));
                            it->width = txt_metrics.width;
                            it->height = txt_metrics.height;
                            it->cache_valid = true;
                        }
                        // Danmaku is empty, ignore it
                        if (it->width == 0) {
                            continue;
                        }
                        // Put danmaku into slot
                        SlotInfo* slot_ptr{ nullptr };
                        DanmakuItem dm_item;
                        auto fill_normal_danmaku_item_fn = [&] {
                            // TODO: Correctly account for text outline
                            float padding_size = 4.0f;
                            dm_item.cache_bmp_offset.x = std::roundf(padding_size * this->panel_scale_x);
                            dm_item.cache_bmp_offset.y = std::roundf(padding_size * this->panel_scale_y);
                            dm_item.width = it->width;
                            dm_item.height = it->height;
                            dm_item.appear_t = it->data.appear_time;
                            com_ptr<ID2D1BitmapRenderTarget> bmp_target;
                            check_hresult(this->d2d1_dev_ctx->CreateCompatibleRenderTarget(
                                { it->width + padding_size * 2, it->height + padding_size * 2 },
                                bmp_target.put()
                            ));
                            bmp_target->BeginDraw();
                            bmp_target->Clear();
                            auto txt_layout = create_text_layout(
                                it->data.content,
                                L"Microsoft YaHei",
                                it->data.font_size,
                                DWRITE_FONT_WEIGHT_BOLD
                            );
                            auto txt_geom = create_mono_text_geometry(this->d2d1_factory, txt_layout);
                            auto stroke_style_props = D2D1::StrokeStyleProperties();
                            // NOTE: Miter line join will incorrectly render strokes of "蝉", etc.
                            stroke_style_props.lineJoin = D2D1_LINE_JOIN_ROUND;
                            com_ptr<ID2D1StrokeStyle> stroke_style;
                            check_hresult(this->d2d1_factory->CreateStrokeStyle(
                                stroke_style_props, nullptr, 0, stroke_style.put()));
                            com_ptr<ID2D1SolidColorBrush> solid_brush;
                            bmp_target->SetTransform(D2D1::Matrix3x2F::Translation(padding_size, padding_size));
                            check_hresult(bmp_target->CreateSolidColorBrush(
                                D2D1::ColorF(util::winrt::to_u32(
                                    util::winrt::get_contrast_white_black(it->data.color))),
                                solid_brush.put())
                            );
                            bmp_target->DrawGeometry(
                                txt_geom.get(),
                                solid_brush.get(),
                                2.0f,
                                stroke_style.get()
                            );
                            solid_brush->SetColor(D2D1::ColorF(util::winrt::to_u32(it->data.color)));
                            bmp_target->FillGeometry(txt_geom.get(), solid_brush.get());
                            // NOTE: Error checking is deliberately ignored
                            bmp_target->EndDraw();
                            // NOTE: Error checking is deliberately ignored
                            bmp_target->GetBitmap(dm_item.cache_bmp.put());
                            dm_item.cache_bmp_pixel_size = dm_item.cache_bmp->GetPixelSize();
                        };
                        // TODO: More danmaku mode
                        switch (it->data.mode) {
                        default:
                            // Not supported; display as scrolling danmaku
                            [[fallthrough]];
                        case VideoDanmakuDisplayMode::Scroll:
                            for (auto& slot : this->scroll_slots) {
                                if (it->data.appear_time > slot.occupied_until) {
                                    slot_ptr = &slot;
                                    break;
                                }
                            }
                            if (!slot_ptr) {
                                this->scroll_slots.push_back(
                                    { .height = scroll_slot_default_height });
                                slot_ptr = &this->scroll_slots.back();
                            }
                            slot_ptr->occupied_until = it->data.appear_time +
                                static_cast<uint64_t>(std::ceil(it->width * 1000 / danmaku_scroll_speed));
                            // Actually render only when danmaku is visible
                            if (!is_scroll_danmaku_out_fn(it->data.appear_time, it->width)) {
                                dm_item.slot_type = SlotType::SlotScroll;
                                dm_item.slot_i = static_cast<uint32_t>(slot_ptr - scroll_slots.data());
                                fill_normal_danmaku_item_fn();
                                this->active_dm_items.push_back(std::move(dm_item));
                            }
                            break;
                        case VideoDanmakuDisplayMode::Bottom:
                            for (auto& slot : this->bottom_slots) {
                                if (it->data.appear_time > slot.occupied_until) {
                                    slot_ptr = &slot;
                                    break;
                                }
                            }
                            if (!slot_ptr) {
                                this->bottom_slots.push_back({});
                                slot_ptr = &this->bottom_slots.back();
                            }
                            slot_ptr->height = it->height;
                            slot_ptr->occupied_until = it->data.appear_time + this->danmaku_still_duration;
                            // Actually render only when danmaku is visible
                            if (!(cur_p > slot_ptr->occupied_until)) {
                                dm_item.slot_type = SlotType::SlotBottom;
                                dm_item.slot_i = static_cast<uint32_t>(slot_ptr - bottom_slots.data());
                                fill_normal_danmaku_item_fn();
                                this->active_dm_items.push_back(std::move(dm_item));
                            }
                            break;
                        case VideoDanmakuDisplayMode::Top:
                            for (auto& slot : this->top_slots) {
                                if (it->data.appear_time > slot.occupied_until) {
                                    slot_ptr = &slot;
                                    break;
                                }
                            }
                            if (!slot_ptr) {
                                this->top_slots.push_back({});
                                slot_ptr = &this->top_slots.back();
                            }
                            slot_ptr->height = it->height;
                            slot_ptr->occupied_until = it->data.appear_time + this->danmaku_still_duration;
                            if (!(cur_p > slot_ptr->occupied_until)) {
                                dm_item.slot_type = SlotType::SlotTop;
                                dm_item.slot_i = static_cast<uint32_t>(slot_ptr - top_slots.data());
                                fill_normal_danmaku_item_fn();
                                this->active_dm_items.push_back(std::move(dm_item));
                            }
                            break;
                            /*
                        case VideoDanmakuDisplayMode::ReverseScroll:
                            break;
                            */
                        }
                    }
                    this->last_update_t = cur_p;
                }

                // NOTE: Caller must manually call BeginDraw / EndDraw
                void draw_active_danmaku(uint64_t cur_p) {
                    // TODO: Maybe we should manually manage a large atlas bitmap?
                    //this->d2d1_dev_ctx->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
                    for (auto const& item : this->active_dm_items) {
                        D2D1_RECT_F dest_rt;
                        bool round_left{}, round_top{};
                        if (item.slot_type == SlotType::SlotScroll) {
                            dest_rt.left = this->vp_width -
                                (cur_p - item.appear_t) * this->danmaku_scroll_speed / 1000;
                            dest_rt.top = std::accumulate(
                                this->scroll_slots.begin(), this->scroll_slots.begin() + item.slot_i,
                                0.0f, [](float a, SlotInfo const& b) { return a + b.height; }
                            );
                            round_top = true;
                        }
                        else if (item.slot_type == SlotType::SlotBottom) {
                            dest_rt.left = (this->vp_width - item.width) / 2;
                            dest_rt.top = this->vp_height - item.height - std::accumulate(
                                this->bottom_slots.begin(), this->bottom_slots.begin() + item.slot_i,
                                0.0f, [](float a, SlotInfo const& b) { return a + b.height; }
                            );
                            round_left = true;
                            round_top = true;
                        }
                        else if (item.slot_type == SlotType::SlotTop) {
                            dest_rt.left = (this->vp_width - item.width) / 2;
                            dest_rt.top = std::accumulate(
                                this->top_slots.begin(), this->top_slots.begin() + item.slot_i,
                                0.0f, [](float a, SlotInfo const& b) { return a + b.height; }
                            );
                            round_left = true;
                            round_top = true;
                        }
                        else {
                            dest_rt = { 0, 0, item.width, item.height };
                        }
                        dest_rt.left *= this->panel_scale_x;
                        dest_rt.top *= this->panel_scale_y;
                        if (round_left) { dest_rt.left = std::roundf(dest_rt.left); }
                        if (round_top) { dest_rt.top = std::roundf(dest_rt.top); }
                        dest_rt.left -= item.cache_bmp_offset.x;
                        dest_rt.top -= item.cache_bmp_offset.y;
                        dest_rt.right = dest_rt.left + item.cache_bmp_pixel_size.width;
                        dest_rt.bottom = dest_rt.top + item.cache_bmp_pixel_size.height;
                        this->d2d1_dev_ctx->DrawBitmap(item.cache_bmp.get(), &dest_rt);
                        /*this->d2d1_dev_ctx->DrawBitmap(item.cache_bmp.get(), &dest_rt,
                            1.0f, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);*/
                    }
                    //this->d2d1_dev_ctx->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                }

                com_ptr<IDWriteFactory3> const& const dwrite_factory{ get_global_dwrite_factory() };
                com_ptr<IDXGISwapChain2> swapchain{ nullptr };
                com_ptr<ID2D1Factory3> d2d1_factory{ nullptr };
                com_ptr<ID2D1Device2> d2d1_dev{ nullptr };
                com_ptr<ID2D1DeviceContext3> d2d1_dev_ctx{ nullptr };
                Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface swapchain_d3d_surface{ nullptr };
                com_ptr<IDXGISurface> swapchain_dxgi_surface;
                float vp_width{}, vp_height{};
                float panel_scale_x{}, panel_scale_y{};
                float danmaku_scroll_speed{ 200 };          // Unit: dip/s
                //float danmaku_scroll_speed{ 192 };          // Unit: dip/s
                uint32_t danmaku_still_duration{ 5000 };    // Unit: ms
                float scroll_slot_default_height{ [] {
                    // TODO: Use a better way to control scrolling slot height
                    auto& dwrite_factory = get_global_dwrite_factory();
                    com_ptr<IDWriteTextFormat1> txt_fmt;
                    com_ptr<IDWriteTextFormat> base_txt_fmt;
                    check_hresult(dwrite_factory->CreateTextFormat(
                        L"Microsoft YaHei",
                        nullptr,
                        DWRITE_FONT_WEIGHT_NORMAL,
                        DWRITE_FONT_STYLE_NORMAL,
                        DWRITE_FONT_STRETCH_NORMAL,
                        25.0f,
                        L"",
                        base_txt_fmt.put()
                    ));
                    base_txt_fmt.as(txt_fmt);
                    com_ptr<IDWriteTextLayout> base_txt_layout;
                    com_ptr<IDWriteTextLayout3> txt_layout;
                    std::wstring_view example_str = L"中文";
                    check_hresult(dwrite_factory->CreateTextLayout(
                        example_str.data(), example_str.length(),
                        txt_fmt.get(),
                        std::numeric_limits<float>::max(),
                        std::numeric_limits<float>::max(),
                        base_txt_layout.put()
                    ));
                    base_txt_layout.as(txt_layout);
                    DWRITE_TEXT_METRICS1 txt_metrics;
                    check_hresult(txt_layout->GetMetrics(&txt_metrics));
                    return txt_metrics.height;
                }() };
                // NOTE: Used to determine what to do with active items
                uint64_t last_update_t{ 0 };
                std::vector<DanmakuItem> active_dm_items;
                std::vector<SlotInfo> scroll_slots;
                std::vector<SlotInfo> bottom_slots, top_slots;
                uint64_t processed_dm_items_cnt{ 0 };

            private:
                // NOTE: Assuming only d2d1_dev is initialized
                void init_device_inner(
                    float width, float height,
                    float panel_scale_x, float panel_scale_y,
                    bool use_transparent_swapchain
                ) {
                    const auto dpi_x = 96 * panel_scale_x;
                    const auto dpi_y = 96 * panel_scale_y;
                    // Swap chain
                    com_ptr<IDXGIDevice> dxgi_dev;
                    check_hresult(this->d2d1_dev->GetDxgiDevice(dxgi_dev.put()));
                    com_ptr<IDXGIAdapter> dxgi_adapter;
                    check_hresult(dxgi_dev->GetAdapter(dxgi_adapter.put()));
                    com_ptr<IDXGIFactory2> dxgi_factory;
                    dxgi_factory.capture(dxgi_adapter, &IDXGIAdapter::GetParent);
                    DXGI_SWAP_CHAIN_DESC1 swap_chain_desc = { 0 };
                    swap_chain_desc.Width = size_dips_to_pixels(width, dpi_x);
                    swap_chain_desc.Height = size_dips_to_pixels(height, dpi_y);
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
                    base_dxgi_swapchain.as(this->swapchain);
                    DXGI_MATRIX_3X2_F inverse_scale = {
                        ._11 = 1.0f / panel_scale_x, ._22 = 1.0f / panel_scale_y,
                    };
                    this->swapchain->SetMatrixTransform(&inverse_scale);
                    // Device context
                    com_ptr<ID2D1DeviceContext1> base_d2d1_dev_ctx;
                    check_hresult(this->d2d1_dev->CreateDeviceContext(
                        D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                        base_d2d1_dev_ctx.put()
                    ));
                    base_d2d1_dev_ctx.as(this->d2d1_dev_ctx);
                    update_device_context_target(dpi_x, dpi_y);
                    this->d2d1_dev_ctx->SetDpi(dpi_x, dpi_y);
                    this->d2d1_dev_ctx->SetUnitMode(D2D1_UNIT_MODE_PIXELS);
                    // Factory
                    com_ptr<ID2D1Factory> base_factory;
                    this->d2d1_dev->GetFactory(base_factory.put());
                    base_factory.as(this->d2d1_factory);
                    // Set remaining data
                    this->vp_width = width;
                    this->vp_height = height;
                    this->panel_scale_x = panel_scale_x;
                    this->panel_scale_y = panel_scale_y;
                }
                // NOTE: Assuming swapchain & d2d1_dev_ctx are valid
                bool try_update_device_context_target(float dpi_x, float dpi_y) noexcept {
                    try { update_device_context_target(dpi_x, dpi_y); return true; }
                    catch (...) { util::winrt::log_current_exception(); return false; }
                }
                // NOTE: Assuming swapchain & d2d1_dev_ctx are valid
                void update_device_context_target(float dpi_x, float dpi_y) {
                    com_ptr<IDXGISurface> dxgi_surface;
                    dxgi_surface.capture(this->swapchain, &IDXGISwapChain1::GetBuffer, 0);
                    D2D1_BITMAP_PROPERTIES1 bmp_props = D2D1::BitmapProperties1(
                        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                        dpi_x, dpi_y
                    );
                    com_ptr<ID2D1Bitmap1> d2d1_bmp;
                    check_hresult(this->d2d1_dev_ctx->CreateBitmapFromDxgiSurface(
                        dxgi_surface.get(), bmp_props, d2d1_bmp.put()));
                    this->d2d1_dev_ctx->SetTarget(d2d1_bmp.get());
                    // NOTE: Surface references are also updated
                    check_hresult(CreateDirect3D11SurfaceFromDXGISurface(
                        dxgi_surface.get(),
                        reinterpret_cast<::IInspectable**>(put_abi(this->swapchain_d3d_surface))
                    ));
                    this->swapchain_dxgi_surface = std::move(dxgi_surface);
                }
                com_ptr<IDWriteTextLayout3> create_text_layout(
                    std::wstring_view str,
                    const wchar_t* font_family,
                    float font_size,
                    DWRITE_FONT_WEIGHT font_weight
                ) {
                    com_ptr<IDWriteTextFormat1> txt_fmt;
                    com_ptr<IDWriteTextFormat> base_txt_fmt;
                    check_hresult(this->dwrite_factory->CreateTextFormat(
                        font_family,
                        nullptr,
                        font_weight,
                        DWRITE_FONT_STYLE_NORMAL,
                        DWRITE_FONT_STRETCH_NORMAL,
                        font_size,
                        L"",
                        base_txt_fmt.put()
                    ));
                    base_txt_fmt.as(txt_fmt);
                    com_ptr<IDWriteTextLayout> base_txt_layout;
                    com_ptr<IDWriteTextLayout3> txt_layout;
                    check_hresult(this->dwrite_factory->CreateTextLayout(
                        str.data(), str.size(),
                        txt_fmt.get(),
                        std::numeric_limits<float>::max(),
                        std::numeric_limits<float>::max(),
                        base_txt_layout.put()
                    ));
                    base_txt_layout.as(txt_layout);
                    return txt_layout;
                }
            } draw_ctx;
            draw_ctx.reset();

            try {
                HRESULT hr;
                auto& dwrite_factory = get_global_dwrite_factory();
                while (true) {
                    if (work_item.Status() == AsyncStatus::Canceled) { break; }

                    {
                        std::scoped_lock guard(shared_data->mutex);

                        const auto dpi_x = 96 * shared_data->swapchain_panel_scale_x;
                        const auto dpi_y = 96 * shared_data->swapchain_panel_scale_y;
                        const auto vp_width = shared_data->swapchain_panel_width;
                        const auto vp_height = shared_data->swapchain_panel_height;
                        const auto vp_scalex = shared_data->swapchain_panel_scale_x;
                        const auto vp_scaley = shared_data->swapchain_panel_scale_y;

                        // Recreate device & swapchain if required
                        bool has_swapchain = static_cast<bool>(draw_ctx.swapchain);
                        bool recreate_swapchain_required = shared_data->should_recreate_swapchain;
                        bool resize_required = shared_data->should_resize_swapchain;
                        if (!has_swapchain || recreate_swapchain_required || resize_required) {
                            if (has_swapchain && !recreate_swapchain_required) {
                                // Resize swapchain
                                if (!draw_ctx.try_resize_view(vp_width, vp_height, vp_scalex, vp_scaley)) {
                                    draw_ctx.reset();
                                    continue;
                                }
                                shared_data->should_resize_swapchain = false;
                            }
                            else {
                                // Recreate swapchain
                                draw_ctx.reset();
                                draw_ctx.init_device_from_win2d_shared(
                                    vp_width, vp_height, vp_scalex, vp_scaley,
                                    shared_data->use_transparent_swapchain
                                );
                                draw_ctx.update_swap_chain_panel(shared_data->swapchain_panel);
                                shared_data->should_recreate_swapchain = false;
                                shared_data->should_resize_swapchain = false;
                            }
                        }

                        // Draw
                        shared_data->should_redraw.store(false);
                        auto& d2d1_dev_ctx = draw_ctx.d2d1_dev_ctx;
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

                            if (!shared_data->background_populator || !shared_data->background_populator_ready) {
                                d2d1_dev_ctx->Clear();
                            }
                            else {
                                shared_data->background_populator(draw_ctx.swapchain_d3d_surface);
                                shared_data->should_redraw.notify_one();
                            }

                            if (shared_data->show_danmaku.load()) {
                                draw_ctx.update_active_danmaku(shared_data->danmaku.get(), cur_p);
                                draw_ctx.draw_active_danmaku(cur_p);
                            }

                            if (shared_data->enable_debug_output.load()) {
                                d2d1_dev_ctx->SetTransform(D2D1::Matrix3x2F::Scale(vp_scalex, vp_scaley));
                                static auto text_fmt = [] {
                                    auto& dwrite_factory = get_global_dwrite_factory();
                                    com_ptr<IDWriteTextFormat1> txt_fmt;
                                    com_ptr<IDWriteTextFormat> base_txt_fmt;
                                    check_hresult(dwrite_factory->CreateTextFormat(
                                        L"Microsoft YaHei",
                                        nullptr,
                                        DWRITE_FONT_WEIGHT_NORMAL,
                                        DWRITE_FONT_STYLE_NORMAL,
                                        DWRITE_FONT_STRETCH_NORMAL,
                                        25.0f,
                                        L"",
                                        base_txt_fmt.put()
                                    ));
                                    base_txt_fmt.as(txt_fmt);
                                    return txt_fmt;
                                }();
                                com_ptr<ID2D1SolidColorBrush> white_brush;
                                check_hresult(d2d1_dev_ctx->CreateSolidColorBrush(
                                    D2D1::ColorF(D2D1::ColorF::White), white_brush.put()));
                                static unsigned cur_counter = 0;
                                static unsigned last_counter = 0;
                                static unsigned last_fps = 0;
                                static std::chrono::steady_clock::time_point last_tp{};
                                auto cur_tp = std::chrono::steady_clock::now();
                                if (cur_tp - last_tp >= std::chrono::milliseconds(999)) {
                                    last_tp = cur_tp;
                                    last_fps = cur_counter - last_counter;
                                    last_counter = cur_counter;
                                }
                                cur_counter++;
                                auto buf = std::format(L"n: {} (fps = {})\nt: {}ms\nisProactive: {}",
                                    cur_counter, last_fps, cur_p, shared_data->use_proactive_render_mode);
                                d2d1_dev_ctx->DrawText(
                                    buf.c_str(),
                                    buf.size(),
                                    text_fmt.get(),
                                    D2D1::RectF(0.0f, 30.0f, 500.0f, 300.0f),
                                    white_brush.get()
                                );
                                d2d1_dev_ctx->SetTransform(D2D1::Matrix3x2F::Identity());
                            }
                        }
                        hr = d2d1_dev_ctx->EndDraw();
                    }

                    // Final present
                    hr = draw_ctx.swapchain->Present(1, 0);
                    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
                        // Device has lost
                        draw_ctx.reset();
                    }
                    else { check_hresult(hr); }

                    // TODO: Change this
                    {
                        std::scoped_lock guard(shared_data->mutex);
                        // NOTE: When populator update inverval is the same as the swapchain,
                        //       enter reactive mode to prevent video frame from being dropped
                        bool proactive_redraw = shared_data->use_proactive_render_mode &&
                            (shared_data->is_playing &&
                            !shared_data->viewport_occluded &&
                            shared_data->show_danmaku.load());
                        if (proactive_redraw) {
                            shared_data->should_redraw.store(true);
                        }
                        else {
                            /*bool should_trim = !shared_data->use_proactive_render_mode &&
                                !shared_data->should_redraw.load();*/
                            bool should_trim = !shared_data->should_redraw.load();
                            if (should_trim) {
                                // Trim device resources
                                draw_ctx.d2d1_dev->ClearResources();
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
            shared_data->thread_running.notify_all();
            shared_data->should_redraw.store(false);
            shared_data->should_redraw.notify_all();
            util::debug::log_trace(L"VideoDanmakuControl Render Thread stopped");
        }, WorkItemPriority::Normal, WorkItemOptions::TimeSliced);
        m_shared_data->thread_running.store(true);
    }
    VideoDanmakuControl::~VideoDanmakuControl() {
        if (m_shared_data->thread_task) {
            m_shared_data->thread_task.Cancel();
            m_shared_data->request_redraw(true);
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
    void VideoDanmakuControl::EnableDebugOutput(bool value) {
        m_shared_data->enable_debug_output.store(value);
    }
    bool VideoDanmakuControl::EnableDebugOutput() {
        return m_shared_data->enable_debug_output.load();
    }
    void VideoDanmakuControl::Start() {
        std::scoped_lock guard(m_shared_data->mutex);
        if (m_shared_data->is_playing) { return; }
        m_shared_data->is_playing = true;
        m_shared_data->last_tp = std::chrono::steady_clock::now();
        m_shared_data->request_redraw_nolock();
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
            m_shared_data->request_redraw_nolock();
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
        m_shared_data->use_proactive_render_mode = handler ? isProactive : true;
        if (previous_transparent != current_transparent) {
            m_shared_data->use_transparent_swapchain = current_transparent;
            m_shared_data->should_recreate_swapchain = true;
        }
        m_shared_data->background_populator_ready = false;
    }
    void VideoDanmakuControl::TriggerBackgroundUpdate() {
        bool requested;
        {
            std::scoped_lock guard(m_shared_data->mutex);
            m_shared_data->background_populator_ready = true;
            requested = m_shared_data->request_redraw_nolock();
        }
        if (!requested) {
            Sleep(250);
            m_shared_data->request_redraw(true);
        }
        m_shared_data->should_redraw.wait(true);
    }
}
