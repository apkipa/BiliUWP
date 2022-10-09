#include "pch.h"
#include "HttpRandomAccessStream.h"
#include "HttpRandomAccessStream.g.cpp"
#include "NewUriRequestedEventArgs.g.cpp"
#include "util.hpp"
#include <deque>

constexpr uint64_t DEFAULT_METRICS_EVENTS_COUNT = 16384;

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Web::Http;
using namespace Windows::Storage::Streams;

namespace winrt::BiliUWP::implementation {
    // No caching
    struct HttpRandomAccessStreamImpl_Direct : HttpRandomAccessStreamImpl {
        HttpRandomAccessStreamImpl_Direct(
            Uri http_uri,
            HttpClient http_client,
            uint64_t size,
            bool extra_integrity_check
        ) : m_http_uris{ std::move(http_uri) }, m_http_client(std::move(http_client)), m_size(size),
            m_extra_integrity_check(extra_integrity_check), m_enable_metrics_collection(false),
            m_metrics()
        {
            if (extra_integrity_check) {
                throw hresult_not_implemented(
                    L"HttpRandomAccessStreamImpl_Direct: extra_integrity_check not implemented"
                );
            }
        }
        void SupplyNewUri(array_view<Uri const> new_uris) {
            // TODO: Introduce uri ranking
            std::unique_lock guard(m_mutex_http_uris);
            for (auto& i : new_uris) {
                m_http_uris.push_back(i);
            }
        }
        event_token NewUriRequested(EventHandlerType_NUR const& handler) {
            return m_ev_new_uri_requested.add(handler);
        }
        void NewUriRequested(event_token const& token) noexcept { m_ev_new_uri_requested.remove(token); }
        IAsyncOperationWithProgress<IBuffer, uint32_t> ReadAsync(
            IBuffer buffer,
            uint64_t start, uint64_t end,
            InputStreamOptions options
        ) {
            auto cancellation_token = co_await get_cancellation_token();
            cancellation_token.enable_propagation();
            auto progress_token = co_await get_progress_token();

            util::debug::log_trace(std::format(L"Fetching http range {}-{}", start, end));

            while (true) {
                // Check whether we have uris
                Uri cur_uri = nullptr;
                {
                    std::shared_lock guard_uri(m_mutex_http_uris);
                    if (!m_http_uris.empty()) {
                        cur_uri = m_http_uris.front();
                    }
                }
                if (cur_uri == nullptr) {
                    // No uris, try to get some
                    co_await trigger_new_uri_requested();
                    // If we cannot get more uris, mark as failed
                    std::shared_lock guard_uri(m_mutex_http_uris);
                    if (m_http_uris.empty()) {
                        throw hresult_error(E_FAIL, L"No uris available after NewUriRequested fired");
                    }
                    cur_uri = m_http_uris.front();
                }
                // Try to fetch content
                try {
                    {
                        std::scoped_lock guard_metrics(m_mutex_metrics);
                        if (m_enable_metrics_collection.load()) {
                            m_metrics.requests_delta++;
                        }
                        if (m_metrics.active_connections++ == 0) {
                            m_metrics.last_start_ts = std::chrono::high_resolution_clock::now();
                        }
                    }
                    deferred([&] {
                        std::scoped_lock guard_metrics(m_mutex_metrics);
                        if (--m_metrics.active_connections == 0) {
                            if (m_enable_metrics_collection.load()) {
                                m_metrics.connection_duration +=
                                    std::chrono::high_resolution_clock::now() - m_metrics.last_start_ts;
                            }
                        }
                    });
                    auto http_content = co_await util::winrt::fetch_partial_http_content(
                        cur_uri, m_http_client, start, end - start);
                    auto op = http_content.WriteToStreamAsync(
                        make<util::winrt::BufferBackedRandomAccessStream>(buffer));
                    uint64_t op_req_bytes = 0;
                    op.Progress([&](auto const&, auto progress) {
                        progress_token(static_cast<uint32_t>(progress));
                        std::scoped_lock guard_metrics(m_mutex_metrics);
                        if (m_enable_metrics_collection.load()) {
                            m_metrics.bytes_delta += progress - op_req_bytes;
                        }
                        op_req_bytes = progress;
                    });
                    co_await std::move(op);
                    co_return buffer;
                }
                catch (hresult_error const&) {
                    // Fetch failed, drop current uri
                    std::unique_lock guard_uri(m_mutex_http_uris);
                    if (!m_http_uris.empty() && cur_uri == m_http_uris.front()) {
                        m_http_uris.pop_front();
                    }
                }
            }
        }
        uint64_t Size() { return m_size; }
        void EnableMetricsCollection(bool enable, uint64_t max_events_count) {
            if (m_enable_metrics_collection.exchange(enable) == enable) {
                return;
            }
            std::scoped_lock guard_metrics(m_mutex_metrics);
            if (enable) {
                m_metrics.last_start_ts = std::chrono::high_resolution_clock::now();
            }
            else {
                m_metrics.connection_duration +=
                    std::chrono::high_resolution_clock::now() - m_metrics.last_start_ts;
            }
        }
        HttpRandomAccessStreamMetrics GetMetrics(bool clear_events) {
            if (!m_enable_metrics_collection.load()) {
                throw hresult_error(E_FAIL, L"Metrics collection is not enabled");
            }
            std::scoped_lock guard(m_mutex_metrics);
            auto actual_duration = m_metrics.connection_duration;
            if (m_metrics.active_connections > 0) {
                actual_duration += std::chrono::high_resolution_clock::now() - m_metrics.last_start_ts;
            }
            auto actual_dur_secs = std::chrono::duration<double>(actual_duration).count();
            if (actual_dur_secs == 0) {
                actual_dur_secs = 1;
            }
            auto inbound_bytes_per_sec = actual_dur_secs == 0 ? 0 : m_metrics.bytes_delta / actual_dur_secs;
            HttpRandomAccessStreamMetrics result{
                .ActiveConnectionsCount = m_metrics.active_connections,
                .SentRequestsDelta = m_metrics.requests_delta,
                .InboundBitsPerSecond = static_cast<uint64_t>(std::llround(inbound_bytes_per_sec * 8)),
                .DownloadedBytesDelta = m_metrics.bytes_delta,
            };
            if (clear_events) {
                m_metrics.last_start_ts = std::chrono::high_resolution_clock::now();
                m_metrics.connection_duration = {};
                m_metrics.requests_delta = 0;
                m_metrics.bytes_delta = 0;
            }
            return result;
        }

    private:
        IAsyncAction trigger_new_uri_requested(void) {
            com_ptr<NewUriRequestedEventArgs> ea_nur = nullptr;
            bool owns_ea = false;
            {
                std::scoped_lock guard(m_mutex_ea_nur);
                ea_nur = m_ea_nur;
                if (!ea_nur) {
                    ea_nur = m_ea_nur = make_self<NewUriRequestedEventArgs>();
                    owns_ea = true;
                }
            }
            deferred([&] {
                if (owns_ea) {
                    std::scoped_lock guard(m_mutex_ea_nur);
                    m_ea_nur = nullptr;
                }
            });
            if (owns_ea) {
                m_ev_new_uri_requested(make<HttpRandomAccessStream>(shared_from_this(), L""), *ea_nur);
            }
            co_return co_await ea_nur->wait_for_deferrals();
        }

        std::mutex m_mutex_ea_nur;
        com_ptr<NewUriRequestedEventArgs> m_ea_nur;
        std::shared_mutex m_mutex_http_uris;
        std::deque<Uri> m_http_uris;
        HttpClient m_http_client;
        uint64_t m_size;
        bool m_extra_integrity_check;
        event<EventHandlerType_NUR> m_ev_new_uri_requested;
        std::atomic_bool m_enable_metrics_collection;
        std::mutex m_mutex_metrics;
        struct {
            uint64_t active_connections;
            std::chrono::high_resolution_clock::time_point last_start_ts;
            std::chrono::high_resolution_clock::duration connection_duration;
            uint64_t requests_delta;
            uint64_t bytes_delta;
        } m_metrics;
    };

    // Caching via provided memory stream
    struct HttpRandomAccessStreamImpl_FullStreamBased : HttpRandomAccessStreamImpl {
        HttpRandomAccessStreamImpl_FullStreamBased(
            InMemoryRandomAccessStream buf_stream
        ) : m_buf_stream(std::move(buf_stream)), m_enable_metrics_collection(false) {}
        void SupplyNewUri(array_view<Uri const> new_uris) {}
        event_token NewUriRequested(EventHandlerType_NUR const& handler) {
            return m_ev_new_uri_requested.add(handler);
        }
        void NewUriRequested(event_token const& token) noexcept { m_ev_new_uri_requested.remove(token); }
        IAsyncOperationWithProgress<IBuffer, uint32_t> ReadAsync(
            IBuffer buffer,
            uint64_t start, uint64_t end,
            InputStreamOptions options
        ) {
            return m_buf_stream.GetInputStreamAt(start).ReadAsync(
                std::move(buffer), static_cast<uint32_t>(end - start), options);
        }
        uint64_t Size() { return m_buf_stream.Size(); }
        void EnableMetricsCollection(bool enable, uint64_t max_events_count) {
            m_enable_metrics_collection.store(enable);
        }
        HttpRandomAccessStreamMetrics GetMetrics(bool clear_events) {
            if (!m_enable_metrics_collection.load()) {
                throw hresult_error(E_FAIL, L"Metrics collection is not enabled");
            }
            return {
                .ActiveConnectionsCount = 0,
                .SentRequestsDelta = 0,
                .InboundBitsPerSecond = 0,
                .DownloadedBytesDelta = 0,
            };
        }

    private:
        InMemoryRandomAccessStream m_buf_stream;
        event<EventHandlerType_NUR> m_ev_new_uri_requested;
        std::atomic_bool m_enable_metrics_collection;
    };

    // Caching via memory stream
    struct HttpRandomAccessStreamImpl_StreamBased : HttpRandomAccessStreamImpl {
        // TODO: Add mutex lock
        HttpRandomAccessStreamImpl_StreamBased(
            Uri http_uri,
            HttpClient http_client,
            bool extra_integrity_check
        ) : m_http_uris{ std::move(http_uri) }, m_http_client(std::move(http_client)),
            m_extra_integrity_check(extra_integrity_check)
        {
            if (extra_integrity_check) {
                throw hresult_not_implemented(
                    L"HttpRandomAccessStreamImpl_StreamBased: extra_integrity_check not implemented"
                );
            }
        }

    private:
        std::vector<Uri> m_http_uris;
        HttpClient m_http_client;
        bool m_extra_integrity_check;
        event<EventHandlerType_NUR> m_ev_new_uri_requested;
    };
}

namespace winrt::BiliUWP::implementation {
    HttpRandomAccessStream::HttpRandomAccessStream(
        std::shared_ptr<HttpRandomAccessStreamImpl> impl,
        hstring content_type,
        uint64_t start_pos
    ) : m_impl(std::move(impl)), m_content_type(std::move(content_type)), m_cur_pos(start_pos) {}
    IAsyncOperation<BiliUWP::HttpRandomAccessStream> HttpRandomAccessStream::CreateAsync(
        Uri http_uri,
        HttpClient http_client,
        HttpRandomAccessStreamBufferOptions buffer_options,
        uint64_t cache_capacity,
        bool extra_integrity_check
    ) {
        switch (buffer_options) {
        case HttpRandomAccessStreamBufferOptions::None:
            break;
        case HttpRandomAccessStreamBufferOptions::Sized:
            // TODO: Implement this
            throw hresult_not_implemented();
            break;
        case HttpRandomAccessStreamBufferOptions::DynamicallySized:
            // TODO: Implement this
            throw hresult_not_implemented();
            break;
        case HttpRandomAccessStreamBufferOptions::Full:
            break;
        case HttpRandomAccessStreamBufferOptions::ImmediateFull:
            break;
        default:
            throw hresult_invalid_argument(L"Unrecognized HttpRandomAccessStreamBufferOptions");
        }

        if (buffer_options == HttpRandomAccessStreamBufferOptions::ImmediateFull) {
            // This option does not require partial downloading, so handle this separately
            auto http_resp = co_await http_client.GetAsync(http_uri, HttpCompletionOption::ResponseHeadersRead);
            http_resp.EnsureSuccessStatusCode();
            auto buf_mem_stream = InMemoryRandomAccessStream();
            co_await http_resp.Content().WriteToStreamAsync(buf_mem_stream);
            buf_mem_stream.Seek(0);
            co_return make<HttpRandomAccessStream>(
                std::make_shared<HttpRandomAccessStreamImpl_FullStreamBased>(std::move(buf_mem_stream)),
                http_resp.Content().Headers().ContentType().MediaType());
        }

        // Check whether server supports range header
        auto http_req = HttpRequestMessage();
        http_req.Method(HttpMethod::Head());
        http_req.Headers().Append(L"Range", L"bytes=0-");
        http_req.RequestUri(http_uri);
        auto http_resp = co_await http_client.SendRequestAsync(
            http_req, HttpCompletionOption::ResponseHeadersRead
        );
        if (http_resp.StatusCode() != HttpStatusCode::PartialContent) {
            throw hresult_error(E_FAIL, L"Requested resource does not support partial downloading");
        }

        // Server supports ranges; proceed with creation
        auto http_resp_cont_hdrs = http_resp.Content().Headers();
        auto cont_type = http_resp_cont_hdrs.ContentType().MediaType();
        auto nullable_cont_len = http_resp_cont_hdrs.ContentLength();
        if (!nullable_cont_len) {
            throw hresult_error(E_FAIL, L"Requested resource does not have Content-Length");
        }
        auto cont_len = nullable_cont_len.Value();

        util::debug::log_trace(std::format(L"New HttpRandomAccessStream: {}, {} Bytes", cont_type, cont_len));

        if (buffer_options == HttpRandomAccessStreamBufferOptions::None) {
            co_return make<HttpRandomAccessStream>(std::make_shared<HttpRandomAccessStreamImpl_Direct>(
                http_uri, http_client, cont_len, false), cont_type);
        }
        else {
            // TODO...
            throw hresult_not_implemented();
        }
    }
    void HttpRandomAccessStream::SupplyNewUri(array_view<Uri const> new_uris) {
        std::shared_lock guard_impl(m_impl_mutex);
        if (!m_impl) { throw hresult_illegal_method_call(); }
        return m_impl->SupplyNewUri(std::move(new_uris));
    }
    void HttpRandomAccessStream::EnableMetricsCollection(bool enable, uint64_t max_events_count) {
        std::shared_lock guard_impl(m_impl_mutex);
        if (!m_impl) { throw hresult_illegal_method_call(); }
        return m_impl->EnableMetricsCollection(enable, max_events_count);
    }
    HttpRandomAccessStreamMetrics HttpRandomAccessStream::GetMetrics(bool clear_events) {
        std::shared_lock guard_impl(m_impl_mutex);
        if (!m_impl) { throw hresult_illegal_method_call(); }
        return m_impl->GetMetrics(clear_events);
    }
    event_token HttpRandomAccessStream::NewUriRequested(EventHandlerType_NUR const& handler) {
        std::shared_lock guard_impl(m_impl_mutex);
        if (!m_impl) { throw hresult_illegal_method_call(); }
        return m_impl->NewUriRequested(handler);
    }
    void HttpRandomAccessStream::NewUriRequested(event_token const& token) noexcept {
        std::shared_lock guard_impl(m_impl_mutex);
        if (!m_impl) { return; }
        return m_impl->NewUriRequested(token);
    }
    void HttpRandomAccessStream::Close() {
        std::shared_ptr<HttpRandomAccessStreamImpl> impl;
        m_impl_mutex.lock();
        impl = std::exchange(m_impl, nullptr);
        m_impl_mutex.unlock();
        if (!impl) { return; }
        // Clean up running async tasks and destruct impl
        std::scoped_lock guard_async(m_pending_async_mutex);
        if (m_pending_async) {
            m_pending_async.Cancel();
        }
    }
    IAsyncOperationWithProgress<IBuffer, uint32_t> HttpRandomAccessStream::ReadAsync(
        IBuffer buffer, uint32_t count, InputStreamOptions options
    ) {
        auto cancellation_token = co_await get_cancellation_token();
        cancellation_token.enable_propagation(true);

        m_impl_mutex.lock_shared();
        std::shared_ptr<HttpRandomAccessStreamImpl> impl = m_impl;
        m_impl_mutex.unlock_shared();
        if (!impl) { throw hresult_illegal_method_call(); }
        IAsyncOperationWithProgress<IBuffer, uint32_t> async = nullptr;
        {
            // Store task
            std::scoped_lock guard_async(m_pending_async_mutex);
            if (m_pending_async) {
                throw hresult_error(E_FAIL, L""
                    "Concurrent access to the same stream will cause data race; "
                    "consider cloning the stream"
                );
            }
            async = impl->ReadAsync(std::move(buffer), m_cur_pos, m_cur_pos + count, options);
            m_pending_async = async;
        }
        auto weak_this = get_weak();
        deferred([&] {
            // Clear task
            auto strong_this = weak_this.get();
            if (!strong_this) { return; }
            std::scoped_lock guard_async(m_pending_async_mutex);
            m_pending_async = nullptr;
        });
        auto progress_token = co_await get_progress_token();
        async.Progress([&](auto const&, auto progress) {
            progress_token(progress);
        });
        auto result = co_await std::move(async);
        m_cur_pos += count;
        co_return result;
    }
    IAsyncOperationWithProgress<uint32_t, uint32_t> HttpRandomAccessStream::WriteAsync(IBuffer buffer) {
        throw hresult_illegal_method_call();
    }
    IAsyncOperation<bool> HttpRandomAccessStream::FlushAsync() {
        throw hresult_illegal_method_call();
    }
    uint64_t HttpRandomAccessStream::Size() {
        std::shared_lock guard_impl(m_impl_mutex);
        if (!m_impl) { throw hresult_illegal_method_call(); }
        return m_impl->Size();
    }
    // NOTE: We don't allow changing the size (or cache capacity) dynamically
    void HttpRandomAccessStream::Size(uint64_t value) {
        throw hresult_illegal_method_call();
    }
    IInputStream HttpRandomAccessStream::GetInputStreamAt(uint64_t position) {
        std::shared_lock guard_impl(m_impl_mutex);
        if (!m_impl) { throw hresult_illegal_method_call(); }
        // TODO: Maybe check position validity
        return make<HttpRandomAccessStream>(m_impl, m_content_type, position);
    }
    IOutputStream HttpRandomAccessStream::GetOutputStreamAt(uint64_t position) {
        throw hresult_illegal_method_call();
    }
    uint64_t HttpRandomAccessStream::Position() {
        std::shared_lock guard_impl(m_impl_mutex);
        if (!m_impl) { throw hresult_illegal_method_call(); }
        return m_cur_pos;
    }
    void HttpRandomAccessStream::Seek(uint64_t position) {
        std::shared_lock guard_impl(m_impl_mutex);
        if (!m_impl) { throw hresult_illegal_method_call(); }
        // TODO: Maybe check position validity
        m_cur_pos = position;
    }
    IRandomAccessStream HttpRandomAccessStream::CloneStream() {
        std::shared_lock guard_impl(m_impl_mutex);
        if (!m_impl) { throw hresult_illegal_method_call(); }
        return make<HttpRandomAccessStream>(m_impl, m_content_type);
    }
    bool HttpRandomAccessStream::CanRead() {
        return true;
    }
    bool HttpRandomAccessStream::CanWrite() {
        return false;
    }
    hstring HttpRandomAccessStream::ContentType() {
        return m_content_type;
    }
}
