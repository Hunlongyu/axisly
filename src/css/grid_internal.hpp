#pragma once

#include "layout_engine.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace axisly::css::detail {

// The explicit grid starts at origin-zero line 0. Each direction may extend
// by 10,000 tracks; explicit tracks consume the positive direction's budget.
inline constexpr std::int32_t grid_track_limit = 10'000;
inline constexpr std::int32_t grid_minimum_line = -grid_track_limit;
inline constexpr std::int32_t grid_maximum_line = grid_track_limit;

struct AxisPlacement {
    std::optional<std::int32_t> start;
    std::int32_t span{1};
};

struct AbsoluteGridPlacement {
    std::optional<std::int32_t> start, end;
};

struct AxisEstimate {
    std::int32_t minimum_line{};
    std::int32_t maximum_line{};
    std::int32_t indefinite_span{1};
};

struct Placement {
    AxisPlacement row, column;
    AxisEstimate row_estimate, column_estimate;
    std::int32_t row_start{}, row_end{}, column_start{}, column_end{};
    bool hidden{};
};

struct ItemContribution {
    Placement placement;
    Size minimum_contribution{}, min_content{}, max_content{};
    Insets margin{};
    bool scrolls_x{}, scrolls_y{};
};

struct TrackState {
    TrackSizing style;
    Unit size{}, preferred{};
    std::optional<Unit> maximum;
    Unit flex{};
    bool intrinsic_min{}, intrinsic_max{}, auto_max{};
    bool collapsed{};
};

struct TrackAxis {
    explicit TrackAxis(std::pmr::memory_resource* memory) : tracks(memory), offsets(memory), gutter_after(memory) {}
    NodeId owner;
    std::int32_t origin{};
    std::pmr::vector<TrackState> tracks;
    std::pmr::vector<Unit> offsets;
    std::pmr::vector<Unit> gutter_after;
    Unit gap{};

    void clear() noexcept { tracks.clear(); offsets.clear(); gutter_after.clear(); }

    [[nodiscard]] Unit line(std::int32_t index) const {
        return offsets.at(static_cast<std::size_t>(index - origin));
    }
    [[nodiscard]] Unit span(std::int32_t start, std::int32_t end) const {
        const auto begin = static_cast<std::size_t>(start - origin);
        const auto finish = static_cast<std::size_t>(end - origin);
        // Track extents are nonnegative. Subtracting rounded absolute lines
        // and the last gutter can turn a zero-sized track into a negative area.
        double value = 0;
        for (auto index = begin; index < finish; ++index) {
            value += tracks.at(index).size;
            if (index + 1 < finish) value += gutter_after.at(index);
        }
        return checked(value, owner);
    }
};

struct ExpandedTemplate {
    std::vector<TrackSizing> tracks;
    std::vector<std::vector<std::string>> line_names;
    std::vector<bool> auto_fit;
};

struct GridExtent {
    std::int32_t row_start{}, row_end{};
    std::int32_t column_start{}, column_end{};
};

struct GridStructureKey {
    std::optional<Unit> width, height;
    Unit column_gap{}, row_gap{};
    bool minimum_width{}, minimum_height{};
    bool operator==(const GridStructureKey&) const = default;
};

// Templates and placement depend on style/topology. Only automatic repeats
// also depend on the repeat-sizing input. Mutable track sizes remain local to
// each layout query, so intrinsic and final passes can share this structure.
struct GridStructure {
    explicit GridStructure(std::pmr::memory_resource* memory)
        : placements(memory), absolute_rows(memory), absolute_columns(memory),
          collapsed_rows(memory), collapsed_columns(memory) {}
    GridStructureKey key;
    ExpandedTemplate columns, rows;
    std::pmr::vector<Placement> placements;
    std::pmr::vector<AbsoluteGridPlacement> absolute_rows, absolute_columns;
    std::pmr::vector<std::uint8_t> collapsed_rows, collapsed_columns;
    GridExtent extent;
};

// Allocated only for Grid containers; other nodes retain one pointer. Queries
// hold immutable structures independently of replacement in these two slots.
struct GridStructureCache {
    std::shared_ptr<const GridStructure> recent, previous;
};

[[nodiscard]] ExpandedTemplate expand_grid_template(
    const std::vector<TemplateTrack>& components,
    const std::vector<std::vector<std::string>>& line_names,
    std::optional<Unit> available, Unit gap, bool minimum_strategy, NodeId id);
void apply_grid_template_areas(const GridStyle& style,
    ExpandedTemplate& rows, ExpandedTemplate& columns);
[[nodiscard]] std::vector<std::vector<std::string>> reported_grid_line_names(
    const ExpandedTemplate& definition, const TrackAxis& axis);
[[nodiscard]] AxisPlacement resolve_grid_placement(const GridPlacement& value,
    const ExpandedTemplate& axis);
[[nodiscard]] AbsoluteGridPlacement resolve_absolute_grid_placement(
    const GridPlacement& value, const ExpandedTemplate& axis);
[[nodiscard]] Rect resolve_absolute_grid_area(const AbsoluteGridPlacement& rows,
    const AbsoluteGridPlacement& columns, const TrackAxis& row_tracks,
    const TrackAxis& column_tracks, Rect padding_box, Rect content_box,
    Direction direction, NodeId id);
[[nodiscard]] AxisEstimate estimate_grid_axis(const GridPlacement& value,
    std::int32_t explicit_count);
[[nodiscard]] GridExtent place_grid_items(std::span<Placement> placements,
    std::int32_t explicit_rows, std::int32_t explicit_columns,
    GridAutoFlow flow, NodeId id,
    std::pmr::memory_resource* memory = std::pmr::get_default_resource());

void make_axis(TrackAxis& axis, const std::vector<TrackSizing>& explicit_tracks,
    const std::vector<TrackSizing>& auto_tracks, std::int32_t minimum_line,
    std::int32_t maximum_line, std::optional<Unit> available, Unit gap, NodeId id);
[[nodiscard]] bool has_cyclic_percentage(const TrackAxis& axis);
void collapse_auto_fit(TrackAxis& axis, std::span<const std::uint8_t> collapsed);
[[nodiscard]] Unit grid_item_minimum(const LayoutNode& child, const LayoutBox& box,
    const Placement& placement, const TrackAxis& axis, Unit min_content, bool columns);
struct AxisSizingSpace {
    AvailableSpace grid_space;
    std::optional<Unit> inner_size;
    std::optional<Unit> minimum;
    std::optional<Unit> maximum;
    ContentAlignment alignment;
};
struct GridSizingWorkspace;
void size_axis(TrackAxis& axis, std::span<const ItemContribution> items,
    bool columns, const AxisSizingSpace& space, NodeId id, GridSizingWorkspace& workspace);
void align_tracks(TrackAxis& axis, Unit available, ContentAlignment alignment, NodeId id);

} // namespace axisly::css::detail
