#pragma once

#include "ImageExFailedEventArgs.g.h"
#include "ImageExDownloadProgressEventArgs.g.h"
#include "ImageExSource.g.h"
#include "ImageExBrush.g.h"
#include "ImageEx.g.h"
#include "HttpCache.h"
#include "util.hpp"

namespace BiliUWP {
    // WARN: Muse be called ahead of any ImageEx API usage
    util::winrt::task<> init_image_ex_async();
    HttpCache get_image_ex_http_cache();
}

namespace winrt::BiliUWP::implementation {
    struct ImageExSource_CompositionSurface;
    struct ImageExSource;
    struct ImageExBrush;
    struct ImageEx;
    struct ImageEx2Source;

    struct RawPixelsData {
        com_array<uint8_t> data;
        uint32_t width, height;
    };

    struct ImageExDownloadProgressEventArgs : ImageExDownloadProgressEventArgsT<ImageExDownloadProgressEventArgs> {
        ImageExDownloadProgressEventArgs();
        uint64_t Current() { return m_current; }
        Windows::Foundation::IReference<uint64_t> Total() { return m_total; }
    private:
        friend struct ImageExSource;
        friend struct ImageEx2Source;

        void update_data(Windows::Web::Http::HttpProgress const& v);
        void update_data(uint64_t current, Windows::Foundation::IReference<uint64_t> const& total);

        uint64_t m_current{};
        Windows::Foundation::IReference<uint64_t> m_total;
    };
    struct ImageExFailedEventArgs : ImageExFailedEventArgsT<ImageExFailedEventArgs> {
        ImageExFailedEventArgs(hresult_error const& e);
        hresult Exception() { return m_exception; }
        hstring Message() { return m_message; }
    private:
        hresult m_exception;
        hstring m_message;
    };
    struct ImageExSource : ImageExSourceT<ImageExSource> {
        ImageExSource();
        ~ImageExSource();

        Windows::Foundation::IAsyncAction SetSourceAsync(
            Windows::Storage::Streams::IRandomAccessStream streamSource
        );

        void CacheOptions(ImageExCacheOptions const& value);
        ImageExCacheOptions CacheOptions();
        void EnableDelayedLoad(bool value);
        bool EnableDelayedLoad();
        void UriSource(Windows::Foundation::Uri const& value);
        Windows::Foundation::Uri UriSource();
        void UriPreprocessor(IImageExUriPreprocessor const& value);
        IImageExUriPreprocessor UriPreprocessor();

        event_token DownloadProgress(
            Windows::Foundation::TypedEventHandler<BiliUWP::ImageExSource, BiliUWP::ImageExDownloadProgressEventArgs> const& handler
        ) {
            return m_ev_download_progress.add(handler);
        }
        void DownloadProgress(event_token token) { m_ev_download_progress.remove(token); }
        event_token ImageOpened(
            Windows::Foundation::TypedEventHandler<BiliUWP::ImageExSource, Windows::Foundation::IInspectable> const& handler
        ) {
            return m_ev_image_opened.add(handler);
        }
        void ImageOpened(event_token token) { m_ev_image_opened.remove(token); }
        event_token ImageFailed(
            Windows::Foundation::TypedEventHandler<BiliUWP::ImageExSource, BiliUWP::ImageExFailedEventArgs> const& handler
        ) {
            return m_ev_image_failed.add(handler);
        }
        void ImageFailed(event_token token) { m_ev_image_failed.remove(token); }

        static void OnCacheOptionsValueChanged(
            Windows::UI::Xaml::DependencyObject const& d,
            Windows::UI::Xaml::DependencyPropertyChangedEventArgs const& e
        );
        static void OnEnableDelayedLoadValueChanged(
            Windows::UI::Xaml::DependencyObject const& d,
            Windows::UI::Xaml::DependencyPropertyChangedEventArgs const& e
        );
        static void OnUriSourceValueChanged(
            Windows::UI::Xaml::DependencyObject const& d,
            Windows::UI::Xaml::DependencyPropertyChangedEventArgs const& e
        );

        static Windows::UI::Xaml::DependencyProperty CacheOptionsProperty() { return m_CacheOptionsProperty; }
        static Windows::UI::Xaml::DependencyProperty EnableDelayedLoadProperty() { return m_EnableDelayedLoadProperty; }
        static Windows::UI::Xaml::DependencyProperty UriSourceProperty() { return m_UriSourceProperty; }

        static void final_release(std::unique_ptr<ImageExSource> ptr) noexcept;

    private:
        friend struct ImageExSource_CompositionSurface;
        friend struct ImageExBrush;
        friend struct ImageEx;

        /* NOTE: The following events may cause video to be invalidated:
        *    - Image source is changed
        *    - The video device was reset
        *  Owners are responsible for calling GetCompositionSurfaceAsync upon receiving the event
        *  to make sure video surface is up-to-date.
        */
        event_token VideoInvalidated(
            Windows::Foundation::TypedEventHandler<BiliUWP::ImageExSource, Windows::Foundation::IInspectable> const& handler
        ) {
            return m_ev_video_invalidated.add(handler);
        }
        void VideoInvalidated(event_token token) { m_ev_video_invalidated.remove(token); }

        // NOTE: Provided hints won't be honored after caching until the cache
        //       has been purged
        void ProvideDimensionHint(
            uint32_t containerWidth, uint32_t containerHeight,
            Windows::UI::Xaml::Media::Stretch stretch
        );
        Windows::Foundation::IAsyncOperation<Windows::UI::Composition::ICompositionSurface> GetCompositionSurfaceAsync();
        util::winrt::task<RawPixelsData> GetPixelsBufferAsync();
        uint32_t PhysicalWidth() { return m_physical_width; }
        uint32_t PhysicalHeight() { return m_physical_height; }

        bool try_lock_comp_surface(void);
        void unlock_comp_surface(void);
        Windows::Foundation::IAsyncAction initiate_load_to_memory_async(
            Windows::Storage::Streams::IRandomAccessStream stream
        );

        void update_video_is_dirty(bool new_value);

        IImageExUriPreprocessor m_uri_preprocessor{ nullptr };
        event<Windows::Foundation::TypedEventHandler<BiliUWP::ImageExSource, BiliUWP::ImageExDownloadProgressEventArgs>> m_ev_download_progress;
        event<Windows::Foundation::TypedEventHandler<BiliUWP::ImageExSource, Windows::Foundation::IInspectable>> m_ev_image_opened;
        event<Windows::Foundation::TypedEventHandler<BiliUWP::ImageExSource, BiliUWP::ImageExFailedEventArgs>> m_ev_image_failed;
        event<Windows::Foundation::TypedEventHandler<BiliUWP::ImageExSource, Windows::Foundation::IInspectable>> m_ev_video_invalidated;
        uint32_t m_physical_width{}, m_physical_height{};
        bool m_video_is_dirty{ true };
        Windows::Graphics::Imaging::BitmapDecoder m_bmp_decoder{ nullptr };
        weak_ref<ImageExSource_CompositionSurface> m_weak_comp_surface;
        com_ptr<ImageExSource_CompositionSurface> m_comp_surface;
        struct {
            uint32_t container_width, container_height;
            Windows::UI::Xaml::Media::Stretch stretch;
        } m_hint{};
        util::winrt::async_storage m_async;

        static Windows::UI::Xaml::DependencyProperty m_CacheOptionsProperty;
        static Windows::UI::Xaml::DependencyProperty m_EnableDelayedLoadProperty;
        static Windows::UI::Xaml::DependencyProperty m_UriSourceProperty;
    };
    struct ImageExBrush : ImageExBrushT<ImageExBrush> {
        ImageExBrush();
        ~ImageExBrush();
        void Source(BiliUWP::ImageExSource const& value);
        BiliUWP::ImageExSource Source();
        void Stretch(Windows::UI::Xaml::Media::Stretch const& value);
        Windows::UI::Xaml::Media::Stretch Stretch();

        void OnConnected();
        void OnDisconnected();

        static void OnSourceValueChanged(
            Windows::UI::Xaml::DependencyObject const& d,
            Windows::UI::Xaml::DependencyPropertyChangedEventArgs const& e
        );
        static void OnStretchValueChanged(
            Windows::UI::Xaml::DependencyObject const& d,
            Windows::UI::Xaml::DependencyPropertyChangedEventArgs const& e
        );

        static Windows::UI::Xaml::DependencyProperty SourceProperty() { return m_SourceProperty; }
        static Windows::UI::Xaml::DependencyProperty StretchProperty() { return m_StretchProperty; }

    private:
        friend struct ImageEx;

        static Windows::UI::Xaml::DependencyProperty m_SourceProperty;
        static Windows::UI::Xaml::DependencyProperty m_StretchProperty;

        void EnableHint(bool new_value) { m_enable_hint = new_value; }
        bool EnableHint() { return m_enable_hint; }

        void update_source_hint(BiliUWP::ImageExSource const& source, Windows::UI::Xaml::Media::Stretch stretch);
        void update_source_hint(ImageExSource* source, Windows::UI::Xaml::Media::Stretch stretch);

        util::winrt::async_storage m_async;
        event_token m_et_video_invalidated;
        bool m_connected = false;
        bool m_enable_hint = true;
        // TODO: Remove this workaround
        Windows::UI::Composition::ICompositionSurfaceFacade m_comp_surface_facade{ nullptr };
    };
    struct ImageEx : ImageExT<ImageEx> {
        ImageEx();
        ~ImageEx();

        void Source(BiliUWP::ImageExSource const& value);
        BiliUWP::ImageExSource Source();
        void Stretch(Windows::UI::Xaml::Media::Stretch const& value);
        Windows::UI::Xaml::Media::Stretch Stretch();

        static void OnSourceValueChanged(
            Windows::UI::Xaml::DependencyObject const& d,
            Windows::UI::Xaml::DependencyPropertyChangedEventArgs const& e
        );
        static void OnStretchValueChanged(
            Windows::UI::Xaml::DependencyObject const& d,
            Windows::UI::Xaml::DependencyPropertyChangedEventArgs const& e
        );

        static Windows::UI::Xaml::DependencyProperty SourceProperty() { return m_SourceProperty; }
        static Windows::UI::Xaml::DependencyProperty StretchProperty() { return m_StretchProperty; }

        void OnApplyTemplate();
        Windows::Foundation::Size MeasureOverride(Windows::Foundation::Size const& availableSize);
        Windows::Foundation::Size ArrangeOverride(Windows::Foundation::Size const& finalSize);

    private:
        static Windows::UI::Xaml::DependencyProperty m_SourceProperty;
        static Windows::UI::Xaml::DependencyProperty m_StretchProperty;

        void update_source_hint(
            BiliUWP::ImageExSource const& source,
            uint32_t container_width,
            uint32_t container_height,
            Windows::UI::Xaml::Media::Stretch stretch
        );
        void update_source_hint(
            ImageExSource* source,
            uint32_t container_width,
            uint32_t container_height,
            Windows::UI::Xaml::Media::Stretch stretch
        );

        event_token m_et_image_opened;
    };
}

namespace winrt::BiliUWP::factory_implementation {
    struct ImageExSource : ImageExSourceT<ImageExSource, implementation::ImageExSource> {};
    struct ImageExBrush : ImageExBrushT<ImageExBrush, implementation::ImageExBrush> {};
    struct ImageEx : ImageExT<ImageEx, implementation::ImageEx> {};
}
