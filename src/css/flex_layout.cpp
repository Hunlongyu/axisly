#include "layout_engine.hpp"
#include "balanced_lines.hpp"
#include "item_alignment.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <utility>

namespace axisly::css::detail {
namespace {
bool is_row(FlexDirection direction) {
    return direction == FlexDirection::Row || direction == FlexDirection::RowReverse;
}

Unit axis(Size size, bool row, bool main) {
    return (row == main) ? size.width : size.height;
}

Unit axis(const Insets& value, bool row, bool main, bool start) {
    if (row == main) return start ? value.left : value.right;
    return start ? value.top : value.bottom;
}

void set_axis_margin(Insets& margin, bool row, bool main,
                     bool logical_start, bool reverse, Unit value) {
    const bool physical_start = logical_start != reverse;
    if (row == main) {
        (physical_start ? margin.left : margin.right) = value;
    } else {
        (physical_start ? margin.top : margin.bottom) = value;
    }
}

std::optional<Unit> axis(const ResolvedDimensions& size, bool row, bool main) {
    return row == main ? size.width : size.height;
}

struct FlexItem {
    NodeId id;
    std::size_t source_index{};
    ResolvedBox box;
    Insets margin;
    Unit base{};
    Unit target{};
    Unit minimum{};
    std::optional<Unit> maximum;
    Unit grow{}, shrink{};
    Unit cross{};
    bool auto_main_start{}, auto_main_end{};
    bool auto_cross_start{}, auto_cross_end{};
};

struct FlexLine {
    std::vector<std::size_t> items;
    Unit cross{};
    Unit baseline_ascent{}, baseline_descent{};
    bool has_baseline{};
};

Unit clamped_main_size(const FlexItem& item, double value);

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
            const Unit hypothetical = clamped_main_size(item, item.base);
            outer_sizes.push_back(checked(static_cast<double>(hypothetical)
                + axis(item.margin, row, true, true)
                + axis(item.margin, row, true, false), item.id));
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
            const auto outer = static_cast<double>(clamped_main_size(item, item.base))
                + axis(item.margin, row, true, true)
                + axis(item.margin, row, true, false);
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
    // Taffy resolves Flex gaps against zero while the container axis is
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
    if (count <= 1 || free <= 0) {
        switch (alignment.position) {
        case ContentPosition::Stretch:
        case ContentPosition::SpaceBetween:
            alignment.position = ContentPosition::FlexStart;
            alignment.safety = AlignmentSafety::Safe;
            break;
        case ContentPosition::SpaceAround:
        case ContentPosition::SpaceEvenly:
            alignment.position = ContentPosition::Center;
            alignment.safety = AlignmentSafety::Safe;
            break;
        default: break;
        }
    }
    if (free <= 0 && alignment.safety == AlignmentSafety::Safe) {
        alignment.position = ContentPosition::Start;
    }
    alignment.safety = AlignmentSafety::Unsafe;
    return alignment;
}

Unit clamped_main_size(const FlexItem& item, double value) {
    if (item.maximum) value = std::min(value, static_cast<double>(*item.maximum));
    return checked(std::max(value, static_cast<double>(item.minimum)), item.id);
}

void resolve_flexible_lengths(FlexLine& line, std::vector<FlexItem>& items,
                              Unit main_size, Unit gap, bool row) {
    const double gutters = line.items.empty() ? 0
        : static_cast<double>(line.items.size() - 1) * gap;
    double base_occupied = gutters, hypothetical_occupied = gutters;
    for (const auto index : line.items) {
        const auto& item = items[index];
        const double margin = static_cast<double>(axis(item.margin, row, true, true))
            + axis(item.margin, row, true, false);
        base_occupied += item.base + margin;
        hypothetical_occupied += clamped_main_size(item, item.base) + margin;
    }
    const bool growing = hypothetical_occupied < main_size;
    const double initial_free = static_cast<double>(main_size) - base_occupied;
    std::vector<bool> frozen(line.items.size(), false);
    for (std::size_t slot = 0; slot < line.items.size(); ++slot) {
        const auto index = line.items[slot];
        auto& item = items[index];
        const Unit hypothetical = clamped_main_size(item, item.base);
        const bool inflexible = (growing ? item.grow : item.shrink) == 0
            || (growing && item.base > hypothetical)
            || (!growing && item.base < hypothetical);
        item.target = inflexible ? hypothetical : item.base;
        frozen[slot] = inflexible;
    }
    for (std::size_t pass = 0; pass < line.items.size(); ++pass) {
        double occupied = gutters, raw_factor_sum = 0, scaled_factor_sum = 0;
        for (std::size_t slot = 0; slot < line.items.size(); ++slot) {
            const auto index = line.items[slot];
            const auto& item = items[index];
            occupied += (frozen[slot] ? item.target : item.base)
                + axis(item.margin, row, true, true)
                + axis(item.margin, row, true, false);
            if (!frozen[slot]) {
                raw_factor_sum += growing ? item.grow : item.shrink;
                scaled_factor_sum += growing ? item.grow
                    : static_cast<double>(item.shrink) * item.base;
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
                : static_cast<double>(item.shrink) * item.base;
            const double ideal = static_cast<double>(item.base)
                + (scaled_factor_sum > 0 ? free * factor / scaled_factor_sum : 0);
            item.target = clamped_main_size(item, ideal);
            violations[slot] = static_cast<double>(item.target) - ideal;
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
    const bool row = is_row(node.style.flex_direction);
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
        ResolvedBox child_box;
        try { child_box = resolve_layout_box(child, inner_width, inner_height); }
        catch (const std::invalid_argument& error) {
            throw LayoutFailure(LayoutErrorCode::InvalidStyle, id, error.what());
        }
        Constraints child_constraints;
        child_constraints.containing_width = inner_width;
        child_constraints.containing_height = inner_height;
        child_constraints.available.width = inner_width
            ? AvailableSpace::definite(*inner_width) : AvailableSpace::indefinite();
        child_constraints.available.height = inner_height
            ? AvailableSpace::definite(*inner_height) : AvailableSpace::indefinite();
        if (row) child_constraints.available.width = AvailableSpace::max_content();
        else child_constraints.available.height = AvailableSpace::max_content();
        // The flex base size is measured before applying min/max main sizes.
        // Those bounds produce the hypothetical size used for line breaking.
        if (row) child_constraints.ignore_min_max_width = true;
        else child_constraints.ignore_min_max_height = true;
        const auto base_layout = layout(id, child_constraints, depth + 1);
        const auto margin = edges(child_box.margin, id);
        Unit basis = axis(base_layout.size, row, true);
        const auto flex_basis = child.style.flex_basis.kind();
        if (flex_basis == LengthKind::Length || flex_basis == LengthKind::Percent
            || flex_basis == LengthKind::Calc) {
            const auto resolved = child.style.flex_basis.resolve_numeric(main_available);
            if (resolved) {
                const auto decoration = row
                    ? child_box.horizontal_decoration() : child_box.vertical_decoration();
                if (!decoration) {
                    throw LayoutFailure(LayoutErrorCode::UnresolvedPercentage, id,
                        "Flex basis decoration is unresolved");
                }
                basis = child.style.box.box_sizing == BoxSizing::ContentBox
                    ? checked(static_cast<double>(std::max(*resolved, Unit{0})) + *decoration, id)
                    : std::max(*resolved, *decoration);
            }
        } else if (flex_basis != LengthKind::Auto) {
            const auto probe = [&](AvailableSpace main_space) {
                auto query = child_constraints;
                if (row) {
                    query.ignore_preferred_width = true;
                    query.available.width = main_space;
                } else {
                    query.ignore_preferred_height = true;
                    query.available.height = main_space;
                }
                return axis(layout(id, query, depth + 1).size, row, true);
            };
            if (flex_basis == LengthKind::Stretch && main_available) {
                basis = std::max(Unit{0}, checked(static_cast<double>(*main_available)
                    - axis(margin, row, true, true)
                    - axis(margin, row, true, false), id));
            } else if (flex_basis == LengthKind::MinContent) {
                basis = probe(AvailableSpace::min_content());
            } else if (flex_basis == LengthKind::Content
                || flex_basis == LengthKind::MaxContent
                || flex_basis == LengthKind::Stretch) {
                basis = probe(AvailableSpace::max_content());
            } else {
                const Unit minimum = probe(AvailableSpace::min_content());
                const Unit maximum = probe(AvailableSpace::max_content());
                std::optional<Unit> limit = flex_basis == LengthKind::FitContent
                    ? main_available
                    : child.style.flex_basis.resolve_fit_content_limit(main_available);
                basis = limit ? std::max(minimum, std::min(maximum, *limit)) : maximum;
            }
        }
        const auto cross_alignment = child.style.align_self.value_or(
            node.style.align_items.value_or(ItemAlignment{}));
        const auto cross_preferred = axis(child_box.preferred, row, false);
        const auto main_preferred = axis(child_box.preferred, row, true);
        const auto& cross_size_style = row
            ? child.style.box.size.height : child.style.box.size.width;
        const bool cross_auto_margin = (row
            ? child_box.margin.top.kind == EdgeKind::Auto
                || child_box.margin.bottom.kind == EdgeKind::Auto
            : child_box.margin.left.kind == EdgeKind::Auto
                || child_box.margin.right.kind == EdgeKind::Auto);
        if (child_box.aspect_ratio && cross_available
            && !main_preferred && !cross_preferred
            && cross_size_style.kind() == LengthKind::Auto
            && (flex_basis == LengthKind::Auto || flex_basis == LengthKind::Content)
            && node.style.flex_wrap == FlexWrap::NoWrap
            && cross_alignment.position == ItemPosition::Stretch
            && !cross_auto_margin) {
            const Unit stretched_cross = std::max(Unit{0}, checked(
                static_cast<double>(*cross_available)
                    - axis(margin, row, false, true)
                    - axis(margin, row, false, false), id));
            const auto transferred = transfer_aspect_ratio(row
                ? ResolvedDimensions{std::nullopt, stretched_cross}
                : ResolvedDimensions{stretched_cross, std::nullopt}, child_box);
            basis = axis(transferred, row, true).value_or(basis);
        }
        auto minimum = axis(child_box.minimum, row, true);
        if (!minimum) {
            const auto overflow = row ? child.style.overflow_x : child.style.overflow_y;
            if (overflow == Overflow::Hidden || overflow == Overflow::Scroll) {
                minimum = Unit{0};
            } else {
                auto min_constraints = child_constraints;
                min_constraints.ignore_min_max_width = false;
                min_constraints.ignore_min_max_height = false;
                if (row) {
                    min_constraints.available.width = AvailableSpace::min_content();
                    min_constraints.ignore_preferred_width = true;
                } else {
                    min_constraints.available.height = AvailableSpace::min_content();
                    min_constraints.ignore_preferred_height = true;
                }
                const auto intrinsic = layout(id, min_constraints, depth + 1);
                minimum = axis(intrinsic.size, row, true);
                if (const auto suggested = axis(child_box.preferred, row, true)) {
                    minimum = std::min(*minimum, *suggested);
                }
            }
        }
        const auto maximum = axis(child_box.maximum, row, true);
        const auto& margin_start = row ? child_box.margin.left : child_box.margin.top;
        const auto& margin_end = row ? child_box.margin.right : child_box.margin.bottom;
        const auto& cross_start = row ? child_box.margin.top : child_box.margin.left;
        const auto& cross_end = row ? child_box.margin.bottom : child_box.margin.right;
        items.push_back({id, source, child_box, margin, basis, basis,
            minimum.value_or(Unit{0}), maximum, child.style.flex_grow, child.style.flex_shrink,
            axis(base_layout.size, row, false),
            (main_reverse ? margin_end : margin_start).kind == EdgeKind::Auto,
            (main_reverse ? margin_start : margin_end).kind == EdgeKind::Auto,
            (cross_reverse ? cross_end : cross_start).kind == EdgeKind::Auto,
            (cross_reverse ? cross_start : cross_end).kind == EdgeKind::Auto});
    }

    auto line_break_limit = main_available;
    const auto main_query = row ? constraints.available.width.kind
        : constraints.available.height.kind;
    if (!line_break_limit && main_query == AvailableKind::MinContent
        && node.style.flex_wrap != FlexWrap::NoWrap) {
        double largest_item = 0;
        for (const auto& item : items) {
            largest_item = std::max(largest_item,
                static_cast<double>(clamped_main_size(item, item.base))
                    + axis(item.margin, row, true, true)
                    + axis(item.margin, row, true, false));
        }
        line_break_limit = checked(std::max(0.0, largest_item), node.id);
    }
    auto lines = collect_flex_lines(items, row, line_break_limit, main_gap,
        node.style.flex_wrap, node.style.flex_line_count);
    if (!main_available) {
        double desired = 0;
        for (const auto& line : lines) {
            double line_size = line.items.empty() ? 0
                : static_cast<double>(line.items.size() - 1) * main_gap;
            for (const auto index : line.items) {
                const auto& item = items[index];
                line_size += item.base + axis(item.margin, row, true, true)
                    + axis(item.margin, row, true, false);
            }
            desired = std::max(desired, line_size);
        }
        const Unit inner = checked(std::max(0.0, desired), node.id);
        if (row) {
            border_width = constrain_axis(checked(static_cast<double>(inner)
                + *deco_width, node.id), box.minimum.width, box.maximum.width);
            inner_width = std::max(Unit{0}, *border_width - *deco_width);
        } else {
            border_height = constrain_axis(checked(static_cast<double>(inner)
                + *deco_height, node.id), box.minimum.height, box.maximum.height);
            inner_height = std::max(Unit{0}, *border_height - *deco_height);
        }
        const Unit constrained_main = row ? *inner_width : *inner_height;
        if (constrained_main < inner && node.style.flex_wrap != FlexWrap::NoWrap) {
            lines = collect_flex_lines(items, row, constrained_main, main_gap,
                node.style.flex_wrap, node.style.flex_line_count);
        }
    }
    const Unit main_size = row ? *inner_width : *inner_height;
    if (!main_available) {
        main_gap = resolved_gap(row ? node.style.gap.width : node.style.gap.height,
            main_size, node.id);
        lines = collect_flex_lines(items, row, main_size, main_gap,
            node.style.flex_wrap, node.style.flex_line_count);
    }
    for (auto& line : lines) {
        resolve_flexible_lengths(line, items, main_size, main_gap, row);
        for (const auto index : line.items) {
            auto& item = items[index];
            Constraints final_constraints;
            final_constraints.containing_width = child_percentage_width;
            final_constraints.containing_height = child_percentage_height;
            final_constraints.available.width = inner_width
                ? AvailableSpace::definite(*inner_width) : AvailableSpace::indefinite();
            final_constraints.available.height = inner_height
                ? AvailableSpace::definite(*inner_height) : AvailableSpace::indefinite();
            if (row) final_constraints.known_width = item.target;
            else final_constraints.known_height = item.target;
            fragments[item.source_index] = layout(item.id, final_constraints, depth + 1);
            item.target = axis(fragments[item.source_index].size, row, true);
            item.cross = axis(fragments[item.source_index].size, row, false);
            const Unit outer_cross = checked(static_cast<double>(item.cross)
                + axis(item.margin, row, false, true)
                + axis(item.margin, row, false, false), item.id);
            line.cross = std::max(line.cross, outer_cross);
            if (row) {
                const auto& child = get(item.id);
                const auto alignment = child.style.align_self.value_or(
                    node.style.align_items.value_or(ItemAlignment{}));
                if (alignment.position == ItemPosition::Baseline) {
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
    const Unit cross_size = row ? *inner_height : *inner_width;
    if (lines.size() == 1) lines.front().cross = cross_size;
    double cross_free = lines.size() == 1 ? 0 : static_cast<double>(cross_size) - used_cross;
    auto align_lines = alignment_fallback(
        node.style.align_content.value_or(ContentAlignment{}),
        cross_free, lines.size());
    const bool wrap_reverse = node.style.flex_wrap == FlexWrap::WrapReverse
        || node.style.flex_wrap == FlexWrap::BalanceReverse;
    // Start/End follow the cross axis of the writing mode. FlexStart/FlexEnd
    // follow the line stacking direction, which wrap-reverse flips.
    if (wrap_reverse) {
        if (align_lines.position == ContentPosition::Start) {
            align_lines.position = ContentPosition::End;
        } else if (align_lines.position == ContentPosition::End) {
            align_lines.position = ContentPosition::Start;
        }
    }
    if (lines.size() > 1 && align_lines.position == ContentPosition::Stretch && cross_free > 0) {
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
            main_occupied += item.target + axis(item.margin, row, true, true)
                + axis(item.margin, row, true, false);
            auto_margins += static_cast<std::size_t>(item.auto_main_start)
                + static_cast<std::size_t>(item.auto_main_end);
        }
        const double remaining = static_cast<double>(main_size) - main_occupied;
        const Unit auto_share = auto_margins && remaining > 0
            ? checked(remaining / auto_margins, node.id) : 0;
        const auto distribution = auto_share == 0
            ? distribute(checked(remaining, node.id), line.items.size(), justify) : Distribution{};
        double main_cursor = distribution.offset;
        for (const auto index : line.items) {
            auto& item = items[index];
            auto& fragment = fragments[item.source_index];
            const auto& child_style = get(item.id).style;
            const auto alignment = child_style.align_self.value_or(node.style.align_items.value_or(
                ItemAlignment{ItemPosition::Stretch, AlignmentSafety::Unsafe}));
            const bool cross_auto = (row ? child_style.box.size.height : child_style.box.size.width)
                .kind() == LengthKind::Auto;
            Unit cross_margin_start = axis(item.margin, row, false, !cross_reverse);
            Unit cross_margin_end = axis(item.margin, row, false, cross_reverse);
            if (alignment.position == ItemPosition::Stretch && cross_auto
                && !item.auto_cross_start && !item.auto_cross_end) {
                const Unit target_cross = std::max(Unit{0}, checked(
                    static_cast<double>(line.cross) - cross_margin_start - cross_margin_end,
                    item.id));
                Constraints stretched;
                stretched.containing_width = child_percentage_width;
                stretched.containing_height = child_percentage_height;
                stretched.available.width = inner_width
                    ? AvailableSpace::definite(*inner_width) : AvailableSpace::indefinite();
                stretched.available.height = inner_height
                    ? AvailableSpace::definite(*inner_height) : AvailableSpace::indefinite();
                if (row) { stretched.known_width = item.target; stretched.known_height = target_cross; }
                else { stretched.known_height = item.target; stretched.known_width = target_cross; }
                fragment = layout(item.id, stretched, depth + 1);
                item.cross = axis(fragment.size, row, false);
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
                cross_offset = checked(static_cast<double>(line.baseline_ascent)
                    - baseline, item.id);
            }
            if (!(item.auto_cross_start || item.auto_cross_end)
                && !(row && line.has_baseline
                    && alignment.position == ItemPosition::Baseline)
                && !(alignment.safety == AlignmentSafety::Safe && cross_remain < 0)) {
                const auto edge = item_edge(alignment.position, !row,
                    node.style.box.direction, child_style.box.direction,
                    cross_reverse, cross_reverse);
                if (edge == ItemEdge::End) {
                    cross_offset = checked(cross_margin_start + cross_remain, item.id);
                } else if (edge == ItemEdge::Center) {
                    cross_offset = checked(cross_margin_start + cross_remain / 2, item.id);
                }
            }
            const Unit main_start_margin = axis(item.margin, row, true, !main_reverse)
                + (item.auto_main_start ? auto_share : 0);
            const Unit main_end_margin = axis(item.margin, row, true, main_reverse)
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
            const Unit content_x = checked(static_cast<double>(border.left) + padding.left, node.id);
            const Unit content_y = checked(static_cast<double>(border.top) + padding.top, node.id);
            translate(fragment,
                row ? checked(static_cast<double>(content_x) + main_position, item.id)
                    : checked(static_cast<double>(content_x) + cross_position, item.id),
                row ? checked(static_cast<double>(content_y) + cross_position, item.id)
                    : checked(static_cast<double>(content_y) + main_position, item.id), item.id);
            const auto [relative_x, relative_y] = relative_offset(get(item.id),
                Size{*inner_width, *inner_height}, node.id);
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
        result.nodes.insert(result.nodes.end(),
            std::make_move_iterator(fragment.nodes.begin()),
            std::make_move_iterator(fragment.nodes.end()));
        result.grid_tracks.insert(result.grid_tracks.end(),
            std::make_move_iterator(fragment.grid_tracks.begin()),
            std::make_move_iterator(fragment.grid_tracks.end()));
    }
    return result;
}

} // namespace axisly::css::detail
