#pragma once

#include "layout_engine.hpp"

namespace axisly::css::detail {
struct BlockItemSizing {
    ResolvedBox box;
    Constraints constraints;
    bool intrinsic_width{}, unresolved_normal_width{};
};

[[nodiscard]] BlockItemSizing resolve_block_item(const LayoutNode& child,
    ResolvedDimensions percentage_basis, Unit used_inline_size);
} // namespace axisly::css::detail
