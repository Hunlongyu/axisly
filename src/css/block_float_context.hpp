#pragma once

#include "layout_engine.hpp"

#include <array>
#include <optional>
#include <utility>
#include <vector>

namespace axisly::css::detail {

struct FloatSlot {
    Unit x{}, y{}, width{}, stretch_width{};
};

struct FloatBounds {
    Unit left{}, right{};
};

struct FloatItemWidth {
    std::optional<Unit> preferred, minimum, maximum;
};

class FloatContext;

// A Block descendant in the same formatting context shares the float regions.
// Coordinates describe its content origin in the formatting context's space.
struct BlockContext {
    FloatContext& floats;
    Unit origin_x{}, origin_y{};
    std::array<bool, 2> adjoining_floats{};
    std::optional<std::array<bool, 2>> top_adjoining_floats;
    void commit_strut() {
        if (!top_adjoining_floats) top_adjoining_floats = adjoining_floats;
        adjoining_floats = {};
    }
    [[nodiscard]] bool has_adjoining_float(ClearSide side) const {
        return ((side == ClearSide::Left || side == ClearSide::Both) && adjoining_floats[0])
            || ((side == ClearSide::Right || side == ClearSide::Both) && adjoining_floats[1]);
    }
    void merge_adjoining(const BlockContext& child) {
        const auto flags = child.top_adjoining_floats.value_or(child.adjoining_floats);
        adjoining_floats[0] = adjoining_floats[0] || flags[0];
        adjoining_floats[1] = adjoining_floats[1] || flags[1];
    }
};

class FloatContext {
public:
    explicit FloatContext(std::pmr::memory_resource* memory = std::pmr::get_default_resource())
        : regions_(memory), occupied_(memory), free_(memory) {}
    void clear() noexcept {
        regions_.clear(); occupied_.clear(); free_.clear();
        left_bottom_.reset(); right_bottom_.reset(); both_bottom_.reset(); maximum_top_.reset();
        maximum_bottom_ = maximum_occupied_bottom_ = 0;
    }

    [[nodiscard]] bool empty() const noexcept { return regions_.empty(); }
    [[nodiscard]] Unit clear_bottom(ClearSide clear) const noexcept;
    [[nodiscard]] std::optional<Unit> clear_threshold(ClearSide clear) const noexcept;
    [[nodiscard]] Unit maximum_bottom() const noexcept;
    [[nodiscard]] std::pair<Unit, Unit> place_float(FloatSide side, ClearSide clear,
        Unit outer_width, Unit outer_height, Unit initial_y,
        FloatBounds bounds, NodeId node) const;
    [[nodiscard]] FloatSlot place_formatting_context(Unit initial_y,
        Unit margin_left, Unit margin_right,
        FloatItemWidth item_width,
        Direction direction, FloatBounds bounds, NodeId node) const;
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

    [[nodiscard]] std::span<const Interval> free_intervals(
        Unit top, Unit bottom, FloatBounds bounds) const;
    [[nodiscard]] std::optional<Unit> next_occupied_bottom(Unit top, Unit bottom) const;

    std::pmr::vector<Region> regions_;
    mutable std::pmr::vector<Interval> occupied_, free_;
    std::optional<Unit> left_bottom_, right_bottom_, both_bottom_, maximum_top_;
    Unit maximum_bottom_{}, maximum_occupied_bottom_{};
};

} // namespace axisly::css::detail
