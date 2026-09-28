#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include <axisly/measurement.hpp>

#include "track_sizing.hpp"

namespace axisly::detail {

struct SpanContribution {
    std::size_t start{};
    std::size_t span{};
    double minimum{};
    double preferred{};
    std::size_t item_index{};
};

struct GrowthCandidate {
    std::size_t index{};
    double weight{};
    double capacity{};
    double growth{};
    bool capped{};
};

struct SpanWorkspace {
    std::vector<TrackState> contributions;
    std::vector<SpanContribution> spans;
    std::vector<double> planned;
    std::vector<GrowthCandidate> candidates;
    // Only topology/classification persist; all measurement contributions reset
    // on every layout. The owner must clear prepared after any input change.
    bool prepared{};
    bool all_fixed{};
};

// Requires validated placements, spans and measurements. Resolve single-cell
// intrinsic bounds first, then spanning minima, then Content preferences.
// Equal-span groups share a snapshot and merge growth by per-track maximum.
void collect_track_contributions(
    std::span<const Track> tracks, std::span<const GridItem> items,
    std::span<const MeasureResult> measurements, Unit gap, bool horizontal, SpanWorkspace& workspace,
    double percentage_basis = 0, std::span<const double> baseline_shims = {});

// The merged cell includes its internal gaps, excluding exterior gaps/padding.
// Requires a nonzero in-bounds span and a successfully resolved axis.
[[nodiscard]] ResolvedTrack spanned_track(
    std::span<const ResolvedTrack> tracks, std::size_t start, std::size_t span, Unit gap) noexcept;

} // namespace axisly::detail
