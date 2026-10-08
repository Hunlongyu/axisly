#pragma once

#include <cstddef>
#include <memory>
#include <memory_resource>

namespace axisly::css {

struct LayoutMemoryStats {
    std::size_t live_bytes{}, cached_bytes{}, peak_live_bytes{};
    std::size_t upstream_allocations{}, reuse_count{};
};

// Reuses freed large allocations across trees. The limit bounds unused cached
// bytes, not live tree storage. Small allocations go directly to the system.
// Thread-safe; trees themselves still require exclusive access. For parallel
// workloads, separate resources per worker avoid allocator contention.
class LayoutMemory final : public std::pmr::memory_resource {
public:
    explicit LayoutMemory(std::size_t cache_limit = 64 * 1024 * 1024);
    ~LayoutMemory() override;
    LayoutMemory(const LayoutMemory&) = delete;
    LayoutMemory& operator=(const LayoutMemory&) = delete;
    [[nodiscard]] LayoutMemoryStats stats() const;
    [[nodiscard]] std::size_t cache_limit() const;
    // Reducing the limit below current retention releases all unused blocks.
    void set_cache_limit(std::size_t bytes);
    // Does not invalidate live trees, results, or allocations.
    void release_unused();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    void* do_allocate(std::size_t bytes, std::size_t alignment) override;
    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override;
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override;
};

} // namespace axisly::css
