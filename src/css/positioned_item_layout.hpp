#pragma once

#include "layout_engine.hpp"

namespace axisly::css::detail {

struct PositionedItemInput {
    Display parent_display;
    Size area; // Insets, style percentages and item edges resolve here.
    Size containing_block; // The child's own layout may use the Flex content box.
    Size available;
    ResolvedDimensions start_inset, end_inset;
};

} // namespace axisly::css::detail
