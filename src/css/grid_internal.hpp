#pragma once

#include "layout_engine.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace axisly::css::detail {

struct AxisPlacement {
    std::optional<std::int32_t> start;
    std::int32_t span{1};
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
    std::optional<ResolvedBox> ratio_from_rows_box;
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
    std::int32_t origin{};
    std::vector<TrackState> tracks;
    std::vector<Unit> offsets;
    std::vector<Unit> gutter_after;
    Unit gap{};

    [[nodiscard]] Unit line(std::int32_t index) const {
        return offsets.at(static_cast<std::size_t>(index - origin));
    }
    [[nodiscard]] Unit span(std::int32_t start, std::int32_t end) const {
        const auto begin = static_cast<std::size_t>(start - origin);
        const auto finish = static_cast<std::size_t>(end - origin);
        return offsets.at(finish) - offsets.at(begin)
            - (end > start ? gutter_after.at(finish - 1) : 0);
    }
};

struct ExpandedTemplate {
    std::vector<TrackSizing> tracks;
    std::vector<std::vector<std::string>> line_names;
    std::vector<bool> auto_fit;
};

[[nodiscard]] TrackAxis make_axis(const std::vector<TrackSizing>& explicit_tracks,
    const std::vector<TrackSizing>& auto_tracks, std::int32_t minimum_line,
    std::int32_t maximum_line, std::optional<Unit> available, Unit gap, NodeId id);
[[nodiscard]] bool has_cyclic_percentage(const TrackAxis& axis);
void collapse_auto_fit(TrackAxis& axis, const ExpandedTemplate& definition,
    const std::vector<Placement>& placements, bool columns);
[[nodiscard]] Unit grid_item_minimum(const LayoutNode& child, const ResolvedBox& box,
    const Placement& placement, const TrackAxis& axis, Unit min_content, bool columns);
struct AxisSizingSpace {
    AvailableSpace grid_space;
    std::optional<Unit> inner_size;
    std::optional<Unit> minimum;
    std::optional<Unit> maximum;
    ContentAlignment alignment;
};
void size_axis(TrackAxis& axis, const std::vector<ItemContribution>& items,
    bool columns, const AxisSizingSpace& space, NodeId id);
void apply_cross_axis_ratio_contributions(const TrackAxis& rows,
    std::vector<ItemContribution>& items, NodeId id);
void align_tracks(TrackAxis& axis, Unit available, ContentAlignment alignment, NodeId id);

} // namespace axisly::css::detail
