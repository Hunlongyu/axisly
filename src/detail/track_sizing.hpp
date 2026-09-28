#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include <axisly/track.hpp>

#include "track_state.hpp"

namespace axisly::detail {

struct ResolvedTrack {
    // Keep sizing precision until cell and aligned item output is built.
    double offset{};
    double size{};
};

struct ResolvedAxis {
    std::vector<ResolvedTrack> tracks;
    Unit extent{};
};

struct WeightedAllocation {
    std::size_t index{};
    double weight{};
    double minimum{};
    double maximum{};
    double size{};
    bool frozen{};
};

struct TrackSizingWorkspace {
    std::vector<WeightedAllocation> allocations;
    std::vector<double> sizes;
    ResolvedAxis result;
};

// Requires validated definitions and non-negative geometry. States have one
// entry per track and satisfy 0 <= minimum <= preferred <= maximum.
// Fixed axes do not need contribution states. Output is held in workspace.result.
// Returns false if offsets or the outer extent exceed Unit's finite range.
[[nodiscard]] bool resolve_tracks(
    std::span<const Track> tracks, Unit available, Unit gap, Unit start_padding, Unit end_padding,
    std::span<const TrackState> states, bool all_fixed, TrackSizingWorkspace& workspace);

} // namespace axisly::detail
