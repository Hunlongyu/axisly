#pragma once

#include "layout_engine.hpp"

namespace axisly::css::detail {
struct BlockFlowPosition {
    Unit y{};
    bool clearance{}, pushed_below_float{};
};

// The active margin strut is separate from committed in-flow height. Clearance
// commits a border edge without necessarily committing an empty box's margins.
class BlockFlowState {
public:
    explicit BlockFlowState(bool collapsible_top) : collapsible_top_(collapsible_top) {}
    [[nodiscard]] Unit float_y(NodeId node) const;
    [[nodiscard]] Unit absolute_y(NodeId node) const;
    [[nodiscard]] BlockFlowPosition position(MarginStrut top,
        std::optional<Unit> clear_threshold, bool forced_clearance,
        std::optional<Unit> float_y, NodeId node) const;
    void commit(BlockFlowPosition position, MarginStrut top, MarginStrut bottom,
        bool collapses_through, Unit height, NodeId node);
    [[nodiscard]] Unit content_height(bool collapsible_bottom, NodeId node) const;
    [[nodiscard]] BlockMargins margins(MarginStrut own_top, MarginStrut own_bottom,
        bool collapsible_bottom, bool through) const;
    [[nodiscard]] bool leading_open() const noexcept { return leading_open_; }
    [[nodiscard]] bool has_flow() const noexcept { return has_flow_; }
    [[nodiscard]] bool has_nonempty_flow() const noexcept { return has_nonempty_flow_; }
    [[nodiscard]] bool has_clearance_strut() const noexcept { return clearance_strut_; }

private:
    MarginStrut active_, leading_;
    Unit committed_y_{};
    bool collapsible_top_{}, leading_open_{true};
    bool has_flow_{}, has_nonempty_flow_{}, clearance_strut_{};
};
} // namespace axisly::css::detail
