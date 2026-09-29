#pragma once

#include "layout_engine.hpp"

#include <optional>
#include <utility>
#include <vector>

namespace axisly::css::detail {

struct FloatSlot {
    Unit x{}, y{}, width{};
};

struct FloatBounds {
    Unit left{}, right{};
};

class FloatContext;

// A Block descendant in the same formatting context shares the float regions.
// Coordinates describe its content origin in the formatting context's space.
struct BlockContext {
    FloatContext& floats;
    Unit origin_x{}, origin_y{};
};

class FloatContext {
public:
    FloatContext() = default;

    [[nodiscard]] bool empty() const noexcept { return regions_.empty(); }
    [[nodiscard]] Unit clear_bottom(ClearSide clear) const noexcept;
    [[nodiscard]] Unit maximum_bottom() const noexcept;
    [[nodiscard]] std::pair<Unit, Unit> place_float(FloatSide side, ClearSide clear,
        Unit outer_width, Unit outer_height, Unit initial_y,
        FloatBounds bounds, NodeId node) const;
    [[nodiscard]] FloatSlot place_formatting_context(Unit initial_y,
        Unit margin_left, Unit margin_right,
        std::optional<Unit> preferred_width, FloatBounds bounds, NodeId node) const;
    void add(FloatSide side, Unit left, Unit top,
        Unit outer_width, Unit outer_height, NodeId node);

private:
    struct Region {
        FloatSide side{};
        Unit left{}, right{}, top{}, bottom{}, occupied_bottom{};
        [[nodiscard]] bool occupies(Unit query_top, Unit query_bottom) const noexcept {
            return query_top == query_bottom
                ? top <= query_top && occupied_bottom > query_top
                : top < query_bottom && occupied_bottom > query_top;
        }
    };
    struct Interval {
        Unit left{}, right{};
    };

    [[nodiscard]] std::vector<Interval> free_intervals(
        Unit top, Unit bottom, FloatBounds bounds) const;
    [[nodiscard]] std::optional<Unit> next_occupied_bottom(Unit top, Unit bottom) const;

    std::vector<Region> regions_;
};

} // namespace axisly::css::detail
