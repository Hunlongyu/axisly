#include "flex_internal.hpp"
#include "balanced_lines.hpp"
#include "item_alignment.hpp"
#include "sizing_keywords.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <utility>

namespace axisly::css::detail {
namespace {
void set_axis_margin(Insets& margin, bool row, bool main,
                     bool logical_start, bool reverse, Unit value) {
    const bool physical_start = logical_start != reverse;
    if (row == main) {
        (physical_start ? margin.left : margin.right) = value;
    } else {
        (physical_start ? margin.top : margin.bottom) = value;
    }
}


struct FlexLine {
    std::vector<std::size_t> items;
    Unit cross{};
    Unit baseline_ascent{}, baseline_descent{};
    bool has_baseline{};
};

double bounded_main_size(const FlexItem& item, double value);
Unit hypothetical_main_size(const FlexItem& item);

std::vector<FlexLine> collect_flex_lines(const std::vector<FlexItem>& items,
    bool row, std::optional<Unit> available, Unit gap, FlexWrap wrap,
    std::uint16_t requested_lines) {
    std::vector<FlexLine> lines(1);
    const bool balanced = wrap == FlexWrap::Balance
        || wrap == FlexWrap::BalanceReverse;
    if (balanced && !items.empty()) {
        std::vector<Unit> outer_sizes;
        outer_sizes.reserve(items.size());
        for (const auto& item : items) {
            const Unit hypothetical = hypothetical_main_size(item);
            outer_sizes.push_back(checked(static_cast<double>(hypothetical)
                + flex_axis(item.margin, row, true, true)
                + flex_axis(item.margin, row, true, false), item.id));
        }
        const auto counts = balanced_line_counts(outer_sizes,
            available.value_or(std::numeric_limits<Unit>::infinity()),
            gap, requested_lines, 25'000'000);
        lines.clear();
        std::size_t next = 0;
        for (const auto count : counts) {
            FlexLine line;
            for (std::size_t i = 0; i < count; ++i) line.items.push_back(next++);
            lines.push_back(std::move(line));
        }
    } else {
        double estimated_main = 0;
        for (std::size_t i = 0; i < items.size(); ++i) {
            const auto& item = items[i];
            const auto outer = static_cast<double>(hypothetical_main_size(item))
                + flex_axis(item.margin, row, true, true)
                + flex_axis(item.margin, row, true, false);
            if (wrap != FlexWrap::NoWrap && available
                && !lines.back().items.empty()
                && estimated_main + gap + outer > *available) {
                lines.emplace_back();
                estimated_main = 0;
            }
            if (!lines.back().items.empty()) estimated_main += gap;
            estimated_main += outer;
            lines.back().items.push_back(i);
        }
    }
    return lines;
}

Unit resolved_gap(const Length& value, std::optional<Unit> basis, NodeId node) {
    // Taffy resolves Flex gaps against zero while the container flex_axis is
    // indefinite. This preserves a calc expression's fixed-length term.
    const auto numeric = value.resolve_numeric(basis.value_or(Unit{0}));
    if (!numeric) return 0;
    if (*numeric < 0) throw LayoutFailure(LayoutErrorCode::InvalidStyle, node, "Flex gap must be nonnegative");
    return *numeric;
}


struct Distribution {
    Unit offset{}, extra_gap{};
};

Distribution distribute(Unit free, std::size_t count, ContentAlignment alignment) {
    if (alignment.safety == AlignmentSafety::Safe && free < 0) return {};
    switch (alignment.position) {
    case ContentPosition::End:
    case ContentPosition::FlexEnd: return {free, 0};
    case ContentPosition::Center: return {free / 2, 0};
    case ContentPosition::SpaceBetween:
        return count > 1 && free > 0 ? Distribution{0, free / static_cast<Unit>(count - 1)} : Distribution{};
    case ContentPosition::SpaceAround:
        return count && free > 0
            ? Distribution{free / (2 * static_cast<Unit>(count)), free / static_cast<Unit>(count)}
            : Distribution{};
    case ContentPosition::SpaceEvenly:
        return count && free > 0
            ? Distribution{free / static_cast<Unit>(count + 1), free / static_cast<Unit>(count + 1)}
            : Distribution{};
    case ContentPosition::Start:
    case ContentPosition::FlexStart:
    case ContentPosition::Stretch: return {};
    }
    return {};
}

ContentAlignment alignment_fallback(ContentAlignment alignment,
                                    double free, std::size_t count) {
    if ((alignment.position == ContentPosition::Stretch && free <= 0)
        || (alignment.position == ContentPosition::SpaceBetween
            && (count <= 1 || free <= 0))) {
        alignment.position = ContentPosition::FlexStart;
        alignment.safety = AlignmentSafety::Safe;
    } else if ((alignment.position == ContentPosition::SpaceAround
                || alignment.position == ContentPosition::SpaceEvenly)
               && (count <= 1 || free <= 0)) {
        alignment.position = ContentPosition::Center;
        alignment.safety = AlignmentSafety::Safe;
    }
    if (free <= 0 && alignment.safety == AlignmentSafety::Safe) {
        alignment.position = ContentPosition::Start;
    }
    alignment.safety = AlignmentSafety::Unsafe;
    return alignment;
}

ContentAlignment flex_justify_alignment(ContentAlignment alignment,
                                        double free, std::size_t count,
                                        bool reverse_direction) {
    // Taffy treats justify-content: stretch as flex-start for one item.
    // For several items its zero distribution offset follows writing start
    // even when the flex direction is reversed.
    if (alignment.position == ContentPosition::Stretch && count <= 1 && free > 0) {
        alignment.position = ContentPosition::FlexStart;
    }
    alignment = alignment_fallback(alignment, free, count);
    if (reverse_direction) {
        // Start/End follow the writing direction; FlexStart/FlexEnd follow
        // the main axis. The output placement loop uses flex-start as origin.
        if (alignment.position == ContentPosition::Start) {
            alignment.position = ContentPosition::FlexEnd;
        } else if (alignment.position == ContentPosition::End) {
            alignment.position = ContentPosition::FlexStart;
        } else if (alignment.position == ContentPosition::Stretch) {
            alignment.position = ContentPosition::FlexEnd;
        }
    }
    return alignment;
}

double bounded_main_size(const FlexItem& item, double value) {
    if (item.maximum) value = std::min(value, static_cast<double>(*item.maximum));
    return std::max(value, static_cast<double>(item.minimum));
}

Unit hypothetical_main_size(const FlexItem& item) {
    double value = item.base;
    if (item.hypothetical_maximum) {
        value = std::min(value, static_cast<double>(*item.hypothetical_maximum));
    }
    return checked(std::max(value, static_cast<double>(item.hypothetical_minimum)), item.id);
}

Unit intrinsic_content_contribution(const FlexItem& item, bool row) {
    const double margin = static_cast<double>(flex_axis(item.margin, row, true, true))
        + flex_axis(item.margin, row, true, false);
    const auto preferred = flex_axis(item.box.preferred, row, true);
    const double clamping_basis = preferred
        ? std::max<double>(item.base, *preferred) : item.base;
    const double minimum = std::max<double>(item.minimum,
        item.shrink == 0 ? clamping_basis : item.minimum);
    const double maximum = std::min<double>(item.maximum.value_or(
        std::numeric_limits<Unit>::infinity()),
        item.grow == 0 ? clamping_basis : std::numeric_limits<Unit>::infinity());
    if (maximum <= minimum) return checked(minimum + margin, item.id);
    if (preferred && maximum <= *preferred) {
        return checked(std::max(minimum, maximum) + margin, item.id);
    }
    if (item.scroll_container) return checked(static_cast<double>(item.base) + margin, item.id);
    double content = preferred.value_or(item.content_main);
    content += margin;
    if (!row) content = std::max(content, static_cast<double>(item.base));
    if (const auto minimum_style = flex_axis(item.box.specified_minimum, row, true)) {
        content = std::max(content, static_cast<double>(*minimum_style));
    }
    if (item.maximum) content = std::min(content,
        static_cast<double>(*item.maximum));
    return checked(content, item.id);
}

Unit intrinsic_item_main_size(const FlexItem& item, bool row) {
    const Unit content = intrinsic_content_contribution(item, row);
    // Intrinsic flex fractions cannot reduce a base whose scaled shrink
    // weight is zero. This also handles a zero-sized item with negative
    // margins without dividing by zero or creating an invalid target size.
    if (content < item.base && item.inner_base == 0) return item.base;
    return content;
}

void resolve_flexible_lengths(FlexLine& line, std::vector<FlexItem>& items,
                              Unit main_size, Unit gap, bool row) {
    const double gutters = line.items.empty() ? 0
        : static_cast<double>(line.items.size() - 1) * gap;
    double hypothetical_occupied = gutters;
    for (const auto index : line.items) {
        const auto& item = items[index];
        const double margin = static_cast<double>(flex_axis(item.margin, row, true, true))
            + flex_axis(item.margin, row, true, false);
        hypothetical_occupied += hypothetical_main_size(item) + margin;
    }
    const bool growing = hypothetical_occupied < main_size;
    const bool shrinking = hypothetical_occupied > main_size;
    const bool exactly_sized = !growing && !shrinking;
    std::vector<bool> frozen(line.items.size(), false);
    for (std::size_t slot = 0; slot < line.items.size(); ++slot) {
        const auto index = line.items[slot];
        auto& item = items[index];
        const Unit hypothetical = hypothetical_main_size(item);
        const bool inflexible = exactly_sized || (item.grow == 0 && item.shrink == 0)
            || (growing && item.base > hypothetical)
            || (shrinking && item.base < hypothetical);
        item.target = hypothetical;
        frozen[slot] = inflexible;
    }
    if (exactly_sized) return;
    double initial_occupied = gutters;
    for (std::size_t slot = 0; slot < line.items.size(); ++slot) {
        const auto& item = items[line.items[slot]];
        initial_occupied += (frozen[slot] ? item.target : item.base)
            + flex_axis(item.margin, row, true, true)
            + flex_axis(item.margin, row, true, false);
    }
    const double initial_free = static_cast<double>(main_size) - initial_occupied;
    for (std::size_t pass = 0; pass < line.items.size(); ++pass) {
        double occupied = gutters, raw_factor_sum = 0, scaled_factor_sum = 0;
        for (std::size_t slot = 0; slot < line.items.size(); ++slot) {
            const auto index = line.items[slot];
            const auto& item = items[index];
            occupied += (frozen[slot] ? item.target : item.base)
                + flex_axis(item.margin, row, true, true)
                + flex_axis(item.margin, row, true, false);
            if (!frozen[slot]) {
                raw_factor_sum += growing ? item.grow : item.shrink;
                scaled_factor_sum += growing ? item.grow
                    : static_cast<double>(item.shrink) * item.inner_base;
            }
        }
        if (raw_factor_sum == 0) break;
        double free = static_cast<double>(main_size) - occupied;
        if (raw_factor_sum < 1) {
            // Taffy 0.14 subtracts the line gutters again when scaling a
            // factor sum below one. Preserve that observable layout result.
            const double scaled_initial = initial_free * raw_factor_sum - gutters;
            free = growing ? std::min(free, scaled_initial)
                : std::max(free, scaled_initial);
        }
        double total_violation = 0;
        std::vector<double> violations(line.items.size());
        for (std::size_t slot = 0; slot < line.items.size(); ++slot) {
            if (frozen[slot]) continue;
            const auto index = line.items[slot];
            auto& item = items[index];
            const double factor = growing ? item.grow
                : static_cast<double>(item.shrink) * item.inner_base;
            const double ideal = static_cast<double>(item.base)
                + (scaled_factor_sum > 0 ? free * factor / scaled_factor_sum : 0);
            const double bounded = bounded_main_size(item, ideal);
            item.target = checked(bounded, item.id);
            // Compare in the same precision as the unconstrained target.
            // Casting to Unit first can turn an unbounded item into a false
            // min/max violation and freeze it in the next pass.
            violations[slot] = bounded - ideal;
            total_violation += violations[slot];
        }
        if (std::abs(total_violation) < 0.0001) break;
        for (std::size_t slot = 0; slot < line.items.size(); ++slot) {
            if (!frozen[slot]
                && (total_violation > 0 ? violations[slot] > 0
                                        : violations[slot] < 0)) {
                frozen[slot] = true;
            }
        }
    }
}

} // namespace

Fragment Engine::flex(const LayoutNode& node, const Constraints& constraints,
                      const ResolvedBox& box, std::size_t depth) {
    const bool row = flex_is_row(node.style.flex_direction);
    const bool reverse_direction = node.style.flex_direction == FlexDirection::RowReverse
        || node.style.flex_direction == FlexDirection::ColumnReverse;
    const bool main_reverse = row
        ? (node.style.flex_direction == FlexDirection::RowReverse)
            != (node.style.box.direction == Direction::Rtl)
        : node.style.flex_direction == FlexDirection::ColumnReverse;
    const bool cross_reverse = (node.style.flex_wrap == FlexWrap::WrapReverse
        || node.style.flex_wrap == FlexWrap::BalanceReverse)
        != (!row && node.style.box.direction == Direction::Rtl);
    const auto deco_width = box.horizontal_decoration();
    const auto deco_height = box.vertical_decoration();
    if (!deco_width || !deco_height) {
        throw LayoutFailure(LayoutErrorCode::UnresolvedPercentage, node.id,
            "Flex decoration requires a definite containing inline size");
    }
    const auto requested = requested_size(constraints, box);
    auto border_width = requested.width;
    auto border_height = requested.height;
    if (border_width) {
        *border_width = constrain_axis(*border_width,
            box.minimum.width, box.maximum.width);
    }
    if (border_height) {
        *border_height = constrain_axis(*border_height,
            box.minimum.height, box.maximum.height);
    }
    auto inner_width = border_width
        ? std::optional<Unit>(std::max(Unit{0}, *border_width - *deco_width)) : std::nullopt;
    auto inner_height = border_height
        ? std::optional<Unit>(std::max(Unit{0}, *border_height - *deco_height)) : std::nullopt;
    // A content-sized Flex container can acquire a final size without making
    // its children's percentage basis definite in the intrinsic pass.
    const auto child_percentage_width = inner_width;
    const auto child_percentage_height = inner_height;
    const auto main_available = row ? inner_width : inner_height;
    const auto cross_available = row ? inner_height : inner_width;
    const auto known_main = row ? constraints.known_width : constraints.known_height;
    const bool main_size_is_definite = known_main
        ? (row ? constraints.known_width_is_definite
               : constraints.known_height_is_definite)
        : flex_axis(box.preferred, row, true).has_value();
    const auto outer_main_space = row ? constraints.available.width
        : constraints.available.height;
    const auto main_query = outer_main_space.kind;
    const bool ignore_main_bounds = row ? constraints.ignore_min_max_width
        : constraints.ignore_min_max_height;
    const auto line_maximum = flex_axis(ignore_main_bounds ? box.specified_maximum
        : box.maximum, row, true);
    const auto line_minimum = flex_axis(ignore_main_bounds ? box.specified_minimum
        : box.minimum, row, true);
    // Line collection uses the parent's remaining space even when the
    // container will derive its own main size from those lines.
    std::optional<Unit> line_available = main_available;
    if (!line_available && main_query == AvailableKind::Definite) {
        const auto margin = edges(box.margin, node.id);
        const double outer_margin = row
            ? static_cast<double>(margin.left) + margin.right
            : static_cast<double>(margin.top) + margin.bottom;
        const double decoration = row ? *deco_width : *deco_height;
        line_available = checked(std::max(0.0,
            static_cast<double>(outer_main_space.value) - outer_margin - decoration), node.id);
    }
    if (!main_available) {
        // A max-size limits collection before the content-sized container
        // computes its used size. A parent flex-basis query ignores this
        // container's own final min/max clamp, but its children still collect
        // lines against the declared bounds.
        if (line_maximum) {
            line_available = std::min(line_available.value_or(*line_maximum), *line_maximum);
            if (line_minimum) {
                line_available = std::max(*line_available, *line_minimum);
            }
        }
    }
    Unit main_gap = resolved_gap(row ? node.style.gap.width : node.style.gap.height,
        main_available, node.id);
    const Unit cross_gap = resolved_gap(row ? node.style.gap.height : node.style.gap.width,
        cross_available, node.id);

    std::vector<Fragment> fragments(node.children.size());
    std::vector<FlexItem> items;
    items.reserve(node.children.size());
    for (std::size_t source = 0; source < node.children.size(); ++source) {
        const auto id = node.children[source];
        const auto& child = get(id);
        if (child.style.display == Display::None) {
            fragments[source] = hidden(child, depth + 1);
            continue;
        }
        if (child.style.position == Position::Absolute) {
            continue;
        }
        items.push_back(prepare_flex_item(node, child, constraints,
            {source, {inner_width, inner_height}, main_query,
                main_size_is_definite, main_reverse, cross_reverse}, depth));
    }

    std::vector<FlexLine> lines;
    const bool main_size_derived = known_main && !main_size_is_definite;
    if (!main_size_derived && !main_available && main_query == AvailableKind::MinContent
        && !line_available && node.style.flex_wrap != FlexWrap::NoWrap
        && !items.empty()) {
        lines.reserve(items.size());
        for (std::size_t index = 0; index < items.size(); ++index) {
            lines.push_back(FlexLine{{index}});
        }
    } else {
        lines = collect_flex_lines(items, row,
            main_size_derived ? std::nullopt : line_available, main_gap,
            node.style.flex_wrap, node.style.flex_line_count);
    }
    if (!main_available) {
        const bool intrinsic_contribution = main_query == AvailableKind::MaxContent
            || (main_query == AvailableKind::MinContent
                && node.style.flex_wrap == FlexWrap::NoWrap);
        std::vector<Unit> outer_sizes;
        outer_sizes.reserve(items.size());
        for (const auto& item : items) {
            if (intrinsic_contribution) {
                outer_sizes.push_back(intrinsic_item_main_size(item, row));
            } else {
                const Unit contribution = std::max(item.base,
                    flex_axis(item.box.specified_minimum, row, true).value_or(Unit{0}));
                const double outer = static_cast<double>(contribution)
                    + flex_axis(item.margin, row, true, true)
                    + flex_axis(item.margin, row, true, false);
                outer_sizes.push_back(checked(std::max(outer,
                    static_cast<double>(item.main_decoration)), item.id));
            }
        }
        const auto widest_line = [&](const std::vector<std::size_t>& counts) {
            std::size_t next = 0;
            double widest = 0;
            for (const auto count : counts) {
                double length = count ? static_cast<double>(count - 1) * main_gap : 0;
                for (std::size_t i = 0; i < count; ++i) length += outer_sizes[next++];
                widest = std::max(widest, length);
            }
            return checked(widest, node.id);
        };
        std::vector<std::size_t> line_counts;
        line_counts.reserve(lines.size());
        for (const auto& line : lines) line_counts.push_back(line.items.size());
        const Unit inner = widest_line(line_counts);
        const Unit decoration = row ? *deco_width : *deco_height;
        Unit candidate_outer = checked(static_cast<double>(inner) + decoration, node.id);
        const bool balanced = node.style.flex_wrap == FlexWrap::Balance
            || node.style.flex_wrap == FlexWrap::BalanceReverse;
        if (main_query == AvailableKind::Definite
            && line_available && balanced && !outer_sizes.empty()) {
            const auto unconstrained_counts = balanced_line_counts(outer_sizes,
                std::numeric_limits<Unit>::infinity(), main_gap,
                node.style.flex_line_count, 25'000'000);
            const Unit max_content = checked(static_cast<double>(
                widest_line(unconstrained_counts)) + decoration, node.id);
            candidate_outer = std::max(candidate_outer,
                std::min(max_content, *line_available));
        } else if (main_query == AvailableKind::Definite
            && line_available && lines.size() > 1) {
            candidate_outer = std::max(candidate_outer, *line_available);
        }
        if (row) {
            border_width = constrain_axis(candidate_outer,
                box.minimum.width, box.maximum.width);
            inner_width = std::max(Unit{0}, *border_width - *deco_width);
        } else {
            border_height = constrain_axis(candidate_outer,
                box.minimum.height, box.maximum.height);
            inner_height = std::max(Unit{0}, *border_height - *deco_height);
        }
    }
    // Measurement space is nonnegative. Alignment and flexible lengths use
    // the physical space, which can be negative when the gutter is larger
    // than the specified border box.
    const Unit main_size = row ? *border_width - *deco_width
        : *border_height - *deco_height;
    if (!main_available) {
        main_gap = resolved_gap(row ? node.style.gap.width : node.style.gap.height,
            std::max(Unit{0}, main_size), node.id);
    }
    for (auto& line : lines) {
        resolve_flexible_lengths(line, items, main_size, main_gap, row);
        for (const auto index : line.items) {
            auto& item = items[index];
            Constraints final_constraints;
            final_constraints.sizing_mode = SizingMode::Content;
            final_constraints.content_based_width = item.content_based_cross_width;
            if (row) final_constraints.known_width_is_definite = item.main_size_is_definite;
            else final_constraints.known_height_is_definite = item.main_size_is_definite;
            if (row) final_constraints.known_height_is_definite = item.cross_size_is_definite;
            else final_constraints.known_width_is_definite = item.cross_size_is_definite;
            final_constraints.containing_width = child_percentage_width;
            final_constraints.containing_height = child_percentage_height;
            final_constraints.available.width = inner_width
                ? AvailableSpace::definite(*inner_width) : constraints.available.width;
            final_constraints.available.height = inner_height
                ? AvailableSpace::definite(*inner_height) : constraints.available.height;
            auto& final_cross_space = row ? final_constraints.available.height
                : final_constraints.available.width;
            final_cross_space = divided_flex_cross_space(node, final_cross_space, cross_available);
            const Unit cross_outer_margin = checked(static_cast<double>(
                flex_axis(item.margin, row, false, true))
                + flex_axis(item.margin, row, false, false), item.id);
            const auto cross_maximum = transfer_aspect_ratio(
                item.box.maximum, item.box);
            const auto cross_min = flex_axis(item.box.minimum, row, false);
            const auto cross_max = flex_axis(cross_maximum, row, false);
            final_cross_space = cross_space_for_item(final_cross_space,
                cross_min, cross_max, cross_outer_margin,
                CrossSpaceStage::Hypothetical, item.id);
            const auto& cross_style = row ? get(item.id).style.box.size.height
                : get(item.id).style.box.size.width;
            if (const auto keyword = sizing_keyword(cross_style,
                    cross_available ? std::optional<Unit>{*cross_available - cross_outer_margin} : std::nullopt,
                    cross_available); keyword && std::holds_alternative<AvailableSpace>(*keyword)) {
                final_cross_space = std::get<AvailableSpace>(*keyword);
            }
            if (row) final_constraints.known_width = item.target;
            else final_constraints.known_height = item.target;
            // Resolve the hypothetical cross size using the post-flex main
            // size, then perform the item layout with both axes fixed. A
            // nested container may lay out its own children differently in
            // that final pass even when its border-box size stays the same.
            const auto cross_preferred = flex_axis(item.box.preferred, row, false);
            Unit cross_measured = cross_preferred.value_or(Unit{0});
            if (!cross_preferred) {
                const auto cross_probe = layout_size(item.id, final_constraints, depth + 1);
                cross_measured = flex_axis(cross_probe.size, row, false);
            }
            const auto decoration = row ? item.box.vertical_padding_border()
                : item.box.horizontal_padding_border();
            const Unit cross_size = std::max(constrain_axis(cross_measured,
                cross_min, cross_max), decoration.value_or(Unit{0}));
            if (row) final_constraints.known_height = cross_size;
            else final_constraints.known_width = cross_size;
            item.final_constraints = final_constraints;
            fragments[item.source_index] = detail_ == FragmentDetail::Size
                ? layout_sizing(item.id, final_constraints, depth + 1)
                : layout(item.id, final_constraints, depth + 1);
            item.target = flex_axis(fragments[item.source_index].size, row, true);
            item.cross = flex_axis(fragments[item.source_index].size, row, false);
            const Unit outer_cross = checked(static_cast<double>(item.cross)
                + flex_axis(item.margin, row, false, true)
                + flex_axis(item.margin, row, false, false), item.id);
            line.cross = std::max(line.cross, outer_cross);
            if (row) {
                const auto& child = get(item.id);
                const auto alignment = child.style.align_self.value_or(
                    node.style.align_items.value_or(ItemAlignment{}));
                if (alignment.position == ItemPosition::Baseline
                    && !item.auto_cross_start && !item.auto_cross_end) {
                    const Unit baseline = child_baseline(
                        fragments[item.source_index], child.style).value_or(item.cross);
                    const Unit before = checked(static_cast<double>(item.margin.top)
                        + baseline, item.id);
                    const Unit after = checked(static_cast<double>(item.margin.bottom)
                        + item.cross - baseline, item.id);
                    line.baseline_ascent = std::max(line.baseline_ascent, before);
                    line.baseline_descent = std::max(line.baseline_descent, after);
                    line.has_baseline = true;
                }
            }
        }
        if (line.has_baseline) {
            line.cross = std::max(line.cross, checked(
                static_cast<double>(line.baseline_ascent) + line.baseline_descent,
                node.id));
        }
    }

    double used_cross = lines.empty() ? 0 : static_cast<double>(lines.size() - 1) * cross_gap;
    for (const auto& line : lines) used_cross += line.cross;
    if (row && !border_height) {
        border_height = constrain_axis(checked(used_cross + *deco_height,
            node.id), box.minimum.height, box.maximum.height);
        inner_height = std::max(Unit{0}, *border_height - *deco_height);
    }
    if (!row && !border_width) {
        border_width = constrain_axis(checked(used_cross + *deco_width,
            node.id), box.minimum.width, box.maximum.width);
        inner_width = std::max(Unit{0}, *border_width - *deco_width);
    }
    if (detail_ == FragmentDetail::Size) {
        Fragment result;
        result.size = constrain_size(Size{*border_width, *border_height}, box);
        result.nodes.push_back(make_layout(node, result.size, box));
        return result;
    }
    if (!cross_available) {
        // The cross probe precedes the container's own content-based cross
        // size. Perform the final child layout with that size available so
        // nested trees can remeasure after the container is resolved.
        const Unit outer_cross = row ? *border_height : *border_width;
        for (const auto& line : lines) {
            for (const auto index : line.items) {
                const auto& item = items[index];
                auto final = item.final_constraints;
                if (row) final.available.height = AvailableSpace::definite(outer_cross);
                else final.available.width = AvailableSpace::definite(outer_cross);
                fragments[item.source_index] = layout(item.id, final, depth + 1);
            }
        }
    }
    const Unit cross_size = row ? *border_height - *deco_height
        : *border_width - *deco_width;
    const bool wraps = node.style.flex_wrap != FlexWrap::NoWrap;
    // A wrapped container can have one line. Its cross size still comes from
    // the items; align-content then distributes the remaining space.
    if (!wraps && lines.size() == 1) lines.front().cross = cross_size;
    double cross_free = wraps ? static_cast<double>(cross_size) - used_cross : 0;
    auto align_lines = alignment_fallback(
        node.style.align_content.value_or(ContentAlignment{}),
        cross_free, lines.size());
    const bool wrap_reverse = node.style.flex_wrap == FlexWrap::WrapReverse
        || node.style.flex_wrap == FlexWrap::BalanceReverse;
    // Start/End follow the cross flex_axis of the writing mode. FlexStart/FlexEnd
    // follow the line stacking direction, which wrap-reverse flips.
    if (wrap_reverse) {
        if (align_lines.position == ContentPosition::Start) {
            align_lines.position = ContentPosition::End;
        } else if (align_lines.position == ContentPosition::End) {
            align_lines.position = ContentPosition::Start;
        }
    }
    if (align_lines.position == ContentPosition::Stretch && cross_free > 0) {
        for (auto& line : lines) line.cross = checked(
            static_cast<double>(line.cross) + cross_free / lines.size(), node.id);
        cross_free = 0;
    }
    const auto cross_distribution = distribute(checked(cross_free, node.id), lines.size(), align_lines);
    double cross_cursor = cross_distribution.offset;
    const auto justify = node.style.justify_content.value_or(
        ContentAlignment{ContentPosition::FlexStart, AlignmentSafety::Unsafe});
    for (const auto& line : lines) {
        double main_occupied = line.items.empty() ? 0 : static_cast<double>(line.items.size() - 1) * main_gap;
        std::size_t auto_margins = 0;
        for (const auto index : line.items) {
            const auto& item = items[index];
            main_occupied += item.target + flex_axis(item.margin, row, true, true)
                + flex_axis(item.margin, row, true, false);
            auto_margins += static_cast<std::size_t>(item.auto_main_start)
                + static_cast<std::size_t>(item.auto_main_end);
        }
        const double remaining = static_cast<double>(main_size) - main_occupied;
        const Unit auto_share = auto_margins && remaining > 0
            ? checked(remaining / auto_margins, node.id) : 0;
        const auto distribution = auto_share == 0
            ? distribute(checked(remaining, node.id), line.items.size(),
                flex_justify_alignment(justify, remaining, line.items.size(), reverse_direction))
            : Distribution{};
        double main_cursor = distribution.offset;
        for (const auto index : line.items) {
            auto& item = items[index];
            auto& fragment = fragments[item.source_index];
            const auto& child_style = get(item.id).style;
            const auto alignment = child_style.align_self.value_or(node.style.align_items.value_or(
                ItemAlignment{ItemPosition::Stretch, AlignmentSafety::Unsafe}));
            const auto cross_kind = (row ? child_style.box.size.height : child_style.box.size.width)
                .kind();
            Unit cross_margin_start = flex_axis(item.margin, row, false, !cross_reverse);
            Unit cross_margin_end = flex_axis(item.margin, row, false, cross_reverse);
            if ((cross_kind == LengthKind::Stretch
                || (alignment.position == ItemPosition::Stretch && cross_kind == LengthKind::Auto))
                && !item.auto_cross_start && !item.auto_cross_end) {
                const Unit stretched_cross = std::max(Unit{0}, checked(
                    static_cast<double>(line.cross) - cross_margin_start - cross_margin_end,
                    item.id));
                // Used stretch bounds do not transfer through the ratio.
                // Ratio-transferred bounds belong to the earlier hypothetical
                // cross-size stage, before the line's used cross size exists.
                const Unit target_cross = constrain_axis(stretched_cross,
                    flex_axis(item.box.specified_minimum, row, false),
                    flex_axis(item.box.specified_maximum, row, false));
                Constraints stretched = item.final_constraints;
                stretched.containing_width = child_percentage_width;
                stretched.containing_height = child_percentage_height;
                stretched.available.width = inner_width
                    ? AvailableSpace::definite(*inner_width) : AvailableSpace::indefinite();
                stretched.available.height = inner_height
                    ? AvailableSpace::definite(*inner_height) : AvailableSpace::indefinite();
                if (row) { stretched.known_width = item.target; stretched.known_height = target_cross; }
                else { stretched.known_height = item.target; stretched.known_width = target_cross; }
                fragment = layout(item.id, stretched, depth + 1);
                item.cross = flex_axis(fragment.size, row, false);
            }
            double cross_remain = static_cast<double>(line.cross) - item.cross
                - cross_margin_start - cross_margin_end;
            if (item.auto_cross_start || item.auto_cross_end) {
                const Unit share = checked(cross_remain
                    / (static_cast<int>(item.auto_cross_start) + static_cast<int>(item.auto_cross_end)), item.id);
                if (item.auto_cross_start) cross_margin_start = share;
                if (item.auto_cross_end) cross_margin_end = share;
                cross_remain = 0;
            }
            Unit cross_offset = cross_margin_start;
            if (row && line.has_baseline
                && alignment.position == ItemPosition::Baseline
                && !item.auto_cross_start && !item.auto_cross_end) {
                const Unit baseline = child_baseline(fragment, child_style)
                    .value_or(item.cross);
                cross_offset = cross_reverse
                    ? checked(static_cast<double>(line.baseline_descent)
                        + baseline - item.cross, item.id)
                    : checked(static_cast<double>(line.baseline_ascent)
                        - baseline, item.id);
            }
            if (!(item.auto_cross_start || item.auto_cross_end)
                && !(row && line.has_baseline
                    && alignment.position == ItemPosition::Baseline)
                && !(alignment.safety == AlignmentSafety::Safe && cross_remain < 0)) {
                if (!row && alignment.position == ItemPosition::Baseline) {
                    // With horizontal writing, a column item's baseline is
                    // placed as a cross-axis start edge. Taffy chooses the
                    // physical edge from wrapping and RTL independently;
                    // converting it after line reversal preserves that edge.
                    const bool physical_end = wrap_reverse
                        != (node.style.box.direction == Direction::Rtl && !wraps);
                    if (cross_reverse != physical_end) {
                        cross_offset = checked(cross_margin_start + cross_remain, item.id);
                    }
                } else {
                    const auto edge = item_edge(alignment.position, !row,
                        node.style.box.direction, child_style.box.direction,
                        cross_reverse, cross_reverse);
                    if (edge == ItemEdge::End) {
                        cross_offset = checked(cross_margin_start + cross_remain, item.id);
                    } else if (edge == ItemEdge::Center) {
                        cross_offset = checked(cross_margin_start + cross_remain / 2, item.id);
                    }
                }
            }
            const Unit main_start_margin = flex_axis(item.margin, row, true, !main_reverse)
                + (item.auto_main_start ? auto_share : 0);
            const Unit main_end_margin = flex_axis(item.margin, row, true, main_reverse)
                + (item.auto_main_end ? auto_share : 0);
            auto& resolved_margin = fragment.nodes.front().margin;
            set_axis_margin(resolved_margin, row, true, true, main_reverse,
                main_start_margin);
            set_axis_margin(resolved_margin, row, true, false, main_reverse,
                main_end_margin);
            set_axis_margin(resolved_margin, row, false, true, cross_reverse,
                cross_margin_start);
            set_axis_margin(resolved_margin, row, false, false, cross_reverse,
                cross_margin_end);
            const double logical_main = main_cursor + main_start_margin;
            const double logical_cross = cross_cursor + cross_offset;
            const Unit main_position = main_reverse
                ? checked(static_cast<double>(main_size) - logical_main - item.target, item.id)
                : checked(logical_main, item.id);
            const Unit cross_position = cross_reverse
                ? checked(static_cast<double>(cross_size) - logical_cross - item.cross, item.id)
                : checked(logical_cross, item.id);
            const auto border = edges(box.border, node.id);
            const auto padding = edges(box.padding, node.id);
            const Unit content_x = content_origin_x(box, border, padding, node.id);
            const Unit content_y = checked(static_cast<double>(border.top) + padding.top, node.id);
            translate(fragment,
                row ? checked(static_cast<double>(content_x) + main_position, item.id)
                    : checked(static_cast<double>(content_x) + cross_position, item.id),
                row ? checked(static_cast<double>(content_y) + cross_position, item.id)
                    : checked(static_cast<double>(content_y) + main_position, item.id), item.id);
            const auto [relative_x, relative_y] = relative_offset(get(item.id),
                Size{*inner_width, *inner_height}, node.style.box.direction, node.id);
            translate(fragment, relative_x, relative_y, item.id);
            main_cursor += main_start_margin + item.target + main_end_margin + main_gap
                + distribution.extra_gap;
        }
        cross_cursor += line.cross + cross_gap + cross_distribution.extra_gap;
    }
    Size candidate{*border_width, *border_height};
    candidate = constrain_size(candidate, box);
    Fragment result;
    result.size = candidate;
    result.nodes.push_back(make_layout(node, candidate, box));
    if (!lines.empty()) {
        const auto& first_line = wrap_reverse ? lines.back() : lines.front();
        if (!first_line.items.empty()) {
            std::size_t selected = first_line.items.front();
            if (row) {
                for (const auto index : first_line.items) {
                    const auto& item = items[index];
                    const auto& child_style = get(item.id).style;
                    const auto alignment = child_style.align_self.value_or(
                        node.style.align_items.value_or(ItemAlignment{}));
                    if (alignment.position == ItemPosition::Baseline
                        && !item.auto_cross_start && !item.auto_cross_end) {
                        selected = index;
                        break;
                    }
                }
            } else if (node.style.flex_direction == FlexDirection::ColumnReverse) {
                selected = first_line.items.back();
            }
            const auto& item = items[selected];
            const auto& child_style = get(item.id).style;
            const auto& fragment = fragments[item.source_index];
            const Unit baseline = child_baseline(fragment, child_style)
                .value_or(fragment.size.height);
            result.nodes.front().first_baseline = checked(
                static_cast<double>(fragment.nodes.front().border_box.y)
                    + baseline, node.id);
        }
    }
    for (std::size_t source = 0; source < fragments.size(); ++source) {
        const auto id = node.children[source];
        if (get(id).style.position == Position::Absolute
            && get(id).style.display != Display::None) {
            fragments[source] = absolute(node, id, candidate, box, depth);
        }
        auto& fragment = fragments[source];
        if (detail_ == FragmentDetail::Full) {
            result.nodes.insert(result.nodes.end(),
                std::make_move_iterator(fragment.nodes.begin()),
                std::make_move_iterator(fragment.nodes.end()));
            result.grid_tracks.insert(result.grid_tracks.end(),
                std::make_move_iterator(fragment.grid_tracks.begin()),
                std::make_move_iterator(fragment.grid_tracks.end()));
        }
    }
    return result;
}

} // namespace axisly::css::detail
