#pragma once

#include "layout_engine.hpp"

namespace axisly::css::detail {
struct BlockItemSizing {
    const LayoutBox* box;
    Constraints constraints;
    bool intrinsic_width{}, unresolved_normal_width{};
};

// Stable resolved boxes can be borrowed. Stage-specific percentage and ratio
// adjustments use caller-owned storage, kept alive across the child's layout.
[[nodiscard]] const LayoutBox& resolve_block_item_box(const LayoutNode& child,
    ResolvedDimensions percentage_basis, Unit used_inline_size, LayoutBox& scratch);
[[nodiscard]] BlockItemSizing resolve_block_item(const LayoutNode& child,
    ResolvedDimensions percentage_basis, Unit used_inline_size, LayoutBox& scratch);
} // namespace axisly::css::detail
