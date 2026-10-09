#include "block_float_context.hpp"

#include <algorithm>

namespace axisly::css::detail {

Unit FloatContext::clear_bottom(ClearSide clear) const noexcept {
    return clear_threshold(clear).value_or(Unit{0});
}

std::optional<Unit> FloatContext::clear_threshold(ClearSide clear) const noexcept {
    switch (clear) {
    case ClearSide::Left: return left_bottom_;
    case ClearSide::Right: return right_bottom_;
    case ClearSide::Both: return both_bottom_;
    default: return std::nullopt;
    }
}

Unit FloatContext::maximum_bottom() const noexcept {
    return maximum_bottom_;
}

std::span<const FloatContext::Interval> FloatContext::free_intervals(
    Unit top, Unit bottom, FloatBounds bounds) const {
    auto& occupied = occupied_;
    occupied.clear();
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
    auto& free = free_;
    free.clear();
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
    Unit y = std::max(initial_y, maximum_top_.value_or(initial_y));
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
    FloatItemWidth item_width,
    Direction direction, FloatBounds bounds, NodeId node) const {
    const Unit margin_left_edge = checked(static_cast<double>(bounds.left) + margin_left, node);
    const Unit margin_right_edge = checked(static_cast<double>(bounds.right) - margin_right, node);
    const Unit minimum_auto_width = checked(-static_cast<double>(margin_left) - margin_right, node);
    Unit y = initial_y;
    for (std::size_t attempt = 0; attempt <= regions_.size(); ++attempt) {
        const auto next_bottom = next_occupied_bottom(y, y);
        if (!next_bottom) {
            const Unit width = std::max(Unit{0}, checked(
                static_cast<double>(bounds.right) - bounds.left
                    - margin_left - margin_right,
                node));
            return {margin_left_edge, y, width, width};
        }
        const auto free = free_intervals(y, y, bounds);
        for (const auto& interval : free) {
            const bool left_float = interval.left > bounds.left;
            const bool right_float = interval.right < bounds.right;
            const bool rtl = direction == Direction::Rtl;
            const Unit fit_left = !rtl
                ? left_float ? std::max(interval.left, margin_left_edge) : margin_left_edge
                : left_float ? interval.left : std::min(bounds.left, margin_left_edge);
            const Unit fit_right = rtl
                ? right_float ? std::min(interval.right, margin_right_edge) : margin_right_edge
                : right_float ? interval.right : std::max(bounds.right, margin_right_edge);
            const Unit stretch_left = left_float ? std::max(interval.left, margin_left_edge) : margin_left_edge;
            const Unit stretch_right = right_float ? std::min(interval.right, margin_right_edge) : margin_right_edge;
            const Unit fit_width = std::max(Unit{0}, checked(static_cast<double>(fit_right) - fit_left, node));
            const Unit stretch_width = std::max(Unit{0}, checked(static_cast<double>(stretch_right) - stretch_left, node));
            const Unit required = constrain_axis(item_width.preferred.value_or(
                std::max(stretch_width, minimum_auto_width)), item_width.minimum, item_width.maximum);
            if (required <= fit_width) {
                return {fit_left, y, fit_width, std::max(stretch_width, minimum_auto_width)};
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
    const Unit prior_occupied_bottom = maximum_occupied_bottom_;
    // Taffy 0.14 updates existing vertical segments when a float begins inside
    // them. The extension below the last segment is represented by the box's
    // actual bottom but does not occupy an additional segment.
    if (top < prior_occupied_bottom) {
        occupied_bottom = std::min(bottom, prior_occupied_bottom);
    }
    regions_.push_back({side, left,
        checked(static_cast<double>(left) + outer_width, node), top,
        bottom, occupied_bottom});
    // Commit summaries only after all validation/allocation above succeeds.
    both_bottom_ = std::max(both_bottom_.value_or(bottom), bottom);
    if (side == FloatSide::Left) left_bottom_ = std::max(left_bottom_.value_or(bottom), bottom);
    if (side == FloatSide::Right) right_bottom_ = std::max(right_bottom_.value_or(bottom), bottom);
    maximum_top_ = std::max(maximum_top_.value_or(top), top);
    maximum_bottom_ = std::max(maximum_bottom_, bottom);
    maximum_occupied_bottom_ = std::max(maximum_occupied_bottom_, occupied_bottom);
}

} // namespace axisly::css::detail
