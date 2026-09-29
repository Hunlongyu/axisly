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
    // Flex base size includes padding and border; shrink weights use its content size.
    Unit base{};
    Unit inner_base{};
    Unit main_decoration{};
    Unit content_main{};
    Unit target{};
    Unit minimum{};
    std::optional<Unit> maximum;
    Unit hypothetical_minimum{};
    std::optional<Unit> hypothetical_maximum;
    Unit grow{}, shrink{};
    Unit cross{};
    bool auto_main_start{}, auto_main_end{};
    bool auto_cross_start{}, auto_cross_end{};
    bool content_based_cross_width{};
    bool main_size_is_definite{}, cross_size_is_definite{};
    bool scroll_container{};
    Constraints final_constraints;
};

struct FlexLine {
    std::vector<std::size_t> items;
    Unit cross{};
    Unit baseline_ascent{}, baseline_descent{};
    bool has_baseline{};
};

double bounded_main_size(const FlexItem& item, double value);
Unit clamped_main_size(const FlexItem& item, double value);
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
            const auto outer = static_cast<double>(hypothetical_main_size(item))
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

enum class CrossSpaceStage { FlexBase, Hypothetical };

AvailableSpace cross_space_for_item(AvailableSpace available,
                                   std::optional<Unit> minimum,
                                   std::optional<Unit> maximum,
                                   Unit outer_margin, CrossSpaceStage stage,
                                   NodeId id) {
    if (minimum) *minimum = checked(static_cast<double>(*minimum) + outer_margin, id);
    if (maximum) *maximum = checked(static_cast<double>(*maximum) + outer_margin, id);
    if (available.kind == AvailableKind::Definite) {
        return AvailableSpace::definite(constrain_axis(available.value, minimum, maximum));
    }
    if (stage == CrossSpaceStage::Hypothetical) return available;
    if (available.kind == AvailableKind::MinContent && minimum) {
        return AvailableSpace::definite(*minimum);
    }
    if (available.kind == AvailableKind::MaxContent && maximum) {
        return AvailableSpace::definite(*maximum);
    }
    return available;
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

Unit clamped_main_size(const FlexItem& item, double value) {
    return checked(bounded_main_size(item, value), item.id);
}

Unit hypothetical_main_size(const FlexItem& item) {
    double value = item.base;
    if (item.hypothetical_maximum) {
        value = std::min(value, static_cast<double>(*item.hypothetical_maximum));
    }
    return checked(std::max(value, static_cast<double>(item.hypothetical_minimum)), item.id);
}

Unit intrinsic_item_main_size(const FlexItem& item, bool row) {
    const double margin = static_cast<double>(axis(item.margin, row, true, true))
        + axis(item.margin, row, true, false);
    const auto preferred = axis(item.box.preferred, row, true);
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
    if (const auto minimum_style = axis(item.box.specified_minimum, row, true)) {
        content = std::max(content, static_cast<double>(*minimum_style));
    }
    if (item.maximum) content = std::min(content,
        static_cast<double>(*item.maximum));
    return checked(content, item.id);
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
    const bool row = is_row(node.style.flex_direction);
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
        : axis(box.preferred, row, true).has_value();
    const auto outer_main_space = row ? constraints.available.width
        : constraints.available.height;
    const auto main_query = outer_main_space.kind;
    const bool ignore_main_bounds = row ? constraints.ignore_min_max_width
        : constraints.ignore_min_max_height;
    const auto line_maximum = axis(ignore_main_bounds ? box.specified_maximum
        : box.maximum, row, true);
    const auto line_minimum = axis(ignore_main_bounds ? box.specified_minimum
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
        ResolvedBox child_box;
        try { child_box = resolve_layout_box(child, inner_width, inner_height); }
        catch (const std::invalid_argument& error) {
            throw LayoutFailure(LayoutErrorCode::InvalidStyle, id, error.what());
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
        const bool content_based_cross_width = !row
            && (child.style.display == Display::Block
                || child.style.display == Display::FlowRoot)
            && cross_size_style.kind() == LengthKind::Auto
            && !cross_preferred
            && (cross_alignment.position != ItemPosition::Stretch
                || cross_auto_margin);
        const auto main_decoration = row
            ? child_box.horizontal_decoration() : child_box.vertical_decoration();
        if (!main_decoration) {
            throw LayoutFailure(LayoutErrorCode::UnresolvedPercentage, id,
                "Flex item decoration requires a definite containing inline size");
        }
        const auto margin = edges(child_box.margin, id);
        const Unit cross_outer_margin = checked(static_cast<double>(
            axis(margin, row, false, true))
            + axis(margin, row, false, false), id);
        Constraints child_constraints;
        child_constraints.content_based_width = content_based_cross_width;
        child_constraints.containing_width = inner_width;
        child_constraints.containing_height = inner_height;
        child_constraints.available.width = inner_width
            ? AvailableSpace::definite(*inner_width) : constraints.available.width;
        child_constraints.available.height = inner_height
            ? AvailableSpace::definite(*inner_height) : constraints.available.height;
        const auto base_space = main_query == AvailableKind::MinContent
            ? AvailableSpace::min_content() : AvailableSpace::max_content();
        if (row) child_constraints.available.width = base_space;
        else child_constraints.available.height = base_space;
        auto& child_cross_space = row ? child_constraints.available.height
            : child_constraints.available.width;
        child_cross_space = cross_space_for_item(child_cross_space,
            axis(child_box.minimum, row, false),
            axis(child_box.maximum, row, false), cross_outer_margin,
            CrossSpaceStage::FlexBase, id);
        // A definite preferred main size is the flex basis before min/max
        // clamping. For an auto-sized subtree, its own layout algorithm
        // observes its bounds while answering the content-size query.
        if (main_preferred) {
            if (row) child_constraints.ignore_min_max_width = true;
            else child_constraints.ignore_min_max_height = true;
        }
        const auto base_layout = layout(id, child_constraints, depth + 1);
        Unit basis = axis(base_layout.size, row, true);
        const auto flex_basis = child.style.flex_basis.kind();
        bool basis_is_definite = flex_basis == LengthKind::Auto
            && main_preferred.has_value();
        if (flex_basis == LengthKind::Length || flex_basis == LengthKind::Percent
            || flex_basis == LengthKind::Calc) {
            const auto resolved = child.style.flex_basis.resolve_numeric(main_available);
            if (resolved) {
                basis_is_definite = true;
                basis = child.style.box.box_sizing == BoxSizing::ContentBox
                    ? checked(static_cast<double>(std::max(*resolved, Unit{0})) + *main_decoration, id)
                    : std::max(*resolved, *main_decoration);
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
                basis_is_definite = true;
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
            basis_is_definite = true;
        }
        auto minimum = axis(child_box.specified_minimum, row, true);
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
        const auto maximum = axis(child_box.specified_maximum, row, true);
        const auto& margin_start = row ? child_box.margin.left : child_box.margin.top;
        const auto& margin_end = row ? child_box.margin.right : child_box.margin.bottom;
        const auto& cross_start = row ? child_box.margin.top : child_box.margin.left;
        const auto& cross_end = row ? child_box.margin.bottom : child_box.margin.right;
        const bool scroll_container = child.style.overflow_x == Overflow::Hidden
            || child.style.overflow_x == Overflow::Scroll
            || child.style.overflow_y == Overflow::Hidden
            || child.style.overflow_y == Overflow::Scroll;
        basis = std::max(basis, *main_decoration);
        FlexItem item{id, source, child_box, margin};
        item.base = basis;
        item.inner_base = checked(static_cast<double>(basis) - *main_decoration, id);
        item.main_decoration = *main_decoration;
        item.content_main = axis(base_layout.size, row, true);
        item.target = basis;
        item.minimum = minimum.value_or(Unit{0});
        item.maximum = maximum;
        item.hypothetical_minimum = std::max(item.minimum,
            axis(child_box.minimum, row, true).value_or(Unit{0}));
        item.hypothetical_maximum = axis(child_box.maximum, row, true);
        item.grow = child.style.flex_grow;
        item.shrink = child.style.flex_shrink;
        item.cross = axis(base_layout.size, row, false);
        item.auto_main_start = (main_reverse ? margin_end : margin_start).kind == EdgeKind::Auto;
        item.auto_main_end = (main_reverse ? margin_start : margin_end).kind == EdgeKind::Auto;
        item.auto_cross_start = (cross_reverse ? cross_end : cross_start).kind == EdgeKind::Auto;
        item.auto_cross_end = (cross_reverse ? cross_start : cross_end).kind == EdgeKind::Auto;
        item.content_based_cross_width = content_based_cross_width;
        item.main_size_is_definite = main_size_is_definite || basis_is_definite;
        const bool cross_available_is_definite = cross_available.has_value()
            || (row ? constraints.available.height : constraints.available.width).kind
                == AvailableKind::Definite;
        const bool stretched_cross = !cross_auto_margin
            && (cross_size_style.kind() == LengthKind::Stretch
                || (cross_alignment.position == ItemPosition::Stretch
                    && cross_size_style.kind() == LengthKind::Auto));
        item.cross_size_is_definite = cross_preferred.has_value() || stretched_cross
            || (!row && cross_available_is_definite);
        item.scroll_container = scroll_container;
        items.push_back(std::move(item));
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
                    axis(item.box.specified_minimum, row, true).value_or(Unit{0}));
                const double outer = static_cast<double>(contribution)
                    + axis(item.margin, row, true, true)
                    + axis(item.margin, row, true, false);
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
    const Unit main_size = row ? *inner_width : *inner_height;
    if (!main_available) {
        main_gap = resolved_gap(row ? node.style.gap.width : node.style.gap.height,
            main_size, node.id);
    }
    for (auto& line : lines) {
        resolve_flexible_lengths(line, items, main_size, main_gap, row);
        for (const auto index : line.items) {
            auto& item = items[index];
            Constraints final_constraints;
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
            const Unit cross_outer_margin = checked(static_cast<double>(
                axis(item.margin, row, false, true))
                + axis(item.margin, row, false, false), item.id);
            final_cross_space = cross_space_for_item(final_cross_space,
                axis(item.box.minimum, row, false),
                axis(item.box.maximum, row, false), cross_outer_margin,
                CrossSpaceStage::Hypothetical, item.id);
            if (row) final_constraints.known_width = item.target;
            else final_constraints.known_height = item.target;
            // Resolve the hypothetical cross size using the post-flex main
            // size, then perform the item layout with both axes fixed. A
            // nested container may lay out its own children differently in
            // that final pass even when its border-box size stays the same.
            const auto cross_probe = layout(item.id, final_constraints, depth + 1);
            if (row) final_constraints.known_height = cross_probe.size.height;
            else final_constraints.known_width = cross_probe.size.width;
            item.final_constraints = final_constraints;
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
    const Unit cross_size = row ? *inner_height : *inner_width;
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
    // Start/End follow the cross axis of the writing mode. FlexStart/FlexEnd
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
            main_occupied += item.target + axis(item.margin, row, true, true)
                + axis(item.margin, row, true, false);
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
