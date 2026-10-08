#include <axisly/css_memory.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <mutex>

namespace axisly::css {

struct LayoutMemory::Impl {
    // Metadata occupies a freed block, so deallocation never allocates and
    // the cache needs no independently allocated hash table entries.
    struct FreeBlock { FreeBlock* next; std::size_t bytes, alignment; };
    static constexpr std::size_t minimum_cached_size = 4096;
    std::array<FreeBlock*, 256> buckets{};
    mutable std::mutex mutex;
    std::size_t limit;
    LayoutMemoryStats stats;
    explicit Impl(std::size_t bytes) : limit(bytes) {}
    static std::size_t actual_alignment(std::size_t bytes, std::size_t alignment) noexcept {
        return bytes >= minimum_cached_size ? std::max(alignment, alignof(FreeBlock)) : alignment;
    }
    static std::size_t bucket(std::size_t bytes, std::size_t alignment) noexcept {
        return ((bytes >> 4) ^ (bytes >> 12) ^ alignment) % 256;
    }
    FreeBlock* detach_unused() noexcept {
        FreeBlock* released = nullptr;
        for (auto& head : buckets) {
            while (head) {
                auto* block = head;
                head = block->next;
                block->next = released;
                released = block;
            }
        }
        stats.cached_bytes = 0;
        return released;
    }
    static void release_blocks(FreeBlock* head) noexcept {
        while (head) {
            auto* block = head;
            head = block->next;
            const auto bytes = block->bytes, alignment = block->alignment;
            std::destroy_at(block);
            std::pmr::new_delete_resource()->deallocate(block, bytes, alignment);
        }
    }
    void acquired(std::size_t bytes) noexcept {
        stats.live_bytes += bytes;
        stats.peak_live_bytes = std::max(stats.peak_live_bytes, stats.live_bytes);
    }
};

LayoutMemory::LayoutMemory(std::size_t limit) : impl_(std::make_unique<Impl>(limit)) {}
LayoutMemory::~LayoutMemory() {
    assert(impl_->stats.live_bytes == 0);
    Impl::release_blocks(impl_->detach_unused());
}
LayoutMemoryStats LayoutMemory::stats() const {
    const std::lock_guard lock(impl_->mutex);
    return impl_->stats;
}
std::size_t LayoutMemory::cache_limit() const {
    const std::lock_guard lock(impl_->mutex);
    return impl_->limit;
}
void LayoutMemory::set_cache_limit(std::size_t bytes) {
    Impl::FreeBlock* released = nullptr;
    {
        const std::lock_guard lock(impl_->mutex);
        impl_->limit = bytes;
        if (impl_->stats.cached_bytes > bytes) released = impl_->detach_unused();
    }
    Impl::release_blocks(released);
}
void LayoutMemory::release_unused() {
    Impl::FreeBlock* released;
    { const std::lock_guard lock(impl_->mutex); released = impl_->detach_unused(); }
    Impl::release_blocks(released);
}
void* LayoutMemory::do_allocate(std::size_t bytes, std::size_t alignment) {
    alignment = Impl::actual_alignment(bytes, alignment);
    if (bytes >= Impl::minimum_cached_size) {
        const std::lock_guard lock(impl_->mutex);
        auto** link = &impl_->buckets[Impl::bucket(bytes, alignment)];
        while (*link) {
            auto* block = *link;
            if (block->bytes == bytes && block->alignment == alignment) {
                *link = block->next;
                std::destroy_at(block);
                impl_->stats.cached_bytes -= bytes;
                ++impl_->stats.reuse_count;
                impl_->acquired(bytes);
                return block;
            }
            link = &block->next;
        }
    }
    // A host new_handler may trim this very resource. Neither upstream
    // allocation nor deallocation may run while the cache mutex is held.
    auto* pointer = std::pmr::new_delete_resource()->allocate(bytes, alignment);
    try {
        const std::lock_guard lock(impl_->mutex);
        ++impl_->stats.upstream_allocations;
        impl_->acquired(bytes);
    } catch (...) {
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
        throw;
    }
    return pointer;
}
void LayoutMemory::do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) {
    alignment = Impl::actual_alignment(bytes, alignment);
    {
        const std::lock_guard lock(impl_->mutex);
        assert(impl_->stats.live_bytes >= bytes);
        impl_->stats.live_bytes -= bytes;
        if (bytes >= Impl::minimum_cached_size && bytes <= impl_->limit - impl_->stats.cached_bytes) {
            auto& head = impl_->buckets[Impl::bucket(bytes, alignment)];
            head = std::construct_at(static_cast<Impl::FreeBlock*>(pointer), head, bytes, alignment);
            impl_->stats.cached_bytes += bytes;
            return;
        }
    }
    std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
}
bool LayoutMemory::do_is_equal(const std::pmr::memory_resource& other) const noexcept {
    return this == &other;
}

} // namespace axisly::css
