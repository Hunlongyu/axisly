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

std::vector<FloatContext::Interval> FloatContext::free_intervals(
    Unit top, Unit bottom, FloatBounds bounds) const {
    std::vector<Interval> occupied;
    occupied.reserve(regions_.size());
    for (const auto& region : regions_) {
        if (!region.occupies(top, bottom)) continue;
        const Unit left = std::clamp(region.left, bounds.left, bounds.right);
        const Unit right = std::clamp(region.right, bounds.left, bounds.right);
        if (left < right) occupied.push_back({left, right});
    }
    std::sort(occupied.begin(), occupied.end(),
        [](const Interval& first, const Interval& second) {
            return first.left < second.left;
        });
    std::vector<Interval> free;
    Unit cursor = bounds.left;
    for (const auto& interval : occupied) {
        if (interval.left > cursor) free.push_back({cursor, interval.left});
        cursor = std::max(cursor, interval.right);
    }
    if (cursor < bounds.right) free.push_back({cursor, bounds.right});
    if (occupied.empty() && bounds.left == bounds.right) {
        free.push_back({bounds.left, bounds.right});
    }
    return free;
}

std::optional<Unit> FloatContext::next_occupied_bottom(Unit top, Unit bottom) const {
    std::optional<Unit> next;
    for (const auto& region : regions_) {
        if (!region.occupies(top, bottom)) continue;
        next = std::min(next.value_or(region.occupied_bottom), region.occupied_bottom);
    }
    return next;
}

std::pair<Unit, Unit> FloatContext::place_float(FloatSide side, ClearSide clear,
    Unit outer_width, Unit outer_height, Unit initial_y,
    FloatBounds bounds, NodeId node) const {
    Unit y = initial_y;
    for (const auto& region : regions_) y = std::max(y, region.top);
    if (clear != ClearSide::None && !regions_.empty()) {
        y = std::max(y, maximum_bottom());
    }
    for (std::size_t attempt = 0; attempt <= regions_.size(); ++attempt) {
        const Unit float_bottom = checked(static_cast<double>(y) + outer_height,
            node);
        const auto free = free_intervals(y, float_bottom, bounds);
        if (side == FloatSide::Left) {
            for (const auto& interval : free) {
                if (interval.right - interval.left >= outer_width) {
                    return {interval.left, y};
                }
            }
        } else {
            for (auto interval = free.rbegin(); interval != free.rend(); ++interval) {
                if (interval->right - interval->left >= outer_width) {
                    return {checked(static_cast<double>(interval->right)
                        - outer_width, node), y};
                }
            }
        }
        const auto next_bottom = next_occupied_bottom(y, float_bottom);
        if (!next_bottom) {
            return {side == FloatSide::Left ? bounds.left
                : checked(static_cast<double>(bounds.right) - outer_width, node), y};
        }
        y = *next_bottom;
    }
    throw LayoutFailure(LayoutErrorCode::LimitExceeded, node,
        "Block float placement did not converge");
}

FloatSlot FloatContext::place_formatting_context(Unit initial_y,
    Unit margin_left, Unit margin_right,
    std::optional<Unit> preferred_width, FloatBounds bounds, NodeId node) const {
    Unit y = initial_y;
    for (std::size_t attempt = 0; attempt <= regions_.size(); ++attempt) {
        const auto next_bottom = next_occupied_bottom(y, y);
        if (!next_bottom) {
            const Unit width = std::max(Unit{0}, checked(
                static_cast<double>(bounds.right) - bounds.left
                    - margin_left - margin_right,
                node));
            return {checked(static_cast<double>(bounds.left) + margin_left,
                node), y, width};
        }
        const auto free = free_intervals(y, y, bounds);
        for (const auto& interval : free) {
            const Unit x = std::max(
                checked(static_cast<double>(bounds.left) + margin_left, node),
                interval.left);
            const Unit trailing_margin = interval.right == bounds.right
                ? margin_right : Unit{0};
            const Unit width = std::max(Unit{0}, checked(
                static_cast<double>(interval.right) - x - trailing_margin, node));
            if (!preferred_width || *preferred_width <= width) {
                return {x, y, width};
            }
        }
        y = *next_bottom;
    }
    throw LayoutFailure(LayoutErrorCode::LimitExceeded, node,
        "Block formatting context placement did not converge");
}

void FloatContext::add(FloatSide side, Unit left, Unit top,
    Unit outer_width, Unit outer_height, NodeId node) {
    const Unit bottom = checked(static_cast<double>(top) + outer_height, node);
    Unit occupied_bottom = bottom;
    Unit prior_occupied_bottom = 0;
    for (const auto& region : regions_) {
        prior_occupied_bottom = std::max(prior_occupied_bottom, region.occupied_bottom);
    }
    // Taffy 0.14 updates existing vertical segments when a float begins inside
    // them. The extension below the last segment is represented by the box's
    // actual bottom but does not occupy an additional segment.
    if (top < prior_occupied_bottom) {
        occupied_bottom = std::min(bottom, prior_occupied_bottom);
    }
    regions_.push_back({side, left,
        checked(static_cast<double>(left) + outer_width, node), top,
        bottom, occupied_bottom});
}

} // namespace axisly::css::detail
