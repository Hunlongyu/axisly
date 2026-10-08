#include "allocation_probe.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>
#include <stdexcept>

namespace {
struct Header { void* base; std::size_t requested; std::uint64_t epoch; };
struct Counter {
    bool enabled{};
    std::uint64_t epoch{};
    axisly::bench::AllocationStats stats;
};
thread_local Counter counter;
std::atomic<std::uint64_t> next_epoch{};

void* allocate(std::size_t requested, std::size_t alignment) {
    alignment = std::max(alignment,alignof(Header));
    const auto payload = std::max(requested,std::size_t{1});
    if (payload > std::numeric_limits<std::size_t>::max()-sizeof(Header)-(alignment-1)) throw std::bad_alloc();
    void* base{};
    while (!(base = std::malloc(payload+sizeof(Header)+alignment-1))) {
        const auto handler = std::get_new_handler();
        if (!handler) throw std::bad_alloc();
        handler();
    }
    const auto raw = reinterpret_cast<std::uintptr_t>(base)+sizeof(Header);
    const auto aligned = (raw+alignment-1)&~static_cast<std::uintptr_t>(alignment-1);
    auto* header = reinterpret_cast<Header*>(aligned)-1;
    *header = {base,requested,counter.enabled ? counter.epoch : 0};
    if (counter.enabled) {
        ++counter.stats.allocations;
        ++counter.stats.live_allocations;
        counter.stats.requested_bytes += requested;
        counter.stats.live_bytes += requested;
        counter.stats.peak_live_bytes = std::max(counter.stats.peak_live_bytes,counter.stats.live_bytes);
    }
    return reinterpret_cast<void*>(aligned);
}
void release(void* pointer) noexcept {
    if (!pointer) return;
    const auto* header = static_cast<const Header*>(pointer)-1;
    if (counter.enabled && header->epoch == counter.epoch) {
        counter.stats.live_bytes -= header->requested;
        --counter.stats.live_allocations;
    }
    std::free(header->base);
}
}

// MSVC's max_align_t can be smaller than the alignment promised by ordinary
// operator new. Compilers may use aligned stores even for an 8-aligned type.
void* operator new(std::size_t size) { return allocate(size,__STDCPP_DEFAULT_NEW_ALIGNMENT__); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { release(pointer); }
void operator delete[](void* pointer) noexcept { release(pointer); }
void operator delete(void* pointer,std::size_t) noexcept { release(pointer); }
void operator delete[](void* pointer,std::size_t) noexcept { release(pointer); }
void* operator new(std::size_t size,std::align_val_t alignment) { return allocate(size,static_cast<std::size_t>(alignment)); }
void* operator new[](std::size_t size,std::align_val_t alignment) { return ::operator new(size,alignment); }
void operator delete(void* pointer,std::align_val_t) noexcept { release(pointer); }
void operator delete[](void* pointer,std::align_val_t) noexcept { release(pointer); }
void operator delete(void* pointer,std::size_t,std::align_val_t) noexcept { release(pointer); }
void operator delete[](void* pointer,std::size_t,std::align_val_t) noexcept { release(pointer); }
void* operator new(std::size_t size,const std::nothrow_t&) noexcept {
    try { return ::operator new(size); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t size,const std::nothrow_t&) noexcept { return ::operator new(size,std::nothrow); }
void operator delete(void* pointer,const std::nothrow_t&) noexcept { release(pointer); }
void operator delete[](void* pointer,const std::nothrow_t&) noexcept { release(pointer); }
void* operator new(std::size_t size,std::align_val_t alignment,const std::nothrow_t&) noexcept {
    try { return ::operator new(size,alignment); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t size,std::align_val_t alignment,const std::nothrow_t&) noexcept { return ::operator new(size,alignment,std::nothrow); }
void operator delete(void* pointer,std::align_val_t,const std::nothrow_t&) noexcept { release(pointer); }
void operator delete[](void* pointer,std::align_val_t,const std::nothrow_t&) noexcept { release(pointer); }

namespace axisly::bench {
void begin_allocations() {
    if (counter.enabled) throw std::logic_error("Allocation probes cannot nest");
    counter.stats = {};
    counter.epoch = next_epoch.fetch_add(1,std::memory_order_relaxed)+1;
    counter.enabled = true;
}
AllocationStats end_allocations() noexcept {
    counter.enabled = false;
    return counter.stats;
}
void allocation_self_test() {
    void* outside = ::operator new(17);
    begin_allocations();
    ::operator delete(outside); // Pre-existing storage must not affect counters.
    void* plain = ::operator new(37);
    void* array = ::operator new[](23,std::nothrow);
    void* aligned = ::operator new(128,std::align_val_t{64});
    void* aligned_array = ::operator new[](256,std::align_val_t{128},std::nothrow);
    const bool alignment_ok = reinterpret_cast<std::uintptr_t>(plain)%__STDCPP_DEFAULT_NEW_ALIGNMENT__ == 0 &&
        reinterpret_cast<std::uintptr_t>(array)%__STDCPP_DEFAULT_NEW_ALIGNMENT__ == 0 &&
        reinterpret_cast<std::uintptr_t>(aligned)%64 == 0 &&
        reinterpret_cast<std::uintptr_t>(aligned_array)%128 == 0;
    ::operator delete(plain,std::size_t{37});
    ::operator delete[](array,std::nothrow);
    ::operator delete(aligned,std::size_t{128},std::align_val_t{64});
    ::operator delete[](aligned_array,std::align_val_t{128},std::nothrow);
    const auto stats = end_allocations();
    if (!alignment_ok || !array || !aligned_array || stats.allocations != 4 || stats.requested_bytes != 444 ||
        stats.peak_live_bytes != 444 || stats.live_bytes != 0 || stats.live_allocations != 0)
        throw std::runtime_error("Allocation counter contract failed");
    begin_allocations();
    void* empty = ::operator new(0);
    ::operator delete(empty);
    const auto zero = end_allocations();
    if (zero.allocations != 1 || zero.requested_bytes != 0 || zero.live_allocations != 0)
        throw std::runtime_error("Zero-sized allocation contract failed");
}
} // namespace axisly::bench
