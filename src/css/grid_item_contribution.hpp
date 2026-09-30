#pragma once

#include "grid_internal.hpp"

namespace axisly::css::detail {

enum class GridCrossSizing { MaximumEstimate, ResolvedTracks };

struct GridContributionInput {
    const LayoutNode& parent;
    const Placement& placement;
    const TrackAxis& columns;
    const TrackAxis& rows;
    ResolvedDimensions container_inner;
    bool columns_axis{};
    GridCrossSizing cross_sizing{GridCrossSizing::ResolvedTracks};
    Unit baseline_shim{};
};

struct GridAxisContribution {
    Unit minimum{}, min_content{}, max_content{};
    Unit margin_start{}, margin_end{};
};

struct GridBaselineItem {
    Unit shim{};
    std::optional<Unit> ascent;
};

[[nodiscard]] bool grid_baseline_participant(const LayoutNode& parent, const LayoutNode& child);

} // namespace axisly::css::detail
