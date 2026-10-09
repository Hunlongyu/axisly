#pragma once

#include "grid_item_contribution.hpp"
#include "workspace_pool.hpp"

namespace axisly::css::detail {
struct SpanningItem {
    const ItemContribution* item;
    std::int32_t start, span;
    bool crosses_flexible_track, crosses_intrinsic_track, crosses_max_content_min;
};

struct GridSizingWorkspace {
    explicit GridSizingWorkspace(std::pmr::memory_resource* memory)
        : spanning(memory), planned(memory), touched(memory), affected(memory), uncapped(memory),
          single_limit(memory), growth_limit(memory), infinitely_growable(memory), frozen(memory),
          bases(memory), limits(memory), increases(memory), capacities(memory) {}
    std::pmr::vector<SpanningItem> spanning;
    std::pmr::vector<Unit> planned;
    std::pmr::vector<std::size_t> touched, affected, uncapped;
    std::pmr::vector<std::uint8_t> single_limit, growth_limit, infinitely_growable, frozen;
    std::pmr::vector<double> bases, limits, increases, capacities;
    void clear() noexcept {
        spanning.clear(); planned.clear(); touched.clear(); affected.clear(); uncapped.clear();
        single_limit.clear(); growth_limit.clear(); infinitely_growable.clear(); frozen.clear();
        bases.clear(); limits.clear(); increases.clear(); capacities.clear();
    }
};

struct GridWorkspace {
    explicit GridWorkspace(std::pmr::memory_resource* memory)
        : columns(memory), rows(memory), contributions(memory), contribution_indices(memory),
          fragments(memory), baseline_items(memory), baseline_order(memory), baselines(memory), sizing(memory) {}
    TrackAxis columns, rows;
    std::pmr::vector<ItemContribution> contributions;
    std::pmr::vector<std::optional<std::size_t>> contribution_indices;
    std::pmr::vector<Fragment> fragments;
    std::pmr::vector<GridBaselineItem> baseline_items;
    std::pmr::vector<std::size_t> baseline_order;
    std::pmr::vector<Unit> baselines;
    GridSizingWorkspace sizing;
    void clear() noexcept {
        columns.clear(); rows.clear(); contributions.clear(); contribution_indices.clear();
        fragments.clear(); baseline_items.clear(); baseline_order.clear(); baselines.clear();
        sizing.clear();
    }
};
} // namespace axisly::css::detail
