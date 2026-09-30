#include "block_item_sizing.hpp"

#include <algorithm>

namespace axisly::css::detail {
namespace {
ResolvedEdge resolved_item_edge(const Length& value, std::optional<Unit> basis) {
    return {EdgeKind::Numeric, value.resolve_numeric(basis.value_or(Unit{0})).value_or(Unit{0})};
}

EdgeValues numeric_edges(Insets values) {
    return {Length::length(values.left), Length::length(values.top),
        Length::length(values.right), Length::length(values.bottom)};
}
} // namespace

BlockItemSizing resolve_block_item(const LayoutNode& child,
    ResolvedDimensions percentage_basis, Unit used_inline_size) {
    BlockItemSizing result;
    try {
        result.box = resolve_layout_box(child, percentage_basis.width, percentage_basis.height);
        // Taffy 0.14's Block item list resolves reported vertical padding and
        // border against the block-axis basis. Child flow still resolves all
        // percentage padding against its containing inline width.
        result.box.padding.top = resolved_item_edge(child.style.box.padding.top, percentage_basis.height);
        result.box.padding.bottom = resolved_item_edge(child.style.box.padding.bottom, percentage_basis.height);
        result.box.border.top = resolved_item_edge(child.style.box.border.top, percentage_basis.height);
        result.box.border.bottom = resolved_item_edge(child.style.box.border.bottom, percentage_basis.height);
        auto sizing_style = child.style.box;
        sizing_style.padding = numeric_edges(edges(result.box.padding, child.id));
        sizing_style.border = numeric_edges(edges(result.box.border, child.id));
        const auto dimensions = resolve_box(sizing_style, percentage_basis.width,
            percentage_basis.height, IndefiniteEdgeBasis::Zero);
        result.box.preferred = dimensions.preferred;
        result.box.minimum = dimensions.minimum;
        result.box.maximum = transfer_aspect_ratio(dimensions.maximum, dimensions);
        result.box.specified_minimum = dimensions.specified_minimum;
        result.box.specified_maximum = dimensions.specified_maximum;
        // Cyclic percentages remain indefinite for item dimensions. Margins
        // are resolved later, after the container has selected its used width.
        BoxStyle margin_style;
        margin_style.margin = child.style.box.margin;
        result.box.margin = resolve_box(margin_style, used_inline_size,
            percentage_basis.height).margin;
    } catch (const std::invalid_argument& error) {
        throw LayoutFailure(LayoutErrorCode::InvalidStyle, child.id, error.what());
    }
    result.intrinsic_width = child.style.item_is_table || child.style.box.replaced;
    auto& constraints = result.constraints;
    constraints.available.width = result.intrinsic_width
        ? AvailableSpace::max_content() : AvailableSpace::definite(used_inline_size);
    constraints.available.height = AvailableSpace::indefinite();
    constraints.containing_width = used_inline_size;
    constraints.containing_height = percentage_basis.height;
    const auto margin = edges(result.box.margin, child.id);
    const auto width_kind = child.style.box.size.width.kind();
    result.unresolved_normal_width = width_kind == LengthKind::Auto
        || width_kind == LengthKind::Percent || width_kind == LengthKind::Calc;
    if (child.style.float_side == FloatSide::None && !result.intrinsic_width) {
        if (result.box.preferred.width) {
            constraints.known_width = constrain_axis(*result.box.preferred.width,
                result.box.minimum.width, result.box.maximum.width);
        } else if (result.unresolved_normal_width || width_kind == LengthKind::Stretch) {
            constraints.known_width = constrain_axis(std::max(Unit{0}, checked(
                static_cast<double>(used_inline_size) - margin.left - margin.right, child.id)),
                result.box.minimum.width, result.box.maximum.width);
        }
        if (result.box.preferred.height) {
            constraints.known_height = constrain_axis(*result.box.preferred.height,
                result.box.minimum.height, result.box.maximum.height);
        }
    }
    if (!constraints.known_height && child.style.box.size.height.kind() == LengthKind::Stretch
        && percentage_basis.height) {
        constraints.known_height = std::max(Unit{0}, *percentage_basis.height - margin.top - margin.bottom);
    }
    return result;
}
} // namespace axisly::css::detail
