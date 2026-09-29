#pragma once

#include "layout_engine.hpp"

#include <optional>
#include <utility>
#include <vector>

namespace axisly::css::detail {

struct FloatSlot {
    Unit x{}, y{}, width{};
};

class FloatContext {
public:
    explicit FloatContext(Unit content_width) : content_width_(content_width) {}

    [[nodiscard]] bool empty() const noexcept { return regions_.empty(); }
    [[nodiscard]] Unit clear_bottom(ClearSide clear) const noexcept;
    [[nodiscard]] Unit maximum_bottom() const noexcept;
    [[nodiscard]] std::pair<Unit, Unit> place_float(FloatSide side, ClearSide clear,
        Unit outer_width, Unit outer_height, Unit initial_y, NodeId node) const;
    [[nodiscard]] FloatSlot place_formatting_context(Unit initial_y,
        Unit margin_left, Unit margin_right,
        std::optional<Unit> preferred_width, NodeId node) const;
    void add(FloatSide side, Unit left, Unit top,
        Unit outer_width, Unit outer_height, NodeId node);

private:
    struct Region {
        FloatSide side{};
        Unit left{}, right{}, top{}, bottom{};
    };

    Unit content_width_{};
    std::vector<Region> regions_;
};

} // namespace axisly::css::detail
