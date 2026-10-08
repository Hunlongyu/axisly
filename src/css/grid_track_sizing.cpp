#include "grid_internal.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace axisly::css::detail {
TrackAxis make_axis(const std::vector<TrackSizing>& explicit_tracks,
                    const std::vector<TrackSizing>& auto_tracks,
                    std::int32_t minimum_line, std::int32_t maximum_line,
                    std::optional<Unit> available, Unit gap, NodeId id) {
    TrackAxis axis;
    axis.owner = id;
    axis.origin = std::min<std::int32_t>(0, minimum_line);
    axis.gap = gap;
    const auto finish = std::max<std::int32_t>(static_cast<std::int32_t>(explicit_tracks.size()), maximum_line);
    if (axis.origin < grid_minimum_line || finish > grid_maximum_line) {
        throw LayoutFailure(LayoutErrorCode::LimitExceeded, id, "Grid track limit exceeded");
    }
    for (auto index = axis.origin; index < finish; ++index) {
        TrackSizing style;
        if (index >= 0 && index < static_cast<std::int32_t>(explicit_tracks.size())) {
            style = explicit_tracks[static_cast<std::size_t>(index)];
        } else if (!auto_tracks.empty()) {
            const auto sequence = index < 0
                ? auto_tracks.size() - 1
                    - (static_cast<std::size_t>(-index - 1) % auto_tracks.size())
                : static_cast<std::size_t>(index - explicit_tracks.size())
                    % auto_tracks.size();
            style = auto_tracks[sequence];
        }
        TrackState state;
        state.style = style;
        const auto minimum = style.minimum.resolve_numeric(available);
        auto maximum = style.maximum.resolve_numeric(available);
        if (!maximum) maximum = style.maximum.resolve_fit_content_limit(available);
        state.size = minimum ? std::max(*minimum, Unit{0}) : 0;
        state.preferred = state.size;
        if (maximum) state.maximum = std::max(*maximum, state.size);
        if (style.maximum.kind() == TrackBreadthKind::Fraction) {
            state.flex = style.maximum.fraction_factor();
        }
        const auto min_kind = style.minimum.kind();
        const auto max_kind = style.maximum.kind();
        state.intrinsic_min = min_kind == TrackBreadthKind::Auto
            || min_kind == TrackBreadthKind::MinContent || min_kind == TrackBreadthKind::MaxContent
            || ((min_kind == TrackBreadthKind::Percent || min_kind == TrackBreadthKind::Calc) && !available);
        state.intrinsic_max = max_kind == TrackBreadthKind::Auto
            || max_kind == TrackBreadthKind::MinContent || max_kind == TrackBreadthKind::MaxContent
            || max_kind == TrackBreadthKind::FitContentLength
            || max_kind == TrackBreadthKind::FitContentPercent
            || ((max_kind == TrackBreadthKind::Percent || max_kind == TrackBreadthKind::Calc) && !available);
        state.auto_max = max_kind == TrackBreadthKind::Auto;
        axis.tracks.push_back(std::move(state));
    }
    axis.gutter_after.assign(axis.tracks.size(), gap);
    if (!axis.gutter_after.empty()) axis.gutter_after.back() = 0;
    return axis;
}

bool has_cyclic_percentage(const TrackAxis& axis) {
    const auto needs_basis = [](const TrackBreadth& breadth) {
        const auto kind = breadth.kind();
        return kind == TrackBreadthKind::Percent
            || kind == TrackBreadthKind::FitContentPercent
            || (kind == TrackBreadthKind::Calc
                && !breadth.resolve_numeric(std::nullopt));
    };
    return std::any_of(axis.tracks.begin(), axis.tracks.end(),
        [&](const TrackState& track) {
            return needs_basis(track.style.minimum)
                || needs_basis(track.style.maximum);
        });
}

void collapse_auto_fit(TrackAxis& axis, const ExpandedTemplate& definition,
                       std::span<const Placement> placements, bool columns) {
    for (std::size_t index = 0; index < definition.auto_fit.size(); ++index) {
        if (!definition.auto_fit[index]) continue;
        const auto occupied = std::any_of(placements.begin(), placements.end(),
            [&](const Placement& placement) {
                const auto start = columns ? placement.column_start : placement.row_start;
                const auto end = columns ? placement.column_end : placement.row_end;
                return !placement.hidden && start <= static_cast<std::int32_t>(index)
                    && end > static_cast<std::int32_t>(index);
            });
        if (occupied) continue;
        auto& track = axis.tracks[static_cast<std::size_t>(
            static_cast<std::int32_t>(index) - axis.origin)];
        track = {};
        track.collapsed = true;
    }
    bool later_visible = false;
    for (std::size_t index = axis.tracks.size(); index-- > 0;) {
        axis.gutter_after[index] = !axis.tracks[index].collapsed && later_visible
            ? axis.gap : Unit{0};
        later_visible |= !axis.tracks[index].collapsed;
    }
}

void grow_nonflexible_tracks(TrackAxis& axis, std::optional<Unit> available,
                             AvailableKind query, double free, NodeId id) {
    if (!available) {
        if (query == AvailableKind::MinContent) return;
        for (auto& track : axis.tracks) {
            if (track.intrinsic_max) track.size = track.preferred;
            else if (track.maximum && track.flex <= 0) track.size = *track.maximum;
        }
        return;
    }
    if (free <= 0) return;
    const auto growth_limit = [](const TrackState& track) -> std::optional<Unit> {
        if (track.flex > 0) return std::nullopt;
        if (track.intrinsic_max) return track.preferred;
        return track.maximum;
    };
    std::vector<double> capacities;
    capacities.reserve(axis.tracks.size());
    for (const auto& track : axis.tracks) {
        const auto limit = growth_limit(track);
        if (limit && *limit > track.size) {
            capacities.push_back(static_cast<double>(*limit) - track.size);
        }
    }
    if (capacities.empty()) return;
    std::sort(capacities.begin(), capacities.end());
    double level = 0;
    std::size_t active = capacities.size();
    double remaining_space = free;
    for (const double capacity : capacities) {
        const double cost = (capacity - level) * active;
        if (remaining_space <= cost) {
            level += remaining_space / active;
            break;
        }
        remaining_space -= cost;
        level = capacity;
        --active;
    }
    for (auto& track : axis.tracks) {
        const auto limit = growth_limit(track);
        if (!limit || *limit <= track.size) continue;
        const Unit previous = track.size;
        const double capacity = static_cast<double>(*limit) - previous;
        track.size = checked(static_cast<double>(previous)
            + std::min(capacity, level), id);
    }
}

double size_of_fraction_unit(const TrackAxis& axis, std::int32_t start,
                             std::int32_t end, double space_to_fill) {
    if (space_to_fill == 0) return 0;
    std::vector<bool> frozen(static_cast<std::size_t>(end - start));
    double unit = 0;
    for (auto pass = start; pass <= end; ++pass) {
        double used = 0, factors = 0;
        for (auto index = start; index < end; ++index) {
            const auto slot = static_cast<std::size_t>(index - axis.origin);
            const auto& track = axis.tracks[slot];
            if (index + 1 < end) used += axis.gutter_after[slot];
            if (track.style.maximum.kind() == TrackBreadthKind::Fraction
                && !frozen[static_cast<std::size_t>(index - start)]) {
                factors += track.flex;
            } else used += track.size;
        }
        unit = (space_to_fill - used) / std::max(1.0, factors);
        bool changed = false;
        for (auto index = start; index < end; ++index) {
            const auto slot = static_cast<std::size_t>(index - axis.origin);
            const auto& track = axis.tracks[slot];
            if (track.style.maximum.kind() != TrackBreadthKind::Fraction) continue;
            const auto frozen_slot = static_cast<std::size_t>(index - start);
            if (!frozen[frozen_slot] && track.flex * unit < track.size) {
                frozen[frozen_slot] = true;
                changed = true;
            }
        }
        if (!changed) break;
    }
    return unit;
}

Unit grid_item_minimum(const LayoutNode& child, const LayoutBox& box,
                       const Placement& placement, const TrackAxis& axis,
                       Unit min_content, bool columns) {
    const auto preferred = columns ? box.preferred.width : box.preferred.height;
    if (preferred) return *preferred;
    const auto minimum = columns ? box.minimum.width : box.minimum.height;
    if (minimum) return *minimum;
    const auto overflow = columns ? child.style.overflow_x : child.style.overflow_y;
    if (overflow == Overflow::Hidden || overflow == Overflow::Scroll) return 0;
    const auto start = columns ? placement.column_start : placement.row_start;
    const auto end = columns ? placement.column_end : placement.row_end;
    bool spans_auto_minimum = false;
    bool spans_flexible_track = false;
    for (auto index = start; index < end; ++index) {
        const auto& track = axis.tracks[static_cast<std::size_t>(index - axis.origin)];
        spans_auto_minimum |= track.style.minimum.kind() == TrackBreadthKind::Auto;
        spans_flexible_track |= track.style.maximum.kind()
            == TrackBreadthKind::Fraction;
    }
    if (!spans_auto_minimum || (end - start > 1 && spans_flexible_track)) return 0;
    if (child.style.box.replaced) {
        // Taffy caps a compressible replaced item's content suggestion using
        // zero as the basis of otherwise indefinite preferred/maximum sizes.
        const auto& size = columns ? child.style.box.size.width : child.style.box.size.height;
        const auto& maximum = columns ? child.style.box.max_size.width : child.style.box.max_size.height;
        if (const auto value = size.resolve_numeric(Unit{0})) min_content = std::min(min_content, *value);
        if (const auto value = maximum.resolve_numeric(Unit{0})) min_content = std::min(min_content, *value);
    }
    std::optional<Unit> fixed_limit{Unit{0}};
    for (auto index = start; index < end; ++index) {
        const auto& track = axis.tracks[static_cast<std::size_t>(index - axis.origin)];
        const auto kind = track.style.maximum.kind();
        if (!track.maximum || kind == TrackBreadthKind::FitContentLength
            || kind == TrackBreadthKind::FitContentPercent) {
            fixed_limit.reset();
            break;
        }
        *fixed_limit = checked(static_cast<double>(*fixed_limit) + *track.maximum, child.id);
        if (index + 1 < end) {
            *fixed_limit = checked(static_cast<double>(*fixed_limit)
                + axis.gutter_after[static_cast<std::size_t>(index - axis.origin)], child.id);
        }
    }
    return fixed_limit ? std::min(min_content, *fixed_limit) : min_content;
}

namespace {
double distribute_to_limits(const std::vector<double>& bases,
    const std::vector<double>& limits,
    std::vector<double>& increases, double remaining) {
    constexpr double threshold = 0.01;
    for (std::size_t pass = 0; pass <= bases.size() && remaining > threshold; ++pass) {
        std::size_t active = 0;
        double capacity = std::numeric_limits<double>::infinity();
        for (std::size_t i = 0; i < bases.size(); ++i) {
            const double room = limits[i] - bases[i] - increases[i];
            if (room <= 0) continue;
            ++active;
            capacity = std::min(capacity, room);
        }
        if (active == 0) break;
        const double share = std::min(remaining / active, capacity);
        for (std::size_t i = 0; i < bases.size(); ++i) {
            if (limits[i] - bases[i] - increases[i] >= share) {
                increases[i] += share;
                remaining -= share;
            }
        }
    }
    return remaining;
}

std::vector<double> plan_spanning_base_increase(const TrackAxis& axis,
    const std::vector<std::size_t>& affected,
    const std::vector<bool>& has_single_growth_limit,
    double deficit, bool max_content_phase) {
    const double infinity = std::numeric_limits<double>::infinity();
    std::vector<double> increases(affected.size());
    std::vector<double> bases;
    bases.reserve(affected.size());
    std::vector<double> limits;
    limits.reserve(affected.size());
    for (const auto slot : affected) {
        const auto& track = axis.tracks[slot];
        bases.push_back(track.size);
        if (track.intrinsic_max) {
            limits.push_back(has_single_growth_limit[slot] ? track.preferred : infinity);
        } else {
            limits.push_back(track.maximum ? *track.maximum : infinity);
        }
        const auto kind = track.style.maximum.kind();
        if (max_content_phase && track.maximum
            && (kind == TrackBreadthKind::FitContentLength || kind == TrackBreadthKind::FitContentPercent)) {
            limits.back() = std::min(limits.back(), std::max<double>(track.size, *track.maximum));
        }
    }
    double remaining = distribute_to_limits(bases, limits, increases, deficit);
    if (remaining <= 0.01) return increases;

    const auto preferred_beyond_limit = [&](std::size_t slot) {
        const auto kind = axis.tracks[slot].style.maximum.kind();
        return max_content_phase
            ? kind == TrackBreadthKind::MaxContent
                || kind == TrackBreadthKind::FitContentLength
                || kind == TrackBreadthKind::FitContentPercent
            : axis.tracks[slot].intrinsic_max;
    };
    const bool has_preferred = std::any_of(affected.begin(), affected.end(),
        preferred_beyond_limit);
    for (std::size_t i = 0; i < affected.size(); ++i) {
        const auto slot = affected[i];
        if (has_preferred && !preferred_beyond_limit(slot)) {
            limits[i] = axis.tracks[slot].size + increases[i];
            continue;
        }
        const auto& track = axis.tracks[slot];
        const auto kind = track.style.maximum.kind();
        const bool fit_content = kind == TrackBreadthKind::FitContentLength
            || kind == TrackBreadthKind::FitContentPercent;
        limits[i] = fit_content && track.maximum ? std::max<double>(track.size, *track.maximum) : infinity;
    }
    distribute_to_limits(bases, limits, increases, remaining);
    return increases;
}

std::vector<double> plan_spanning_growth_limit_increase(const TrackAxis& axis,
    const std::vector<std::size_t>& affected,
    const std::vector<bool>& growth_limit_set,
    const std::vector<bool>& infinitely_growable, double deficit) {
    const double infinity = std::numeric_limits<double>::infinity();
    std::vector<double> bases;
    std::vector<double> limits;
    std::vector<std::size_t> uncapped;
    bases.reserve(affected.size());
    limits.reserve(affected.size());
    for (std::size_t i = 0; i < affected.size(); ++i) {
        const auto slot = affected[i];
        const auto& track = axis.tracks[slot];
        const auto max_kind = track.style.maximum.kind();
        const bool fit_content = max_kind == TrackBreadthKind::FitContentLength
            || max_kind == TrackBreadthKind::FitContentPercent;
        bases.push_back(std::max(track.size, track.preferred));
        limits.push_back(fit_content && track.maximum
            ? std::max<double>(*track.maximum, track.size) : infinity);
        if (infinitely_growable[slot]
            || (!growth_limit_set[slot] && limits.back() == infinity)) {
            uncapped.push_back(i);
        }
    }
    std::vector<double> increases(affected.size());
    if (uncapped.empty()) {
        distribute_to_limits(bases, limits, increases, deficit);
    } else {
        const double share = deficit / uncapped.size();
        for (const auto index : uncapped) increases[index] = share;
    }
    return increases;
}
} // namespace

void size_axis(TrackAxis& axis, std::span<const ItemContribution> items,
               bool columns, const AxisSizingSpace& space, NodeId id) {
    const auto query = space.grid_space.kind;
    const auto range = [&](const ItemContribution& item) {
        return columns
            ? std::pair{item.placement.column_start, item.placement.column_end}
            : std::pair{item.placement.row_start, item.placement.row_end};
    };
    enum class ContributionKind { Minimum, MinContent, MaxContent };
    const auto contribution = [&](const ItemContribution& item,
                                  ContributionKind kind) {
        const auto& size = kind == ContributionKind::Minimum
            ? item.minimum_contribution
            : kind == ContributionKind::MinContent
                ? item.min_content : item.max_content;
        const Unit content = columns
            ? size.width : size.height;
        const double margin = columns
            ? static_cast<double>(item.margin.left) + item.margin.right
            : static_cast<double>(item.margin.top) + item.margin.bottom;
        return checked(std::max(0.0, static_cast<double>(content) + margin), id);
    };
    if (!items.empty()) {
        const bool intrinsic_query = query == AvailableKind::MinContent
            || query == AvailableKind::MaxContent;
        struct SpanningItem {
            const ItemContribution* item;
            std::int32_t start, span;
            bool crosses_flexible_track;
            bool crosses_intrinsic_track;
        };
        std::vector<SpanningItem> spanning_items;
        spanning_items.reserve(items.size());
        std::vector<bool> has_single_growth_limit(axis.tracks.size());
        const auto intrinsic_breadth = [](TrackBreadthKind kind) {
            return kind == TrackBreadthKind::Auto
                || kind == TrackBreadthKind::MinContent
                || kind == TrackBreadthKind::MaxContent
                || kind == TrackBreadthKind::FitContentLength
                || kind == TrackBreadthKind::FitContentPercent;
        };
        for (const auto& item : items) {
            const auto [start, end] = range(item);
            if (end - start <= 1) continue;
            bool crosses_flexible_track = false;
            bool crosses_intrinsic_track = false;
            for (auto index = start; index < end; ++index) {
                const auto& track = axis.tracks[static_cast<std::size_t>(index - axis.origin)];
                crosses_flexible_track |= track.style.maximum.kind() == TrackBreadthKind::Fraction;
                crosses_intrinsic_track |= intrinsic_breadth(track.style.minimum.kind())
                    || intrinsic_breadth(track.style.maximum.kind());
            }
            spanning_items.push_back({&item, start, end - start,
                crosses_flexible_track, crosses_intrinsic_track});
        }
        std::stable_sort(spanning_items.begin(), spanning_items.end(),
            [](const SpanningItem& a, const SpanningItem& b) {
                if (a.crosses_flexible_track != b.crosses_flexible_track) {
                    return !a.crosses_flexible_track;
                }
                if (a.span != b.span) return a.span < b.span;
                return a.start < b.start;
            });
        const auto scaled_flexible_contribution = [&](const SpanningItem& spanning,
                                                       ContributionKind kind) {
            const auto& item = *spanning.item;
            const double raw = contribution(item, kind);
            const bool scrolls = columns ? item.scrolls_x : item.scrolls_y;
            if (!intrinsic_query || !spanning.crosses_flexible_track || scrolls) {
                return raw;
            }
            const auto [start, end] = range(item);
            double inflexible = 0, factors = 0;
            for (auto index = start; index < end; ++index) {
                const auto& track = axis.tracks[static_cast<std::size_t>(index - axis.origin)];
                if (track.style.maximum.kind() == TrackBreadthKind::Fraction) {
                    factors += track.flex;
                } else {
                    inflexible += track.size;
                }
            }
            const double scaled = inflexible
                + std::max(0.0, raw - inflexible) * std::min(factors, 1.0);
            return std::max(scaled,
                static_cast<double>(contribution(item, ContributionKind::Minimum)));
        };
        for (const auto& item : items) {
            const auto [start, end] = range(item);
            const auto count = end - start;
            if (count != 1) continue;
            auto& track = axis.tracks[static_cast<std::size_t>(start - axis.origin)];
            const auto minimum = contribution(item, ContributionKind::MinContent);
            const auto minimum_contribution = contribution(item, ContributionKind::Minimum);
            const auto preferred = contribution(item, query == AvailableKind::MinContent
                ? ContributionKind::MinContent : ContributionKind::MaxContent);
            if (track.intrinsic_min
                && track.style.maximum.kind() != TrackBreadthKind::Fraction) {
                const auto kind = track.style.minimum.kind();
                track.size = std::max(track.size,
                    kind == TrackBreadthKind::MaxContent
                        ? contribution(item, ContributionKind::MaxContent)
                        : kind == TrackBreadthKind::Auto
                            ? intrinsic_query && !(columns ? item.scrolls_x : item.scrolls_y)
                                ? std::max(minimum_contribution,
                                    std::min(minimum, track.maximum.value_or(minimum)))
                                : minimum_contribution
                            : minimum);
            }
            if (track.intrinsic_max) {
                const auto kind = track.style.maximum.kind();
                const Unit growth = kind == TrackBreadthKind::MinContent
                    ? minimum : preferred;
                if (growth > 0) {
                    has_single_growth_limit[static_cast<std::size_t>(start - axis.origin)] = true;
                    track.preferred = std::max(track.preferred, growth);
                }
            }
            if (track.flex > 0) track.preferred = std::max(track.preferred, preferred);
        }
        const auto grow_spanning_bases = [&](ContributionKind kind, auto eligible) {
            std::vector<Unit> planned(axis.tracks.size());
            std::vector<std::size_t> touched;
            for (std::size_t first = 0; first < spanning_items.size();) {
                std::size_t last = first + 1;
                while (last < spanning_items.size()
                    && spanning_items[last].crosses_flexible_track
                        == spanning_items[first].crosses_flexible_track
                    && spanning_items[last].span == spanning_items[first].span) {
                    ++last;
                }
                for (auto item_index = first; item_index < last; ++item_index) {
                    const auto& spanning = spanning_items[item_index];
                    if (spanning.crosses_flexible_track) continue;
                    if (kind == ContributionKind::Minimum
                        && !spanning.crosses_intrinsic_track) continue;
                    const auto& item = *spanning.item;
                    const auto [start, end] = range(item);
                    double occupied = 0;
                    std::vector<std::size_t> affected;
                    for (auto index = start; index < end; ++index) {
                        const auto slot = static_cast<std::size_t>(index - axis.origin);
                        const auto& track = axis.tracks[slot];
                        occupied += track.size;
                        if (index + 1 < end) occupied += axis.gutter_after[slot];
                        if (eligible(track, start, end)) affected.push_back(slot);
                    }
                    double target = contribution(item, kind);
                    if (kind == ContributionKind::Minimum && intrinsic_query
                        && !(columns ? item.scrolls_x : item.scrolls_y)) {
                        double limit = 0;
                        bool all_fixed = true;
                        for (auto index = start; index < end; ++index) {
                            const auto& track = axis.tracks[static_cast<std::size_t>(index - axis.origin)];
                            if (!track.maximum) { all_fixed = false; break; }
                            limit += *track.maximum;
                        }
                        const double min_content = contribution(item, ContributionKind::MinContent);
                        target = std::max(target, all_fixed ? std::min(min_content, limit) : min_content);
                    }
                    const double deficit = target - occupied;
                    if (deficit <= 0 || affected.empty()) continue;
                    const auto increases = plan_spanning_base_increase(axis, affected,
                        has_single_growth_limit, deficit,
                        kind == ContributionKind::MaxContent);
                    for (std::size_t i = 0; i < affected.size(); ++i) {
                        const auto slot = affected[i];
                        const Unit added = checked(increases[i], id);
                        if (added <= 0) continue;
                        if (planned[slot] == 0) touched.push_back(slot);
                        planned[slot] = std::max(planned[slot], added);
                    }
                }
                for (const auto slot : touched) {
                    axis.tracks[slot].size = checked(static_cast<double>(axis.tracks[slot].size)
                        + planned[slot], id);
                    planned[slot] = 0;
                }
                touched.clear();
                first = last;
            }
        };
        grow_spanning_bases(ContributionKind::Minimum,
            [](const TrackState& track, std::int32_t, std::int32_t) {
                return track.intrinsic_min;
            });
        grow_spanning_bases(ContributionKind::MinContent,
            [](const TrackState& track, std::int32_t, std::int32_t) {
                const auto kind = track.style.minimum.kind();
                return kind == TrackBreadthKind::MinContent
                    || kind == TrackBreadthKind::MaxContent;
            });
        if (query == AvailableKind::MaxContent) {
            grow_spanning_bases(ContributionKind::MaxContent,
                [&](const TrackState& track, std::int32_t start, std::int32_t end) {
                    const auto kind = track.style.minimum.kind();
                    const bool has_max_content_min = std::any_of(
                        axis.tracks.begin() + (start - axis.origin),
                        axis.tracks.begin() + (end - axis.origin),
                        [](const TrackState& candidate) {
                            return candidate.style.minimum.kind()
                                == TrackBreadthKind::MaxContent;
                        });
                    return has_max_content_min
                        ? kind == TrackBreadthKind::MaxContent
                        : kind == TrackBreadthKind::Auto
                            && track.style.maximum.kind() != TrackBreadthKind::MinContent;
                });
        }
        grow_spanning_bases(ContributionKind::MaxContent,
            [](const TrackState& track, std::int32_t, std::int32_t) {
                return track.style.minimum.kind() == TrackBreadthKind::MaxContent;
            });
        // Items crossing flexible tracks are sized as one batch, regardless of
        // their span. Each item sees the same base sizes; competing contributions
        // are combined by the largest planned increase on each track.
        {
            std::vector<Unit> planned(axis.tracks.size());
            for (const auto& item : items) {
                const auto [start, end] = range(item);
                bool crosses_flex = false;
                double occupied = 0, factor_sum = 0, inflexible = 0;
                std::size_t affected_count = 0;
                for (auto index = start; index < end; ++index) {
                    const auto slot = static_cast<std::size_t>(index - axis.origin);
                    const auto& track = axis.tracks[slot];
                    crosses_flex |= track.style.maximum.kind()
                        == TrackBreadthKind::Fraction;
                    occupied += track.size;
                    if (index + 1 < end) occupied += axis.gutter_after[slot];
                    if (track.style.maximum.kind() == TrackBreadthKind::Fraction
                        && track.intrinsic_min) {
                        factor_sum += track.flex;
                        ++affected_count;
                    } else inflexible += track.size;
                }
                if (!crosses_flex) continue;
                if (affected_count == 0) continue;
                const double minimum = contribution(item, ContributionKind::Minimum);
                double target = minimum;
                if (intrinsic_query && !(columns ? item.scrolls_x : item.scrolls_y)) {
                    const double min_content = contribution(item, ContributionKind::MinContent);
                    target = std::max(minimum, inflexible
                        + std::max(0.0, min_content - inflexible)
                            * std::min(factor_sum, 1.0));
                }
                const double deficit = target - occupied;
                if (deficit <= 0) continue;
                for (auto index = start; index < end; ++index) {
                    const auto slot = static_cast<std::size_t>(index - axis.origin);
                    const auto& track = axis.tracks[slot];
                    if (track.style.maximum.kind() != TrackBreadthKind::Fraction
                        || !track.intrinsic_min) continue;
                    const double weight = factor_sum > 0 ? track.flex : 1.0;
                    const double total = factor_sum > 0 ? factor_sum : affected_count;
                    planned[slot] = std::max(planned[slot], checked(
                        deficit * weight / total, id));
                }
            }
            for (std::size_t slot = 0; slot < axis.tracks.size(); ++slot) {
                if (planned[slot] > 0) {
                    axis.tracks[slot].size = checked(
                        static_cast<double>(axis.tracks[slot].size) + planned[slot], id);
                }
            }
        }
        std::vector<bool> growth_limit_set = has_single_growth_limit;
        std::vector<bool> infinitely_growable(axis.tracks.size());
        const auto grow_spanning_limits = [&](ContributionKind kind, auto eligible,
                                               bool mark_infinitely_growable) {
            std::vector<Unit> planned(axis.tracks.size());
            for (std::size_t first = 0; first < spanning_items.size();) {
                std::size_t last = first + 1;
                while (last < spanning_items.size()
                    && spanning_items[last].crosses_flexible_track
                        == spanning_items[first].crosses_flexible_track
                    && spanning_items[last].span == spanning_items[first].span) ++last;
                for (std::size_t item_index = first; item_index < last; ++item_index) {
                    const auto& spanning = spanning_items[item_index];
                    if (spanning.crosses_flexible_track) continue;
                    const auto [start, end] = range(*spanning.item);
                    double occupied = 0;
                    std::vector<std::size_t> affected;
                    for (auto index = start; index < end; ++index) {
                        const auto slot = static_cast<std::size_t>(index - axis.origin);
                        const auto& track = axis.tracks[slot];
                        occupied += track.intrinsic_max
                            ? std::max(track.size, track.preferred)
                            : track.maximum.value_or(track.size);
                        if (index + 1 < end) occupied += axis.gutter_after[slot];
                        if (eligible(track)) affected.push_back(slot);
                    }
                    const double deficit = contribution(*spanning.item, kind) - occupied;
                    if (deficit <= 0 || affected.empty()) continue;
                    const auto increases = plan_spanning_growth_limit_increase(axis,
                        affected, growth_limit_set, infinitely_growable, deficit);
                    for (std::size_t i = 0; i < affected.size(); ++i) {
                        const auto slot = affected[i];
                        planned[slot] = std::max(planned[slot], checked(increases[i], id));
                    }
                }
                for (std::size_t slot = 0; slot < axis.tracks.size(); ++slot) {
                    if (planned[slot] <= 0) continue;
                    auto& track = axis.tracks[slot];
                    track.preferred = checked(std::max(track.size, track.preferred)
                        + planned[slot], id);
                    growth_limit_set[slot] = true;
                    infinitely_growable[slot] = mark_infinitely_growable;
                    planned[slot] = 0;
                }
                first = last;
            }
        };
        grow_spanning_limits(ContributionKind::MinContent,
            [](const TrackState& track) { return track.intrinsic_max; }, true);
        grow_spanning_limits(ContributionKind::MaxContent,
            [](const TrackState& track) {
                const auto kind = track.style.maximum.kind();
                return kind == TrackBreadthKind::Auto
                    || kind == TrackBreadthKind::MaxContent
                    || kind == TrackBreadthKind::FitContentLength
                    || kind == TrackBreadthKind::FitContentPercent;
            }, false);
        for (const auto& spanning_item : spanning_items) {
            if (!spanning_item.crosses_flexible_track) continue;
            const auto& item = *spanning_item.item;
            const auto [start, end] = range(item);
            const bool weighted = intrinsic_query
                && spanning_item.crosses_flexible_track
                && !(columns ? item.scrolls_x : item.scrolls_y);
            double preferred_occupied = 0;
            for (auto index = start; index + 1 < end; ++index) {
                preferred_occupied += axis.gutter_after[static_cast<std::size_t>(index - axis.origin)];
            }
            double weight_sum = 0;
            for (auto index = start; index < end; ++index) {
                const auto& track = axis.tracks[static_cast<std::size_t>(index - axis.origin)];
                preferred_occupied += track.intrinsic_max || track.flex > 0
                    ? std::max(track.size, track.preferred)
                    : track.maximum.value_or(track.size);
                if (spanning_item.crosses_flexible_track
                    ? track.style.maximum.kind() == TrackBreadthKind::Fraction
                    : track.intrinsic_max || track.flex > 0) {
                    weight_sum += weighted ? track.flex : 1.0;
                }
            }
            const auto preferred_kind = query == AvailableKind::MinContent
                ? ContributionKind::MinContent : ContributionKind::MaxContent;
            const double target = weighted
                ? scaled_flexible_contribution(spanning_item, preferred_kind)
                : contribution(item, preferred_kind);
            const double preferred_deficit = target - preferred_occupied;
            if (preferred_deficit > 0 && weight_sum > 0) {
                const double unit = preferred_deficit / weight_sum;
                for (auto index = start; index < end; ++index) {
                    auto& track = axis.tracks[static_cast<std::size_t>(index - axis.origin)];
                    if (spanning_item.crosses_flexible_track
                        ? track.style.maximum.kind() == TrackBreadthKind::Fraction
                        : track.intrinsic_max || track.flex > 0) {
                        track.preferred = checked(std::max(track.size, track.preferred)
                            + unit * (weighted ? track.flex : 1.0), id);
                    }
                }
            }
        }
    }
    double occupied = 0;
    for (const auto gap : axis.gutter_after) occupied += gap;
    for (auto& track : axis.tracks) {
        track.preferred = std::max(track.preferred, track.size);
        if (track.maximum) {
            track.maximum = std::max(*track.maximum, track.size);
            track.preferred = std::min(track.preferred, *track.maximum);
        }
        occupied += track.size;
    }
    const auto grid_size = query == AvailableKind::Definite
        ? std::optional<Unit>{space.grid_space.value} : std::nullopt;
    const double free = grid_size ? static_cast<double>(*grid_size) - occupied : 0;
    grow_nonflexible_tracks(axis, grid_size, query, free, id);
    double expansion_free = space.inner_size ? static_cast<double>(*space.inner_size) : 0;
    for (const auto gap : axis.gutter_after) expansion_free -= gap;
    for (const auto& track : axis.tracks) expansion_free -= track.size;
    if (space.inner_size && expansion_free > 0) {
        std::vector<bool> frozen(axis.tracks.size());
        double nonflex = 0;
        for (const auto gap : axis.gutter_after) nonflex += gap;
        for (std::size_t i = 0; i < axis.tracks.size(); ++i) {
            if (axis.tracks[i].flex <= 0) {
                frozen[i] = true;
                nonflex += axis.tracks[i].size;
            }
        }
        for (std::size_t pass = 0; pass < axis.tracks.size(); ++pass) {
            double factors = 0;
            for (std::size_t i = 0; i < axis.tracks.size(); ++i) {
                if (!frozen[i]) factors += axis.tracks[i].flex;
            }
            if (factors <= 0) break;
            const double unit = (static_cast<double>(*space.inner_size) - nonflex)
                / std::max(1.0, factors);
            bool changed = false;
            for (std::size_t i = 0; i < axis.tracks.size(); ++i) {
                if (!frozen[i] && axis.tracks[i].size > unit * axis.tracks[i].flex) {
                    frozen[i] = true;
                    nonflex += axis.tracks[i].size;
                    changed = true;
                }
            }
            if (changed) continue;
            for (std::size_t i = 0; i < axis.tracks.size(); ++i) {
                if (!frozen[i]) axis.tracks[i].size = checked(
                    std::max<double>(axis.tracks[i].size, unit * axis.tracks[i].flex), id);
            }
            break;
        }
    } else if (!space.inner_size && query != AvailableKind::MinContent) {
        double fr_unit = 0;
        for (const auto& track : axis.tracks) {
            if (track.style.maximum.kind() == TrackBreadthKind::Fraction) {
                fr_unit = std::max(fr_unit, static_cast<double>(track.size)
                    / std::max(1.0, static_cast<double>(track.flex)));
            }
        }
        for (const auto& item : items) {
            const auto [start, end] = range(item);
            const bool crosses_flex = std::any_of(
                axis.tracks.begin() + (start - axis.origin),
                axis.tracks.begin() + (end - axis.origin),
                [](const TrackState& track) {
                    return track.style.maximum.kind() == TrackBreadthKind::Fraction;
                });
            if (crosses_flex) {
                fr_unit = std::max(fr_unit, size_of_fraction_unit(axis,
                    start, end, contribution(item, ContributionKind::MaxContent)));
            }
        }
        double hypothetical = 0;
        for (const auto gap : axis.gutter_after) hypothetical += gap;
        for (const auto& track : axis.tracks) {
            hypothetical += track.style.maximum.kind() == TrackBreadthKind::Fraction
                ? std::max<double>(track.size, fr_unit * track.flex)
                : track.size;
        }
        if (space.minimum && hypothetical < *space.minimum) {
            fr_unit = size_of_fraction_unit(axis, axis.origin,
                axis.origin + static_cast<std::int32_t>(axis.tracks.size()),
                *space.minimum);
        } else if (space.maximum && hypothetical > *space.maximum) {
            fr_unit = size_of_fraction_unit(axis, axis.origin,
                axis.origin + static_cast<std::int32_t>(axis.tracks.size()),
                *space.maximum);
        }
        for (auto& track : axis.tracks) {
            if (track.style.maximum.kind() == TrackBreadthKind::Fraction) {
                track.size = checked(std::max<double>(track.size,
                    fr_unit * track.flex), id);
            }
        }
    }
    const auto stretch_size = space.inner_size ? space.inner_size : space.minimum;
    if (space.alignment.position == ContentPosition::Stretch && stretch_size) {
        const auto count = static_cast<std::size_t>(std::count_if(axis.tracks.begin(), axis.tracks.end(),
            [](const TrackState& track) { return track.auto_max; }));
        if (count) {
            double stretch_free = *stretch_size;
            for (const auto gap : axis.gutter_after) stretch_free -= gap;
            for (const auto& track : axis.tracks) stretch_free -= track.size;
            const Unit share = checked(std::max(0.0, stretch_free) / count, id);
            for (auto& track : axis.tracks) if (track.auto_max) {
                track.size = checked(static_cast<double>(track.size) + share, id);
            }
        }
    }
    axis.offsets.resize(axis.tracks.size() + 1);
    for (std::size_t i = 0; i < axis.tracks.size(); ++i) {
        axis.offsets[i + 1] = checked(static_cast<double>(axis.offsets[i])
            + axis.tracks[i].size + axis.gutter_after[i], id);
    }
}

void align_tracks(TrackAxis& axis, Unit available,
                  ContentAlignment alignment, NodeId id) {
    const Unit used = axis.offsets.empty() ? 0 : axis.offsets.back();
    const Unit free = checked(static_cast<double>(available) - used, id);
    if (alignment.safety == AlignmentSafety::Safe && free < 0) return;
    std::size_t visible = 0;
    for (const auto& track : axis.tracks) if (!track.collapsed) ++visible;
    Unit start = 0, extra_gap = 0;
    switch (alignment.position) {
    case ContentPosition::End:
    case ContentPosition::FlexEnd: start = free; break;
    case ContentPosition::Center: start = checked(static_cast<double>(free) / 2, id); break;
    case ContentPosition::SpaceBetween:
        if (free > 0 && visible > 1) {
            extra_gap = checked(static_cast<double>(free) / (visible - 1), id);
        }
        break;
    case ContentPosition::SpaceAround:
        if (free > 0 && visible) {
            extra_gap = checked(static_cast<double>(free) / visible, id);
            start = checked(static_cast<double>(extra_gap) / 2, id);
        }
        break;
    case ContentPosition::SpaceEvenly:
        if (free > 0 && visible) {
            extra_gap = checked(static_cast<double>(free) / (visible + 1), id);
            start = extra_gap;
        }
        break;
    default: break;
    }
    bool later_visible = false;
    for (std::size_t i = axis.tracks.size(); i-- > 0;) {
        if (!axis.tracks[i].collapsed && later_visible) {
            axis.gutter_after[i] = checked(
                static_cast<double>(axis.gutter_after[i]) + extra_gap, id);
        }
        later_visible |= !axis.tracks[i].collapsed;
    }
    if (axis.offsets.empty()) axis.offsets.resize(1);
    axis.offsets[0] = start;
    for (std::size_t i = 0; i < axis.tracks.size(); ++i) {
        axis.offsets[i + 1] = checked(static_cast<double>(axis.offsets[i])
            + axis.tracks[i].size + axis.gutter_after[i], id);
    }
}

} // namespace axisly::css::detail
