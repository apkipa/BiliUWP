#pragma once
#include "HttpRandomAccessStream.g.h"
#include "NewUriRequestedEventArgs.g.h"
#include <shared_mutex>
#include "util.hpp"

namespace winrt::BiliUWP::implementation {
    using EventHandlerType_NUR = Windows::Foundation::TypedEventHandler<
        BiliUWP::HttpRandomAccessStream, BiliUWP::NewUriRequestedEventArgs>;

    struct NewUriRequestedEventArgs : NewUriRequestedEventArgsT<NewUriRequestedEventArgs>,
                                      deferrable_event_args<NewUriRequestedEventArgs>
    {};

    struct HttpRandomAccessStreamImpl : std::enable_shared_from_this<HttpRandomAccessStreamImpl> {
        virtual void SupplyNewUri(array_view<Windows::Foundation::Uri const> new_uris) = 0;
        virtual event_token NewUriRequested(EventHandlerType_NUR const& handler) = 0;
        virtual void NewUriRequested(event_token const& token) noexcept = 0;
        virtual Windows::Foundation::IAsyncOperationWithProgress<Windows::Storage::Streams::IBuffer, uint32_t> ReadAtAsync(
            Windows::Storage::Streams::IBuffer buffer,
            uint64_t start, uint64_t end,
            Windows::Storage::Streams::InputStreamOptions options
        ) = 0;
        virtual uint64_t Size() = 0;
        virtual void EnableMetricsCollection(bool enable, uint64_t max_events_count) = 0;
        virtual HttpRandomAccessStreamMetrics GetMetrics(bool clear_events) = 0;
    };

    struct HttpRandomAccessStream : HttpRandomAccessStreamT<HttpRandomAccessStream> {
        HttpRandomAccessStream(
            std::shared_ptr<HttpRandomAccessStreamImpl> impl,
            hstring content_type,
            uint64_t start_pos = 0
        );
        ~HttpRandomAccessStream() { Close(); }
        static Windows::Foundation::IAsyncOperation<BiliUWP::HttpRandomAccessStream> CreateAsync(
            Windows::Foundation::Uri http_uri,
            Windows::Web::Http::HttpClient http_client,
            HttpRandomAccessStreamBufferOptions buffer_options,
            uint64_t cache_capacity,
            bool extra_integrity_check
        );
        void SupplyNewUri(array_view<Windows::Foundation::Uri const> new_uris);
        void EnableMetricsCollection(bool enable, uint64_t max_events_count);
        HttpRandomAccessStreamMetrics GetMetrics(bool clear_events);
        event_token NewUriRequested(EventHandlerType_NUR const& handler);
        void NewUriRequested(event_token const& token) noexcept;
        void Close();
        Windows::Foundation::IAsyncOperationWithProgress<Windows::Storage::Streams::IBuffer, uint32_t> ReadAsync(
            Windows::Storage::Streams::IBuffer buffer,
            uint32_t count,
            Windows::Storage::Streams::InputStreamOptions options
        );
        Windows::Foundation::IAsyncOperationWithProgress<uint32_t, uint32_t> WriteAsync(
            Windows::Storage::Streams::IBuffer buffer
        );
        Windows::Foundation::IAsyncOperation<bool> FlushAsync();
        uint64_t Size();
        void Size(uint64_t value);
        Windows::Storage::Streams::IInputStream GetInputStreamAt(uint64_t position);
        Windows::Storage::Streams::IOutputStream GetOutputStreamAt(uint64_t position);
        uint64_t Position();
        void Seek(uint64_t position);
        Windows::Storage::Streams::IRandomAccessStream CloneStream();
        bool CanRead();
        bool CanWrite();
        hstring ContentType();

    private:
        hstring m_content_type;

        uint64_t m_cur_pos;
        std::shared_mutex m_impl_mutex;
        std::shared_ptr<HttpRandomAccessStreamImpl> m_impl;
        std::mutex m_pending_async_mutex;
        Windows::Foundation::IAsyncInfo m_pending_async;
    };
}
namespace winrt::BiliUWP::factory_implementation {
    struct HttpRandomAccessStream : HttpRandomAccessStreamT<HttpRandomAccessStream, implementation::HttpRandomAccessStream> {};
}
