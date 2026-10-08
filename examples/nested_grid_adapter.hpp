#pragma once

// Host-side reference adapter, deliberately outside the installed core API.
// The host owns every grid and callback context; all addresses and inputs must
// remain valid and unchanged until compute_nested_grid() returns.
#include <axisly/axisly.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace axisly::examples {

struct NestedGrid {
    GridSpec spec;
    std::vector<GridItem> items;
    // IDs belong to this grid only. A child can be reused under different paths.
    std::map<ItemId, const NestedGrid*> children;
    MeasureCallback measure{}; // Ordinary items only; null uses static preferred_size.
    void* user_data{};
};

struct NestedLimits {
    std::size_t maximum_depth{32}; // Root counts as depth 1.
    std::size_t maximum_layouts{4096}; // Includes measurement probes and final layouts.
};

enum class NestedErrorCode {
    NotComputed, None, GridFailure, InvalidChild, Cycle, DepthLimit, WorkLimit, NumericOverflow
};

struct NestedError {
    NestedErrorCode code{NestedErrorCode::NotComputed};
    std::string message;
    std::vector<ItemId> path;
    GridError grid_error; // Original diagnostic when code == GridFailure.
};

struct NestedItemLayout {
    // Full path from the root, including this item's ID. No global-ID requirement.
    std::vector<ItemId> path;
    Rect rect; // Root-relative coordinates; overflow beyond a parent is not clipped.
    Rect cell_rect;
};

struct NestedResult {
    Size size{}; // Root occupied size, not the union of overflowing descendants.
    std::vector<NestedItemLayout> items; // Preorder: container, then its descendants.
    NestedError error;
    std::size_t layout_count{};
    std::size_t measurement_count{}; // All adapter calls, including child-grid items.

    [[nodiscard]] bool succeeded() const noexcept { return error.code == NestedErrorCode::None; }
};

namespace nested_detail {

struct Abort { NestedError error; };

class LayoutRun {
public:
    explicit LayoutRun(NestedLimits limits) : limits_(limits) {}

    NestedResult run(const NestedGrid& root, Size available) {
        try {
            result_.size = arrange(root, available, 0, 0, std::nullopt);
            result_.error.code = NestedErrorCode::None;
        } catch (const Abort& failure) {
            // Never expose partial geometry, and never apply anything to a widget.
            result_.size = {};
            result_.items.clear();
            result_.error = failure.error;
        }
        return std::move(result_);
    }

private:
    [[noreturn]] void fail(NestedErrorCode code, std::string message,
                          std::optional<ItemId> item = std::nullopt, GridError grid_error = {}) const {
        auto path = path_;
        if (item) path.push_back(*item);
        throw Abort{{code, std::move(message), std::move(path), std::move(grid_error)}};
    }

    class Scope {
    public:
        Scope(LayoutRun& run, const NestedGrid& grid, std::optional<ItemId> edge)
            : run_(run), has_edge_(edge.has_value()) {
            if (std::find(run.active_.begin(), run.active_.end(), &grid) != run.active_.end()) {
                run.fail(NestedErrorCode::Cycle, "Child grid refers to an active ancestor", edge);
            }
            if (run.active_.size() >= run.limits_.maximum_depth) {
                run.fail(NestedErrorCode::DepthLimit, "Nested grid depth limit exceeded", edge);
            }
            if (edge) run.path_.push_back(*edge);
            try {
                run.active_.push_back(&grid);
            } catch (...) {
                if (has_edge_) run.path_.pop_back();
                throw;
            }
        }

        ~Scope() {
            run_.active_.pop_back();
            if (has_edge_) run_.path_.pop_back();
        }

        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        LayoutRun& run_;
        bool has_edge_;
    };

    void validate_children(const NestedGrid& grid) const {
        for (const auto& [id, child] : grid.children) {
            const auto item = std::find_if(grid.items.begin(), grid.items.end(),
                [id](const GridItem& value) { return value.id == id; });
            if (!child || item == grid.items.end()) {
                fail(NestedErrorCode::InvalidChild, "Child mapping needs a non-null grid and an existing item ID", id);
            }
        }
    }

    GridResult solve(const NestedGrid& grid, Size available) {
        if (result_.layout_count >= limits_.maximum_layouts) {
            fail(NestedErrorCode::WorkLimit, "Nested grid layout budget exceeded");
        }
        ++result_.layout_count;
        auto result = compute_grid(grid.spec, grid.items, available,
            [&](ItemId id, const MeasureInput& input) -> MeasureResult {
                ++result_.measurement_count;
                const auto child = grid.children.find(id);
                if (child != grid.children.end()) return measure_child(*child->second, id, input);
                if (grid.measure) return grid.measure(id, input, grid.user_data);
                // compute_grid validated the ID before invoking this callback.
                const auto item = std::find_if(grid.items.begin(), grid.items.end(),
                    [id](const GridItem& value) { return value.id == id; });
                return {{}, item->preferred_size};
            });
        if (!result.succeeded()) {
            fail(NestedErrorCode::GridFailure, result.error.message, result.error.item_id, result.error);
        }
        return result;
    }

    MeasureResult measure_child(const NestedGrid& child, ItemId id, const MeasureInput& input) {
        const Scope scope{*this, child, id};
        validate_children(child);
        const Size available{
            input.known_width.value_or(input.available.width),
            input.known_height.value_or(input.available.height)
        };
        const auto preferred = solve(child, available).size;
        // This host's finite, available-space measurement policy (not CSS intrinsic
        // sizing): probe minimum width at zero space, but minimum height at the SAME
        // width as the preferred pass. A narrow wrapping probe must not inflate the
        // parent's height. See docs/native-grid.md for limitations.
        const auto height_floor = available.height == 0
            ? preferred : solve(child, {available.width, 0}).size;
        const auto width_floor = available.width == 0
            ? height_floor : solve(child, {}).size;
        return {{std::min(width_floor.width, preferred.width),
                 std::min(height_floor.height, preferred.height)}, preferred};
    }

    Unit translated(double origin, Unit local, std::optional<ItemId> id = std::nullopt) const {
        const double value = origin + static_cast<double>(local);
        if (!std::isfinite(value) || value > std::numeric_limits<Unit>::max()) {
            fail(NestedErrorCode::NumericOverflow, "Nested root-relative coordinates exceed finite Unit", id);
        }
        return static_cast<Unit>(value);
    }

    Rect translated(Rect local, double x, double y, ItemId id) const {
        return {translated(x, local.x, id), translated(y, local.y, id), local.width, local.height};
    }

    Size arrange(const NestedGrid& grid, Size available, double x, double y, std::optional<ItemId> edge) {
        const Scope scope{*this, grid, edge};
        validate_children(grid);
        const auto result = solve(grid, available);
        // Check the full occupied extent too, even if no item touches its far edge.
        (void)translated(x, result.size.width);
        (void)translated(y, result.size.height);
        for (const auto& item : result.items) {
            auto path = path_;
            path.push_back(item.id);
            result_.items.push_back({std::move(path), translated(item.rect, x, y, item.id),
                                     translated(item.cell_rect, x, y, item.id)});
            const auto child = grid.children.find(item.id);
            if (child != grid.children.end()) {
                // Always recompute at the final ALIGNED rectangle, not the cell or
                // a measurement probe. Accumulate offsets in double before publishing.
                (void)arrange(*child->second, {item.rect.width, item.rect.height},
                              x + item.rect.x, y + item.rect.y, item.id);
            }
        }
        return result.size;
    }

    NestedLimits limits_;
    NestedResult result_;
    std::vector<const NestedGrid*> active_;
    std::vector<ItemId> path_;
};

} // namespace nested_detail

// Each invocation owns its stack/results. User exceptions propagate unchanged;
// a later call starts fresh. Apply the result only after succeeded() is true.
[[nodiscard]] inline NestedResult compute_nested_grid(
    const NestedGrid& root, Size available, NestedLimits limits = {}) {
    return nested_detail::LayoutRun{limits}.run(root, available);
}

} // namespace axisly::examples
