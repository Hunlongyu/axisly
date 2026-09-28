#pragma once

#include <cstddef>

namespace gridly::bench {
struct AllocationStats {
    std::size_t allocations{};
    std::size_t requested_bytes{};
    std::size_t peak_live_bytes{};
    std::size_t live_bytes{};
    std::size_t live_allocations{};
};
void begin_allocations();
[[nodiscard]] AllocationStats end_allocations() noexcept;
void allocation_self_test();
} // namespace gridly::bench
