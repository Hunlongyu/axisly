#include "block_float_context.hpp"

#include <algorithm>

namespace axisly::css::detail {

Unit FloatContext::clear_bottom(ClearSide clear) const noexcept {
    Unit bottom = 0;
    for (const auto& region : regions_) {
        if (clear == ClearSide::Both
            || (clear == ClearSide::Left && region.side == FloatSide::Left)
            || (clear == ClearSide::Right && region.side == FloatSide::Right)) {
            bottom = std::max(bottom, region.bottom);
        }
    }
    return bottom;
}

Unit FloatContext::maximum_bottom() const noexcept {
    Unit bottom = 0;
    for (const auto& region : regions_) bottom = std::max(bottom, region.bottom);
    return bottom;
}

std::pair<Unit, Unit> FloatContext::place_float(FloatSide side, ClearSide clear,
    Unit outer_width, Unit outer_height, Unit initial_y, NodeId node) const {
    const Unit y_after_clear = clear == ClearSide::None || regions_.empty()
        ? initial_y : std::max(initial_y, maximum_bottom());
    Unit y = y_after_clear;
    for (std::size_t attempt = 0; attempt <= regions_.size(); ++attempt) {
        Unit left = 0, right = content_width_;
        const Unit float_bottom = checked(static_cast<double>(y) + outer_height,
            node);
        std::optional<Unit> next_bottom;
        for (const auto& region : regions_) {
            if (region.top >= float_bottom || region.bottom <= y) continue;
            if (region.side == FloatSide::Left) left = std::max(left, region.right);
            else right = std::min(right, region.left);
            if (region.bottom > y) {
                next_bottom = std::min(next_bottom.value_or(region.bottom), region.bottom);
            }
        }
        if (right - left >= outer_width || !next_bottom) {
            const Unit x = side == FloatSide::Left ? left
                : checked(static_cast<double>(right) - outer_width, node);
            return {x, y};
        }
        y = *next_bottom;
    }
    throw LayoutFailure(LayoutErrorCode::LimitExceeded, node,
        "Block float placement did not converge");
}

FloatSlot FloatContext::place_formatting_context(Unit initial_y,
    Unit margin_left, Unit margin_right,
    std::optional<Unit> preferred_width, NodeId node) const {
    Unit y = initial_y;
    for (std::size_t attempt = 0; attempt <= regions_.size(); ++attempt) {
        Unit left = 0, right = content_width_;
        std::optional<Unit> next_bottom;
        for (const auto& region : regions_) {
            if (region.top > y || region.bottom <= y) continue;
            if (region.side == FloatSide::Left) left = std::max(left, region.right);
            else right = std::min(right, region.left);
            next_bottom = std::min(next_bottom.value_or(region.bottom), region.bottom);
        }
        const Unit x = std::max(margin_left, left);
        const Unit width = std::max(Unit{0}, checked(
            static_cast<double>(right) - x - margin_right, node));
        if (!preferred_width || *preferred_width <= width || !next_bottom) {
            return {x, y, width};
        }
        y = *next_bottom;
    }
    throw LayoutFailure(LayoutErrorCode::LimitExceeded, node,
        "Block formatting context placement did not converge");
}

void FloatContext::add(FloatSide side, Unit left, Unit top,
    Unit outer_width, Unit outer_height, NodeId node) {
    regions_.push_back({side, left,
        checked(static_cast<double>(left) + outer_width, node), top,
        checked(static_cast<double>(top) + outer_height, node)});
}

} // namespace axisly::css::detail
