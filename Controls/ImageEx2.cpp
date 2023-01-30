#include "pch.h"
#include "ImageEx2.h"
#include "ImageEx2Source.g.cpp"
#if __has_include("ImageEx2.g.cpp")
#include "ImageEx2.g.cpp"
#endif

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Storage::Streams;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Media::Imaging;

namespace winrt::BiliUWP::implementation {
    ImageEx2Source::ImageEx2Source() {
        m_bitmap_image.AutoPlay(false);
    }
    ImageEx2Source::~ImageEx2Source() {
        if (m_ev_download_progress) {
            m_bitmap_image.DownloadProgress(m_et_download_progress);
        }
    }
    IAsyncAction ImageEx2Source::SetSourceAsync(IRandomAccessStream const& streamSource) {
        auto op = m_bitmap_image.SetSourceAsync(streamSource);
        m_need_update_inner_source = false;
        m_ev_source_changed(*this, nullptr);
        return op;
    }
    void ImageEx2Source::EnableDelayedLoad(bool value) {
        SetValue(m_EnableDelayedLoadProperty, box_value(value));
    }
    bool ImageEx2Source::EnableDelayedLoad() {
        return winrt::unbox_value<bool>(GetValue(m_EnableDelayedLoadProperty));
    }
    void ImageEx2Source::UriSource(Uri const& value) {
        SetValue(m_UriSourceProperty, value);
    }
    Uri ImageEx2Source::UriSource() {
        return GetValue(m_UriSourceProperty).try_as<Uri>();
    }
    void ImageEx2Source::UriPreprocessor(IImageExUriPreprocessor const& value) {
        m_uri_preprocessor = value;
    }
    IImageExUriPreprocessor ImageEx2Source::UriPreprocessor() {
        return m_uri_preprocessor;
    }
    void ImageEx2Source::OnEnableDelayedLoadValueChanged(
        Windows::UI::Xaml::DependencyObject const& d,
        Windows::UI::Xaml::DependencyPropertyChangedEventArgs const& e
    ) {
        // TODO：Trigger refresh
        auto old_value = unbox_value<bool>(e.OldValue());
        auto new_value = unbox_value<bool>(e.NewValue());
        if (old_value == new_value) { return; }
    }
    void ImageEx2Source::OnUriSourceValueChanged(
        Windows::UI::Xaml::DependencyObject const& d,
        Windows::UI::Xaml::DependencyPropertyChangedEventArgs const& e
    ) {
        auto that = d.as<ImageEx2Source>();
        that->m_need_update_inner_source = true;
        that->m_ev_source_changed(*that, nullptr);
        if (that->EnableDelayedLoad()) { return; }
        that->UpdateInnerSource();
    }
    void ImageEx2Source::ProvideDimensionHint(
        uint32_t containerWidth, uint32_t containerHeight,
        Windows::UI::Xaml::Media::Stretch stretch
    ) {
        m_hint = { containerWidth, containerHeight, stretch };
    }
    BitmapImage ImageEx2Source::GetInnerSource() {
        return m_bitmap_image;
    }
    void ImageEx2Source::UpdateInnerSource() {
        if (!m_need_update_inner_source) { return; }
        m_need_update_inner_source = false;
        auto is_http_uri_fn = [](Uri const& uri) {
            auto scheme = uri.SchemeName();
            return scheme == L"http" || scheme == L"https";
        };
        m_async.cancel_running();
        auto uri = UriSource();
        if (uri && m_uri_preprocessor) {
            uri = m_uri_preprocessor.ProcessUri(
                uri,
                m_hint.container_width, m_hint.container_height,
                m_hint.stretch
            );
        }
        if (uri && is_http_uri_fn(uri) && m_use_http_cache) {
            m_async.cancel_and_run([](ImageEx2Source* that, Uri const& uri) -> IAsyncAction {
                auto weak_store = util::winrt::make_weak_storage(*that);
                auto op = ::BiliUWP::get_image_ex_http_cache().fetch_as_local_uri_async(uri);
                auto eargs = make_self<ImageExDownloadProgressEventArgs>();
                op.Progress([&](auto const&, Windows::Web::Http::HttpProgress const& progress) {
                    eargs->update_data(progress);
                    that->m_ev_download_progress(*that, eargs.as<BiliUWP::ImageExDownloadProgressEventArgs>());
                });
                weak_store.lock();
                auto uri_src = co_await std::move(op);
                weak_store.unlock();
                if (weak_store.lock()) {
                    that->m_bitmap_image.UriSource(uri_src);
                }
            }, this, uri);
        }
        else {
            m_bitmap_image.UriSource(uri);
        }
    }
    void ImageEx2Source::OnInnerDownloadProgress(IInspectable const&, DownloadProgressEventArgs const& e) {
        auto eargs = make<ImageExDownloadProgressEventArgs>();
        get_self<ImageExDownloadProgressEventArgs>(eargs)->update_data(
            static_cast<uint32_t>(e.Progress()), 100);
        m_ev_download_progress(*this, eargs);
    }

    ImageEx2::ImageEx2() {
        Loaded([this](auto&&, auto&&) {
            auto source = Source();
            if (!source) { return; }
            get_self<ImageEx2Source>(source)->UpdateInnerSource();
        });
        RegisterPropertyChangedCallback(Windows::UI::Xaml::Controls::Control::BorderThicknessProperty(),
            [](DependencyObject const& sender, DependencyProperty const& dp) {
                auto that = sender.as<ImageEx2>();
                auto thickness = that->BorderThickness();
                thickness.Left = -thickness.Left;
                thickness.Top = -thickness.Top;
                thickness.Right = -thickness.Right;
                thickness.Bottom = -thickness.Bottom;
                that->InnerImage().Margin(thickness);
            }
        );
        DragStarting([this](auto&&, DragStartingEventArgs const& e) {
            // TODO: Maybe improve image drag handling
            using namespace Windows::ApplicationModel::DataTransfer;
            using namespace Windows::Storage;
            auto source = Source();
            if (!source) { return; }
            auto uri = get_self<ImageEx2Source>(source)->UriSource();
            if (!uri) { return; }
            e.AllowedOperations(DataPackageOperation::Copy);
            auto data_pkg = e.Data();
            if (util::winrt::is_web_link_uri(uri)) {
                data_pkg.SetWebLink(uri);
                /*auto html = HtmlFormatHelper::CreateHtmlFormat(std::format(
                    L"<img src='{}'>", uri));
                data_pkg.SetHtmlFormat(html);*/
                data_pkg.SetDataProvider(StandardDataFormats::StorageItems(),
                    [uri](DataProviderRequest request) -> fire_forget_except {
                        co_safe_capture(uri);
                        auto deferral = request.GetDeferral();
                        deferred([&] { deferral.Complete(); });
                        auto local_uri_path = (co_await ::BiliUWP::get_image_ex_http_cache()
                            .fetch_as_local_uri_async(uri)).Path();
                        std::wstring local_path{ std::wstring_view(local_uri_path).substr(1) };
                        for (auto& ch : local_path) {
                            if (ch == L'/') { ch = L'\\'; }
                        }
                        std::vector files{ co_await StorageFile::GetFileFromPathAsync(local_path) };
                        request.SetData(single_threaded_vector(std::move(files)));
                    }
                );
            }
            else {
                data_pkg.SetApplicationLink(uri);
            }
        });
    }
    ImageEx2::~ImageEx2() {
        if (auto source = Source()) {
            get_self<ImageEx2Source>(source)->SourceChanged(m_et_source_changed);
        }
    }
    void ImageEx2::Source(BiliUWP::ImageEx2Source const& value) {
        SetValue(m_SourceProperty, value);
    }
    BiliUWP::ImageEx2Source ImageEx2::Source() {
        return GetValue(m_SourceProperty).try_as<BiliUWP::ImageEx2Source>();
    }
    void ImageEx2::Stretch(Windows::UI::Xaml::Media::Stretch const& value) {
        SetValue(m_StretchProperty, box_value(value));
    }
    Windows::UI::Xaml::Media::Stretch ImageEx2::Stretch() {
        return winrt::unbox_value<Windows::UI::Xaml::Media::Stretch>(GetValue(m_StretchProperty));
    }
    void ImageEx2::OnSourceValueChanged(
        DependencyObject const& d,
        DependencyPropertyChangedEventArgs const& e
    ) {
        auto that = d.as<ImageEx2>().get();
        if (auto old_value = e.OldValue()) {
            auto source = old_value.as<ImageEx2Source>();
            source->SourceChanged(that->m_et_source_changed);
        }
        if (auto new_value = e.NewValue()) {
            auto source = new_value.as<ImageEx2Source>();
            if (that->IsLoaded()) {
                source->UpdateInnerSource();
            }
            that->InnerImage().Source(source->GetInnerSource());
            that->m_et_source_changed = source->SourceChanged([](BiliUWP::ImageEx2Source const& sender, auto&&) {
                get_self<ImageEx2Source>(sender)->UpdateInnerSource();
            });
        }
        else {
            that->InnerImage().Source(nullptr);
        }
    }
    void ImageEx2::OnStretchValueChanged(
        DependencyObject const& d,
        DependencyPropertyChangedEventArgs const& e
    ) {
        // TODO：Trigger source refresh
        auto old_value = unbox_value<Windows::UI::Xaml::Media::Stretch>(e.OldValue());
        auto new_value = unbox_value<Windows::UI::Xaml::Media::Stretch>(e.NewValue());
        if (old_value == new_value) { return; }
    }
    Size ImageEx2::MeasureOverride(Size const& availableSize) {
        auto source = Source().try_as<ImageEx2Source>();
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
        return base_type::MeasureOverride(availableSize);
    }
    void ImageEx2::update_source_hint(
        BiliUWP::ImageEx2Source const& source,
        uint32_t container_width,
        uint32_t container_height,
        Windows::UI::Xaml::Media::Stretch stretch
    ) {
        if (!source) { return; }
        update_source_hint(get_self<ImageEx2Source>(source), container_width, container_height, stretch);
    }
    void ImageEx2::update_source_hint(
        ImageEx2Source* source,
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

#define gen_dp_instantiation_self_type ImageEx2Source
    gen_dp_instantiation(EnableDelayedLoad, box_value(true),
        PropertyChangedCallback{ &ImageEx2Source::OnEnableDelayedLoadValueChanged });
    gen_dp_instantiation(UriSource, nullptr,
        PropertyChangedCallback{ &ImageEx2Source::OnUriSourceValueChanged });
#undef gen_dp_instantiation_self_type

#define gen_dp_instantiation_self_type ImageEx2
    gen_dp_instantiation(Source, nullptr,
        PropertyChangedCallback{ &ImageEx2::OnSourceValueChanged });
    gen_dp_instantiation(Stretch, box_value(Windows::UI::Xaml::Media::Stretch::Uniform),
        PropertyChangedCallback{ &ImageEx2::OnStretchValueChanged });
#undef gen_dp_instantiation_self_type

#undef gen_dp_instantiation
}
