#pragma once

#include "flex_internal.hpp"
#include "balanced_lines.hpp"
#include "workspace_pool.hpp"
#include <ranges>

namespace axisly::css::detail {

struct FlexLine {
    std::ranges::iota_view<std::size_t, std::size_t> items;
    Unit cross{};
    Unit baseline_ascent{}, baseline_descent{};
    bool has_baseline{};
};

struct FlexWorkspace {
    explicit FlexWorkspace(std::pmr::memory_resource* memory)
        : fragments(memory), items(memory), boxes(memory), constraints(memory),
          lines(memory), outer_sizes(memory), line_counts(memory), balanced(memory) {}
    std::pmr::vector<Fragment> fragments;
    std::pmr::vector<FlexItem> items;
    std::pmr::vector<LayoutBox> boxes;
    std::pmr::vector<Constraints> constraints;
    std::pmr::vector<FlexLine> lines;
    std::pmr::vector<Unit> outer_sizes;
    std::pmr::vector<std::size_t> line_counts;
    BalancedLineWorkspace balanced;

    void clear() noexcept {
        // Release all borrowed roots and descendants before leaving a query.
        // Only vector storage survives; no layout state crosses query stages.
        fragments.clear();
        items.clear();
        boxes.clear();
        constraints.clear();
        lines.clear();
        outer_sizes.clear();
        line_counts.clear();
        balanced.clear();
    }
};

} // namespace axisly::css::detail
