#include <axisly/css_batch.hpp>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace axisly::css {

struct LayoutBatch::Impl {
    std::vector<std::thread> workers;
    std::vector<BatchResult> results;
    std::vector<LayoutTree*> identities;
    std::span<const LayoutRequest> requests;
    std::atomic<std::size_t> next{};
    std::atomic_flag busy = ATOMIC_FLAG_INIT;
    std::mutex mutex;
    std::condition_variable ready, finished;
    std::size_t pending{};
    bool stopping{}, generation{};

    explicit Impl(std::size_t count) {
        if (!count) throw std::invalid_argument("CSS batch thread count must be positive");
        workers.reserve(count - 1);
        try {
            for (std::size_t i = 1; i < count; ++i) workers.emplace_back([this] { worker(); });
        } catch (...) {
            stop();
            throw;
        }
    }
    ~Impl() { stop(); }
    void stop() noexcept {
        { const std::lock_guard lock(mutex); stopping = true; }
        ready.notify_all();
        for (auto& thread : workers) thread.join();
    }
    void calculate(std::size_t index) noexcept {
        const auto& request = requests[index];
        try {
            results[index].layout = request.rounding
                ? &request.tree->compute(request.root, request.available, *request.rounding)
                : &request.tree->compute(request.root, request.available);
        }
        catch (...) { results[index].exception = std::current_exception(); }
    }
    void run_jobs() noexcept {
        for (;;) {
            const auto index = next.fetch_add(1, std::memory_order_relaxed);
            if (index >= requests.size()) return;
            calculate(index);
        }
    }
    void worker() noexcept {
        bool observed = false;
        for (;;) {
            std::unique_lock lock(mutex);
            ready.wait(lock, [&] { return stopping || observed != generation; });
            if (stopping) return;
            observed = generation;
            lock.unlock();
            run_jobs();
            lock.lock();
            if (--pending == 0) finished.notify_one();
        }
    }
};

LayoutBatch::LayoutBatch(std::size_t threads) : impl_(std::make_unique<Impl>(threads)) {}
LayoutBatch::~LayoutBatch() = default;
std::size_t LayoutBatch::thread_count() const noexcept { return impl_->workers.size() + 1; }

std::span<const BatchResult> LayoutBatch::compute(std::span<const LayoutRequest> requests,
                                                BatchExecution execution) {
    if (impl_->busy.test_and_set(std::memory_order_acquire)) {
        throw std::logic_error("CSS batch executor is already computing");
    }
    struct Guard {
        std::atomic_flag& flag;
        ~Guard() { flag.clear(std::memory_order_release); }
    } guard{impl_->busy};
    impl_->identities.clear();
    impl_->identities.reserve(requests.size());
    for (const auto& request : requests) {
        if (!request.tree) throw std::invalid_argument("CSS batch contains a null tree");
        impl_->identities.push_back(request.tree);
    }
    std::sort(impl_->identities.begin(), impl_->identities.end(), std::less<LayoutTree*>{});
    if (std::adjacent_find(impl_->identities.begin(), impl_->identities.end()) != impl_->identities.end()) {
        throw std::invalid_argument("CSS batch contains a duplicate tree");
    }
    impl_->results.clear();
    impl_->results.resize(requests.size());
    impl_->requests = requests;
    if (execution == BatchExecution::CallerThread || impl_->workers.empty() || requests.size() <= 1) {
        for (std::size_t i = 0; i < requests.size(); ++i) impl_->calculate(i);
    } else {
        {
            const std::lock_guard lock(impl_->mutex);
            impl_->next.store(0, std::memory_order_relaxed);
            impl_->pending = impl_->workers.size();
            impl_->generation = !impl_->generation;
        }
        impl_->ready.notify_all();
        impl_->run_jobs();
        std::unique_lock lock(impl_->mutex);
        impl_->finished.wait(lock, [&] { return impl_->pending == 0; });
    }
    impl_->requests = {};
    return impl_->results;
}

} // namespace axisly::css
