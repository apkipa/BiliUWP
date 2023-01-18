#pragma once

#include "ImageEx.h"
#include "ImageEx2Source.g.h"
#include "ImageEx2.g.h"

namespace winrt::BiliUWP::implementation {
    struct ImageEx2;

    struct ImageEx2Source : ImageEx2SourceT<ImageEx2Source> {
        ImageEx2Source();
        ~ImageEx2Source();

        Windows::Foundation::IAsyncAction SetSourceAsync(
            Windows::Storage::Streams::IRandomAccessStream const& streamSource
        );

        void EnableDelayedLoad(bool value);
        bool EnableDelayedLoad();
        void UriSource(Windows::Foundation::Uri const& value);
        Windows::Foundation::Uri UriSource();
        void UriPreprocessor(IImageExUriPreprocessor const& value);
        IImageExUriPreprocessor UriPreprocessor();

        event_token DownloadProgress(
            Windows::Foundation::TypedEventHandler<BiliUWP::ImageEx2Source, BiliUWP::ImageExDownloadProgressEventArgs> const& handler
        ) {
            if (!m_ev_download_progress) {
                m_et_download_progress = m_bitmap_image.DownloadProgress(
                    { get_weak(), &ImageEx2Source::OnInnerDownloadProgress });
            }
            return m_ev_download_progress.add(handler);
        }
        void DownloadProgress(event_token token) {
            m_ev_download_progress.remove(token);
            if (!m_ev_download_progress) {
                m_bitmap_image.DownloadProgress(m_et_download_progress);
            }
        }
        event_token ImageOpened(Windows::UI::Xaml::RoutedEventHandler const& handler) {
            return m_bitmap_image.ImageOpened([weak = get_weak(), handler](auto&&, auto&& e) {
                if (auto strong = weak.get()) { handler(*strong.get(), e); }
            });
        }
        void ImageOpened(event_token token) { m_bitmap_image.ImageOpened(token); }
        event_token ImageFailed(Windows::UI::Xaml::ExceptionRoutedEventHandler const& handler) {
            return m_bitmap_image.ImageFailed([weak = get_weak(), handler](auto&&, auto&& e) {
                if (auto strong = weak.get()) { handler(*strong.get(), e); }
            });
        }
        void ImageFailed(event_token token) { m_bitmap_image.ImageFailed(token); }

        static void OnEnableDelayedLoadValueChanged(
            Windows::UI::Xaml::DependencyObject const& d,
            Windows::UI::Xaml::DependencyPropertyChangedEventArgs const& e
        );
        static void OnUriSourceValueChanged(
            Windows::UI::Xaml::DependencyObject const& d,
            Windows::UI::Xaml::DependencyPropertyChangedEventArgs const& e
        );

        static Windows::UI::Xaml::DependencyProperty EnableDelayedLoadProperty() { return m_EnableDelayedLoadProperty; }
        static Windows::UI::Xaml::DependencyProperty UriSourceProperty() { return m_UriSourceProperty; }

    private:
        friend struct ImageEx2;

        void ProvideDimensionHint(
            uint32_t containerWidth, uint32_t containerHeight,
            Windows::UI::Xaml::Media::Stretch stretch
        );
        Windows::UI::Xaml::Media::Imaging::BitmapImage GetInnerSource();
        void UpdateInnerSource();
        void OnInnerDownloadProgress(
            Windows::Foundation::IInspectable const&,
            Windows::UI::Xaml::Media::Imaging::DownloadProgressEventArgs const& e
        );

        event_token SourceChanged(
            Windows::Foundation::TypedEventHandler<BiliUWP::ImageEx2Source, Windows::Foundation::IInspectable> const& handler
        ) {
            return m_ev_source_changed.add(handler);
        }
        void SourceChanged(event_token token) { m_ev_source_changed.remove(token); }

        IImageExUriPreprocessor m_uri_preprocessor{ nullptr };
        Windows::UI::Xaml::Media::Imaging::BitmapImage m_bitmap_image;
        struct {
            uint32_t container_width, container_height;
            Windows::UI::Xaml::Media::Stretch stretch;
        } m_hint{};
        event<Windows::Foundation::TypedEventHandler<BiliUWP::ImageEx2Source, BiliUWP::ImageExDownloadProgressEventArgs>> m_ev_download_progress;
        event_token m_et_download_progress;
        event<Windows::Foundation::TypedEventHandler<BiliUWP::ImageEx2Source, Windows::Foundation::IInspectable>> m_ev_source_changed;
        bool m_use_http_cache{ true };
        bool m_need_update_inner_source = false;
        util::winrt::async_storage m_async;

        static Windows::UI::Xaml::DependencyProperty m_EnableDelayedLoadProperty;
        static Windows::UI::Xaml::DependencyProperty m_UriSourceProperty;
    };
    struct ImageEx2 : ImageEx2T<ImageEx2> {
        ImageEx2();
        ~ImageEx2();

        void Source(BiliUWP::ImageEx2Source const& value);
        BiliUWP::ImageEx2Source Source();
        void Stretch(Windows::UI::Xaml::Media::Stretch const& value);
        Windows::UI::Xaml::Media::Stretch Stretch();

        event_token ImageOpened(Windows::UI::Xaml::RoutedEventHandler const& handler) {
            return InnerImage().ImageOpened([weak = get_weak(), handler](auto&&, auto&& e) {
                if (auto strong = weak.get()) { handler(*strong.get(), e); }
            });
        }
        void ImageOpened(event_token token) { InnerImage().ImageOpened(token); }
        event_token ImageFailed(Windows::UI::Xaml::ExceptionRoutedEventHandler const& handler) {
            return InnerImage().ImageFailed([weak = get_weak(), handler](auto&&, auto&& e) {
                if (auto strong = weak.get()) { handler(*strong.get(), e); }
            });
        }
        void ImageFailed(event_token token) { InnerImage().ImageFailed(token); }

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

        Windows::Foundation::Size MeasureOverride(Windows::Foundation::Size const& availableSize);

    private:
        void update_source_hint(
            BiliUWP::ImageEx2Source const& source,
            uint32_t container_width,
            uint32_t container_height,
            Windows::UI::Xaml::Media::Stretch stretch
        );
        void update_source_hint(
            ImageEx2Source* source,
            uint32_t container_width,
            uint32_t container_height,
            Windows::UI::Xaml::Media::Stretch stretch
        );

        event_token m_et_source_changed;

        static Windows::UI::Xaml::DependencyProperty m_SourceProperty;
        static Windows::UI::Xaml::DependencyProperty m_StretchProperty;
    };
}

namespace winrt::BiliUWP::factory_implementation {
    struct ImageEx2Source : ImageEx2SourceT<ImageEx2Source, implementation::ImageEx2Source> {};
    struct ImageEx2 : ImageEx2T<ImageEx2, implementation::ImageEx2> {};
}
