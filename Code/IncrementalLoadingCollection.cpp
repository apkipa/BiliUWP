#include "pch.h"
#include "IncrementalLoadingCollection.h"

using namespace winrt;
using namespace Windows::Foundation;

namespace winrt::BiliUWP {
    IncrementalLoadingCollection MakeIncrementalLoadingCollection(
        std::shared_ptr<::BiliUWP::IIncrementalSource> src)
    {
        return make<implementation::IncrementalLoadingCollection>(std::move(src));
    }
    std::shared_ptr<::BiliUWP::IIncrementalSource> IncrementalSourceFromCollection(
        IncrementalLoadingCollection collection
    ) {
        return get_self<implementation::IncrementalLoadingCollection>(collection)->GetIncrementalSource();
    }
}

namespace winrt::BiliUWP::implementation {
    IAsyncOperation<Windows::UI::Xaml::Data::LoadMoreItemsResult> IncrementalLoadingCollection::LoadMoreItemsAsync(
        uint32_t count
    ) {
        auto cancellation_token = co_await get_cancellation_token();
        cancellation_token.enable_propagation();

        auto weak_store = util::winrt::make_weak_storage(*this);
        auto src = m_src;   // Keep source alive

        if (m_is_loading.exchange(true)) { co_return{}; }
        m_on_start_loading(*this, nullptr);
        deferred([&]() {
            if (!weak_store.lock()) { return; }
            m_on_end_loading(*this, nullptr);
            weak_store->m_is_loading.store(false);
        });

        auto on_exception_fn = [&] {
            if (!weak_store.lock()) { return; }
            weak_store->m_has_more = false;
            weak_store->m_on_error(*this, nullptr);
            // NOTE: Caller dislikes exceptions, so swallow them
        };

        try {
            auto result = std::move(co_await weak_store.ual(src->GetMoreItemsAsync(count)));
            auto actual_count = result.size();
            if (actual_count == 0) {
                m_has_more = false;
                co_return{ .Count = 0 };
            }

            if (m_vec.Size() == 0) {
                m_vec.ReplaceAll(std::move(result));
            }
            else {
                for (auto const& i : std::move(result)) {
                    m_vec.Append(i);
                }
            }
            co_return{ .Count = static_cast<uint32_t>(actual_count) };
        }
        catch (hresult_canceled const&) { on_exception_fn(); }
        catch (...) {
            util::winrt::log_current_exception();
            on_exception_fn();
        }

        co_return{};
    }
    void IncrementalLoadingCollection::Reload(void) {
        if (m_is_loading.load()) { return; }
        this->GetIncrementalSource()->Reset();
        m_has_more = true;
        m_vec.Clear();
    }
}
