#include "span_resolution.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace axisly::detail {
namespace {

std::pair<double, double> requirements(const GridItem& item, const MeasureResult& measured,
                                     bool horizontal, double baseline_shim) {
    const double margins = horizontal ? static_cast<double>(item.margin.left) + item.margin.right
                                      : static_cast<double>(item.margin.top) + item.margin.bottom;
    if (!horizontal && item.vertical_alignment == VerticalAlignment::Baseline) {
        const double baseline = measured.baseline.value_or(measured.preferred.height);
        return {margins + std::max(static_cast<double>(measured.minimum.height), baseline) + baseline_shim,
                margins + measured.preferred.height + baseline_shim};
    }
    return {margins + (horizontal ? measured.minimum.width : measured.minimum.height),
            margins + (horizontal ? measured.preferred.width : measured.preferred.height)};
}

double plan_growth(std::span<const TrackState> states, const SpanContribution& item, double deficit,
                    TrackGrowth growth, bool preferred, std::vector<double>& planned,
                    std::vector<GrowthCandidate>& candidates) {
    candidates.clear();
    for (std::size_t index = item.start; index < item.start + item.span; ++index) {
        const auto& state = states[index];
        if (state.growth == growth) {
            const double capacity = state.maximum - (preferred ? state.preferred : state.minimum);
            if (capacity > 0.0) {
                candidates.push_back({index, growth == TrackGrowth::Weighted ? state.weight : 1.0, capacity, 0.0, false});
            }
        }
    }

    // Distribute increments above existing bounds, not total track sizes.
    // Capped tracks freeze; recomputing active weights retains tiny survivors.
    double residual = deficit;
    for (;;) {
        double frozen_growth = 0.0;
        double active_weight = 0.0;
        for (const auto& candidate : candidates) {
            if (candidate.capped) {
                frozen_growth += candidate.growth;
            } else {
                active_weight += candidate.weight;
            }
        }
        residual = std::max(0.0, deficit - frozen_growth);
        if (active_weight == 0.0) {
            break;
        }
        const double remaining = residual;
        bool capped_any = false;
        for (auto& candidate : candidates) {
            if (!candidate.capped) {
                const double share = remaining * (candidate.weight / active_weight);
                candidate.growth = std::min(share, candidate.capacity);
                if (share > candidate.capacity) {
                    candidate.capped = true;
                    capped_any = true;
                }
            }
        }
        if (!capped_any) {
            // The active shares consumed the residual analytically. Avoid
            // passing a sum-rounding remainder on to another growth category.
            residual = 0.0;
            break;
        }
    }
    for (const auto& candidate : candidates) {
        planned[candidate.index] = std::max(planned[candidate.index], candidate.growth);
    }
    return residual;
}

void grow_group(std::span<const SpanContribution> group, Unit gap,
                bool preferred, std::vector<TrackState>& contributions, std::vector<double>& planned,
                std::vector<GrowthCandidate>& candidates) {
    std::fill(planned.begin(), planned.end(), 0.0);
    for (const auto& item : group) {
        double occupied = static_cast<double>(gap) * static_cast<double>(item.span - 1);
        for (std::size_t index = item.start; index < item.start + item.span; ++index) {
            occupied += preferred ? contributions[index].preferred : contributions[index].minimum;
        }
        const double required = preferred ? item.preferred : item.minimum;
        const double deficit = std::max(0.0, required - occupied);
        if (deficit == 0.0) {
            continue;
        }
        const double residual = plan_growth(contributions, item, deficit, TrackGrowth::Content, preferred, planned, candidates);
        if (!preferred && residual > 0.0) {
            (void)plan_growth(contributions, item, residual, TrackGrowth::Weighted, false, planned, candidates);
        }
        // A remaining preference does not impose a weighted minimum. Container
        // space and numeric limits control weighted tracks' final sizes.
    }

    for (std::size_t index = 0; index < contributions.size(); ++index) {
        if (preferred) {
            contributions[index].preferred = std::min(contributions[index].preferred + planned[index], contributions[index].maximum);
        } else {
            contributions[index].minimum = std::min(contributions[index].minimum + planned[index], contributions[index].maximum);
        }
    }
}

} // namespace

void collect_track_contributions(
    std::span<const Track> tracks, std::span<const GridItem> items,
    std::span<const MeasureResult> measurements, Unit gap, bool horizontal, SpanWorkspace& workspace,
    double percentage_basis, std::span<const double> baseline_shims) {
    auto& result = workspace.contributions;
    auto& spans = workspace.spans;
    result.clear();
    if (!workspace.prepared) {
        spans.clear();
        workspace.all_fixed = std::all_of(tracks.begin(), tracks.end(), [](const Track& track) {
            return track.kind() == TrackKind::Fixed;
        });
    }
    // Fixed tracks never grow from content. Measurement still happens in the
    // core, preserving callback order/counts and item alignment semantics.
    if (workspace.all_fixed) {
        workspace.prepared = true;
        return;
    }
    result.reserve(tracks.size());
    for (const auto& track : tracks) {
        result.push_back(initialize_track(track, percentage_basis));
    }

    for (std::size_t index = 0; index < items.size(); ++index) {
        const auto& item = items[index];
        const auto& measured = measurements[index];
        const std::size_t start = horizontal ? item.column : item.row;
        const std::size_t span = horizontal ? item.column_span : item.row_span;
        if (span > 1 && workspace.prepared) continue;
        const auto [minimum, preferred] = requirements(item, measured, horizontal,
            baseline_shims.empty() ? 0 : baseline_shims[index]);
        if (span == 1) {
            auto& state = result[start];
            if (state.intrinsic_minimum) {
                state.minimum = std::min(std::max(state.minimum, minimum), state.maximum);
            }
            if (state.intrinsic_preferred) {
                state.preferred = std::min(std::max(state.preferred, preferred), state.maximum);
            }
        } else {
            if (spans.empty()) {
                // Only inputs with spans need this pass. Reserve exactly the
                // span count instead of repeatedly moving the prepared records;
                // a mostly single-cell grid must not reserve one per item.
                const auto count = std::count_if(items.begin() + static_cast<std::ptrdiff_t>(index), items.end(),
                    [horizontal](const GridItem& candidate) {
                        return (horizontal ? candidate.column_span : candidate.row_span) > 1;
                    });
                spans.reserve(static_cast<std::size_t>(count));
            }
            spans.push_back({start, span, minimum, preferred, index});
        }
    }
    for (auto& state : result) {
        state.preferred = std::max(state.preferred, state.minimum);
    }
    if (workspace.prepared) {
        for (auto& span : spans) {
            const auto& item = items[span.item_index];
            const auto& measured = measurements[span.item_index];
            const auto [minimum, preferred] = requirements(item, measured, horizontal,
                baseline_shims.empty() ? 0 : baseline_shims[span.item_index]);
            span.minimum = minimum;
            span.preferred = preferred;
        }
    } else {
        std::sort(spans.begin(), spans.end(), [](const SpanContribution& left, const SpanContribution& right) {
            return left.span < right.span;
        });
        workspace.prepared = true;
    }
    if (spans.empty()) return;
    auto& planned = workspace.planned;
    planned.resize(tracks.size());
    // A candidate array can never exceed its span. Reserving the largest span
    // also covers later measurements that activate previously dormant growth.
    workspace.candidates.reserve(spans.back().span);
    for (const bool preferred : {false, true}) {
        if (preferred) {
            for (auto& contribution : result) {
                contribution.preferred = std::max(contribution.preferred, contribution.minimum);
            }
        }
        for (std::size_t first = 0; first < spans.size();) {
            std::size_t last = first + 1;
            while (last < spans.size() && spans[last].span == spans[first].span) {
                ++last;
            }
            grow_group(std::span{spans}.subspan(first, last - first), gap, preferred, result, planned, workspace.candidates);
            first = last;
        }
    }
}

ResolvedTrack spanned_track(std::span<const ResolvedTrack> tracks, std::size_t start, std::size_t span, Unit gap) noexcept {
    if (span == 1) {
        return tracks[start];
    }
    double size = 0.0;
    for (std::size_t index = start; index < start + span; ++index) {
        if (index != start) {
            size += gap;
        }
        size += tracks[index].size;
    }
    // Summing sizes preserves a small span at a huge absolute offset. Subtracting
    // two end coordinates could erase it. Successful axis sizing bounds the sum;
    // cap only cumulative roundoff at the representable limit.
    return {tracks[start].offset, std::min(size, static_cast<double>(std::numeric_limits<Unit>::max()))};
}

} // namespace axisly::detail
