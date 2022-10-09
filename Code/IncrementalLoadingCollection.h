#pragma once
#include "IncrementalLoadingCollection.g.h"
#include "util.hpp"

namespace BiliUWP {
    // NOTE: It can be safely assumed that `this` is always alive during task execution
    struct IIncrementalSource : std::enable_shared_from_this<IIncrementalSource> {
        virtual util::winrt::task<std::vector<winrt::Windows::Foundation::IInspectable>> GetMoreItemsAsync(
            uint32_t expected_count
        ) = 0;
        virtual void Reset(void) = 0;
    };
    /*
    template<typename ContinuationContext>
    struct IncrementalSource {
        IncrementalSource(ContinuationContext ctx) : m_ctx(std::move(ctx)) {}
        util::winrt::task<std::vector<winrt::Windows::Foundation::IInspectable>> GetMoreItemsAsync(
            ContinuationContext& ctx
        );
        void Reset(ContinuationContext& ctx);
    private:
        ContinuationContext m_ctx;
    };
    */
}

namespace winrt::BiliUWP {
    IncrementalLoadingCollection MakeIncrementalLoadingCollection(
        std::shared_ptr<::BiliUWP::IIncrementalSource> src);
    std::shared_ptr<::BiliUWP::IIncrementalSource> IncrementalSourceFromCollection(
        IncrementalLoadingCollection collection
    );
}

namespace winrt::BiliUWP::implementation {
    struct IncrementalLoadingCollection : IncrementalLoadingCollectionT<IncrementalLoadingCollection> {
        IncrementalLoadingCollection(std::shared_ptr<::BiliUWP::IIncrementalSource> src) :
            m_vec(single_threaded_observable_vector<Windows::Foundation::IInspectable>()),
            m_src(std::move(src)), m_is_loading(false), m_has_more(true) {}

        Windows::Foundation::Collections::IIterator<Windows::Foundation::IInspectable> First() { return m_vec.First(); }
        Windows::Foundation::IInspectable GetAt(uint32_t index) { return m_vec.GetAt(index); }
        uint32_t Size() { return m_vec.Size(); }
        Windows::Foundation::Collections::IVectorView<Windows::Foundation::IInspectable> GetView() { return m_vec.GetView(); }
        bool IndexOf(Windows::Foundation::IInspectable const& value, uint32_t& index) { return m_vec.IndexOf(value, index); }
        void SetAt(uint32_t index, Windows::Foundation::IInspectable const& value) { m_vec.SetAt(index, value); }
        void InsertAt(uint32_t index, Windows::Foundation::IInspectable const& value) { m_vec.InsertAt(index, value); }
        void RemoveAt(uint32_t index) { m_vec.RemoveAt(index); }
        void Append(Windows::Foundation::IInspectable const& value) { m_vec.Append(value); }
        void RemoveAtEnd() { m_vec.RemoveAtEnd(); }
        void Clear() { m_vec.Clear(); }
        uint32_t GetMany(uint32_t startIndex, array_view<Windows::Foundation::IInspectable> items) {
            return m_vec.GetMany(startIndex, items);
        }
        void ReplaceAll(array_view<Windows::Foundation::IInspectable const> items) { m_vec.ReplaceAll(items); }
        event_token VectorChanged(
            Windows::Foundation::Collections::VectorChangedEventHandler<Windows::Foundation::IInspectable> const& vhnd
        ) {
            return m_vec.VectorChanged(vhnd);
        }
        void VectorChanged(event_token const& token) noexcept { m_vec.VectorChanged(token); };
        Windows::Foundation::IAsyncOperation<Windows::UI::Xaml::Data::LoadMoreItemsResult> LoadMoreItemsAsync(
            uint32_t count
        );
        bool HasMoreItems() { return m_has_more; }

        void Reload(void);
        bool IsLoading() { return m_is_loading; };

        event_token OnStartLoading(
            Windows::Foundation::TypedEventHandler<winrt::BiliUWP::IncrementalLoadingCollection, IInspectable> const& handler
        ) {
            return m_on_start_loading.add(handler);
        }
        void OnStartLoading(event_token const& token) noexcept {
            m_on_start_loading.remove(token);
        }
        event_token OnEndLoading(
            Windows::Foundation::TypedEventHandler<winrt::BiliUWP::IncrementalLoadingCollection, IInspectable> const& handler
        ) {
            return m_on_end_loading.add(handler);
        }
        void OnEndLoading(event_token const& token) noexcept {
            m_on_end_loading.remove(token);
        }
        event_token OnError(
            Windows::Foundation::TypedEventHandler<winrt::BiliUWP::IncrementalLoadingCollection, IInspectable> const& handler
        ) {
            return m_on_error.add(handler);
        }
        void OnError(event_token const& token) noexcept {
            m_on_error.remove(token);
        }

        std::shared_ptr<::BiliUWP::IIncrementalSource> GetIncrementalSource(void) { return m_src; }

    private:
        Windows::Foundation::Collections::IObservableVector<IInspectable> m_vec;
        event<Windows::Foundation::TypedEventHandler<winrt::BiliUWP::IncrementalLoadingCollection, IInspectable>> m_on_start_loading;
        event<Windows::Foundation::TypedEventHandler<winrt::BiliUWP::IncrementalLoadingCollection, IInspectable>> m_on_end_loading;
        event<Windows::Foundation::TypedEventHandler<winrt::BiliUWP::IncrementalLoadingCollection, IInspectable>> m_on_error;
        std::shared_ptr<::BiliUWP::IIncrementalSource> m_src;
        std::atomic_bool m_is_loading;
        bool m_has_more;
    };
}
