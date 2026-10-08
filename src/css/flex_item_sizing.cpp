#include "flex_internal.hpp"
#include "sizing_keywords.hpp"

#include <algorithm>

namespace axisly::css::detail {
AvailableSpace divided_flex_cross_space(const LayoutNode& node,
    AvailableSpace available, std::optional<Unit> percentage_basis) {
    if (node.style.flex_wrap == FlexWrap::NoWrap || node.style.flex_line_count <= 1
        || available.kind != AvailableKind::Definite) return available;
    const auto& gap_style = flex_is_row(node.style.flex_direction)
        ? node.style.gap.height : node.style.gap.width;
    const Unit gap = gap_style.resolve_numeric(percentage_basis.value_or(Unit{0})).value_or(Unit{0});
    const auto count = node.style.flex_line_count;
    return AvailableSpace::definite(std::max(Unit{0}, checked(
        (static_cast<double>(available.value) - (count - 1.0) * gap) / count, node.id)));
}

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


namespace {
const LayoutBox& flex_item_box(const LayoutNode& node, const LayoutNode& child,
    const FlexItemSizingInput& input, std::pmr::vector<LayoutBox>& owned_boxes) {
    const bool row = flex_is_row(node.style.flex_direction);
    const auto inner_width = input.inner.width, inner_height = input.inner.height;
    const bool main_size_is_definite = input.main_size_is_definite;
    const auto id = child.id;
    const auto& shared = resolve_layout_box(child, inner_width, inner_height);
    // Without percentage dependencies or ratio transfer, later child queries
    // cannot change this box. Borrow it throughout the Flex pass. Stage-specific
    // boxes remain owned by the workspace and reserve once before any pointers
    // to them escape, including when topology has grown since the last pass.
    if (!child.box_depends_on_width && !child.box_depends_on_height && !shared.aspect_ratio) return shared;
    if (owned_boxes.empty()) owned_boxes.reserve(node.layout_children().size());
    auto& child_box = owned_boxes.emplace_back(shared);
    try {
        if (!main_size_is_definite
            && (row ? child.box_depends_on_width : child.box_depends_on_height)) {
            // Percentage dimensions use a definite main axis; percentage
            // edges continue to use the actual containing inline width.
            auto dimensions = child_box;
            dimensions.scrollbar = {};
            resolve_box_dimensions(dimensions, child.style.box,
                row ? std::nullopt : inner_width, row ? inner_height : std::nullopt);
            child_box.preferred = dimensions.preferred;
            child_box.specified_minimum = dimensions.specified_minimum;
            child_box.specified_maximum = dimensions.specified_maximum;
        }
    }
    catch (const std::invalid_argument& error) {
        throw LayoutFailure(LayoutErrorCode::InvalidStyle, id, error.what());
    }
    // Flex resolves raw min/max styles to border-box values first. Its
    // hypothetical-size ratio transfer then uses those border-box axes,
    // while preferred style sizes transfer before content-box decoration.
    child_box.minimum = child_box.specified_minimum;
    child_box.maximum = child_box.specified_maximum;
    if (child_box.aspect_ratio) {
        auto item_ratio_box = child_box;
        item_ratio_box.box_sizing = BoxSizing::BorderBox;
        child_box.minimum = transfer_aspect_ratio(child_box.minimum, item_ratio_box);
        child_box.maximum = transfer_aspect_ratio(child_box.maximum, item_ratio_box);
    }
    return child_box;
}
} // namespace

// Separate the common numeric preparation from intrinsic content queries.
// Definite bases and explicit minima do not need a measurement workspace.
template<bool IsRow>
class FlexItemSizer {
public:
    FlexItemSizer(Engine& engine, FlexItem& item, const LayoutBox& box,
        const LayoutNode& node, const LayoutNode& child, const Constraints& constraints,
        const FlexItemSizingInput& input, std::size_t depth)
        : engine(engine), item(item), child_box(box), node(node), child(child),
          constraints(constraints), input(input), depth(depth), id(child.id),
          source(input.source_index),
          inner_width(input.inner.width), inner_height(input.inner.height),
          main_available(row ? inner_width : inner_height),
          cross_available(row ? inner_height : inner_width), main_query(input.main_query),
          main_size_is_definite(input.main_size_is_definite),
          main_reverse(input.main_reverse), cross_reverse(input.cross_reverse),
          cross_size_style(row ? child.style.box.size.height : child.style.box.size.width) {}
    void run();
private:
    struct Sizes { Unit basis, minimum, content; bool definite; };
    Engine& engine;
    FlexItem& item;
    const LayoutBox& child_box;
    const LayoutNode& node;
    const LayoutNode& child;
    const Constraints& constraints;
    const FlexItemSizingInput& input;
    std::size_t depth;
    NodeId id;
    std::size_t source;
    static constexpr bool row = IsRow;
    std::optional<Unit> inner_width, inner_height, main_available, cross_available;
    AvailableKind main_query;
    bool main_size_is_definite, main_reverse, cross_reverse;
    const Length& cross_size_style;
    ItemAlignment cross_alignment;
    std::optional<Unit> cross_preferred, main_preferred;
    bool cross_auto_margin{}, content_based_cross_width{}, intrinsic_contribution{};
    Unit main_decoration{}, cross_outer_margin{};
    Insets margin;
    void initialize();
    [[nodiscard]] Sizes resolve_content(std::optional<Unit> basis, bool definite,
                                       std::optional<Unit> minimum);
    void publish(Sizes sizes);
};

template<bool IsRow>
void FlexItemSizer<IsRow>::initialize() {
    item.box = &child_box;
    cross_alignment = child.style.align_self.value_or(
        node.style.align_items.value_or(ItemAlignment{}));
    cross_preferred = flex_axis(child_box.preferred, row, false);
    main_preferred = flex_axis(child_box.preferred, row, true);
    cross_auto_margin = (row
        ? child_box.margin.top.kind == EdgeKind::Auto
            || child_box.margin.bottom.kind == EdgeKind::Auto
        : child_box.margin.left.kind == EdgeKind::Auto
            || child_box.margin.right.kind == EdgeKind::Auto);
    content_based_cross_width = !row
        && (child.style.display == Display::Block
            || child.style.display == Display::FlowRoot)
        && cross_size_style.kind() == LengthKind::Auto
        && !cross_preferred
        && (cross_alignment.position != ItemPosition::Stretch
            || cross_auto_margin);
    main_decoration = row
        ? child_box.horizontal_padding_border() : child_box.vertical_padding_border();
    margin = edges(child_box.margin, id);
    cross_outer_margin = checked(static_cast<double>(
        flex_axis(margin, row, false, true))
        + flex_axis(margin, row, false, false), id);
    intrinsic_contribution = !main_available && !main_preferred
        && (main_query == AvailableKind::MaxContent
            || (main_query == AvailableKind::MinContent
                && node.style.flex_wrap == FlexWrap::NoWrap));
}

template<bool IsRow>
void FlexItemSizer<IsRow>::run() {
    initialize();
    std::optional<Unit> basis;
    const auto kind = child.style.flex_basis.kind();
    if (kind == LengthKind::Length || kind == LengthKind::Percent || kind == LengthKind::Calc) {
        if (const auto resolved = child.style.flex_basis.resolve_numeric(
                main_size_is_definite ? main_available : std::nullopt)) {
            basis = child.style.box.box_sizing == BoxSizing::ContentBox
                ? checked(static_cast<double>(std::max(*resolved, Unit{0})) + main_decoration, id)
                : std::max(*resolved, main_decoration);
        } else basis = main_preferred;
    } else if (kind == LengthKind::Auto) basis = main_preferred;
    auto minimum = flex_axis(child_box.specified_minimum, row, true);
    const auto overflow = row ? child.style.overflow_x : child.style.overflow_y;
    if (!minimum && (overflow == Overflow::Hidden || overflow == Overflow::Scroll)) minimum = Unit{0};
    if (basis && minimum && !child_box.aspect_ratio && !intrinsic_contribution) {
        const auto base = std::max(*basis, main_decoration);
        publish({base, *minimum, base, true});
    } else publish(resolve_content(basis, basis.has_value(), minimum));
}

template<bool IsRow>
typename FlexItemSizer<IsRow>::Sizes FlexItemSizer<IsRow>::resolve_content(std::optional<Unit> resolved_basis,
    bool basis_is_definite, std::optional<Unit> minimum) {
    std::optional<Constraints> query_storage;
    const auto content_constraints = [&]() -> const Constraints& {
        if (query_storage) return *query_storage;
        auto& child_constraints = query_storage.emplace();
        child_constraints.sizing_mode = SizingMode::Content;
        child_constraints.content_based_width = content_based_cross_width;
        // Main-axis percentages must not contribute to an intrinsic main size.
        // Item styles were resolved above; content queries retain only the cross
        // containing block and available-space constraint.
        child_constraints.containing_width = row ? std::nullopt : inner_width;
        child_constraints.containing_height = row ? inner_height : std::nullopt;
        child_constraints.available.width = inner_width
            ? AvailableSpace::definite(*inner_width) : input.available_inner.width;
        child_constraints.available.height = inner_height
            ? AvailableSpace::definite(*inner_height) : input.available_inner.height;
        const auto base_space = main_query == AvailableKind::MinContent
            ? AvailableSpace::min_content() : AvailableSpace::max_content();
        if (row) child_constraints.available.width = base_space;
        else child_constraints.available.height = base_space;
        auto& child_cross_space = row ? child_constraints.available.height
            : child_constraints.available.width;
        child_cross_space = divided_flex_cross_space(node, child_cross_space, cross_available);
        child_cross_space = cross_space_for_item(child_cross_space,
            flex_axis(child_box.minimum, row, false),
            flex_axis(child_box.maximum, row, false), cross_outer_margin,
            CrossSpaceStage::FlexBase, id);
        if (const auto keyword = sizing_keyword(cross_size_style,
                cross_available ? std::optional<Unit>{*cross_available - cross_outer_margin} : std::nullopt,
                cross_available); keyword && std::holds_alternative<AvailableSpace>(*keyword)) {
            child_cross_space = std::get<AvailableSpace>(*keyword);
        }
        auto known_cross = cross_preferred;
        if (known_cross) {
            *known_cross = constrain_axis(*known_cross,
                flex_axis(child_box.minimum, row, false),
                flex_axis(child_box.maximum, row, false));
        } else if (cross_alignment.position == ItemPosition::Stretch && !cross_auto_margin) {
            if (const auto available = child_cross_space.definite_value()) {
                known_cross = std::max(Unit{0}, *available - cross_outer_margin);
            }
        }
        if (row) child_constraints.known_height = known_cross;
        else child_constraints.known_width = known_cross;
        return child_constraints;
    };
    // Definite flex bases do not require an intrinsic measurement. Keep the
    // content query lazy: only an unresolved basis or an intrinsic container
    // contribution depends on it. Cross sizing happens after flex resolution.
    std::optional<Size> base_size;
    const auto content_size = [&]() -> Size {
        if (!base_size) base_size = engine.layout_size(child, content_constraints(), depth + 1);
        return *base_size;
    };
    const auto flex_basis = child.style.flex_basis.kind();
    const auto main_percentage_basis = main_size_is_definite ? main_available : std::nullopt;
    const Unit main_margin = checked(static_cast<double>(flex_axis(margin, row, true, true))
        + flex_axis(margin, row, true, false), id);
    const auto stretch = main_percentage_basis
        ? std::optional<Unit>{*main_percentage_basis - main_margin} : std::nullopt;
    const auto measure_main = [&](AvailableSpace main_space) {
        auto query = content_constraints();
        if (row) query.available.width = main_space;
        else query.available.height = main_space;
        return flex_axis(engine.layout_size(child, query, depth + 1), row, true);
    };
    const auto use_keyword = [&](const Length& style) {
        const auto keyword = sizing_keyword(style, stretch, main_percentage_basis);
        if (!keyword) return false;
        if (const auto exact = std::get_if<Unit>(&*keyword)) {
            resolved_basis = *exact;
            basis_is_definite = true;
        } else {
            resolved_basis = measure_main(std::get<AvailableSpace>(*keyword));
        }
        return true;
    };
    if (!resolved_basis) {
        if (flex_basis == LengthKind::Length || flex_basis == LengthKind::Percent
            || flex_basis == LengthKind::Calc) {
            const auto resolved = child.style.flex_basis.resolve_numeric(main_percentage_basis);
            if (resolved) {
                basis_is_definite = true;
                resolved_basis = child.style.box.box_sizing == BoxSizing::ContentBox
                    ? checked(static_cast<double>(std::max(*resolved, Unit{0})) + main_decoration, id)
                    : std::max(*resolved, main_decoration);
            } else if (main_preferred) {
                resolved_basis = *main_preferred;
                basis_is_definite = true;
            } else use_keyword(row ? child.style.box.size.width : child.style.box.size.height);
        } else if (flex_basis != LengthKind::Auto) {
            use_keyword(child.style.flex_basis);
        } else if (main_preferred) {
            resolved_basis = *main_preferred;
            basis_is_definite = true;
        } else {
            use_keyword(row ? child.style.box.size.width : child.style.box.size.height);
        }
    }
    if (child_box.aspect_ratio && cross_available
        && !main_preferred && !cross_preferred
        && cross_size_style.kind() == LengthKind::Auto
        && (flex_basis == LengthKind::Auto || flex_basis == LengthKind::Content)
        && node.style.flex_wrap == FlexWrap::NoWrap
        && cross_alignment.position == ItemPosition::Stretch
        && !cross_auto_margin) {
        const Unit cross_margin = checked(static_cast<double>(
            flex_axis(margin, row, false, true))
            + flex_axis(margin, row, false, false), id);
        const auto transferred_max = transfer_aspect_ratio(
            child_box.maximum, child_box);
        const auto cross_limit = [&](std::optional<Unit> limit) -> std::optional<Unit> {
            return limit ? std::optional<Unit>(checked(
                static_cast<double>(*limit) + cross_margin, id)) : std::nullopt;
        };
        const Unit constrained_cross_space = constrain_axis(*cross_available,
            cross_limit(flex_axis(child_box.minimum, row, false)),
            cross_limit(flex_axis(transferred_max, row, false)));
        const Unit stretched_cross = std::max(Unit{0}, checked(
            static_cast<double>(constrained_cross_space) - cross_margin, id));
        auto item_ratio_box = child_box;
        item_ratio_box.box_sizing = BoxSizing::BorderBox;
        const auto transferred = transfer_aspect_ratio(row
            ? ResolvedDimensions{std::nullopt, stretched_cross}
            : ResolvedDimensions{stretched_cross, std::nullopt}, item_ratio_box);
        if (const auto value = flex_axis(transferred, row, true)) resolved_basis = *value;
        basis_is_definite = true;
    }
    if (!minimum) {
        const auto overflow = row ? child.style.overflow_x : child.style.overflow_y;
        if (overflow == Overflow::Hidden || overflow == Overflow::Scroll) {
            minimum = Unit{0};
        } else {
            auto min_constraints = content_constraints();
            min_constraints.ignore_min_max_width = false;
            min_constraints.ignore_min_max_height = false;
            if (row) {
                min_constraints.available.width = AvailableSpace::min_content();
                min_constraints.ignore_preferred_width = true;
            } else {
                min_constraints.available.height = AvailableSpace::min_content();
                min_constraints.ignore_preferred_height = true;
            }
            const auto intrinsic = engine.layout_size(child, min_constraints, depth + 1);
            minimum = flex_axis(intrinsic, row, true);
            if (const auto suggested = flex_axis(child_box.preferred, row, true)) {
                minimum = std::min(*minimum, *suggested);
            }
            if (const auto transferred_max = flex_axis(child_box.maximum, row, true)) {
                minimum = std::min(*minimum, *transferred_max);
            }
            minimum = std::max(*minimum, main_decoration);
        }
    }
    const Unit basis = std::max(resolved_basis ? *resolved_basis
        : flex_axis(content_size(), row, true), main_decoration);
    Unit content_main = intrinsic_contribution ? flex_axis(content_size(), row, true) : basis;
    if (child_box.aspect_ratio) {
        const auto& query = content_constraints();
        const auto known_cross = row ? query.known_height : query.known_width;
        if (known_cross) {
            const Unit transferred = row ? *known_cross * *child_box.aspect_ratio
                : *known_cross / *child_box.aspect_ratio;
            content_main = std::max(content_main, transferred);
        }
    }
    return {basis, minimum.value_or(Unit{0}), content_main, basis_is_definite};
}

template<bool IsRow>
void FlexItemSizer<IsRow>::publish(Sizes sizes) {
    const auto maximum = flex_axis(child_box.specified_maximum, row, true);
    const auto& margin_start = row ? child_box.margin.left : child_box.margin.top;
    const auto& margin_end = row ? child_box.margin.right : child_box.margin.bottom;
    const auto& cross_start = row ? child_box.margin.top : child_box.margin.left;
    const auto& cross_end = row ? child_box.margin.bottom : child_box.margin.right;
    const bool scroll_container = child.style.overflow_x == Overflow::Hidden
        || child.style.overflow_x == Overflow::Scroll
        || child.style.overflow_y == Overflow::Hidden
        || child.style.overflow_y == Overflow::Scroll;
    const auto basis = sizes.basis;
    item.id = id;
    item.source_index = source;
    item.margin = {flex_axis(margin, row, true, true), flex_axis(margin, row, true, false),
        flex_axis(margin, row, false, true), flex_axis(margin, row, false, false)};
    item.alignment = cross_alignment;
    item.node = &child;
    item.base = basis;
    item.inner_base = checked(static_cast<double>(basis) - main_decoration, id);
    item.main_decoration = main_decoration;
    item.content_main = sizes.content;
    item.target = basis;
    item.minimum = sizes.minimum;
    item.maximum = maximum;
    const auto hypothetical_minimum = std::max(item.minimum,
        flex_axis(child_box.minimum, row, true).value_or(Unit{0}));
    const auto hypothetical_maximum = flex_axis(transfer_aspect_ratio(
        child_box.maximum, child_box), row, true);
    item.hypothetical = constrain_axis(basis, hypothetical_minimum, hypothetical_maximum);
    item.grow = child.style.flex_grow;
    item.shrink = child.style.flex_shrink;
    item.auto_main_start = (main_reverse ? margin_end : margin_start).kind == EdgeKind::Auto;
    item.auto_main_end = (main_reverse ? margin_start : margin_end).kind == EdgeKind::Auto;
    item.auto_cross_start = (cross_reverse ? cross_end : cross_start).kind == EdgeKind::Auto;
    item.auto_cross_end = (cross_reverse ? cross_start : cross_end).kind == EdgeKind::Auto;
    item.content_based_cross_width = content_based_cross_width;
    item.main_size_is_definite = main_size_is_definite || sizes.definite;
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
}

void Engine::prepare_flex_item(FlexItem& item, std::pmr::vector<LayoutBox>& item_boxes,
    const LayoutNode& node, const LayoutNode& child, const Constraints& constraints,
    const FlexItemSizingInput& input, std::size_t depth) {
    const auto& box = flex_item_box(node, child, input, item_boxes);
    if (flex_is_row(node.style.flex_direction)) {
        FlexItemSizer<true>(*this, item, box, node, child, constraints, input, depth).run();
    } else {
        FlexItemSizer<false>(*this, item, box, node, child, constraints, input, depth).run();
    }
}
} // namespace axisly::css::detail
