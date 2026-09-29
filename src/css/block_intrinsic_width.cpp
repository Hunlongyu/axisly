#include "layout_engine.hpp"

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
    AvailableSpace available, std::size_t depth) {
    const auto available_width = available.definite_value();
    FloatWidthContribution floats(available);
    double maximum_child_width = 0;
    for (const auto child_id : node.children) {
        const auto& child = get(child_id);
        if (child.style.display == Display::None
            || child.style.position == Position::Absolute) continue;

        ResolvedBox child_box;
        try { child_box = resolve_layout_box(child, available_width, std::nullopt); }
        catch (const std::invalid_argument& error) {
            throw LayoutFailure(LayoutErrorCode::InvalidStyle, child_id, error.what());
        }
        const auto margin = edges(child_box.margin, child_id);
        const Unit horizontal_margin = checked(static_cast<double>(margin.left)
            + margin.right, child_id);
        Constraints child_constraints;
        child_constraints.content_based_width = true;
        child_constraints.containing_width = available_width;
        child_constraints.available.width = available_width
            ? AvailableSpace::definite(std::max(Unit{0}, *available_width
                - horizontal_margin)) : available;
        child_constraints.available.height = AvailableSpace::min_content();
        const auto child_width = layout(child_id, child_constraints, depth + 1).size.width;
        const Unit outer_width = checked(std::max(0.0,
            static_cast<double>(child_width) + horizontal_margin), child_id);
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
