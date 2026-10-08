#include "layout_engine.hpp"

#include <algorithm>

namespace axisly::css::detail {
namespace {

// A leaf only overrides dimensions for a query. Decoration stays in the
// node's resolved box, so intrinsic and final passes never copy all its edges.
struct LeafDimensions {
    ResolvedDimensions preferred, minimum, maximum;
    std::optional<Unit> aspect_ratio;
    LeafDimensions(const LayoutBox& box, const Constraints& constraints) {
        if (constraints.sizing_mode == SizingMode::Content) return;
        preferred = box.preferred;
        minimum = box.minimum;
        maximum = box.maximum;
        aspect_ratio = box.aspect_ratio;
        if (constraints.ignore_preferred_width) preferred.width.reset();
        if (constraints.ignore_preferred_height) preferred.height.reset();
        if (constraints.ignore_min_max_width) {
            minimum.width.reset();
            maximum.width.reset();
        }
        if (constraints.ignore_min_max_height) {
            minimum.height.reset();
            maximum.height.reset();
        }
    }
};

struct LeafMeasureAxis {
    std::optional<Unit> known;
    AvailableSpace available;
};

LeafMeasureAxis leaf_measurement_axis(AvailableSpace available,
    std::optional<Unit> requested, std::optional<Unit> minimum,
    std::optional<Unit> maximum, Unit margin, Unit decoration, NodeId node) {
    // Definite item/style dimensions replace the parent's available space.
    // Otherwise the parent contributes a margin-box budget for this leaf.
    Unit budget;
    if (requested) {
        if (!(*requested >= 0 && *requested <= std::numeric_limits<Unit>::max())) {
            return {{}, AvailableSpace::definite(*requested)}; // Preserve invalid-input diagnostics.
        }
        budget = *requested;
    } else if (available.kind == AvailableKind::Definite) {
        budget = std::max(Unit{0}, checked(static_cast<double>(available.value) - margin, node));
    } else return {{}, available};
    const Unit content = std::max(Unit{0}, checked(
        static_cast<double>(constrain_axis(budget, minimum, maximum)) - decoration, node));
    // A known border-box dimension supplies both fields of the measure
    // request. Resolve it once; checked() and the zero floor establish the
    // finite, nonnegative invariant required by definite available space.
    return {requested ? std::optional<Unit>(content) : std::nullopt,
        {AvailableKind::Definite, content}};
}

} // namespace

LeafSizingResult Engine::size_leaf(const LayoutNode& node, const Constraints& constraints,
                                   const LayoutBox& box, bool include_content) {
    // Content queries with both axes supplied already have their final size.
    // The parent has applied style bounds and ratio. A full leaf query still
    // measures painted content and baselines, using the fixed content budget.
    if (constraints.sizing_mode == SizingMode::Content
        && constraints.known_width && constraints.known_height) {
        const auto size = validate_border_size({*constraints.known_width, *constraints.known_height});
        if (!include_content || !has_measure(node)) return {size, {}, include_content};
        const Unit width = std::max(Unit{0}, size.width - box.horizontal_decoration());
        const Unit height = std::max(Unit{0}, size.height - box.vertical_decoration());
        const MeasureRequest request{width, height,
            {AvailableKind::Definite, width}, {AvailableKind::Definite, height}};
        return {size, measure(node, request), true};
    }
    const LeafDimensions dimensions(box, constraints);
    const auto decoration_width = box.horizontal_decoration();
    const auto decoration_height = box.vertical_decoration();
    const auto intrinsic = [&](const Length& value, bool horizontal) -> std::optional<Unit> {
        const auto kind = value.kind();
        if (kind == LengthKind::Auto || kind == LengthKind::Content
            || kind == LengthKind::Length || kind == LengthKind::Percent
            || kind == LengthKind::Calc) return std::nullopt;
        const Unit decoration = horizontal ? decoration_width : decoration_height;
        const auto available = horizontal ? constraints.available.width : constraints.available.height;
        if (kind == LengthKind::Stretch) {
            const auto definite = available.definite_value();
            if (!definite) return std::nullopt;
            const auto margin = edges(box.margin, node.id);
            const Unit outer_margin = horizontal
                ? checked(static_cast<double>(margin.left) + margin.right, node.id)
                : checked(static_cast<double>(margin.top) + margin.bottom, node.id);
            return std::max(decoration, *definite - outer_margin);
        }
        const auto probe = [&](AvailableKind query) {
            MeasureRequest request;
            request.available_width = constraints.available.width;
            request.available_height = constraints.available.height;
            auto& axis = horizontal ? request.available_width : request.available_height;
            axis = query == AvailableKind::MinContent
                ? AvailableSpace::min_content() : AvailableSpace::max_content();
            const auto result = measure(node, request);
            const Unit content = horizontal ? result.content_size.width : result.content_size.height;
            return checked(static_cast<double>(content) + decoration, node.id);
        };
        if (kind == LengthKind::MinContent) return probe(AvailableKind::MinContent);
        if (kind == LengthKind::MaxContent) return probe(AvailableKind::MaxContent);
        const Unit minimum = probe(AvailableKind::MinContent);
        const Unit maximum = probe(AvailableKind::MaxContent);
        std::optional<Unit> limit;
        if (kind == LengthKind::FitContent) limit = available.definite_value();
        else {
            limit = value.resolve_fit_content_limit(horizontal
                ? constraints.containing_width : constraints.containing_height);
            if (limit) {
                *limit = box.box_sizing == BoxSizing::ContentBox
                    ? checked(static_cast<double>(std::max(*limit, Unit{0})) + decoration, node.id)
                    : std::max(*limit, decoration);
            }
        }
        if (!limit) return maximum;
        return std::max(minimum, std::min(maximum, *limit));
    };
    auto width = constraints.known_width ? constraints.known_width : dimensions.preferred.width;
    auto height = constraints.known_height ? constraints.known_height : dimensions.preferred.height;
    if (!width && constraints.sizing_mode == SizingMode::Inherent
        && !constraints.ignore_preferred_width) {
        width = intrinsic(node.style.box.size.width, true);
    }
    if (!height && constraints.sizing_mode == SizingMode::Inherent
        && !constraints.ignore_preferred_height) {
        height = intrinsic(node.style.box.size.height, false);
    }
    if (!include_content && width && height) {
        LeafSizingResult result{};
        result.size = constrain_size(Size{*width, *height}, dimensions);
        if (dimensions.aspect_ratio) {
            result.size.height = std::max(result.size.height,
                checked(static_cast<double>(result.size.width) / *dimensions.aspect_ratio, node.id));
        }
        return result;
    }
    // Style dimensions have already transferred through the ratio in
    // resolve_box. A parent-known leaf axis does not make the other axis
    // definite before measurement; the ratio floors the measured height
    // after min/max clamping instead.
    Measurement measured;
    if (has_measure(node)) {
        const auto margin = edges(box.margin, node.id);
        const auto horizontal = leaf_measurement_axis(constraints.available.width,
            width, dimensions.minimum.width, dimensions.maximum.width,
            checked(static_cast<double>(margin.left) + margin.right, node.id),
            decoration_width, node.id);
        const auto vertical = leaf_measurement_axis(constraints.available.height,
            height, dimensions.minimum.height, dimensions.maximum.height,
            checked(static_cast<double>(margin.top) + margin.bottom, node.id),
            decoration_height, node.id);
        measured = measure(node, {horizontal.known, vertical.known,
            horizontal.available, vertical.available});
    }
    Size candidate{
        width ? *width : checked(static_cast<double>(measured.content_size.width) + decoration_width, node.id),
        height ? *height : checked(static_cast<double>(measured.content_size.height) + decoration_height, node.id),
    };
    candidate = constrain_size(candidate, dimensions);
    if (dimensions.aspect_ratio) {
        candidate.height = std::max(candidate.height,
            checked(static_cast<double>(candidate.width) / *dimensions.aspect_ratio,
                node.id));
    }
    return {candidate, measured, true};
}

Fragment Engine::leaf(const LayoutNode& node, const Constraints& constraints,
                      const LayoutBox& box) {
    const auto [candidate, measured, has_content] = size_leaf(node, constraints, box,
        detail_ != FragmentDetail::Size);
    Fragment fragment;
    fragment.size = candidate;
    fragment.root = make_layout(node, candidate, box,
        measured.first_baseline, measured.last_baseline);
    if (node.style.contain == Contain::Layout || node.style.contain == Contain::Content) {
        auto& root_layout = fragment.mutable_root_layout();
        root_layout.first_baseline.reset();
        root_layout.last_baseline.reset();
    }
    if (node.style.display == Display::Block) {
        const LeafDimensions dimensions(box, constraints);
        const auto margin = edges(box.margin, node.id);
        const auto decoration = box.vertical_decoration();
        const bool in_block_flow = node.parent &&
            (get(*node.parent).style.display == Display::Block
                || get(*node.parent).style.display == Display::FlowRoot);
        const bool through = in_block_flow && !has_measure(node)
            && node.style.float_side == FloatSide::None
            && !node.style.item_is_table && !node.style.box.replaced
            && node.style.contain == Contain::None
            && node.style.overflow_x != Overflow::Hidden
            && node.style.overflow_x != Overflow::Scroll
            && node.style.overflow_y != Overflow::Hidden
            && node.style.overflow_y != Overflow::Scroll
            && !constraints.known_height && !dimensions.preferred.height
            && !dimensions.minimum.height.value_or(Unit{0})
            && decoration == 0 && fragment.size.height == 0;
        fragment.block_margins = BlockMargins{
            MarginStrut::from(margin.top), MarginStrut::from(margin.bottom),
            through};
    }
    if (!has_content) return fragment;
    // Intrinsic Block/Flex layout may leave percentage edges unresolved for
    // the reported box while measuring the leaf with a definite available
    // width. Taffy's leaf scrollable extent uses that later width.
    auto padding = box.padding();
    if (node.box_depends_on_width && !constraints.containing_width
        && constraints.available.width.definite_value()) {
        padding = resolve_layout_box(node,
            constraints.available.width.definite_value(),
            constraints.containing_height).padding();
    }
    const bool scroll_container = node.style.overflow_x == Overflow::Hidden
        || node.style.overflow_x == Overflow::Scroll
        || node.style.overflow_y == Overflow::Hidden
        || node.style.overflow_y == Overflow::Scroll;
    const Unit inline_start = node.style.box.direction == Direction::Rtl
        ? padding.right : padding.left;
    const Unit inline_end = node.style.box.direction == Direction::Rtl
        ? padding.left : padding.right;
    const Size scrollable_content = measured.scrollable_content_size
        .value_or(measured.content_size);
    fragment.mutable_root_layout().scrollable_overflow_rect = {
        0, 0,
        checked(static_cast<double>(inline_start) + scrollable_content.width
            + (scroll_container ? inline_end : 0), node.id),
        checked(static_cast<double>(padding.top) + scrollable_content.height
            + (scroll_container ? padding.bottom : 0), node.id)};
    return fragment;
}

} // namespace axisly::css::detail
