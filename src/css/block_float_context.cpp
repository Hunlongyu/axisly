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
    Unit top, Unit bottom) const {
    std::vector<Interval> occupied;
    occupied.reserve(regions_.size());
    for (const auto& region : regions_) {
        if (!region.occupies(top, bottom)) continue;
        const Unit left = std::clamp(region.left, Unit{0}, content_width_);
        const Unit right = std::clamp(region.right, Unit{0}, content_width_);
        if (left < right) occupied.push_back({left, right});
    }
    std::sort(occupied.begin(), occupied.end(),
        [](const Interval& first, const Interval& second) {
            return first.left < second.left;
        });
    std::vector<Interval> free;
    Unit cursor = 0;
    for (const auto& interval : occupied) {
        if (interval.left > cursor) free.push_back({cursor, interval.left});
        cursor = std::max(cursor, interval.right);
    }
    if (cursor < content_width_) free.push_back({cursor, content_width_});
    if (occupied.empty() && content_width_ == 0) free.push_back({0, 0});
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
    Unit outer_width, Unit outer_height, Unit initial_y, NodeId node) const {
    Unit y = initial_y;
    for (const auto& region : regions_) y = std::max(y, region.top);
    if (clear != ClearSide::None && !regions_.empty()) {
        y = std::max(y, maximum_bottom());
    }
    for (std::size_t attempt = 0; attempt <= regions_.size(); ++attempt) {
        const Unit float_bottom = checked(static_cast<double>(y) + outer_height,
            node);
        const auto free = free_intervals(y, float_bottom);
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
            return {side == FloatSide::Left ? Unit{0}
                : checked(static_cast<double>(content_width_) - outer_width, node), y};
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
        const auto next_bottom = next_occupied_bottom(y, y);
        if (!next_bottom) {
            const Unit width = std::max(Unit{0}, checked(
                static_cast<double>(content_width_) - margin_left - margin_right,
                node));
            return {margin_left, y, width};
        }
        const auto free = free_intervals(y, y);
        for (const auto& interval : free) {
            const Unit x = std::max(margin_left, interval.left);
            const Unit trailing_margin = interval.right == content_width_
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
