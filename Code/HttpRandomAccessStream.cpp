#include "pch.h"
#include "HttpRandomAccessStream.h"
#include "HttpRandomAccessStream.g.cpp"
#include "NewUriRequestedEventArgs.g.cpp"
#include "util.hpp"

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Web::Http;
using namespace Windows::Storage::Streams;

namespace winrt::BiliUWP::implementation {
    HttpRandomAccessStreamImpl::~HttpRandomAccessStreamImpl() = default;

    // No caching
    struct HttpRandomAccessStreamImpl_Direct : HttpRandomAccessStreamImpl {
        // TODO: Add mutex lock
        HttpRandomAccessStreamImpl_Direct(
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

    // Caching via provided memory stream
    struct HttpRandomAccessStreamImpl_FullStreamBased : HttpRandomAccessStreamImpl {
        HttpRandomAccessStreamImpl_FullStreamBased(
            InMemoryRandomAccessStream buf_stream
        ) : m_buf_stream(std::move(buf_stream)) {}
        void SupplyNewUri(array_view<Uri const> new_uris) {}
        event_token NewUriRequested(EventHandlerType_NUR const& handler) {
            return m_ev_new_uri_requested.add(handler);
        }
        void NewUriRequested(event_token const& token) noexcept { m_ev_new_uri_requested.remove(token); }
        IAsyncOperationWithProgress<IBuffer, uint32_t> ReadAsync(
            IBuffer buffer,
            uint32_t count,
            InputStreamOptions options
        ) {
            return m_buf_stream.ReadAsync(std::move(buffer), count, options);
        }
        uint64_t Size() { return m_buf_stream.Size(); }
        IInputStream GetInputStreamAt(uint64_t position) {
            return m_buf_stream.GetInputStreamAt(position);
        }
        uint64_t Position() { return m_buf_stream.Position(); }
        void Seek(uint64_t position) { m_buf_stream.Seek(position); }
    private:
        InMemoryRandomAccessStream m_buf_stream;
        event<EventHandlerType_NUR> m_ev_new_uri_requested;
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
        hstring content_type
    ) : m_impl(std::move(impl)), m_content_type(std::move(content_type)) {}
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
                http_resp.Content().Headers().ContentType().MediaType()
            );
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

        if (buffer_options == HttpRandomAccessStreamBufferOptions::None) {
            // TODO...
            throw hresult_not_implemented();
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
        // If no tasks are running, simply return
        if (m_pending_async.empty()) {
            return;
        }
        // Otherwise, cancel all running tasks and return
        for (auto& i : m_pending_async) {
            i.Cancel();
        }
    }
    IAsyncOperationWithProgress<IBuffer, uint32_t> HttpRandomAccessStream::ReadAsync(
        IBuffer buffer, uint32_t count, InputStreamOptions options
    ) {
        auto cancellation_token = co_await get_cancellation_token();
        cancellation_token.enable_propagation(true);

        std::shared_ptr<HttpRandomAccessStreamImpl> impl;
        m_impl_mutex.lock_shared();
        impl = m_impl;
        m_impl_mutex.unlock_shared();
        if (!impl) { throw hresult_illegal_method_call(); }
        auto async = impl->ReadAsync(std::move(buffer), count, options);
        IAsyncInfo async_info = async;
        {
            // Add to tasks list
            std::scoped_lock guard_async(m_pending_async_mutex);
            m_pending_async.push_back(async_info);
        }
        auto weak_this = get_weak();
        deferred([&] {
            // Remove from tasks list
            auto strong_this = weak_this.get();
            if (!strong_this) { return; }
            std::scoped_lock guard_async(m_pending_async_mutex);
            auto it = std::find(m_pending_async.begin(), m_pending_async.end(), async_info);
            if (it != m_pending_async.end()) { m_pending_async.erase(it); }
        });
        auto progress_token = co_await get_progress_token();
        async.Progress([&](auto const&, auto progress) {
            progress_token(progress);
        });
        co_return co_await std::move(async);
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
        return m_impl->GetInputStreamAt(position);
    }
    IOutputStream HttpRandomAccessStream::GetOutputStreamAt(uint64_t position) {
        throw hresult_illegal_method_call();
    }
    uint64_t HttpRandomAccessStream::Position() {
        std::shared_lock guard_impl(m_impl_mutex);
        if (!m_impl) { throw hresult_illegal_method_call(); }
        return m_impl->Position();
    }
    void HttpRandomAccessStream::Seek(uint64_t position) {
        std::shared_lock guard_impl(m_impl_mutex);
        if (!m_impl) { throw hresult_illegal_method_call(); }
        return m_impl->Seek(position);
    }
    // NOTE: CloneStream is typically difficult and impractical to implement for
    //       http streams; ignore them for now
    IRandomAccessStream HttpRandomAccessStream::CloneStream() {
        throw hresult_illegal_method_call();
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
