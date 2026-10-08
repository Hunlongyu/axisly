#include "block_item_sizing.hpp"

#include <algorithm>

namespace axisly::css::detail {
namespace {
ResolvedEdge resolved_item_edge(const Length& value, std::optional<Unit> basis) {
    return {EdgeKind::Numeric, value.resolve_numeric(basis).value_or(Unit{0})};
}

} // namespace

const LayoutBox& resolve_block_item_box(const LayoutNode& child,
    ResolvedDimensions percentage_basis, Unit used_inline_size, LayoutBox& scratch) {
    try {
        const auto& resolved = resolve_layout_box(child, percentage_basis.width, percentage_basis.height);
        if (!child.box_depends_on_width && !child.box_depends_on_height
            && !resolved.aspect_ratio) return resolved;
        auto& box = scratch;
        box = resolved;
        // Taffy 0.14's Block item list resolves reported vertical padding and
        // border against the block-axis basis. Child flow still resolves all
        // percentage padding against its containing inline width.
        const auto scrollbar = box.scrollbar;
        box.scrollbar = {};
        if (child.vertical_edges_depend_on_width && percentage_basis.width != percentage_basis.height) {
            auto padding = box.padding();
            auto border = box.border();
            padding.top = resolved_item_edge(child.style.box.padding.top, percentage_basis.height).value;
            padding.bottom = resolved_item_edge(child.style.box.padding.bottom, percentage_basis.height).value;
            border.top = resolved_item_edge(child.style.box.border.top, percentage_basis.height).value;
            border.bottom = resolved_item_edge(child.style.box.border.bottom, percentage_basis.height).value;
            box.set_decoration(padding, border);
            resolve_box_dimensions(box, child.style.box,
                percentage_basis.width, percentage_basis.height);
        }
        box.maximum = transfer_aspect_ratio(box.specified_maximum, box);
        box.scrollbar = scrollbar;
        // Cyclic percentages remain indefinite for item dimensions. Margins
        // are resolved later, after the container has selected its used width.
        if (child.margin_depends_on_width && percentage_basis.width != used_inline_size) {
            box.margin = resolve_box_margins(child.style.box.margin, used_inline_size);
        }
        return box;
    } catch (const std::invalid_argument& error) {
        throw LayoutFailure(LayoutErrorCode::InvalidStyle, child.id, error.what());
    }
}

BlockItemSizing resolve_block_item(const LayoutNode& child,
    ResolvedDimensions percentage_basis, Unit used_inline_size, LayoutBox& scratch) {
    const auto& box = resolve_block_item_box(child, percentage_basis, used_inline_size, scratch);
    BlockItemSizing result{&box};
    result.intrinsic_width = child.style.item_is_table || child.style.box.replaced;
    auto& constraints = result.constraints;
    constraints.available.width = result.intrinsic_width
        ? AvailableSpace::max_content() : AvailableSpace::definite(used_inline_size);
    constraints.available.height = AvailableSpace::indefinite();
    constraints.containing_width = used_inline_size;
    constraints.containing_height = percentage_basis.height;
    const auto margin = edges(box.margin, child.id);
    const auto width_kind = child.style.box.size.width.kind();
    result.unresolved_normal_width = width_kind == LengthKind::Auto
        || width_kind == LengthKind::Percent || width_kind == LengthKind::Calc;
    if (child.style.float_side == FloatSide::None && !result.intrinsic_width) {
        if (box.preferred.width) {
            constraints.known_width = constrain_axis(*box.preferred.width,
                box.minimum.width, box.maximum.width);
        } else if (result.unresolved_normal_width || width_kind == LengthKind::Stretch) {
            constraints.known_width = constrain_axis(std::max(Unit{0}, checked(
                static_cast<double>(used_inline_size) - margin.left - margin.right, child.id)),
                box.minimum.width, box.maximum.width);
        }
        if (box.preferred.height) {
            constraints.known_height = constrain_axis(*box.preferred.height,
                box.minimum.height, box.maximum.height);
        }
    }
    if (!constraints.known_height && child.style.box.size.height.kind() == LengthKind::Stretch
        && percentage_basis.height) {
        constraints.known_height = std::max(Unit{0}, *percentage_basis.height - margin.top - margin.bottom);
    }
    return result;
}
} // namespace axisly::css::detail
