#include "block_flow_state.hpp"

#include <algorithm>

namespace axisly::css::detail {
Unit BlockFlowState::float_y(NodeId node) const {
    return leading_open_ && collapsible_top_ ? committed_y_
        : checked(static_cast<double>(committed_y_) + active_.resolve(node), node);
}

Unit BlockFlowState::absolute_y(NodeId node) const {
    return checked(static_cast<double>(committed_y_) + active_.resolve(node), node);
}

BlockFlowPosition BlockFlowState::position(MarginStrut top,
    std::optional<Unit> clear_threshold, bool forced_clearance,
    std::optional<Unit> float_y, NodeId node) const {
    const Unit hypothetical = checked(static_cast<double>(committed_y_)
        + active_.merged(top).resolve(node), node);
    Unit y = leading_open_ && collapsible_top_ ? committed_y_ : hypothetical;
    bool clearance = false;
    if (clear_threshold && (forced_clearance || hypothetical < *clear_threshold)) {
        clearance = true;
        const Unit escaped = leading_open_ && collapsible_top_ ? active_.resolve(node) : Unit{0};
        y = checked(static_cast<double>(*clear_threshold) - escaped, node);
    }
    const bool pushed = float_y && *float_y > y;
    if (pushed) y = *float_y;
    return {y, clearance, pushed};
}

void BlockFlowState::commit(BlockFlowPosition position, MarginStrut top,
    MarginStrut bottom, bool collapses_through, Unit height, NodeId node) {
    if (leading_open_) {
        if (!position.clearance && !position.pushed_below_float) {
            leading_ = leading_.merged(top);
            if (collapses_through) leading_ = leading_.merged(bottom);
        }
        if (!collapses_through || position.clearance || position.pushed_below_float) {
            leading_open_ = false;
        }
    }
    if (collapses_through && !position.clearance) {
        active_ = active_.merged(top).merged(bottom);
    } else {
        committed_y_ = checked(static_cast<double>(position.y) + height, node);
        if (collapses_through && position.clearance) {
            committed_y_ = checked(static_cast<double>(committed_y_) - top.resolve(node), node);
            active_ = top.merged(bottom);
            clearance_strut_ = true;
        } else {
            active_ = bottom;
            clearance_strut_ = false;
        }
        has_nonempty_flow_ = true;
    }
    has_flow_ = true;
}

Unit BlockFlowState::content_height(bool collapsible_bottom, NodeId node) const {
    return std::max(Unit{0}, checked(static_cast<double>(committed_y_)
        + (collapsible_bottom && !clearance_strut_ ? 0 : active_.resolve(node)), node));
}

BlockMargins BlockFlowState::margins(MarginStrut own_top, MarginStrut own_bottom,
    bool collapsible_bottom, bool through) const {
    if (through) {
        const auto all = own_top.merged(own_bottom).merged(active_);
        return {all, all, true};
    }
    if (collapsible_top_ && has_flow_) own_top = own_top.merged(leading_);
    if (collapsible_bottom && !clearance_strut_ && has_flow_) own_bottom = own_bottom.merged(active_);
    return {own_top, own_bottom, false};
}
} // namespace axisly::css::detail
