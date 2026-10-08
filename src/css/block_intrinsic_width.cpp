#include "block_item_sizing.hpp"
#include "sizing_keywords.hpp"

#include <algorithm>

namespace axisly::css::detail {
namespace {

class FloatWidthContribution {
public:
    explicit FloatWidthContribution(AvailableSpace available) : available_(available) {}

    void add(Unit width, FloatSide side, ClearSide clear) {
        if (available_.kind == AvailableKind::MinContent) {
            contribution_ = std::max(contribution_, static_cast<double>(width));
        } else {
            if (clear == ClearSide::Left || clear == ClearSide::Both) left_ = 0;
            if (clear == ClearSide::Right || clear == ClearSide::Both) right_ = 0;
            if (side == FloatSide::Left) left_ += width;
            else right_ += width;
            contribution_ = std::max(contribution_, left_ + right_);
        }
        widest_ = std::max(widest_, static_cast<double>(width));
    }

    [[nodiscard]] double width() const {
        if (const auto limit = available_.definite_value()) {
            return std::max(widest_, std::min(contribution_, static_cast<double>(*limit)));
        }
        return contribution_;
    }

private:
    AvailableSpace available_;
    double left_{}, right_{}, contribution_{}, widest_{};
};

} // namespace

Unit Engine::block_content_based_width(const LayoutNode& node,
    AvailableSpace available, ResolvedDimensions percentage_basis, std::size_t depth) {
    const auto available_width = available.definite_value();
    FloatWidthContribution floats(available);
    double maximum_child_width = 0;
    LayoutBox item_box;
    for (const auto child_id : node.layout_children()) {
        const auto& child = get(child_id);
        if (child.style.display == Display::None
            || child.style.position == Position::Absolute) continue;

        // Generate dimensions with the container's early known content size.
        // The measurement itself has no containing block; known dimensions
        // carry independently resolved percentage sizes into that query.
        const auto& child_box = resolve_block_item_box(child, percentage_basis,
            available_width.value_or(Unit{0}), item_box);
        const auto margin = edges(resolve_box_margins(child.style.box.margin,
            available_width, IndefiniteEdgeBasis::Zero), child_id);
        const Unit horizontal_margin = checked(static_cast<double>(margin.left)
            + margin.right, child_id);
        Constraints child_constraints;
        child_constraints.content_based_width = true;
        child_constraints.containing_width = std::nullopt;
        if (child_box.preferred.width) {
            child_constraints.known_width = constrain_axis(*child_box.preferred.width,
                child_box.minimum.width, child_box.maximum.width);
        }
        if (child_box.preferred.height) {
            child_constraints.known_height = constrain_axis(*child_box.preferred.height,
                child_box.minimum.height, child_box.maximum.height);
        }
        child_constraints.available.width = available_width
            ? AvailableSpace::definite(std::max(Unit{0}, *available_width
                - horizontal_margin)) : available;
        child_constraints.available.height = AvailableSpace::min_content();
        if (const auto keyword = sizing_keyword(child.style.box.size.width,
                std::nullopt, std::nullopt)) {
            child_constraints.available.width = std::holds_alternative<Unit>(*keyword)
                ? AvailableSpace::definite(std::get<Unit>(*keyword))
                : std::get<AvailableSpace>(*keyword);
        }
        const auto child_width = child_constraints.known_width.value_or(Unit{0});
        const auto measured_width = child_constraints.known_width ? child_width
            : layout_size(child, child_constraints, depth + 1).width;
        const Unit outer_width = checked(std::max(0.0,
            static_cast<double>(std::max(measured_width,
                child_box.horizontal_padding_border()))
                + horizontal_margin), child_id);
        if (child.style.float_side != FloatSide::None) {
            floats.add(outer_width, child.style.float_side, child.style.clear);
        } else {
            maximum_child_width = std::max(maximum_child_width,
                static_cast<double>(outer_width));
        }
    }
    return checked(std::max(maximum_child_width, floats.width()), node.id);
}

} // namespace axisly::css::detail
