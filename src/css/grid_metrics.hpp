#pragma once

#include <cstdint>

namespace axisly::css::detail {
// Development-only counters. Ordinary builds contain no counter updates.
struct GridMetrics {
    std::uint64_t structure_builds{}, structure_hits{}, placement_probes{};
    std::uint64_t occupied_visits{}, auto_fit_visits{}, limit_visits{};
};
#ifdef AXISLY_ENABLE_GRID_METRICS
inline thread_local GridMetrics grid_metrics;
#define AXISLY_GRID_COUNT(field, amount) (::axisly::css::detail::grid_metrics.field += (amount))
#else
#define AXISLY_GRID_COUNT(field, amount) ((void)0)
#endif
} // namespace axisly::css::detail
