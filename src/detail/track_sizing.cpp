#include "track_sizing.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <variant>

namespace axisly::detail {
namespace {

bool fits_unit(double value) noexcept {
    return std::isfinite(value) && value >= 0.0
        && value <= static_cast<double>(std::numeric_limits<Unit>::max());
}

void distribute_weighted_space(std::vector<WeightedAllocation>& allocations, double budget) {
    for (;;) {
        double frozen_size = 0.0;
        double active_weight = 0.0;
        for (const auto& allocation : allocations) {
            if (allocation.frozen) {
                frozen_size += allocation.size;
            } else {
                // Recompute after freezing; subtracting huge weights can erase tiny survivors.
                active_weight += allocation.weight;
            }
        }
        if (active_weight == 0.0) {
            return;
        }

        const double remaining = std::max(0.0, budget - frozen_size);
        double clamped_total = 0.0;
        bool below_minimum = false;
        bool above_maximum = false;
        for (auto& allocation : allocations) {
            if (!allocation.frozen) {
                const double share = remaining * (allocation.weight / active_weight);
                below_minimum = below_minimum || share < allocation.minimum;
                above_maximum = above_maximum || share > allocation.maximum;
                allocation.size = std::clamp(share, allocation.minimum, allocation.maximum);
                clamped_total += allocation.size;
            }
        }
        if ((!below_minimum && !above_maximum) || clamped_total == remaining) {
            return;
        }

        // The sign selects bounds that must remain active at the solution.
        // Freezing both kinds at once would incorrectly pin a minimum when
        // another track's maximum later releases enough space for it to grow.
        const bool freeze_minimum = clamped_total > remaining;
        bool progress = false;
        for (auto& allocation : allocations) {
            if (allocation.frozen) {
                continue;
            }
            const double share = remaining * (allocation.weight / active_weight);
            if ((freeze_minimum && share < allocation.minimum)
                || (!freeze_minimum && share > allocation.maximum)) {
                allocation.frozen = true;
                progress = true;
            }
        }
        if (!progress) {
            // Only roundoff remains when the residual has no matching bound violation.
            return;
        }
    }
}

bool resolve_fixed_tracks(
    std::span<const Track> tracks, Unit gap, Unit start_padding, Unit end_padding, ResolvedAxis& result) {
    result.tracks.reserve(tracks.size());

    // Accumulate outside Unit precision before checking the representable range.
    double cursor = start_padding;
    for (std::size_t index = 0; index < tracks.size(); ++index) {
        if (index != 0) {
            cursor += gap;
        }
        const Unit size = std::get<FixedTrack>(tracks[index].definition()).size;
        const double end = cursor + size;
        if (!fits_unit(cursor) || !fits_unit(end)) {
            return false;
        }
        result.tracks.push_back({cursor, size});
        cursor = end;
    }

    const double extent = cursor + end_padding;
    if (!fits_unit(extent)) {
        return false;
    }
    result.extent = static_cast<Unit>(extent);
    return true;
}

} // namespace

bool resolve_tracks(
    std::span<const Track> tracks, Unit available, Unit gap, Unit start_padding, Unit end_padding,
    std::span<const TrackState> states, bool all_fixed, TrackSizingWorkspace& workspace) {
    auto& result = workspace.result;
    result.tracks.clear();
    result.extent = 0;
    if (all_fixed) {
        return resolve_fixed_tracks(tracks, gap, start_padding, end_padding, result);
    }

    auto& allocations = workspace.allocations;
    auto& sizes = workspace.sizes;
    allocations.clear();
    sizes.assign(tracks.size(), 0.0);
    double rigid_extent = static_cast<double>(start_padding) + end_padding;
    double minimum_sum = 0.0;
    double maximum_sum = 0.0;
    double content_minimum = 0.0;
    double content_preferred = 0.0;
    for (std::size_t index = 0; index < tracks.size(); ++index) {
        if (index != 0) {
            rigid_extent += gap;
        }
        const auto& state = states[index];
        if (state.growth == TrackGrowth::Fixed) {
            rigid_extent += state.minimum;
            sizes[index] = state.minimum;
        } else if (state.growth == TrackGrowth::Content) {
            content_minimum += state.minimum;
            content_preferred += state.preferred;
        } else {
            if (allocations.empty()) allocations.reserve(tracks.size());
            const double maximum = std::min(state.maximum, static_cast<double>(std::numeric_limits<Unit>::max()));
            allocations.push_back({index, state.weight, state.minimum, maximum, state.minimum, false});
            minimum_sum += state.minimum;
            maximum_sum += maximum;
        }
    }
    if (!fits_unit(rigid_extent + minimum_sum + content_minimum)) {
        return false;
    }

    // Reserve fixed geometry and numeric weighted minima first. Content uses
    // its preferred sizes when possible, otherwise a common fraction of each
    // track's shrinkable interval. Never shrink below a content minimum.
    const double content_budget = std::clamp(static_cast<double>(available) - rigid_extent - minimum_sum,
                                             content_minimum, content_preferred);
    const double flexibility = content_preferred - content_minimum;
    const double fraction = flexibility > 0.0 ? (content_budget - content_minimum) / flexibility : 0.0;
    for (std::size_t index = 0; index < tracks.size(); ++index) {
        if (states[index].growth == TrackGrowth::Content) {
            const auto& contribution = states[index];
            sizes[index] = content_budget == content_preferred ? contribution.preferred
                : contribution.minimum + (contribution.preferred - contribution.minimum) * fraction;
        }
    }
    rigid_extent += content_budget;

    const double minimum_extent = rigid_extent + minimum_sum;
    if (!fits_unit(minimum_extent)) {
        return false;
    }
    double extent = minimum_extent;
    if (!allocations.empty() && static_cast<double>(available) > minimum_extent) {
        const double maximum_extent = rigid_extent + maximum_sum;
        if (static_cast<double>(available) >= maximum_extent) {
            for (auto& allocation : allocations) {
                allocation.size = allocation.maximum;
            }
            extent = maximum_extent;
        } else {
            distribute_weighted_space(allocations, static_cast<double>(available) - rigid_extent);
            extent = available;
        }
    }

    result.tracks.reserve(tracks.size());
    result.extent = static_cast<Unit>(extent);
    double cursor = start_padding;
    std::size_t weighted_index = 0;
    for (std::size_t index = 0; index < tracks.size(); ++index) {
        if (index != 0) {
            cursor += gap;
        }
        double size = 0.0;
        if (weighted_index < allocations.size() && allocations[weighted_index].index == index) {
            size = allocations[weighted_index++].size;
        } else {
            size = sizes[index];
        }
        // The analytic occupied extent is finite. Clamp cumulative roundoff
        // at its boundary, including the case available == Unit::max().
        const double offset = std::min(cursor, extent);
        if (!fits_unit(size)) {
            return false;
        }
        result.tracks.push_back({offset, size});
        cursor += size;
    }
    return true;
}

} // namespace axisly::detail
