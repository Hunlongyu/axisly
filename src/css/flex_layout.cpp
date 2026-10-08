#include "flex_internal.hpp"
#include "flex_workspace.hpp"
#include "balanced_lines.hpp"
#include "item_alignment.hpp"
#include "sizing_keywords.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <ranges>
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


double bounded_main_size(const FlexItem& item, double value);

void collect_flex_lines(std::pmr::vector<FlexLine>& lines, const std::pmr::vector<FlexItem>& items,
    std::optional<Unit> available, Unit gap, FlexWrap wrap,
    std::uint16_t requested_lines) {
    lines.resize(1);
    if (wrap == FlexWrap::NoWrap) {
        lines.front().items = std::views::iota(std::size_t{0}, items.size());
        return;
    }
    const bool balanced = wrap == FlexWrap::Balance
        || wrap == FlexWrap::BalanceReverse;
    if (balanced && !items.empty()) {
        std::vector<Unit> outer_sizes;
        outer_sizes.reserve(items.size());
        for (const auto& item : items) {
            const Unit hypothetical = item.hypothetical;
            outer_sizes.push_back(checked(static_cast<double>(hypothetical)
                + item.margin.main_start
                + item.margin.main_end, item.id));
        }
        const auto counts = balanced_line_counts(outer_sizes,
            available.value_or(std::numeric_limits<Unit>::infinity()),
            gap, requested_lines, 25'000'000);
        lines.clear();
        std::size_t next = 0;
        for (const auto count : counts) {
            FlexLine line;
            line.items = std::views::iota(next, next + count);
            next += count;
            lines.push_back(std::move(line));
        }
    } else {
        double estimated_main = 0;
        for (std::size_t i = 0; i < items.size(); ++i) {
            const auto& item = items[i];
            const auto outer = static_cast<double>(item.hypothetical)
                + item.margin.main_start
                + item.margin.main_end;
            if (wrap != FlexWrap::NoWrap && available
                && !lines.back().items.empty()
                && estimated_main + gap + outer > *available) {
                lines.emplace_back();
                estimated_main = 0;
            }
            if (!lines.back().items.empty()) estimated_main += gap;
            estimated_main += outer;
            lines.back().items = std::views::iota(i - lines.back().items.size(), i + 1);
        }
    }
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

Unit intrinsic_content_contribution(const FlexItem& item, bool row) {
    const double margin = static_cast<double>(item.margin.main_start)
        + item.margin.main_end;
    const auto preferred = flex_axis(item.box->preferred, row, true);
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
    if (const auto minimum_style = flex_axis(item.box->specified_minimum, row, true)) {
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

void resolve_flexible_lengths(FlexLine& line, std::pmr::vector<FlexItem>& items,
                              Unit main_size, Unit gap) {
    const double gutters = line.items.empty() ? 0
        : static_cast<double>(line.items.size() - 1) * gap;
    double hypothetical_occupied = gutters;
    for (const auto index : line.items) {
        const auto& item = items[index];
        const double margin = static_cast<double>(item.margin.main_start)
            + item.margin.main_end;
        hypothetical_occupied += item.hypothetical + margin;
    }
    const bool growing = hypothetical_occupied < main_size;
    const bool shrinking = hypothetical_occupied > main_size;
    const bool exactly_sized = !growing && !shrinking;
    for (std::size_t slot = 0; slot < line.items.size(); ++slot) {
        const auto index = line.items[slot];
        auto& item = items[index];
        const Unit hypothetical = item.hypothetical;
        const bool inflexible = exactly_sized || (item.grow == 0 && item.shrink == 0)
            || (growing && item.base > hypothetical)
            || (shrinking && item.base < hypothetical);
        item.target = hypothetical;
        item.frozen = inflexible;
    }
    if (exactly_sized) return;
    double initial_occupied = gutters;
    for (std::size_t slot = 0; slot < line.items.size(); ++slot) {
        const auto& item = items[line.items[slot]];
        initial_occupied += (item.frozen ? item.target : item.base)
            + item.margin.main_start
            + item.margin.main_end;
    }
    const double initial_free = static_cast<double>(main_size) - initial_occupied;
    for (std::size_t pass = 0; pass < line.items.size(); ++pass) {
        double occupied = gutters, raw_factor_sum = 0, scaled_factor_sum = 0;
        for (std::size_t slot = 0; slot < line.items.size(); ++slot) {
            const auto index = line.items[slot];
            const auto& item = items[index];
            occupied += (item.frozen ? item.target : item.base)
                + item.margin.main_start
                + item.margin.main_end;
            if (!item.frozen) {
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
        for (std::size_t slot = 0; slot < line.items.size(); ++slot) {
            const auto index = line.items[slot];
            auto& item = items[index];
            if (item.frozen) continue;
            const double factor = growing ? item.grow
                : static_cast<double>(item.shrink) * item.inner_base;
            const double ideal = static_cast<double>(item.base)
                + (scaled_factor_sum > 0 ? free * factor / scaled_factor_sum : 0);
            const double bounded = bounded_main_size(item, ideal);
            item.target = checked(bounded, item.id);
            // Compare in the same precision as the unconstrained target.
            // Casting to Unit first can turn an unbounded item into a false
            // min/max violation and freeze it in the next pass.
            item.violation = bounded - ideal;
            total_violation += item.violation;
        }
        if (std::abs(total_violation) < 0.0001) break;
        for (std::size_t slot = 0; slot < line.items.size(); ++slot) {
            auto& item = items[line.items[slot]];
            if (!item.frozen
                && (total_violation > 0 ? item.violation > 0
                                        : item.violation < 0)) {
                item.frozen = true;
            }
        }
    }
}

} // namespace

// Per-container state outlives each phase, while phase-local temporaries do
// not remain on the recursive stack during unrelated child queries.
// Resolve the physical main axis once at the container boundary. Keeping it
// in the type removes repeated row/column projection from each item loop.
template<bool IsRow>
class FlexLayout {
public:
    FlexLayout(Engine& engine_arg, const LayoutNode& node_arg,
        const Constraints& constraints_arg, const LayoutBox& box_arg, std::size_t depth_arg)
        : engine(engine_arg), node(node_arg), constraints(constraints_arg), box(box_arg), depth(depth_arg),
          workspace(engine.cache_.flex_workspace()), fragments(workspace.get().fragments),
          items(workspace.get().items), item_boxes(workspace.get().boxes),
          used_item_constraints(workspace.get().constraints), lines(workspace.get().lines) {}
    [[nodiscard]] Fragment run();
private:
    Engine& engine;
    const LayoutNode& node;
    const Constraints& constraints;
    const LayoutBox& box;
    std::size_t depth;
    static constexpr bool row = IsRow;
    bool reverse_direction{}, main_reverse{}, cross_reverse{}, wrap_reverse{}, main_size_is_definite{};
    Unit deco_width{}, deco_height{};
    std::optional<Unit> border_width, border_height, inner_width, inner_height;
    std::optional<Unit> child_percentage_width, child_percentage_height, main_available, cross_available;
    std::optional<Unit> known_main, line_available;
    AvailableSize available_inner;
    AvailableKind main_query{};
    Unit main_gap{}, cross_gap{}, main_size{};
    double used_cross{};
    ResolvedDimensions child_layout_basis;
    WorkspacePool<FlexWorkspace>::Lease workspace;
    std::pmr::vector<Fragment>& fragments;
    std::pmr::vector<FlexItem>& items;
    // Flex resolution scans only the compact mutable item state. Resolved
    // boxes and final-layout constraints are needed by separate phases.
    std::pmr::vector<LayoutBox>& item_boxes;
    std::pmr::vector<Constraints>& used_item_constraints;
    std::pmr::vector<FlexLine>& lines;
    void initialize();
    void collect_items();
    void measure_lines();
    void resolve_cross_size();
    void layout_items();
    [[nodiscard]] Fragment finish();
    [[nodiscard]] Fragment size_result() const;
};

template<bool IsRow>
Fragment FlexLayout<IsRow>::run() {
    initialize();
    if (engine.detail_ == FragmentDetail::Size && border_width && border_height) return size_result();
    collect_items();
    measure_lines();
    resolve_cross_size();
    if (engine.detail_ == FragmentDetail::Size) return size_result();
    layout_items();
    return finish();
}

template<bool IsRow>
Fragment FlexLayout<IsRow>::size_result() const {
    Fragment result;
    result.size = constrain_size(Size{*border_width, *border_height}, box);
    result.root = make_layout(node, result.size, box);
    return result;
}

template<bool IsRow>
void FlexLayout<IsRow>::initialize() {
    wrap_reverse = node.style.flex_wrap == FlexWrap::WrapReverse
        || node.style.flex_wrap == FlexWrap::BalanceReverse;
    reverse_direction = node.style.flex_direction == FlexDirection::RowReverse
        || node.style.flex_direction == FlexDirection::ColumnReverse;
    main_reverse = row
        ? (node.style.flex_direction == FlexDirection::RowReverse)
            != (node.style.box.direction == Direction::Rtl)
        : node.style.flex_direction == FlexDirection::ColumnReverse;
    cross_reverse = (node.style.flex_wrap == FlexWrap::WrapReverse
        || node.style.flex_wrap == FlexWrap::BalanceReverse)
        != (!row && node.style.box.direction == Direction::Rtl);
    deco_width = box.horizontal_decoration();
    deco_height = box.vertical_decoration();
    const auto requested = requested_size(constraints, box);
    border_width = requested.width;
    border_height = requested.height;
    if (border_width) {
        *border_width = constrain_axis(*border_width,
            box.minimum.width, box.maximum.width);
    }
    if (border_height) {
        *border_height = constrain_axis(*border_height,
            box.minimum.height, box.maximum.height);
    }
    inner_width = border_width
        ? std::optional<Unit>(std::max(Unit{0}, *border_width - deco_width)) : std::nullopt;
    inner_height = border_height
        ? std::optional<Unit>(std::max(Unit{0}, *border_height - deco_height)) : std::nullopt;
    const auto container_margin = edges(box.margin, node.id);
    available_inner = AvailableSize{
        subtract_decoration(constraints.available.width, checked(
            static_cast<double>(deco_width) + container_margin.left + container_margin.right, node.id)),
        subtract_decoration(constraints.available.height, checked(
            static_cast<double>(deco_height) + container_margin.top + container_margin.bottom, node.id))};
    // A content-sized Flex container can acquire a final size without making
    // its children's percentage basis definite in the intrinsic pass.
    child_percentage_width = inner_width;
    child_percentage_height = inner_height;
    main_available = row ? inner_width : inner_height;
    cross_available = row ? inner_height : inner_width;
    known_main = row ? constraints.known_width : constraints.known_height;
    main_size_is_definite = known_main
        ? (row ? constraints.known_width_is_definite
               : constraints.known_height_is_definite)
        : flex_axis(box.preferred, row, true).has_value();
    const auto outer_main_space = row ? constraints.available.width
        : constraints.available.height;
    main_query = outer_main_space.kind;
    const bool ignore_main_bounds = row ? constraints.ignore_min_max_width
        : constraints.ignore_min_max_height;
    const auto line_maximum = flex_axis(ignore_main_bounds ? box.specified_maximum
        : box.maximum, row, true);
    const auto line_minimum = flex_axis(ignore_main_bounds ? box.specified_minimum
        : box.minimum, row, true);
    // Line collection uses the parent's remaining space even when the
    // container will derive its own main size from those lines.
    line_available = main_available;
    if (!line_available && main_query == AvailableKind::Definite) {
        const auto margin = edges(box.margin, node.id);
        const double outer_margin = row
            ? static_cast<double>(margin.left) + margin.right
            : static_cast<double>(margin.top) + margin.bottom;
        const double decoration = row ? deco_width : deco_height;
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
    main_gap = resolved_gap(row ? node.style.gap.width : node.style.gap.height,
        main_available, node.id);
    cross_gap = resolved_gap(row ? node.style.gap.height : node.style.gap.width,
        cross_available, node.id);
}

template<bool IsRow>
void FlexLayout<IsRow>::collect_items() {
    fragments.resize(engine.detail_ == FragmentDetail::Size ? 0 : node.layout_children().size());
    items.reserve(node.layout_children().size());
    if (engine.detail_ != FragmentDetail::Size) used_item_constraints.resize(node.layout_children().size());
    for (std::size_t source = 0; source < node.layout_children().size(); ++source) {
        const auto id = node.layout_children()[source];
        const auto& child = engine.get(id);
        if (child.style.display == Display::None) {
            if (engine.detail_ != FragmentDetail::Size) fragments[source] = engine.hidden(child, depth + 1);
            continue;
        }
        if (child.style.position == Position::Absolute) {
            continue;
        }
        engine.prepare_flex_item(items.emplace_back(), item_boxes, node, child, constraints,
            {source, {inner_width, inner_height}, available_inner, main_query,
                main_size_is_definite, main_reverse, cross_reverse}, depth);
    }

    const bool main_size_derived = known_main && !main_size_is_definite;
    if (!main_size_derived && !main_available && main_query == AvailableKind::MinContent
        && !line_available && node.style.flex_wrap != FlexWrap::NoWrap
        && !items.empty()) {
        lines.reserve(items.size());
        for (std::size_t index = 0; index < items.size(); ++index) {
            lines.push_back(FlexLine{std::views::iota(index, index + 1)});
        }
    } else {
        collect_flex_lines(lines, items,
            main_size_derived ? std::nullopt : line_available, main_gap,
            node.style.flex_wrap, node.style.flex_line_count);
    }
    if (!main_available) {
        const bool intrinsic_contribution = main_query == AvailableKind::MaxContent
            || (main_query == AvailableKind::MinContent
                && node.style.flex_wrap == FlexWrap::NoWrap);
        auto& outer_sizes = workspace.get().outer_sizes;
        outer_sizes.reserve(items.size());
        for (const auto& item : items) {
            if (intrinsic_contribution) {
                outer_sizes.push_back(intrinsic_item_main_size(item, row));
            } else {
                const Unit contribution = std::max(item.base,
                    flex_axis(item.box->specified_minimum, row, true).value_or(Unit{0}));
                const double outer = static_cast<double>(contribution)
                    + item.margin.main_start
                    + item.margin.main_end;
                outer_sizes.push_back(checked(std::max(outer,
                    static_cast<double>(item.main_decoration)), item.id));
            }
        }
        const auto widest_line = [&](std::span<const std::size_t> counts) {
            std::size_t next = 0;
            double widest = 0;
            for (const auto count : counts) {
                double length = count ? static_cast<double>(count - 1) * main_gap : 0;
                for (std::size_t i = 0; i < count; ++i) length += outer_sizes[next++];
                widest = std::max(widest, length);
            }
            return checked(widest, node.id);
        };
        auto& line_counts = workspace.get().line_counts;
        line_counts.reserve(lines.size());
        for (const auto& line : lines) line_counts.push_back(line.items.size());
        const Unit inner = widest_line(line_counts);
        const Unit decoration = row ? deco_width : deco_height;
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
            inner_width = std::max(Unit{0}, *border_width - deco_width);
        } else {
            border_height = constrain_axis(candidate_outer,
                box.minimum.height, box.maximum.height);
            inner_height = std::max(Unit{0}, *border_height - deco_height);
        }
    }
    // Measurement space is nonnegative. Alignment and flexible lengths use
    // the physical space, which can be negative when the gutter is larger
    // than the specified border box.
    main_size = row ? *border_width - deco_width
        : *border_height - deco_height;
    if (!main_available) {
        main_gap = resolved_gap(row ? node.style.gap.width : node.style.gap.height,
            std::max(Unit{0}, main_size), node.id);
    }
    // The selected main size supplies a new child-layout percentage basis.
    // The cross basis and item box metadata retain their collection-stage values.
    child_layout_basis = ResolvedDimensions{
        row ? inner_width : child_percentage_width,
        row ? child_percentage_height : inner_height};
}

template<bool IsRow>
void FlexLayout<IsRow>::measure_lines() {
    Constraints line_constraints;
    line_constraints.sizing_mode = SizingMode::Content;
    line_constraints.containing_width = child_layout_basis.width;
    line_constraints.containing_height = child_layout_basis.height;
    line_constraints.available.width = inner_width
        ? AvailableSpace::definite(*inner_width) : available_inner.width;
    line_constraints.available.height = inner_height
        ? AvailableSpace::definite(*inner_height) : available_inner.height;
    auto& line_cross_space = row ? line_constraints.available.height : line_constraints.available.width;
    line_cross_space = divided_flex_cross_space(node, line_cross_space, cross_available);
    for (auto& line : lines) {
        resolve_flexible_lengths(line, items, main_size, main_gap);
        for (const auto index : line.items) {
            auto& item = items[index];
            auto final_constraints = line_constraints;
            final_constraints.content_based_width = item.content_based_cross_width;
            if (row) final_constraints.known_width_is_definite = item.main_size_is_definite;
            else final_constraints.known_height_is_definite = item.main_size_is_definite;
            if (row) final_constraints.known_height_is_definite = item.cross_size_is_definite;
            else final_constraints.known_width_is_definite = item.cross_size_is_definite;
            auto& final_cross_space = row ? final_constraints.available.height
                : final_constraints.available.width;
            const Unit cross_outer_margin = checked(static_cast<double>(
                item.margin.cross_start)
                + item.margin.cross_end, item.id);
            const auto cross_maximum = transfer_aspect_ratio(
                item.box->maximum, *item.box);
            const auto cross_min = flex_axis(item.box->minimum, row, false);
            const auto cross_max = flex_axis(cross_maximum, row, false);
            final_cross_space = cross_space_for_item(final_cross_space,
                cross_min, cross_max, cross_outer_margin,
                CrossSpaceStage::Hypothetical, item.id);
            const auto& cross_style = row ? item.node->style.box.size.height
                : item.node->style.box.size.width;
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
            const auto cross_preferred = flex_axis(item.box->preferred, row, false);
            Unit cross_measured = cross_preferred.value_or(Unit{0});
            if (!cross_preferred) {
                const auto cross_probe = engine.layout_size(*item.node, final_constraints, depth + 1);
                cross_measured = flex_axis(cross_probe, row, false);
            }
            const auto decoration = row ? item.box->vertical_padding_border()
                : item.box->horizontal_padding_border();
            const Unit cross_size = std::max(constrain_axis(cross_measured,
                cross_min, cross_max), decoration);
            if (row) final_constraints.known_height = cross_size;
            else final_constraints.known_width = cross_size;
            if (engine.detail_ != FragmentDetail::Size) {
                used_item_constraints[item.source_index] = final_constraints;
            }
            const auto& child = *item.node;
            const auto alignment = item.alignment;
            const bool needs_baseline = row
                && alignment.position == ItemPosition::Baseline
                && !item.auto_cross_start && !item.auto_cross_end;
            // Line collection consumes size and, for baseline alignment,
            // baseline only. Full child geometry belongs to the used-size
            // pass after the container's cross size and stretch are known.
            Fragment baseline_fragment;
            Size measured;
            if (needs_baseline) {
                baseline_fragment = engine.layout_sizing(child, final_constraints, depth + 1);
                measured = baseline_fragment.size;
            } else if (child.is_leaf()) {
                // Both axes are now known. A leaf's content-size query ignores
                // style min/max and ratio, so its output is exactly these axes.
                // Final layout still measures content and validates decoration.
                measured = {*final_constraints.known_width, *final_constraints.known_height};
            } else {
                measured = engine.layout_size(child, final_constraints, depth + 1);
            }
            item.target = flex_axis(measured, row, true);
            item.cross = flex_axis(measured, row, false);
            const Unit outer_cross = checked(static_cast<double>(item.cross)
                + item.margin.cross_start
                + item.margin.cross_end, item.id);
            line.cross = std::max(line.cross, outer_cross);
            if (needs_baseline) {
                const Unit baseline = child_baseline(
                    baseline_fragment, child.style).value_or(item.cross);
                const Unit before = checked(static_cast<double>(item.margin.cross_start)
                    + baseline, item.id);
                const Unit after = checked(static_cast<double>(item.margin.cross_end)
                    + item.cross - baseline, item.id);
                line.baseline_ascent = std::max(line.baseline_ascent, before);
                line.baseline_descent = std::max(line.baseline_descent, after);
                line.has_baseline = true;
            }
        }
        if (line.has_baseline) {
            line.cross = std::max(line.cross, checked(
                static_cast<double>(line.baseline_ascent) + line.baseline_descent,
                node.id));
        }
    }
}

template<bool IsRow>
void FlexLayout<IsRow>::resolve_cross_size() {
    used_cross = lines.empty() ? 0 : static_cast<double>(lines.size() - 1) * cross_gap;
    for (const auto& line : lines) used_cross += line.cross;
    if (row && !border_height) {
        border_height = constrain_axis(checked(used_cross + deco_height,
            node.id), box.minimum.height, box.maximum.height);
        inner_height = std::max(Unit{0}, *border_height - deco_height);
    }
    if (!row && !border_width) {
        border_width = constrain_axis(checked(used_cross + deco_width,
            node.id), box.minimum.width, box.maximum.width);
        inner_width = std::max(Unit{0}, *border_width - deco_width);
    }
}

template<bool IsRow>
void FlexLayout<IsRow>::layout_items() {
    const auto container_border = edges(box.border(), node.id);
    const auto container_padding = edges(box.padding(), node.id);
    const Unit content_x = content_origin_x(box, container_border, container_padding, node.id);
    const Unit content_y = checked(static_cast<double>(container_border.top) + container_padding.top, node.id);
    const Unit cross_size = row ? *border_height - deco_height
        : *border_width - deco_width;
    const bool wraps = node.style.flex_wrap != FlexWrap::NoWrap;
    const AvailableSize stretched_available{
        inner_width ? AvailableSpace::definite(*inner_width) : AvailableSpace::indefinite(),
        inner_height ? AvailableSpace::definite(*inner_height) : AvailableSpace::indefinite()};
    // A wrapped container can have one line. Its cross size still comes from
    // the items; align-content then distributes the remaining space.
    if (!wraps && lines.size() == 1) lines.front().cross = cross_size;
    double cross_free = wraps ? static_cast<double>(cross_size) - used_cross : 0;
    auto align_lines = alignment_fallback(
        node.style.align_content.value_or(ContentAlignment{}),
        cross_free, lines.size());
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
            main_occupied += item.target + item.margin.main_start
                + item.margin.main_end;
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
            const auto& child_style = item.node->style;
            const auto alignment = item.alignment;
            const auto cross_kind = (row ? child_style.box.size.height : child_style.box.size.width)
                .kind();
            Unit cross_margin_start = (cross_reverse ? item.margin.cross_end : item.margin.cross_start);
            Unit cross_margin_end = (cross_reverse ? item.margin.cross_start : item.margin.cross_end);
            Constraints used_constraints = used_item_constraints[item.source_index];
            if (!cross_available) {
                const auto used_space = AvailableSpace::definite(row ? *border_height : *border_width);
                if (row) used_constraints.available.height = used_space;
                else used_constraints.available.width = used_space;
            }
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
                    flex_axis(item.box->specified_minimum, row, false),
                    flex_axis(item.box->specified_maximum, row, false));
                used_constraints.available = stretched_available;
                if (row) { used_constraints.known_width = item.target; used_constraints.known_height = target_cross; }
                else { used_constraints.known_height = item.target; used_constraints.known_width = target_cross; }
            }
            fragment = engine.layout(*item.node, used_constraints, depth + 1);
            item.cross = flex_axis(fragment.size, row, false);
            if (fragment.root_layout().padding != item.box->padding()
                || fragment.root_layout().border != item.box->border()) {
                publish_item_box(fragment, *item.node, *item.box);
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
            const Unit main_start_margin = (main_reverse ? item.margin.main_end : item.margin.main_start)
                + (item.auto_main_start ? auto_share : 0);
            const Unit main_end_margin = (main_reverse ? item.margin.main_start : item.margin.main_end)
                + (item.auto_main_end ? auto_share : 0);
            auto& resolved_margin = fragment.mutable_root_layout().margin;
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
            engine.translate(fragment,
                row ? checked(static_cast<double>(content_x) + main_position, item.id)
                    : checked(static_cast<double>(content_x) + cross_position, item.id),
                row ? checked(static_cast<double>(content_y) + cross_position, item.id)
                    : checked(static_cast<double>(content_y) + main_position, item.id), item.id);
            engine.apply_relative_position(fragment, *item.node,
                Size{*inner_width, *inner_height}, node.style.box.direction, node.id);
            main_cursor += main_start_margin + item.target + main_end_margin + main_gap
                + distribution.extra_gap;
        }
        cross_cursor += line.cross + cross_gap + cross_distribution.extra_gap;
    }
}

template<bool IsRow>
Fragment FlexLayout<IsRow>::finish() {
    Size candidate{*border_width, *border_height};
    candidate = constrain_size(candidate, box);
    Fragment result;
    result.size = candidate;
    result.root = make_layout(node, candidate, box);
    if (!lines.empty()) {
        const auto& first_line = wrap_reverse ? lines.back() : lines.front();
        if (!first_line.items.empty()) {
            std::size_t selected = first_line.items.front();
            if (row) {
                for (const auto index : first_line.items) {
                    const auto& item = items[index];
                    const auto alignment = item.alignment;
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
            const auto& child_style = item.node->style;
            const auto& fragment = fragments[item.source_index];
            const Unit baseline = child_baseline(fragment, child_style)
                .value_or(fragment.size.height);
            result.mutable_root_layout().first_baseline = checked(
                static_cast<double>(fragment.root_layout().border_box.y)
                    + baseline, node.id);
        }
    }
    if (engine.detail_ == FragmentDetail::Full) result.reserve_children(fragments.size());
    for (std::size_t source = 0; source < fragments.size(); ++source) {
        const auto id = node.layout_children()[source];
        if (engine.get(id).style.position == Position::Absolute
            && engine.get(id).style.display != Display::None) {
            fragments[source] = engine.absolute(node, id, candidate, box, depth);
        }
        auto& fragment = fragments[source];
        if (engine.detail_ == FragmentDetail::Full) {
            engine.append_fragment(result, std::move(fragment));
        }
    }
    return result;
}

Fragment Engine::flex(const LayoutNode& node, const Constraints& constraints,
                      const LayoutBox& box, std::size_t depth) {
    if (flex_is_row(node.style.flex_direction)) {
        return FlexLayout<true>(*this, node, constraints, box, depth).run();
    }
    return FlexLayout<false>(*this, node, constraints, box, depth).run();
}

} // namespace axisly::css::detail
