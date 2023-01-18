#pragma once

#include "util.hpp"

// TODO: HttpCache

namespace BiliUWP {
    namespace details { struct HttpCacheImpl; }
    struct HttpCache {
        HttpCache(std::nullptr_t) : m_impl(nullptr) {}
        ~HttpCache() {}
        static util::winrt::task<HttpCache> create_async(
            winrt::Windows::Storage::StorageFolder const& root,
            winrt::hstring const& name,
            winrt::Windows::Web::Http::Filters::IHttpFilter const& http_filter
        );
        // NOTE: The default age of cache is obtained from Cache-Control in HTTP response.
        //       If not present, `Cache-Control: no-cache` will be assumed
        // NOTE: Only received bytes are used in HttpProgress
        winrt::Windows::Foundation::IAsyncOperationWithProgress<
            winrt::Windows::Storage::Streams::IRandomAccessStream,
            winrt::Windows::Web::Http::HttpProgress
        > fetch_async(
            winrt::Windows::Foundation::Uri const& uri
        ) const;
        winrt::Windows::Foundation::IAsyncOperationWithProgress<
            winrt::Windows::Storage::Streams::IRandomAccessStream,
            winrt::Windows::Web::Http::HttpProgress
        > fetch_async(
            winrt::Windows::Foundation::Uri const& uri,
            uint64_t override_age   // In seconds
        ) const;
        winrt::Windows::Foundation::IAsyncOperationWithProgress<
            winrt::Windows::Foundation::Uri,
            winrt::Windows::Web::Http::HttpProgress
        > fetch_as_local_uri_async(
            winrt::Windows::Foundation::Uri const& uri
        ) const;
        winrt::Windows::Foundation::IAsyncOperationWithProgress<
            winrt::Windows::Foundation::Uri,
            winrt::Windows::Web::Http::HttpProgress
        > fetch_as_local_uri_async(
            winrt::Windows::Foundation::Uri const& uri,
            uint64_t override_age   // In seconds
        ) const;
        // NOTE: The following two methods only works for specific directories
        //       that is accessible through ms-appdata:/// protocol. If such uri
        //       does not exist, an exception will be thrown.
        winrt::Windows::Foundation::IAsyncOperationWithProgress<
            winrt::Windows::Foundation::Uri,
            winrt::Windows::Web::Http::HttpProgress
        > fetch_as_app_uri_async(
            winrt::Windows::Foundation::Uri const& uri
        ) const;
        winrt::Windows::Foundation::IAsyncOperationWithProgress<
            winrt::Windows::Foundation::Uri,
            winrt::Windows::Web::Http::HttpProgress
        > fetch_as_app_uri_async(
            winrt::Windows::Foundation::Uri const& uri,
            uint64_t override_age   // In seconds
        ) const;
        util::winrt::task<> remove_expired_async(void) const;
        util::winrt::task<> clear_async(void) const;

        operator bool() const { return static_cast<bool>(m_impl); }
        bool operator==(std::nullptr_t) const { return m_impl == nullptr; }
    private:
        std::shared_ptr<details::HttpCacheImpl> m_impl;
    };
}
