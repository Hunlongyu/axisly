#pragma once

#include <cstddef>
#include <span>
#include <utility>
#include <vector>

#include <gridly/grid.hpp>
#include <gridly/measurement.hpp>
#include <gridly/result.hpp>

#include "span_resolution.hpp"
#include "track_sizing.hpp"
#include "placement.hpp"

namespace gridly::detail {

// Per-call for the stateless API; owned by one GridCache for repeated layouts.
// No process-wide/thread-local storage and no caller-owned input references.
struct LayoutWorkspace {
    std::vector<std::pair<ItemId, std::size_t>> ids;
    std::vector<MeasureResult> measurements;
    std::vector<ResolvedTrack> item_columns;
    SpanWorkspace column_contributions;
    SpanWorkspace row_contributions;
    TrackSizingWorkspace columns;
    TrackSizingWorkspace rows;
    PlacementWorkspace placement;
    std::vector<double> baseline_offsets;
    std::vector<double> baseline_shims;
    bool validated{};

    void invalidate_inputs() noexcept {
        validated = false;
        column_contributions.prepared = false;
        row_contributions.prepared = false;
        placement.prepared = false;
    }
};

// May retain output/workspace capacities. Only immutable input validation and span
// topology may be reused; container validity, measurements and numeric checks
// are evaluated on every call. Caller must invalidate_inputs after input edits.
void compute_grid_into(const GridSpec& spec, std::span<const GridItem> items, Size available,
                       MeasureCallback measure, void* user_data, LayoutWorkspace& workspace, GridResult& result,
                       bool retain_scratch);

} // namespace gridly::detail
