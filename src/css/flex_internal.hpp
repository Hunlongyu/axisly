#pragma once

#include "layout_engine.hpp"

namespace axisly::css::detail {
inline bool flex_is_row(FlexDirection direction) {
    return direction == FlexDirection::Row || direction == FlexDirection::RowReverse;
}

inline Unit flex_axis(Size size, bool row, bool main) {
    return (row == main) ? size.width : size.height;
}

inline Unit flex_axis(const Insets& value, bool row, bool main, bool start) {
    if (row == main) return start ? value.left : value.right;
    return start ? value.top : value.bottom;
}

inline std::optional<Unit> flex_axis(const ResolvedDimensions& size, bool row, bool main) {
    return row == main ? size.width : size.height;
}

struct FlexItem {
    NodeId id;
    std::size_t source_index{};
    ResolvedBox box;
    Insets margin;
    // Flex base size includes padding and border; shrink weights use its content size.
    Unit base{};
    Unit inner_base{};
    Unit main_decoration{};
    Unit content_main{};
    Unit target{};
    Unit minimum{};
    std::optional<Unit> maximum;
    Unit hypothetical_minimum{};
    std::optional<Unit> hypothetical_maximum;
    Unit grow{}, shrink{};
    Unit cross{};
    bool auto_main_start{}, auto_main_end{};
    bool auto_cross_start{}, auto_cross_end{};
    bool content_based_cross_width{};
    bool main_size_is_definite{}, cross_size_is_definite{};
    bool scroll_container{};
    Constraints final_constraints;
    double violation{};
    bool frozen{};
};


struct FlexItemSizingInput {
    std::size_t source_index;
    ResolvedDimensions inner;
    AvailableSize available_inner;
    AvailableKind main_query;
    bool main_size_is_definite, main_reverse, cross_reverse;
};

enum class CrossSpaceStage { FlexBase, Hypothetical };
[[nodiscard]] AvailableSpace divided_flex_cross_space(const LayoutNode& node,
    AvailableSpace available, std::optional<Unit> percentage_basis);
[[nodiscard]] AvailableSpace cross_space_for_item(AvailableSpace available,
    std::optional<Unit> minimum, std::optional<Unit> maximum,
    Unit outer_margin, CrossSpaceStage stage, NodeId id);
} // namespace axisly::css::detail
