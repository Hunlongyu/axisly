#pragma once

#include "layout_engine.hpp"

namespace axisly::css::detail {

enum class GridItemSizingPhase { Intrinsic, Final };

struct GridItemSizing {
    ResolvedDimensions minimum, maximum;
    ResolvedDimensions size;
};

// Stretch fills the inline axis first. Its ratio-derived block size takes
// precedence over block-axis stretch. Intrinsic probes retain optional bounds;
// final item sizing floors both minimum axes at padding and border.
[[nodiscard]] GridItemSizing resolve_grid_item_sizing(const ResolvedBox& box,
    ResolvedDimensions stretch, GridItemSizingPhase phase);

// Grid reports the item's final border box independently from the child's
// content layout. Keep its measured overflow and descendants intact.
void finalize_grid_item_fragment(Fragment& fragment, const ResolvedBox& box,
    const GridItemSizing& sizing, ResolvedDimensions known);

} // namespace axisly::css::detail
