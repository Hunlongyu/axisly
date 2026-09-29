#include "grid_internal.hpp"

#include <algorithm>
#include <utility>

namespace axisly::css::detail {
TrackAxis make_axis(const std::vector<TrackSizing>& explicit_tracks,
                    const std::vector<TrackSizing>& auto_tracks,
                    std::int32_t minimum_line, std::int32_t maximum_line,
                    std::optional<Unit> available, Unit gap, NodeId id) {
    TrackAxis axis;
    axis.origin = std::min<std::int32_t>(0, minimum_line);
    axis.gap = gap;
    const auto finish = std::max<std::int32_t>(static_cast<std::int32_t>(explicit_tracks.size()), maximum_line);
    if (finish - axis.origin > 10'000) {
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
            || (min_kind == TrackBreadthKind::Percent && !available);
        state.intrinsic_max = max_kind == TrackBreadthKind::Auto
            || max_kind == TrackBreadthKind::MinContent || max_kind == TrackBreadthKind::MaxContent
            || max_kind == TrackBreadthKind::FitContentLength
            || max_kind == TrackBreadthKind::FitContentPercent
            || (max_kind == TrackBreadthKind::Percent && !available);
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
                       const std::vector<Placement>& placements, bool columns) {
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

double grow_nonflexible_tracks(TrackAxis& axis, std::optional<Unit> available,
                               AvailableKind query, double free, NodeId id) {
    if (!available) {
        if (query == AvailableKind::MinContent) return free;
        for (auto& track : axis.tracks) {
            if (track.intrinsic_max) track.size = track.preferred;
        }
        return free;
    }
    if (free <= 0) return free;
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
    if (capacities.empty()) return free;
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
    double distributed = 0;
    for (auto& track : axis.tracks) {
        const auto limit = growth_limit(track);
        if (!limit || *limit <= track.size) continue;
        const Unit previous = track.size;
        const double capacity = static_cast<double>(*limit) - previous;
        track.size = checked(static_cast<double>(previous)
            + std::min(capacity, level), id);
        distributed += static_cast<double>(track.size) - previous;
    }
    return std::max(0.0, free - distributed);
}

Unit grid_item_minimum(const LayoutNode& child, const ResolvedBox& box,
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
    return spans_auto_minimum && (end - start == 1 || !spans_flexible_track)
        ? min_content : Unit{0};
}

void apply_cross_axis_ratio_contributions(const TrackAxis& rows,
    std::vector<ItemContribution>& items, NodeId id) {
    for (auto& item : items) {
        if (!item.stretched_ratio_box) continue;
        const auto& box = *item.stretched_ratio_box;
        const Unit row_span = rows.span(item.placement.row_start,
            item.placement.row_end);
        const Unit available_height = std::max(Unit{0}, checked(
            static_cast<double>(row_span) - item.margin.top
                - item.margin.bottom, id));
        const auto derived = transfer_aspect_ratio(
            {std::nullopt, available_height}, box).width;
        if (!derived) continue;
        const Unit width = constrain_axis(*derived,
            box.minimum.width, box.maximum.width);
        for (auto* value : {&item.minimum_contribution.width,
            &item.min_content.width, &item.max_content.width}) {
            *value = std::max(constrain_axis(*value,
                box.minimum.width, box.maximum.width), width);
        }
    }
}

void size_axis(TrackAxis& axis, const std::vector<ItemContribution>& items,
               bool columns, std::optional<Unit> available, AvailableKind query,
               NodeId id) {
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
    const bool intrinsic_query = !available
        && (query == AvailableKind::MinContent || query == AvailableKind::MaxContent);
    struct SpanningItem {
        const ItemContribution* item;
        std::int32_t start, span;
        bool crosses_flexible_track;
    };
    std::vector<SpanningItem> spanning_items;
    spanning_items.reserve(items.size());
    for (const auto& item : items) {
        const auto [start, end] = range(item);
        if (end - start <= 1) continue;
        bool crosses_flexible_track = false;
        for (auto index = start; index < end; ++index) {
            if (axis.tracks[static_cast<std::size_t>(index - axis.origin)]
                    .style.maximum.kind() == TrackBreadthKind::Fraction) {
                crosses_flexible_track = true;
                break;
            }
        }
        spanning_items.push_back({&item, start, end - start,
            crosses_flexible_track});
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
        const auto preferred = contribution(item, query == AvailableKind::MinContent
            ? ContributionKind::MinContent : ContributionKind::MaxContent);
        if (track.intrinsic_min) {
            const auto kind = track.style.minimum.kind();
            track.size = std::max(track.size,
                kind == TrackBreadthKind::MaxContent
                    ? contribution(item, ContributionKind::MaxContent)
                    : kind == TrackBreadthKind::Auto
                        ? contribution(item, ContributionKind::Minimum) : minimum);
        }
        if (track.intrinsic_max) {
            const auto kind = track.style.maximum.kind();
            track.preferred = std::max(track.preferred,
                kind == TrackBreadthKind::MinContent ? minimum : preferred);
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
                const auto& item = *spanning.item;
                const auto [start, end] = range(item);
                double occupied = 0;
                double weight_sum = 0;
                const bool weighted = kind == ContributionKind::Minimum
                    && intrinsic_query && spanning.crosses_flexible_track
                    && !(columns ? item.scrolls_x : item.scrolls_y);
                for (auto index = start; index < end; ++index) {
                    const auto slot = static_cast<std::size_t>(index - axis.origin);
                    const auto& track = axis.tracks[slot];
                    occupied += track.size;
                    if (index + 1 < end) occupied += axis.gutter_after[slot];
                    if (eligible(track)) {
                        weight_sum += weighted ? track.flex : 1.0;
                    }
                }
                const double target = weighted
                    ? scaled_flexible_contribution(spanning,
                        ContributionKind::MinContent)
                    : contribution(item, kind);
                const double deficit = target - occupied;
                if (deficit <= 0 || weight_sum <= 0) continue;
                const double unit = deficit / weight_sum;
                for (auto index = start; index < end; ++index) {
                    const auto slot = static_cast<std::size_t>(index - axis.origin);
                    const auto& track = axis.tracks[slot];
                    if (!eligible(track)) continue;
                    const Unit increase = checked(unit
                        * (weighted ? track.flex : 1.0), id);
                    if (increase <= 0) continue;
                    if (planned[slot] == 0) touched.push_back(slot);
                    planned[slot] = std::max(planned[slot], increase);
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
        [](const TrackState& track) { return track.intrinsic_min; });
    grow_spanning_bases(ContributionKind::MinContent,
        [](const TrackState& track) {
            const auto kind = track.style.minimum.kind();
            return kind == TrackBreadthKind::MinContent
                || kind == TrackBreadthKind::MaxContent;
        });
    grow_spanning_bases(ContributionKind::MaxContent,
        [](const TrackState& track) {
            return track.style.minimum.kind() == TrackBreadthKind::MaxContent;
        });
    for (const auto& spanning_item : spanning_items) {
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
            preferred_occupied += std::max(track.size, track.preferred);
            if (track.intrinsic_max || track.flex > 0) {
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
                if (track.intrinsic_max || track.flex > 0) {
                    track.preferred = checked(std::max(track.size, track.preferred)
                        + unit * (weighted ? track.flex : 1.0), id);
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
    double free = available ? static_cast<double>(*available) - occupied : 0;
    free = grow_nonflexible_tracks(axis, available, query, free, id);
    if (available && free > 0) {
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
            const double unit = (static_cast<double>(*available) - nonflex)
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
        free = static_cast<double>(*available);
        for (const auto gap : axis.gutter_after) free -= gap;
        for (const auto& track : axis.tracks) free -= track.size;
    } else if (!available) {
        double fr_unit = 0;
        for (const auto& track : axis.tracks) {
            if (track.flex > 0) {
                fr_unit = std::max(fr_unit,
                    static_cast<double>(track.preferred) / track.flex);
            }
        }
        for (auto& track : axis.tracks) {
            if (track.flex > 0) {
                track.size = checked(std::max<double>(track.size,
                    fr_unit * track.flex), id);
            }
        }
    }
    if (available && free > 0) {
        const auto count = static_cast<std::size_t>(std::count_if(axis.tracks.begin(), axis.tracks.end(),
            [](const TrackState& track) { return track.auto_max; }));
        if (count) {
            const Unit share = checked(free / count, id);
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
