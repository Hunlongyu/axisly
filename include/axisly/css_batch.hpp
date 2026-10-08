#pragma once

#include <axisly/css_tree.hpp>
#include <exception>

namespace axisly::css {

struct LayoutRequest {
    LayoutTree* tree{};
    NodeId root;
    AvailableSize available;
    std::optional<PixelRounding> rounding;
};
struct BatchResult {
    const LayoutResult* layout{};
    std::exception_ptr exception;
    [[nodiscard]] bool succeeded() const noexcept {
        return layout && layout->succeeded() && !exception;
    }
};
enum class BatchExecution {
    CallerThread,
    // Explicitly permits measurement callbacks on workers and concurrently
    // with callbacks from other trees. All shared callback state must be safe
    // for that use. Trees and requests require exclusive access until return.
    Parallel,
};

// Synchronous batches of independent trees, using persistent workers. The
// thread count includes the calling thread; one means fully sequential.
// Trees keep their ordinary single-threaded layout/cache implementation.
class LayoutBatch {
public:
    explicit LayoutBatch(std::size_t thread_count = 1);
    ~LayoutBatch();
    LayoutBatch(const LayoutBatch&) = delete;
    LayoutBatch& operator=(const LayoutBatch&) = delete;
    [[nodiscard]] std::size_t thread_count() const noexcept;

    // Rejects null or duplicate trees before computing anything. Reentrant or
    // concurrent calls on this executor throw logic_error. Callback exceptions
    // are captured per request; all requests finish before return. Successful
    // trees publish normally even if another request fails.
    // The returned span lasts until the next call or executor destruction;
    // layout pointers obey the corresponding LayoutTree result lifetime.
    [[nodiscard]] std::span<const BatchResult> compute(std::span<const LayoutRequest> requests,
        BatchExecution execution = BatchExecution::CallerThread);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace axisly::css
