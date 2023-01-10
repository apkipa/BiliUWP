#include "pch.h"
#include "HttpRandomAccessStream.h"
#include "HttpRandomAccessStream.g.cpp"
#include "NewUriRequestedEventArgs.g.cpp"
#include "util.hpp"
#include <numeric>
#include <deque>

constexpr uint64_t DEFAULT_METRICS_EVENTS_COUNT = 16384;
constexpr uint64_t DEFAULT_HTTP_CHUNK_SIZE = 262144;

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
        com_array<Uri> GetActiveUris() {
            std::shared_lock guard(m_mutex_http_uris);
            return { m_http_uris.begin(), m_http_uris.end() };
        }
        event_token NewUriRequested(EventHandlerType_NUR const& handler) {
            return m_ev_new_uri_requested.add(handler);
        }
        void NewUriRequested(event_token const& token) noexcept { m_ev_new_uri_requested.remove(token); }
        IAsyncOperationWithProgress<IBuffer, uint32_t> ReadAtAsync(
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
                        if (m_enable_metrics_collection.load()) {
                            std::scoped_lock guard_metrics(m_mutex_metrics);
                            m_metrics.bytes_delta += progress - op_req_bytes;
                        }
                        op_req_bytes = progress;
                    });
                    co_await std::move(op);
                    co_return buffer;
                }
                catch (hresult_canceled const&) { throw; }
                catch (hresult_error const& e) {
                    util::debug::log_debug(std::format(
                        L"HRAS: Failed to fetch `{}` with range {}-{} (0x{:08x}: {})",
                        cur_uri.ToString(), start, end,
                        static_cast<uint32_t>(e.code()), e.message()
                    ));
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
            if (m_enable_metrics_collection.load() != enable) {
                // Value has changed, leaving current state stale
                return;
            }
            if (enable) {
                m_metrics.last_start_ts = std::chrono::high_resolution_clock::now();
            }
            else {
                if (m_metrics.active_connections > 0) {
                    m_metrics.connection_duration +=
                        std::chrono::high_resolution_clock::now() - m_metrics.last_start_ts;
                }
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
            auto inbound_bytes_per_sec = actual_dur_secs == 0 ? 0 : m_metrics.bytes_delta / actual_dur_secs;
            HttpRandomAccessStreamMetrics result{
                .ActiveConnectionsCount = m_metrics.active_connections,
                .SentRequestsDelta = m_metrics.requests_delta,
                .InboundBitsPerSecond = static_cast<uint64_t>(std::llround(inbound_bytes_per_sec * 8)),
                .DownloadedBytesDelta = m_metrics.bytes_delta,
                .AllocatedBufferSize = 0,
                .UsedBufferSize = 0,
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
        com_array<Uri> GetActiveUris() {
            return {};
        }
        event_token NewUriRequested(EventHandlerType_NUR const& handler) {
            return m_ev_new_uri_requested.add(handler);
        }
        void NewUriRequested(event_token const& token) noexcept { m_ev_new_uri_requested.remove(token); }
        IAsyncOperationWithProgress<IBuffer, uint32_t> ReadAtAsync(
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
            auto size = m_buf_stream.Size();
            return {
                .ActiveConnectionsCount = 0,
                .SentRequestsDelta = 0,
                .InboundBitsPerSecond = 0,
                .DownloadedBytesDelta = 0,
                .AllocatedBufferSize = size,
                .UsedBufferSize = size,
            };
        }

    private:
        InMemoryRandomAccessStream m_buf_stream;
        event<EventHandlerType_NUR> m_ev_new_uri_requested;
        std::atomic_bool m_enable_metrics_collection;
    };

    // Caching via memory stream
    struct HttpRandomAccessStreamImpl_StreamBased : HttpRandomAccessStreamImpl {
        HttpRandomAccessStreamImpl_StreamBased(
            Uri http_uri,
            HttpClient http_client,
            uint64_t size,
            bool extra_integrity_check
        ) : m_http_uris{ std::move(http_uri) }, m_http_client(std::move(http_client)), m_size(size),
            m_extra_integrity_check(extra_integrity_check), m_enable_metrics_collection(false),
            m_metrics(), m_buf_stream(InMemoryRandomAccessStream()),
            m_unbuffered_intervals{ { 0, size } }
        {
            if (extra_integrity_check) {
                throw hresult_not_implemented(
                    L"HttpRandomAccessStreamImpl_StreamBased: extra_integrity_check not implemented"
                );
            }

            // WARN: InMemoryRandomAccessStream does not support >4GB data
            // TODO: Use custom stream which supports >4GB data
            m_buf_stream = util::winrt::InMemoryStream().as_random_access_stream();
            m_buf_stream.Size(size);
        }
        void SupplyNewUri(array_view<Uri const> new_uris) {
            // TODO: Introduce uri ranking
            std::unique_lock guard(m_mutex_http_uris);
            for (auto& i : new_uris) {
                m_http_uris.push_back(i);
            }
        }
        com_array<Uri> GetActiveUris() {
            std::shared_lock guard(m_mutex_http_uris);
            return { m_http_uris.begin(), m_http_uris.end() };
        }
        event_token NewUriRequested(EventHandlerType_NUR const& handler) {
            return m_ev_new_uri_requested.add(handler);
        }
        void NewUriRequested(event_token const& token) noexcept { m_ev_new_uri_requested.remove(token); }
        IAsyncOperationWithProgress<IBuffer, uint32_t> ReadAtAsync(
            IBuffer buffer,
            uint64_t start, uint64_t end,
            InputStreamOptions options
        ) {
            auto cancellation_token = co_await get_cancellation_token();
            cancellation_token.enable_propagation();
            auto progress_token = co_await get_progress_token();

            /*util::debug::log_trace(std::format(L"Fetching stream range {}-{} with option {}",
                start, end, std::to_underlying(options)));*/

            if (start > end) {
                throw hresult_invalid_argument(L"Invalid read operation (negative-sized read)");
            }
            if (end > m_size) {
                // NOTE: Silently clamp the range to be compatible with MediaPlayer out-of-range fetching
                end = m_size;
                if (start > end) { start = end; }
            }

            // NOTE: Never invoke multithreaded downloading, unless explicitly allowed (currently not considered)

            // Fetch only a signle part in each iteration
            while (true) {
                std::optional<std::pair<uint64_t, uint64_t>> target_interval = std::nullopt;
                auto get_its_pair_nolock_fn = [this](uint64_t start, uint64_t end) {
                    auto itb = std::upper_bound(
                        m_unbuffered_intervals.begin(), m_unbuffered_intervals.end(),
                        start,
                        [](uint64_t const& a, std::pair<uint64_t, uint64_t> const& b) { return a < b.second; }
                    );
                    auto ite = std::lower_bound(
                        m_unbuffered_intervals.begin(), m_unbuffered_intervals.end(),
                        end,
                        [](std::pair<uint64_t, uint64_t> const& a, uint64_t const& b) { return a.first < b; }
                    );
                    return std::pair(itb, ite);
                };
                {   // Calculate and set interval to be fetched, if there's one
                    std::shared_lock guard(m_mutex_unbuffered_intervals);
                    auto [itb, ite] = get_its_pair_nolock_fn(start, end);
                    if (itb < ite) {
                        // Found intersecting intervals
                        // NOTE: Temporary fix for slow downloading by fetching a larger chunk
                        // TODO: Use a better way to optimize http fetching
                        // TODO: Add a prefetch API for optimal video fetching
                        //       (requires waiting on pending requests, etc.)
                        auto final_start = start;
                        auto final_end = std::clamp(start + DEFAULT_HTTP_CHUNK_SIZE, end, m_size);
                        std::tie(itb, ite) = get_its_pair_nolock_fn(final_start, final_end);
                        ite--;
                        if (itb == ite) {
                            target_interval = {
                                std::max(final_start, itb->first), std::min(final_end, ite->second) };
                        }
                        else {
                            target_interval = { std::max(final_start, itb->first), itb->second };
                        }
                    }
                }
                // All data are fetched, stop iteration
                if (!target_interval) { break; }
                // Try to fill buffer
                try {
                    auto op = populate_buf_stream(target_interval->first, target_interval->second);
                    auto progress_start_pos = target_interval->first - start;
                    op.Progress([&](auto const&, auto progress) {
                        progress_token(static_cast<uint32_t>(progress_start_pos + progress));
                    });
                    co_await std::move(op);
                }
                catch (hresult_canceled const&) { throw; }
                catch (hresult_error const&) {
                    // Failed, just propagate the exception
                    throw;
                }
                {   // Update unbuffered intervals
                    std::unique_lock guard(m_mutex_unbuffered_intervals);
                    auto [itb, ite] = get_its_pair_nolock_fn(target_interval->first, target_interval->second);
                    if (itb < ite) {
                        // Found intersecting intervals
                        ite--;
                        if (itb == ite) {
                            uint64_t temp;
                            // Split into multiple parts / one part / erase part
                            switch (((target_interval->first > itb->first) << 1) |
                                (target_interval->second < ite->second))
                            {
                            case 0b00:
                                m_unbuffered_intervals.erase(itb);
                                break;
                            case 0b01:
                                ite->first = target_interval->second;
                                break;
                            case 0b10:
                                itb->second = target_interval->first;
                                break;
                            case 0b11:
                                temp = itb->first;
                                itb->first = target_interval->second;
                                m_unbuffered_intervals.insert(itb, { temp, target_interval->first });
                                break;
                            }
                        }
                        else {
                            if (target_interval->first > itb->first) {
                                itb->second = target_interval->first;
                                itb++;
                            }
                            if (target_interval->second < ite->second) {
                                ite->first = target_interval->second;
                            }
                            else {
                                ite++;
                            }
                            m_unbuffered_intervals.erase(itb, ite);
                        }
                        // Release intermediate memory when done
                        if (m_unbuffered_intervals.empty()) { m_unbuffered_intervals.shrink_to_fit(); }
                    }
                }
            }

            co_return co_await m_buf_stream.GetInputStreamAt(start).ReadAsync(
                std::move(buffer), static_cast<uint32_t>(end - start), options);
        }
        uint64_t Size() { return m_size; }
        void EnableMetricsCollection(bool enable, uint64_t max_events_count) {
            if (m_enable_metrics_collection.exchange(enable) == enable) {
                return;
            }
            std::scoped_lock guard_metrics(m_mutex_metrics);
            if (m_enable_metrics_collection.load() != enable) {
                // Value has changed, leaving current state stale
                return;
            }
            if (enable) {
                m_metrics.last_start_ts = std::chrono::high_resolution_clock::now();
            }
            else {
                if (m_metrics.active_connections > 0) {
                    m_metrics.connection_duration +=
                        std::chrono::high_resolution_clock::now() - m_metrics.last_start_ts;
                }
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
            auto inbound_bytes_per_sec = actual_dur_secs == 0 ? 0 : m_metrics.bytes_delta / actual_dur_secs;
            uint64_t unallocated_buf_total_size = 0;
            {
                std::shared_lock guard_buf_ints(m_mutex_unbuffered_intervals);
                for (auto const& i : m_unbuffered_intervals) {
                    unallocated_buf_total_size += i.second - i.first;
                }
            }
            auto buf_total_size = m_buf_stream.Size();
            HttpRandomAccessStreamMetrics result{
                .ActiveConnectionsCount = m_metrics.active_connections,
                .SentRequestsDelta = m_metrics.requests_delta,
                .InboundBitsPerSecond = static_cast<uint64_t>(std::llround(inbound_bytes_per_sec * 8)),
                .DownloadedBytesDelta = m_metrics.bytes_delta,
                .AllocatedBufferSize = buf_total_size,
                .UsedBufferSize = buf_total_size - unallocated_buf_total_size,
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
            auto correlation_id = static_cast<uint32_t>(util::num::gen_global_seqid());
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
                util::debug::log_trace(std::format(L"Triggering event NewUriRequested (CorrelationId: {:08x})", correlation_id));
                m_ev_new_uri_requested(make<HttpRandomAccessStream>(shared_from_this(), L""), *ea_nur);
            }
            // WARN: winrt::deferrable_event_args::wait_for_deferrals() does not support
            //       multiple awaiters, don't use it
            co_await ea_nur->wait_for_deferrals();
            if (owns_ea) {
                util::debug::log_trace(std::format(L"Finished event NewUriRequested (CorrelationId: {:08x})", correlation_id));
            }
        }
        // NOTE: This method only fills the specified part of buffer stream. Buffer ranges
        //       should be managed outside the method.
        IAsyncActionWithProgress<uint64_t> populate_buf_stream(uint64_t start, uint64_t end) {
            auto cancellation_token = co_await get_cancellation_token();
            cancellation_token.enable_propagation();
            auto progress_token = co_await get_progress_token();

            auto correlation_id = static_cast<uint32_t>(util::num::gen_global_seqid());
            util::debug::log_trace(std::format(L"Fetching http range {}-{} (CorrelationId: {:08x})", start, end, correlation_id));

            if (end > m_size) {
                throw hresult_invalid_argument(L"Invalid read operation (out-of-bounds read)");
            }
            if (start > end) {
                throw hresult_invalid_argument(L"Invalid read operation (negative-sized read)");
            }

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
                    // TODO: Known issue: HttpClient does not support concurrent requests
                    auto http_content = co_await util::winrt::fetch_partial_http_content(
                        cur_uri, m_http_client, start, end - start);
                    auto op = http_content.WriteToStreamAsync(m_buf_stream.GetOutputStreamAt(start));
                    uint64_t op_req_bytes = 0;
                    op.Progress([&](auto const&, auto progress) {
                        progress_token(static_cast<uint32_t>(progress));
                        if (m_enable_metrics_collection.load()) {
                            std::scoped_lock guard_metrics(m_mutex_metrics);
                            m_metrics.bytes_delta += progress - op_req_bytes;
                        }
                        op_req_bytes = progress;
                    });
                    co_await std::move(op);
                    util::debug::log_trace(std::format(L"Done fetching http range {}-{} (CorrelationId: {:08x})", start, end, correlation_id));
                    co_return;
                }
                catch (hresult_canceled const&) { throw; }
                catch (hresult_error const& e) {
                    util::debug::log_debug(std::format(
                        L"HRAS: Failed to fetch `{}` with range {}-{} (0x{:08x}: {}) (CorrelationId: {:08x})",
                        cur_uri.ToString(), start, end,
                        static_cast<uint32_t>(e.code()), e.message(),
                        correlation_id
                    ));
                    if (e.code() == E_CHANGED_STATE) {
                        // Workaround HttpClient concurrency issue by ignoring E_CHANGED_STATE
                        util::debug::log_debug(L"HRAS: Ignoring E_CHANGED_STATE exception");
                        continue;
                    }
                    if (e.code() == E_ABORT || e.code() == E_HANDLE) {
                        // HttpClient is reallocating resources; don't treat this as an error
                        util::debug::log_debug(L"HRAS: Ignoring E_ABORT / E_HANDLE exception");
                        continue;
                    }
                    // Fetch failed, drop current uri
                    // TODO: Implement retry policy
                    std::unique_lock guard_uri(m_mutex_http_uris);
                    if (!m_http_uris.empty() && cur_uri == m_http_uris.front()) {
                        m_http_uris.pop_front();
                    }
                }

                co_await std::chrono::milliseconds(100);
            }
        }

        IRandomAccessStream m_buf_stream;
        std::mutex m_mutex_ea_nur;
        com_ptr<NewUriRequestedEventArgs> m_ea_nur;
        std::shared_mutex m_mutex_http_uris;
        std::deque<Uri> m_http_uris;
        std::shared_mutex m_mutex_unbuffered_intervals;
        std::vector<std::pair<uint64_t, uint64_t>> m_unbuffered_intervals;
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

        co_await resume_background();

        if (buffer_options == HttpRandomAccessStreamBufferOptions::ImmediateFull) {
            // This option does not require partial downloading, so handle this separately
            auto http_resp = co_await http_client.GetAsync(http_uri, HttpCompletionOption::ResponseHeadersRead);
            http_resp.EnsureSuccessStatusCode();
            auto buf_mem_stream = InMemoryRandomAccessStream();
            // TODO: Maybe optimize performance by explicitly setting stream size
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
        auto status_code = http_resp.EnsureSuccessStatusCode().StatusCode();
        if (status_code != HttpStatusCode::PartialContent) {
            util::debug::log_warn(std::format(
                L"HRAS: Requested resource does not support partial downloading (HTTP {}), "
                "ignoring as a workaround",
                std::to_underlying(status_code)
            ));
            /*throw hresult_error(E_FAIL, std::format(
                L"Requested resource does not support partial downloading (HTTP {})",
                std::to_underlying(status_code)
            ));*/
        }

        // Server supports ranges; proceed with creation
        auto http_resp_cont_hdrs = http_resp.Content().Headers();
        auto cont_type = http_resp_cont_hdrs.ContentType().MediaType();
        auto nullable_cont_len = http_resp_cont_hdrs.ContentLength();
        if (!nullable_cont_len) {
            throw hresult_error(E_FAIL, L"HTTP partial response does not have Content-Length set");
        }
        auto cont_len = nullable_cont_len.Value();

        util::debug::log_trace(std::format(L"New HttpRandomAccessStream: {}, {} Bytes", cont_type, cont_len));

        if (buffer_options == HttpRandomAccessStreamBufferOptions::None) {
            co_return make<HttpRandomAccessStream>(std::make_shared<HttpRandomAccessStreamImpl_Direct>(
                http_uri, http_client, cont_len, false), cont_type);
        }
        else if (buffer_options == HttpRandomAccessStreamBufferOptions::Full) {
            co_return make<HttpRandomAccessStream>(std::make_shared<HttpRandomAccessStreamImpl_StreamBased>(
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
    com_array<Uri> HttpRandomAccessStream::GetActiveUris() {
        std::shared_lock guard_impl(m_impl_mutex);
        if (!m_impl) { throw hresult_illegal_method_call(); }
        return m_impl->GetActiveUris();
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
    HttpRandomAccessStreamRetryPolicy HttpRandomAccessStream::RetryPolicy() {
        std::shared_lock guard_impl(m_impl_mutex);
        if (!m_impl) { throw hresult_illegal_method_call(); }
        return m_impl->RetryPolicy();
    }
    void HttpRandomAccessStream::RetryPolicy(HttpRandomAccessStreamRetryPolicy const& value) {
        std::shared_lock guard_impl(m_impl_mutex);
        if (!m_impl) { throw hresult_illegal_method_call(); }
        return m_impl->RetryPolicy(value);
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
            async = impl->ReadAtAsync(std::move(buffer), m_cur_pos, m_cur_pos + count, options);
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
