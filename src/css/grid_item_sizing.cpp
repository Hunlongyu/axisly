#include "grid_item_sizing.hpp"

#include <algorithm>

namespace axisly::css::detail {

GridItemSizing resolve_grid_item_sizing(const ResolvedBox& box,
    ResolvedDimensions stretch, GridItemSizingPhase phase) {
    auto ratio_box = box;
    // Item stretch dimensions are already border-box sizes. Scrollbars reduce
    // content space; they do not participate in the preferred ratio transfer.
    ratio_box.box_sizing = BoxSizing::BorderBox;
    ratio_box.scrollbar = {};
    auto bound_box = box;
    bound_box.scrollbar = {};
    GridItemSizing result;
    result.minimum = box.minimum;
    result.maximum = transfer_aspect_ratio(box.specified_maximum, bound_box);
    if (phase == GridItemSizingPhase::Final) {
        const Unit width_floor = bound_box.horizontal_decoration().value_or(0);
        const Unit height_floor = bound_box.vertical_decoration().value_or(0);
        result.minimum = {
            std::max(box.specified_minimum.width.value_or(width_floor), width_floor),
            std::max(box.specified_minimum.height.value_or(height_floor), height_floor)};
    }
    result.size = box.preferred;
    if (!result.size.width) result.size.width = stretch.width;
    result.size = transfer_aspect_ratio(result.size, ratio_box);
    if (!result.size.height) result.size.height = stretch.height;
    result.size = transfer_aspect_ratio(result.size, ratio_box);
    if (result.size.width) {
        result.size.width = constrain_axis(*result.size.width,
            result.minimum.width, result.maximum.width);
    }
    if (result.size.height) {
        result.size.height = constrain_axis(*result.size.height,
            result.minimum.height, result.maximum.height);
    }
    return result;
}

void finalize_grid_item_fragment(Fragment& fragment, const ResolvedBox& box,
    const GridItemSizing& sizing, ResolvedDimensions known) {
    const Size size{
        constrain_axis(known.width.value_or(fragment.size.width),
            sizing.minimum.width, sizing.maximum.width),
        constrain_axis(known.height.value_or(fragment.size.height),
            sizing.minimum.height, sizing.maximum.height)};
    set_fragment_border_size(fragment, size, box);
}

} // namespace axisly::css::detail
