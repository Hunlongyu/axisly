#include "positioned_item_layout.hpp"

#include <algorithm>
#include <variant>

namespace axisly::css::detail {
namespace {

using KeywordSize = std::variant<Unit, AvailableSpace>;

std::optional<KeywordSize> resolve_keyword(const Length& value,
    Unit stretch, Unit basis) {
    switch (value.kind()) {
    case LengthKind::MinContent: return AvailableSpace::min_content();
    case LengthKind::MaxContent: return AvailableSpace::max_content();
    case LengthKind::FitContent: return AvailableSpace::definite(stretch);
    case LengthKind::FitContentLength:
    case LengthKind::FitContentPercent:
        return AvailableSpace::definite(value.resolve_fit_content_limit(basis).value());
    case LengthKind::Stretch: return stretch;
    default: return std::nullopt;
    }
}

struct PositionedSize {
    ResolvedDimensions known, minimum, maximum;
    ResolvedBox ratio_box;

    void apply_ratio() { known = transfer_aspect_ratio(known, ratio_box); }
    void clamp() {
        if (known.width) *known.width = constrain_axis(*known.width, minimum.width, maximum.width);
        if (known.height) *known.height = constrain_axis(*known.height, minimum.height, maximum.height);
    }
    [[nodiscard]] Size clamp(Size value) const {
        return {constrain_axis(value.width, minimum.width, maximum.width),
            constrain_axis(value.height, minimum.height, maximum.height)};
    }
};

PositionedSize resolve_positioned_size(const ResolvedBox& box, bool grid) {
    auto bounds_box = box;
    bounds_box.scrollbar = {};
    auto ratio_box = bounds_box;
    ratio_box.box_sizing = BoxSizing::BorderBox;
    const Unit width_floor = bounds_box.horizontal_decoration().value_or(0);
    const Unit height_floor = bounds_box.vertical_decoration().value_or(0);
    auto minimum = grid ? box.specified_minimum : box.minimum;
    minimum.width = std::max(minimum.width.value_or(width_floor), width_floor);
    minimum.height = std::max(minimum.height.value_or(height_floor), height_floor);
    if (grid) minimum = transfer_aspect_ratio(minimum, ratio_box);
    return {box.preferred, minimum,
        transfer_aspect_ratio(box.specified_maximum, bounds_box), ratio_box};
}

} // namespace

Fragment Engine::positioned_item(const LayoutNode& child,
    const ResolvedBox& box, const PositionedItemInput& input, std::size_t depth) {
    const bool grid = input.parent_display == Display::Grid;
    const auto margin = edges(box.margin, child.id);
    auto sizing = resolve_positioned_size(box, grid);
    const Size fill{
        std::max(Unit{0}, checked(static_cast<double>(input.area.width)
            - margin.left - margin.right - input.start_inset.width.value_or(0)
            - input.end_inset.width.value_or(0), child.id)),
        std::max(Unit{0}, checked(static_cast<double>(input.area.height)
            - margin.top - margin.bottom - input.start_inset.height.value_or(0)
            - input.end_inset.height.value_or(0), child.id))};
    const auto keyword_width = resolve_keyword(child.style.box.size.width,
        fill.width, input.area.width);
    const auto keyword_height = resolve_keyword(child.style.box.size.height,
        fill.height, input.area.height);
    Constraints constraints;
    constraints.containing_width = input.containing_block.width;
    constraints.containing_height = input.containing_block.height;
    constraints.sizing_mode = grid ? SizingMode::Inherent : SizingMode::Content;
    constraints.ignore_preferred_width = true;
    constraints.ignore_preferred_height = true;
    constraints.content_based_width = child.style.box.size.width.kind()
        == LengthKind::Auto && !(input.start_inset.width && input.end_inset.width);
    const auto probe = [&](ResolvedDimensions known, AvailableSize available) {
        auto query = constraints;
        query.known_width = known.width;
        query.known_height = known.height;
        query.available = available;
        // Keyword percentages resolve against the positioning area, even when
        // a later Flex content layout has a different containing block.
        query.containing_width = input.area.width;
        query.containing_height = input.area.height;
        return layout_size(child.id, query, depth + 1).size;
    };
    const auto measure_keyword = [&](const KeywordSize& keyword, bool horizontal) {
        if (const auto* exact = std::get_if<Unit>(&keyword)) return *exact;
        AvailableSize available{AvailableSpace::definite(
            sizing.known.width.value_or(fill.width)), AvailableSpace::definite(fill.height)};
        if (horizontal) available.width = std::get<AvailableSpace>(keyword);
        else available.height = std::get<AvailableSpace>(keyword);
        const auto measured = probe(sizing.known, available);
        return horizontal ? measured.width : measured.height;
    };
    if (!grid) {
        sizing.clamp();
        if (!sizing.known.width && !sizing.known.height && keyword_width && keyword_height
            && std::holds_alternative<AvailableSpace>(*keyword_width)
            && std::holds_alternative<AvailableSpace>(*keyword_height)) {
            const auto measured = probe({}, {std::get<AvailableSpace>(*keyword_width),
                std::get<AvailableSpace>(*keyword_height)});
            sizing.known = {measured.width, measured.height};
        } else {
            if (!sizing.known.width && keyword_width) {
                sizing.known.width = measure_keyword(*keyword_width, true);
            }
            if (!sizing.known.height && keyword_height) {
                sizing.known.height = measure_keyword(*keyword_height, false);
            }
        }
        sizing.apply_ratio();
        sizing.clamp();
    }
    if (!sizing.known.width) {
        if (input.start_inset.width && input.end_inset.width) sizing.known.width = fill.width;
        else if (grid && keyword_width) sizing.known.width = measure_keyword(*keyword_width, true);
    }
    sizing.apply_ratio();
    if (!grid) sizing.clamp();
    if (!sizing.known.height) {
        if (input.start_inset.height && input.end_inset.height) sizing.known.height = fill.height;
        else if (grid && keyword_height) sizing.known.height = measure_keyword(*keyword_height, false);
    }
    sizing.apply_ratio();
    sizing.clamp();
    constraints.available = {AvailableSpace::definite(std::max(Unit{0}, grid
        ? input.available.width - margin.left - margin.right
        : constrain_axis(input.available.width, sizing.minimum.width, sizing.maximum.width))),
        AvailableSpace::definite(std::max(Unit{0}, grid
        ? input.available.height - margin.top - margin.bottom
        : constrain_axis(input.available.height, sizing.minimum.height, sizing.maximum.height)))};
    constraints.known_width = sizing.known.width;
    constraints.known_height = sizing.known.height;
    Size final_size;
    if (sizing.known.width && sizing.known.height) final_size = {*sizing.known.width, *sizing.known.height};
    else {
        const auto measured = layout_size(child.id, constraints, depth + 1).size;
        final_size = sizing.clamp({sizing.known.width.value_or(measured.width),
            sizing.known.height.value_or(measured.height)});
    }
    constraints.known_width = final_size.width;
    constraints.known_height = final_size.height;
    constraints.known_width_is_final = true;
    constraints.known_height_is_final = true;
    auto fragment = layout(child.id, constraints, depth + 1);
    set_fragment_border_size(fragment, final_size, box);
    auto& layout = fragment.nodes.front();
    layout.padding = edges(box.padding, child.id);
    layout.border = edges(box.border, child.id);
    return fragment;
}

} // namespace axisly::css::detail
