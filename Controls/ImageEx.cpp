#include "pch.h"
#include "ImageEx.h"
#include "ImageExFailedEventArgs.g.cpp"
#include "ImageExDownloadProgressEventArgs.g.cpp"
#include "ImageExSource.g.cpp"
#include "ImageExBrush.g.cpp"
#if __has_include("ImageEx.g.cpp")
#include "ImageEx.g.cpp"
#endif

#include <winrt/Microsoft.Graphics.Canvas.h>
#include <winrt/Microsoft.Graphics.Canvas.UI.Composition.h>

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Storage::Streams;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Composition;
using namespace Windows::Graphics::Imaging;
using namespace Microsoft::Graphics::Canvas;
using namespace Microsoft::Graphics::Canvas::UI::Composition;

namespace BiliUWP {
    static HttpCache http_cache = nullptr;
    util::winrt::task<> init_image_ex_async() {
        static util::winrt::mutex s_mutex;
        co_await s_mutex.lock_async();
        deferred([] { s_mutex.unlock(); });
        if (!http_cache) {
            http_cache = co_await HttpCache::create_async(
                Windows::Storage::ApplicationData::Current().TemporaryFolder(),
                L"ImageExCache",
                nullptr
            );
        }
    }
    HttpCache get_image_ex_http_cache() {
        return http_cache;
    }
}

namespace winrt::BiliUWP::implementation {
    ImageExFailedEventArgs::ImageExFailedEventArgs(hresult_error const& e) :
        m_exception(e.code()), m_message(e.message()) {}
    ImageExDownloadProgressEventArgs::ImageExDownloadProgressEventArgs() {}
    void ImageExDownloadProgressEventArgs::update_data(Windows::Web::Http::HttpProgress const& v) {
        update_data(v.BytesReceived, v.TotalBytesToReceive);
    }
    void ImageExDownloadProgressEventArgs::update_data(uint64_t current, IReference<uint64_t> const& total) {
        m_current = current;
        m_total = total;
    }
    // NOTE: When there are no references to ImageExSource, the ownership will be passed
    //       to the associated ICompositionSurface (if there is one)
    struct ImageExSource_CompositionSurface :
        implements<ImageExSource_CompositionSurface, ICompositionSurface, ICompositionSurfaceFacade>
    {
        // TODO: Maybe write to the surface directly instead of creating intermediate bitmaps
        ImageExSource_CompositionSurface(ImageExSource* raw_src) :
            m_raw_src(raw_src), m_comp_surface(nullptr),
            m_canvas_device(CanvasDevice::GetSharedDevice()), m_graphics_device(nullptr)
        {
            m_graphics_device = CanvasComposition::CreateCompositionGraphicsDevice(
                Window::Current().Compositor(), m_canvas_device);
            m_et_device_lost = m_canvas_device.DeviceLost(
                { this, &ImageExSource_CompositionSurface::OnDeviceLost });
            m_comp_surface = m_graphics_device.CreateDrawingSurface(
                { 0, 0 },
                Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
                Windows::Graphics::DirectX::DirectXAlphaMode::Premultiplied
            );
        }
        ~ImageExSource_CompositionSurface() {
            m_canvas_device.DeviceLost(m_et_device_lost);
        }
        void OnDeviceLost(CanvasDevice const& sender, IInspectable const&) {
            m_canvas_device.DeviceLost(m_et_device_lost);
            m_canvas_device = CanvasDevice::GetSharedDevice();
            m_et_device_lost = m_canvas_device.DeviceLost(
                { this, &ImageExSource_CompositionSurface::OnDeviceLost });
            CanvasComposition::SetCanvasDevice(m_graphics_device, m_canvas_device);
            // Request redraw
            m_raw_src->update_video_is_dirty(true);
            /*[](ImageExSource_CompositionSurface* that) -> fire_forget_except {
                auto strong_this{ that->get_strong() };
                auto result = std::move(co_await that->m_raw_src->GetPixelsBufferAsync());
                that->CopyFromBytes(result.data, result.width, result.height);
            }(this);*/
        }
        void CopyFromBytes(array_view<uint8_t> bytes, uint32_t width, uint32_t height) {
            if (width == 0 || height == 0) {
                // An empty bitmap was provided; empty the surface
                m_comp_surface.Resize({ 0, 0 });
                return;
            }
            /*CanvasComposition::Resize(m_comp_surface,
                { static_cast<float>(width), static_cast<float>(height) });*/
            m_comp_surface.Resize({ static_cast<int32_t>(width), static_cast<int32_t>(height) });
            auto session = CanvasComposition::CreateDrawingSession(m_comp_surface);
            // TODO: Account for transparency (CanvasComposite?)
            auto canvas_bitmap = CanvasBitmap::CreateFromBytes(
                m_canvas_device,
                bytes,
                static_cast<int32_t>(width), static_cast<int32_t>(height),
                Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized
            );
            session.DrawImage(canvas_bitmap);
            std::tie(m_width, m_height) = std::tuple(width, height);
        }
        ICompositionSurface GetRealSurface() {
            return m_comp_surface;
        }

    private:
        friend struct ImageExSource;

        uint32_t GetPixelsWidth() { return m_width; }
        uint32_t GetPixelsHeight() { return m_height; }

        void set_ownership(std::unique_ptr<ImageExSource> ptr) {
            m_unique_src = std::move(ptr);
        }

        ImageExSource* m_raw_src;
        std::unique_ptr<ImageExSource> m_unique_src;
        CompositionDrawingSurface m_comp_surface;
        CanvasDevice m_canvas_device;
        CompositionGraphicsDevice m_graphics_device;
        event_token m_et_device_lost;
        uint32_t m_width{}, m_height{};
    };

    ImageExSource::ImageExSource() {}
    ImageExSource::~ImageExSource() {}
    IAsyncAction ImageExSource::SetSourceAsync(IRandomAccessStream streamSource) {
        m_async.cancel_running();
        UriSource(nullptr);
        m_bmp_decoder = nullptr;
        update_video_is_dirty(false);
        update_video_is_dirty(true);
        util::winrt::awaitable_event ae;
        m_async.run_if_idle([&](util::winrt::awaitable_event* ae) -> IAsyncAction {
            auto cancellation_token = co_await get_cancellation_token();
            cancellation_token.enable_propagation(true);
            deferred([&] { ae->set(); });
            co_await initiate_load_to_memory_async(streamSource);
        }, &ae);
        co_await ae;
    }
    void ImageExSource::final_release(std::unique_ptr<ImageExSource> ptr) noexcept {
        if (ptr->try_lock_comp_surface()) {
            auto comp_surface = std::move(ptr->m_comp_surface);
            comp_surface->set_ownership(std::move(ptr));
        }
    }
    void ImageExSource::CacheOptions(ImageExCacheOptions const& value) {
        SetValue(m_CacheOptionsProperty, box_value(value));
    }
    ImageExCacheOptions ImageExSource::CacheOptions() {
        return winrt::unbox_value<ImageExCacheOptions>(GetValue(m_CacheOptionsProperty));
    }
    void ImageExSource::EnableDelayedLoad(bool value) {
        SetValue(m_EnableDelayedLoadProperty, box_value(value));
    }
    bool ImageExSource::EnableDelayedLoad() {
        return winrt::unbox_value<bool>(GetValue(m_EnableDelayedLoadProperty));
    }
    void ImageExSource::UriSource(Uri const& value) {
        SetValue(m_UriSourceProperty, value);
    }
    Uri ImageExSource::UriSource() {
        return GetValue(m_UriSourceProperty).try_as<Uri>();
    }
    void ImageExSource::UriPreprocessor(IImageExUriPreprocessor const& value) {
        m_uri_preprocessor = value;
    }
    IImageExUriPreprocessor ImageExSource::UriPreprocessor() {
        return m_uri_preprocessor;
    }
    void ImageExSource::OnCacheOptionsValueChanged(
        DependencyObject const& d,
        DependencyPropertyChangedEventArgs const& e
    ) {
        // TODO：Trigger refresh
        auto old_value = unbox_value<ImageExCacheOptions>(e.OldValue());
        auto new_value = unbox_value<ImageExCacheOptions>(e.NewValue());
        if (old_value == new_value) { return; }
    }
    void ImageExSource::OnEnableDelayedLoadValueChanged(
        Windows::UI::Xaml::DependencyObject const& d,
        Windows::UI::Xaml::DependencyPropertyChangedEventArgs const& e
    ) {
        // TODO：Trigger refresh
        auto old_value = unbox_value<bool>(e.OldValue());
        auto new_value = unbox_value<bool>(e.NewValue());
        if (old_value == new_value) { return; }
    }
    void ImageExSource::OnUriSourceValueChanged(
        Windows::UI::Xaml::DependencyObject const& d,
        Windows::UI::Xaml::DependencyPropertyChangedEventArgs const& e
    ) {
        auto that = d.as<ImageExSource>();
        that->m_async.cancel_running();
        if (!e.NewValue()) { return; }
        that->m_bmp_decoder = nullptr;
        that->update_video_is_dirty(false);
        that->update_video_is_dirty(true);
        if (that->EnableDelayedLoad()) { return; }
        that->m_async.run_if_idle(&ImageExSource::initiate_load_to_memory_async, that, nullptr);
    }
    void ImageExSource::ProvideDimensionHint(
        uint32_t containerWidth, uint32_t containerHeight,
        Windows::UI::Xaml::Media::Stretch stretch
    ) {
        m_hint = { containerWidth, containerHeight, stretch };
    }
    IAsyncOperation<ICompositionSurface> ImageExSource::GetCompositionSurfaceAsync() {
        auto is_video_cache_enabled_fn = [this] {
            return static_cast<bool>(CacheOptions() & ImageExCacheOptions::EnableVideo);
        };
        if (!try_lock_comp_surface()) {
            // Allocate new composition surface
            m_comp_surface = make_self<ImageExSource_CompositionSurface>(this);
            m_weak_comp_surface = make_weak(m_comp_surface);
            // TODO: Maybe investigate the possibility of using LoadedImageSurface & BitmapEncoder?
        }
        if (m_video_is_dirty) {
            auto pixels_data = std::move(co_await GetPixelsBufferAsync());
            m_comp_surface->CopyFromBytes(pixels_data.data, pixels_data.width, pixels_data.height);
            update_video_is_dirty(false);
        }
        auto comp_surface = m_comp_surface.as<ICompositionSurface>();
        if (!is_video_cache_enabled_fn()) {
            m_comp_surface = nullptr;
        }
        co_return comp_surface;
    }
    util::winrt::task<RawPixelsData> ImageExSource::GetPixelsBufferAsync() try {
        auto is_memory_cache_enabled_fn = [this] {
            return static_cast<bool>(CacheOptions() & ImageExCacheOptions::EnableMemory);
        };
        auto should_keep_memory_cache_fn = [&] {
            if (!UriSource()) { return true; }
            return is_memory_cache_enabled_fn();
        };
        auto strong_this{ get_strong() };
        // Load image into memory
        if (!m_bmp_decoder) {
            apartment_context ui_ctx;
            util::winrt::awaitable_event ae;
            bool cancelled = false;
            m_async.cancel_and_run([this](util::winrt::awaitable_event* ae, bool* cancelled) -> IAsyncAction {
                auto cancellation_token = co_await get_cancellation_token();
                cancellation_token.enable_propagation(true);
                deferred([&] { ae->set(); });
                try { co_await initiate_load_to_memory_async(nullptr); }
                catch (winrt::hresult_canceled const&) { *cancelled = true; }
                catch (...) { util::winrt::log_current_exception(); }
            }, &ae, &cancelled);
            co_await ae;
            co_await ui_ctx;
            if (!m_bmp_decoder) {
                if (cancelled) {
                    throw hresult_canceled();
                }
                else {
                    // No source provided or load failed; return empty buffer
                    co_return{};
                }
            }
        }
        // Rescale pixels
        RawPixelsData result;
        PixelDataProvider pixel_provider{ nullptr };
        BitmapTransform bmp_transform;
        auto div_mul_fn = [](uint32_t a, uint32_t b, uint32_t c) {
            return static_cast<uint32_t>(std::lround(static_cast<double>(a) / b * c));
        };
        if (m_hint.container_width == 0 && m_hint.container_height == 0) {
            result.width = m_physical_width;
            result.height = m_physical_height;
        }
        else if (m_hint.container_width == 0) {
            result.height = m_hint.container_height;
            result.width = div_mul_fn(m_hint.container_height, m_physical_height, m_physical_width);
            bmp_transform.ScaledWidth(result.width);
            bmp_transform.ScaledHeight(result.height);
        }
        else if (m_hint.container_height == 0) {
            result.width = m_hint.container_width;
            result.height = div_mul_fn(m_hint.container_width, m_physical_width, m_physical_height);
            bmp_transform.ScaledWidth(result.width);
            bmp_transform.ScaledHeight(result.height);
        }
        else {
            using Windows::UI::Xaml::Media::Stretch;
            switch (m_hint.stretch) {
            case Stretch::None:
                // Identity
                result.width = m_physical_width;
                result.height = m_physical_height;
                break;
            case Stretch::Fill:
                // UniformToFill
                result.width = std::max(m_hint.container_width,
                    div_mul_fn(m_hint.container_height, m_physical_height, m_physical_width));
                result.height = std::max(m_hint.container_height,
                    div_mul_fn(m_hint.container_width, m_physical_width, m_physical_height));
                bmp_transform.ScaledWidth(result.width);
                bmp_transform.ScaledHeight(result.height);
                break;
            case Stretch::Uniform:
                // Uniform
                result.width = std::min(m_hint.container_width,
                    div_mul_fn(m_hint.container_height, m_physical_height, m_physical_width));
                result.height = std::min(m_hint.container_height,
                    div_mul_fn(m_hint.container_width, m_physical_width, m_physical_height));
                bmp_transform.ScaledWidth(result.width);
                bmp_transform.ScaledHeight(result.height);
                break;
            case Stretch::UniformToFill:
                // UniformToFill
                result.width = std::max(m_hint.container_width,
                    div_mul_fn(m_hint.container_height, m_physical_height, m_physical_width));
                result.height = std::max(m_hint.container_height,
                    div_mul_fn(m_hint.container_width, m_physical_width, m_physical_height));
                bmp_transform.ScaledWidth(result.width);
                bmp_transform.ScaledHeight(result.height);
                break;
            }
        }
        bmp_transform.InterpolationMode(BitmapInterpolationMode::Fant);
        {
            auto bmp_decoder = m_bmp_decoder;
            pixel_provider = co_await m_bmp_decoder.GetPixelDataAsync(
                BitmapPixelFormat::Bgra8,
                BitmapAlphaMode::Premultiplied,
                bmp_transform,
                ExifOrientationMode::IgnoreExifOrientation,
                ColorManagementMode::DoNotColorManage
            );
        }
        result.data = pixel_provider.DetachPixelData();

        if (!should_keep_memory_cache_fn()) {
            m_bmp_decoder = nullptr;
        }
        m_ev_image_opened(*this, nullptr);
        co_return std::move(result);
    }
    catch (winrt::hresult_error const& e) {
        m_ev_image_failed(*this, make<ImageExFailedEventArgs>(e));
        throw;
    }
    bool ImageExSource::try_lock_comp_surface(void) {
        if (!m_comp_surface) {
            m_comp_surface = m_weak_comp_surface.get();
        }
        return static_cast<bool>(m_comp_surface);
    }
    void ImageExSource::unlock_comp_surface(void) {
        m_comp_surface = nullptr;
    }
    IAsyncAction ImageExSource::initiate_load_to_memory_async(IRandomAccessStream stream) {
        auto is_local_cache_enabled_fn = [this] {
            return static_cast<bool>(CacheOptions() & ImageExCacheOptions::EnableLocal);
        };
        auto is_http_uri_fn = [](Uri const& uri) {
            auto scheme = uri.SchemeName();
            return scheme == L"http" || scheme == L"https";
        };
        if (!stream) {
            // Try to load from uri instead
            auto uri = UriSource();
            if (uri && m_uri_preprocessor) {
                uri = m_uri_preprocessor.ProcessUri(
                    uri,
                    m_hint.container_width, m_hint.container_height,
                    m_hint.stretch
                );
            }
            if (!uri) {
                // No source proivded, do nothing
                co_return;
            }
            if (is_http_uri_fn(uri) && is_local_cache_enabled_fn()) {
                auto op = ::BiliUWP::get_image_ex_http_cache().fetch_async(uri);
                auto eargs = make_self<ImageExDownloadProgressEventArgs>();
                op.Progress([&](auto const&, Windows::Web::Http::HttpProgress const& progress) {
                    eargs->update_data(progress);
                    m_ev_download_progress(*this, eargs.as<BiliUWP::ImageExDownloadProgressEventArgs>());
                });
                stream = co_await std::move(op);
            }
            else {
                stream = co_await RandomAccessStreamReference::CreateFromUri(uri).OpenReadAsync();
            }
        }
        m_bmp_decoder = co_await BitmapDecoder::CreateAsync(stream);
        m_physical_width = m_bmp_decoder.PixelWidth();
        m_physical_height = m_bmp_decoder.PixelHeight();
    }
    void ImageExSource::update_video_is_dirty(bool new_value) {
        if (m_video_is_dirty != new_value) {
            m_video_is_dirty = new_value;
            if (new_value) {
                m_ev_video_invalidated(*this, nullptr);
            }
        }
    }

    ImageExBrush::ImageExBrush() {}
    ImageExBrush::~ImageExBrush() {
        if (auto source = Source()) {
            get_self<ImageExSource>(source)->VideoInvalidated(m_et_video_invalidated);
        }
    }
    void ImageExBrush::Source(BiliUWP::ImageExSource const& value) {
        SetValue(m_SourceProperty, value);
    }
    BiliUWP::ImageExSource ImageExBrush::Source() {
        return GetValue(m_SourceProperty).try_as<BiliUWP::ImageExSource>();
    }
    void ImageExBrush::Stretch(Windows::UI::Xaml::Media::Stretch const& value) {
        SetValue(m_StretchProperty, box_value(value));
    }
    Windows::UI::Xaml::Media::Stretch ImageExBrush::Stretch() {
        return winrt::unbox_value<Windows::UI::Xaml::Media::Stretch>(GetValue(m_StretchProperty));
    }
    void ImageExBrush::OnConnected() {
        m_connected = true;
        if (CompositionBrush()) { return; }
        if (!Source()) { return; }
        m_async.cancel_and_run([](ImageExBrush* that) -> IAsyncAction {
            auto cancellation_token = co_await get_cancellation_token();
            cancellation_token.enable_propagation();
            auto strong_this{ that->get_strong() };
            auto comp_surface = co_await that->Source().as<ImageExSource>()->GetCompositionSurfaceAsync();
            auto compositor = Window::Current().Compositor();
            // TODO: Remove this workaround (ICompositionSurfaceFacade is only supported on Windows 11)
            that->m_comp_surface_facade = comp_surface.try_as<ICompositionSurfaceFacade>();
            auto comp_brush = compositor.CreateSurfaceBrush(that->m_comp_surface_facade ?
                that->m_comp_surface_facade.GetRealSurface() : comp_surface
            );
            //auto comp_brush = compositor.CreateSurfaceBrush(comp_surface);
            that->CompositionBrush(comp_brush);
        }, this);
    }
    void ImageExBrush::OnDisconnected() {
        m_connected = false;
        m_async.cancel_running();
        CompositionBrush(nullptr);
        m_comp_surface_facade = nullptr;
    }
    void ImageExBrush::OnSourceValueChanged(
        DependencyObject const& d,
        DependencyPropertyChangedEventArgs const& e
    ) {
        auto that = d.as<ImageExBrush>().get();
        if (auto old_value = e.OldValue()) {
            old_value.as<ImageExSource>()->VideoInvalidated(that->m_et_video_invalidated);
        }
        auto reconnect_fn = [](ImageExBrush* that) {
            auto need_reconnect = that->m_connected;
            that->OnDisconnected();
            if (need_reconnect) {
                that->OnConnected();
            }
        };
        if (auto new_value = e.NewValue()) {
            auto img_src = new_value.as<ImageExSource>();
            that->update_source_hint(img_src.get(), that->Stretch());
            that->m_et_video_invalidated = img_src->VideoInvalidated(
                [=](BiliUWP::ImageExSource const&, IInspectable const&) {
                    reconnect_fn(that);
                }
            );
        }
        reconnect_fn(that);
    }
    void ImageExBrush::OnStretchValueChanged(
        DependencyObject const& d,
        DependencyPropertyChangedEventArgs const& e
    ) {
        auto old_value = unbox_value<Windows::UI::Xaml::Media::Stretch>(e.OldValue());
        auto new_value = unbox_value<Windows::UI::Xaml::Media::Stretch>(e.NewValue());
        if (old_value == new_value) { return; }
        auto that = d.as<ImageExBrush>();
        that->update_source_hint(that->Source(), new_value);
    }
    void ImageExBrush::update_source_hint(
        BiliUWP::ImageExSource const& source, Windows::UI::Xaml::Media::Stretch stretch
    ) {
        if (!source) { return; }
        update_source_hint(get_self<ImageExSource>(source), stretch);
    }
    void ImageExBrush::update_source_hint(ImageExSource* source, Windows::UI::Xaml::Media::Stretch stretch) {
        if (!m_enable_hint) { return; }
        if (!source) { return; }
        source->ProvideDimensionHint(0, 0, stretch);
    }

    ImageEx::ImageEx() {}
    ImageEx::~ImageEx() {
        if (auto source = Source()) {
            get_self<ImageExSource>(source)->ImageOpened(m_et_image_opened);
        }
    }
    void ImageEx::Source(BiliUWP::ImageExSource const& value) {
        SetValue(m_SourceProperty, value);
    }
    BiliUWP::ImageExSource ImageEx::Source() {
        return GetValue(m_SourceProperty).try_as<BiliUWP::ImageExSource>();
    }
    void ImageEx::Stretch(Windows::UI::Xaml::Media::Stretch const& value) {
        SetValue(m_StretchProperty, box_value(value));
    }
    Windows::UI::Xaml::Media::Stretch ImageEx::Stretch() {
        return winrt::unbox_value<Windows::UI::Xaml::Media::Stretch>(GetValue(m_StretchProperty));
    }
    void ImageEx::OnSourceValueChanged(
        DependencyObject const& d,
        DependencyPropertyChangedEventArgs const& e
    ) {
        auto that = d.as<ImageEx>().get();
        if (auto old_value = e.OldValue()) {
            auto source = old_value.as<ImageExSource>();
            source->ImageOpened(that->m_et_image_opened);
        }
        auto relayout_fn = [](ImageEx* that) {
            that->InvalidateMeasure();
        };
        if (auto new_value = e.NewValue()) {
            auto source = new_value.as<ImageExSource>();
            that->m_et_image_opened = source->ImageOpened(
                [=](BiliUWP::ImageExSource const&, IInspectable const&) {
                    relayout_fn(that);
                }
            );
        }
        relayout_fn(that);
    }
    void ImageEx::OnStretchValueChanged(
        DependencyObject const& d,
        DependencyPropertyChangedEventArgs const& e
    ) {
        // TODO：Trigger source refresh
        auto old_value = unbox_value<Windows::UI::Xaml::Media::Stretch>(e.OldValue());
        auto new_value = unbox_value<Windows::UI::Xaml::Media::Stretch>(e.NewValue());
        if (old_value == new_value) { return; }
    }
    void ImageEx::OnApplyTemplate() {
        LayoutRoot().Background().as<ImageExBrush>()->EnableHint(false);
    }
    Size ImageEx::MeasureOverride(Size const& availableSize) {
        auto source = Source().try_as<ImageExSource>();
        if (!source) { return {}; }
        {
            auto dpi_scale = Windows::Graphics::Display::DisplayInformation::GetForCurrentView()
                .LogicalDpi() / 96;
            // TODO: Maybe improve logic to make dimension more exact
            auto get_n_extent_fn = [&](float extent) -> uint32_t {
                if (std::isinf(extent)) { return 0; }
                return static_cast<uint32_t>(std::lround(extent * dpi_scale));
            };
            update_source_hint(
                source.get(),
                get_n_extent_fn(availableSize.Width),
                get_n_extent_fn(availableSize.Height),
                Stretch()
            );
        }
        auto physical_width = source->PhysicalWidth();
        auto physical_height = source->PhysicalHeight();
        if (physical_width == 0 || physical_height == 0) { return {}; }
        Size final_size{
            std::min(availableSize.Width, availableSize.Height / physical_height * physical_width),
            std::min(availableSize.Height, availableSize.Width / physical_width * physical_height),
        };
        return final_size;
    }
    Size ImageEx::ArrangeOverride(Size const& finalSize) {
        return base_type::ArrangeOverride(finalSize);
    }
    void ImageEx::update_source_hint(
        BiliUWP::ImageExSource const& source,
        uint32_t container_width,
        uint32_t container_height,
        Windows::UI::Xaml::Media::Stretch stretch
    ) {
        if (!source) { return; }
        update_source_hint(get_self<ImageExSource>(source), container_width, container_height, stretch);
    }
    void ImageEx::update_source_hint(
        ImageExSource* source,
        uint32_t container_width,
        uint32_t container_height,
        Windows::UI::Xaml::Media::Stretch stretch
    ) {
        if (!source) { return; }
        source->ProvideDimensionHint(container_width, container_height, stretch);
    }

#define gen_dp_instantiation(prop_name, ...)                                                \
    DependencyProperty gen_dp_instantiation_self_type::m_ ## prop_name ## Property =        \
        DependencyProperty::Register(                                                       \
            L"" #prop_name,                                                                 \
            winrt::xaml_typename<                                                           \
                decltype(std::declval<gen_dp_instantiation_self_type>().prop_name())        \
            >(),                                                                            \
            winrt::xaml_typename<winrt::BiliUWP::gen_dp_instantiation_self_type>(),         \
            Windows::UI::Xaml::PropertyMetadata{ __VA_ARGS__ }                              \
    )

#define gen_dp_instantiation_self_type ImageExSource
    gen_dp_instantiation(CacheOptions,
        box_value(ImageExCacheOptions::EnableLocal |
            ImageExCacheOptions::EnableMemory |
            ImageExCacheOptions::EnableVideo),
        PropertyChangedCallback{ &ImageExSource::OnCacheOptionsValueChanged });
    gen_dp_instantiation(EnableDelayedLoad, box_value(true),
        PropertyChangedCallback{ &ImageExSource::OnEnableDelayedLoadValueChanged });
    gen_dp_instantiation(UriSource, nullptr,
        PropertyChangedCallback{ &ImageExSource::OnUriSourceValueChanged });
#undef gen_dp_instantiation_self_type

#define gen_dp_instantiation_self_type ImageExBrush
    gen_dp_instantiation(Source, nullptr,
        PropertyChangedCallback{ &ImageExBrush::OnSourceValueChanged });
    gen_dp_instantiation(Stretch, box_value(Windows::UI::Xaml::Media::Stretch::Uniform),
        PropertyChangedCallback{ &ImageExBrush::OnStretchValueChanged });
#undef gen_dp_instantiation_self_type

#define gen_dp_instantiation_self_type ImageEx
    gen_dp_instantiation(Source, nullptr,
        PropertyChangedCallback{ &ImageEx::OnSourceValueChanged });
    gen_dp_instantiation(Stretch, box_value(Windows::UI::Xaml::Media::Stretch::Uniform),
        PropertyChangedCallback{ &ImageEx::OnStretchValueChanged });
#undef gen_dp_instantiation_self_type

#undef gen_dp_instantiation
}
