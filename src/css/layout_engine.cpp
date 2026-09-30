#include "layout_engine.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <utility>

namespace axisly::css::detail {

[[noreturn]] void layout_numeric_overflow(NodeId id) {
    throw LayoutFailure(LayoutErrorCode::NumericOverflow, id, "CSS layout numeric overflow");
}

[[noreturn]] void unresolved_layout_edge(NodeId id) {
    throw LayoutFailure(LayoutErrorCode::UnresolvedPercentage, id,
        "CSS percentage edge has no definite containing inline size");
}

namespace {
bool suppresses_baseline(Contain contain) noexcept {
    return contain == Contain::Layout || contain == Contain::Content;
}

LayoutCacheKey cache_key_for(const LayoutNode& node, Constraints key, FragmentDetail detail, std::size_t depth) {
    key.detail = detail;
    // Only percentage-dependent properties use the containing block in the key.
    if (!node.box_depends_on_width) key.containing_width.reset();
    if (!node.box_depends_on_height) key.containing_height.reset();
    if (!key.known_width) key.known_width_is_definite = true;
    if (!key.known_height) key.known_height_is_definite = true;
    if ((node.children.empty() && key.known_width && key.known_height
            && (!node.box_depends_on_width || key.containing_width))
        || (!node.children.empty() && key.sizing_mode == SizingMode::Content
            && (node.style.display == Display::Flex || node.style.display == Display::Grid))) {
        if (key.known_width) key.available.width = AvailableSpace::definite(*key.known_width);
        if (key.known_height) key.available.height = AvailableSpace::definite(*key.known_height);
    }
    // The same subtree can be computed from different roots. Reusing a
    // shallower query must not bypass the tree's recursive depth budget.
    return LayoutCacheKey::from(key, depth);
}

class ScopedDetail final {
public:
    ScopedDetail(FragmentDetail& detail, FragmentDetail requested)
        : detail_(detail), previous_(std::exchange(detail, requested)) {}
    ~ScopedDetail() { detail_ = previous_; }
    ScopedDetail(const ScopedDetail&) = delete;
    ScopedDetail& operator=(const ScopedDetail&) = delete;
private:
    FragmentDetail& detail_;
    FragmentDetail previous_;
};
} // namespace

void LayoutNode::reset_style_cache() {
    resolved_box.reset();
    const auto dependent = [](const Length& value) {
        return value.kind() == LengthKind::Percent
            || value.kind() == LengthKind::FitContentPercent
            || value.kind() == LengthKind::Calc;
    };
    const auto& box = style.box;
    box_depends_on_width = dependent(box.size.width) || dependent(box.min_size.width)
        || dependent(box.max_size.width);
    box_depends_on_height = dependent(box.size.height) || dependent(box.min_size.height)
        || dependent(box.max_size.height);
    for (const auto* edges : {&box.margin, &box.padding, &box.border}) {
        box_depends_on_width |= dependent(edges->left) || dependent(edges->right)
            || dependent(edges->top) || dependent(edges->bottom);
    }
}

MarginStrut MarginStrut::from(Unit value) {
    return {std::max(Unit{0}, value), std::min(Unit{0}, value)};
}

MarginStrut MarginStrut::merged(MarginStrut other) const {
    return {std::max(positive, other.positive),
            std::min(negative, other.negative)};
}

Unit MarginStrut::resolve(NodeId node) const {
    return checked(static_cast<double>(positive) + negative, node);
}

ResolvedDimensions requested_size(const Constraints& constraints,
                                  const ResolvedBox& box) {
    // Style dimensions have already applied content-box sizing in resolve_box.
    // Parent-supplied dimensions have algorithm-specific ratio handling.
    return {
        constraints.known_width ? constraints.known_width : box.preferred.width,
        constraints.known_height ? constraints.known_height : box.preferred.height,
    };
}

void update_layout_box_cache(const LayoutNode& node,
                               std::optional<Unit> containing_width,
                               std::optional<Unit> containing_height) {
    const auto width_key = node.box_depends_on_width ? containing_width : std::nullopt;
    const auto height_key = node.box_depends_on_height ? containing_height : std::nullopt;
    auto resolved = resolve_box(node.style.box, containing_width, containing_height,
        IndefiniteEdgeBasis::Zero);
    node.resolved_box = LayoutNode::BoxCache{width_key, height_key, resolved};
}

NodeLayout make_layout(const LayoutNode& node, Size size, const ResolvedBox& resolved,
                       std::optional<Unit> first_baseline,
                       std::optional<Unit> last_baseline) {
    const auto margin = edges(resolved.margin, node.id);
    const auto padding = edges(resolved.padding, node.id);
    const auto border = edges(resolved.border, node.id);
    const Unit left = content_origin_x(resolved, border, padding, node.id);
    const Unit top = padding.top + border.top;
    const Unit width = std::max(Unit{0}, content_axis(size.width,
        padding.left, padding.right, border.left, border.right) - resolved.scrollbar.width);
    const Unit height = std::max(Unit{0}, content_axis(size.height,
        padding.top, padding.bottom, border.top, border.bottom) - resolved.scrollbar.height);
    return {node.id, node.parent, {0, 0, size.width, size.height},
        {left, top, width, height}, margin, padding, border, border,
        resolved.scrollbar,
        first_baseline ? std::optional<Unit>(*first_baseline + top) : std::nullopt,
        last_baseline ? std::optional<Unit>(*last_baseline + top) : std::nullopt};
}

void set_fragment_border_size(Fragment& fragment, Size size, const ResolvedBox& box) {
    fragment.size = size;
    auto& layout = fragment.root_layout();
    layout.border_box.width = size.width;
    layout.border_box.height = size.height;
    const auto padding = edges(box.padding, layout.node);
    const auto border = edges(box.border, layout.node);
    layout.content_box.width = std::max(Unit{0}, content_axis(size.width,
        padding.left, padding.right, border.left, border.right) - box.scrollbar.width);
    layout.content_box.height = std::max(Unit{0}, content_axis(size.height,
        padding.top, padding.bottom, border.top, border.bottom) - box.scrollbar.height);
}

void publish_item_box(Fragment& fragment, const LayoutNode& node, const ResolvedBox& box) {
    auto& root = fragment.root_layout();
    const auto padding = edges(box.padding, node.id);
    const auto border = edges(box.border, node.id);
    root.padding = padding;
    root.border = border;
    root.content_box = {content_origin_x(box, border, padding, node.id),
        padding.top + border.top,
        std::max(Unit{0}, content_axis(fragment.size.width,
            padding.left, padding.right, border.left, border.right) - box.scrollbar.width),
        std::max(Unit{0}, content_axis(fragment.size.height,
            padding.top, padding.bottom, border.top, border.bottom) - box.scrollbar.height)};
    // The child's late flow resolution owns its overflow origin and baseline.
    // Item box reporting belongs to the parent's earlier sizing stage.
}

void translate(Fragment& fragment, Unit x, Unit y, NodeId node) {
    if (x == 0 && y == 0) return;
    auto move = [&](NodeLayout& layout) {
        layout.border_box.x = checked(static_cast<double>(layout.border_box.x) + x, node);
        layout.border_box.y = checked(static_cast<double>(layout.border_box.y) + y, node);
        layout.content_box.x = checked(static_cast<double>(layout.content_box.x) + x, node);
        layout.content_box.y = checked(static_cast<double>(layout.content_box.y) + y, node);
    };
    if (fragment.root) move(*fragment.root);
    fragment.translate_descendants(x, y, node);
}

Engine::Engine(const LayoutNodes& nodes, TreeLimits limits, LayoutCache& cache)
    : nodes_(nodes), cache_(cache), limits_(limits) {}

bool Engine::has_float_subtree(NodeId id, std::size_t depth) {
    if (depth > limits_.maximum_depth) {
        throw LayoutFailure(LayoutErrorCode::LimitExceeded, id,
            "CSS layout depth exceeded");
    }
    if (const auto cached = float_subtree_.find(id.value);
        cached != float_subtree_.end()) return cached->second;
    const auto& node = get(id);
    bool has_float = node.style.display != Display::None
        && node.style.float_side != FloatSide::None;
    if (node.style.display != Display::None) {
        for (const auto child : node.children) {
            if (has_float_subtree(child, depth + 1)) {
                has_float = true;
                break;
            }
        }
    }
    float_subtree_.emplace(id.value, has_float);
    return has_float;
}

LayoutResult Engine::compute(NodeId root, AvailableSize available) {
    LayoutResult result;
    try {
        Constraints constraints;
        constraints.available = available;
        constraints.containing_width = available.width.definite_value();
        constraints.containing_height = available.height.definite_value();
        const auto& root_node = get(root);
        cache_.reserve_hint(root_node.children.size() + 1);
        if (root_node.style.display == Display::Block
            || root_node.style.display == Display::FlowRoot) {
            const auto root_box = resolve_layout_box(root_node,
                constraints.containing_width, constraints.containing_height);
            const auto margin = edges(root_box.margin, root);
            const auto padding = edges(root_box.padding, root);
            const auto border = edges(root_box.border, root);
            const Unit minimum_border_width = checked(static_cast<double>(padding.left)
                + padding.right + border.left + border.right, root);
            const Unit minimum_border_height = checked(static_cast<double>(padding.top)
                + padding.bottom + border.top + border.bottom, root);
            const auto known_axis = [](std::optional<Unit> preferred,
                                       std::optional<Unit> minimum,
                                       std::optional<Unit> maximum,
                                       std::optional<Unit> available_axis,
                                       Unit decoration) -> std::optional<Unit> {
                // Root Block sizing picks an overconstrained min/max pair
                // before the styled size, then falls back to available width.
                // The selected dimensions are passed to Block as parent-known
                // border-box sizes and are not clamped a second time there.
                if (minimum && maximum && *maximum <= *minimum) {
                    return std::max(*minimum, decoration);
                }
                if (preferred) {
                    return std::max(constrain_axis(*preferred, minimum, maximum),
                        decoration);
                }
                if (available_axis) return std::max(*available_axis, decoration);
                return std::nullopt;
            };
            const std::optional<Unit> available_border_width =
                available.width.kind == AvailableKind::Definite
                    ? std::optional<Unit>(std::max(Unit{0}, checked(
                        static_cast<double>(available.width.value)
                            - margin.left - margin.right, root)))
                    : std::nullopt;
            constraints.known_width = known_axis(root_box.preferred.width,
                root_box.minimum.width, root_box.maximum.width,
                available_border_width, minimum_border_width);
            constraints.known_height = known_axis(root_box.preferred.height,
                root_box.minimum.height, root_box.maximum.height,
                std::nullopt, minimum_border_height);
            constraints.known_width_is_final = constraints.known_width.has_value();
            constraints.known_height_is_final = constraints.known_height.has_value();
        }
        auto fragment = layout(root, constraints, 1);
        if (get(root).style.box.direction == Direction::Rtl
            && available.width.kind == AvailableKind::Definite) {
            const Unit root_x = checked(static_cast<double>(available.width.value)
                - fragment.size.width, root);
            translate(fragment, root_x, 0, root);
        }
        result.size = fragment.size;
        result.nodes = std::move(fragment).take_nodes();
        result.grid_tracks = std::move(fragment.grid_tracks);
        compute_scrollable_overflow(result.nodes, nodes_);
    } catch (const LayoutFailure& error) {
        result.error = {error.code, error.node, error.what()};
    } catch (const std::overflow_error& error) {
        result.error = {LayoutErrorCode::NumericOverflow, root, error.what()};
    } catch (const std::length_error& error) {
        result.error = {LayoutErrorCode::LimitExceeded, root, error.what()};
    }
    result.measurement_count = measurements_;
    result.layout_count = layouts_;
    result.cache_hits = hits_;
    result.cache_hit = root_hit_;
    return result;
}

Fragment Engine::layout(NodeId id, Constraints constraints, std::size_t depth,
                        BlockContext* block_context) {
    if (depth > limits_.maximum_depth) {
        throw LayoutFailure(LayoutErrorCode::LimitExceeded, id, "CSS layout depth exceeded");
    }
    const auto& node = get(id);
    const bool trivial_size = detail_ == FragmentDetail::Size
        && ((constraints.known_width && constraints.known_height)
            || (node.children.empty() && !node.measure));
    const bool cacheable = !block_context && !trivial_size;
    const auto cache_key = cache_key_for(node, constraints, detail_, depth);
    if (cacheable) {
        // Query stages have distinct semantics: a full Grid layout can rerun
        // tracks after the size-query exit, changing its intrinsic result.
        // Cache only the requested stage, even when richer output exists.
        if (const auto* hit = cache_.find(node.cache, cache_key)) {
            ++hits_;
            if (depth == 1) root_hit_ = true;
            return *hit;
        }
    }
    ++layouts_;
    Fragment fragment;
    if (node.style.display == Display::None) {
        fragment = hidden(node, depth);
        if (cacheable) cache_.stage(node.cache, cache_key, fragment);
        return fragment;
    }
    auto box = query_box(node, constraints);
    if (node.children.empty()) {
        fragment = leaf(node, constraints, box);
        if (suppresses_baseline(node.style.contain)) {
            fragment.root_layout().first_baseline.reset();
            fragment.root_layout().last_baseline.reset();
        }
        if (node.style.display == Display::Block) {
            const auto margin = edges(box.margin, node.id);
            const auto decoration = box.vertical_decoration();
            const bool in_block_flow = node.parent &&
                (get(*node.parent).style.display == Display::Block
                    || get(*node.parent).style.display == Display::FlowRoot);
            const bool through = in_block_flow && !node.measure
                && node.style.float_side == FloatSide::None
                && !node.style.item_is_table && !node.style.box.replaced
                && node.style.contain == Contain::None
                && node.style.overflow_x != Overflow::Hidden
                && node.style.overflow_x != Overflow::Scroll
                && node.style.overflow_y != Overflow::Hidden
                && node.style.overflow_y != Overflow::Scroll
                && !constraints.known_height && !box.preferred.height
                && !box.minimum.height.value_or(Unit{0})
                && decoration && *decoration == 0 && fragment.size.height == 0;
            fragment.block_margins = BlockMargins{
                MarginStrut::from(margin.top), MarginStrut::from(margin.bottom),
                through};
        }
        if (cacheable) cache_.stage(node.cache, cache_key, fragment);
        return fragment;
    }
    if (node.parent) {
        if (const auto intrinsic = intrinsic_container_size(node, constraints, box, depth, true)) {
            box.preferred.width = *intrinsic;
        }
        if (const auto intrinsic = intrinsic_container_size(node, constraints, box, depth, false)) {
            box.preferred.height = *intrinsic;
        }
    }
    switch (node.style.display) {
    case Display::Block:
    case Display::FlowRoot:
        fragment = block(node, constraints, box, depth, block_context); break;
    case Display::Flex: fragment = flex(node, constraints, box, depth); break;
    case Display::Grid: fragment = grid(node, constraints, box, depth); break;
    case Display::None: break;
    default:
        throw LayoutFailure(LayoutErrorCode::InvalidStyle, id,
            "CSS display value is invalid");
    }
    if (suppresses_baseline(node.style.contain)) {
        fragment.root_layout().first_baseline.reset();
        fragment.root_layout().last_baseline.reset();
    }
    if (cacheable) cache_.stage(node.cache, cache_key, fragment);
    return fragment;
}

Fragment Engine::layout_sizing(NodeId id, Constraints constraints,
                               std::size_t depth) {
    ScopedDetail scope(detail_, FragmentDetail::Baseline);
    return layout(id, constraints, depth);
}

ResolvedBox Engine::query_box(const LayoutNode& node, const Constraints& constraints) const {
    ResolvedBox box;
    try {
        box = resolve_layout_box(node, constraints.containing_width, constraints.containing_height);
    } catch (const std::invalid_argument& error) {
        throw LayoutFailure(LayoutErrorCode::InvalidStyle, node.id, error.what());
    }
    if (constraints.sizing_mode == SizingMode::Content) {
        box.preferred = {};
        if (node.children.empty()) {
            box.minimum = {};
            box.maximum = {};
            box.aspect_ratio.reset();
        }
    }
    if (constraints.ignore_preferred_width) box.preferred.width.reset();
    if (constraints.ignore_preferred_height) box.preferred.height.reset();
    if (constraints.ignore_min_max_width) {
        box.minimum.width.reset();
        box.maximum.width.reset();
    }
    if (constraints.ignore_min_max_height) {
        box.minimum.height.reset();
        box.maximum.height.reset();
    }
    return box;
}

Size Engine::layout_size(NodeId id, Constraints constraints,
                             std::size_t depth) {
    if (depth > limits_.maximum_depth) {
        throw LayoutFailure(LayoutErrorCode::LimitExceeded, id, "CSS layout depth exceeded");
    }
    const auto& node = get(id);
    if (node.style.display == Display::None) return {};
    const auto box = query_box(node, constraints);
    auto requested = requested_size(constraints, box);
    if (node.children.empty() && !node.measure) {
        const bool stretch_width = !constraints.ignore_preferred_width
            && node.style.box.size.width.kind() == LengthKind::Stretch;
        const bool stretch_height = !constraints.ignore_preferred_height
            && node.style.box.size.height.kind() == LengthKind::Stretch;
        if (!requested.width && (!stretch_width || constraints.sizing_mode == SizingMode::Content)) {
            requested.width = box.horizontal_decoration();
        }
        if (!requested.height && (!stretch_height || constraints.sizing_mode == SizingMode::Content)) {
            requested.height = box.vertical_decoration();
        }
    }
    if (requested.width && requested.height
        && (node.children.empty() || node.style.display == Display::Flex
            || node.style.display == Display::Grid)) {
        ++layouts_;
        auto size = constrain_size({*requested.width, *requested.height}, box);
        if (node.children.empty() && box.aspect_ratio) {
            size.height = std::max(size.height,
                checked(static_cast<double>(size.width) / *box.aspect_ratio, id));
        } else if (!node.children.empty() && node.style.display == Display::Grid
            && constraints.preserve_known_grid_size) {
            if (constraints.known_width) size.width = *constraints.known_width;
            if (constraints.known_height) size.height = *constraints.known_height;
        }
        return size;
    }
    if (node.children.empty()) {
        // Measurement caching already retains the expensive host work. A size
        // query needs neither a second box resolution nor published geometry.
        const auto key = cache_key_for(node, constraints, FragmentDetail::LeafSize, depth);
        if (const auto* cached = cache_.find(node.cache, key)) {
            ++hits_;
            return cached->size;
        }
        ++layouts_;
        Fragment sized;
        sized.size = size_leaf(node, constraints, box, false).size;
        cache_.stage(node.cache, key, sized);
        return sized.size;
    }
    ScopedDetail scope(detail_, FragmentDetail::Size);
    return layout(id, constraints, depth).size;
}

std::optional<Unit> Engine::intrinsic_container_size(const LayoutNode& node,
    const Constraints& constraints, const ResolvedBox& box,
    std::size_t depth, bool horizontal) {
    // Content queries use the parent's selected measurement constraint.
    if (constraints.sizing_mode == SizingMode::Content) return std::nullopt;
    const auto& value = horizontal ? node.style.box.size.width : node.style.box.size.height;
    const auto kind = value.kind();
    if (kind != LengthKind::MinContent && kind != LengthKind::MaxContent
        && kind != LengthKind::FitContent && kind != LengthKind::FitContentLength
        && kind != LengthKind::FitContentPercent) return std::nullopt;
    if (!horizontal && node.parent) {
        const auto parent_display = get(*node.parent).style.display;
        if (parent_display == Display::Block || parent_display == Display::FlowRoot) {
            return std::nullopt;
        }
    }
    if (horizontal ? constraints.known_width || constraints.ignore_preferred_width
                   : constraints.known_height || constraints.ignore_preferred_height) {
        return std::nullopt;
    }
    const auto probe = [&](AvailableSpace available) {
        Constraints measured = constraints;
        if (horizontal) {
            measured.known_width.reset();
            measured.available.width = available;
            measured.ignore_preferred_width = true;
            measured.ignore_min_max_width = true;
            const auto other = node.style.box.size.height.kind();
            if (other == LengthKind::MinContent || other == LengthKind::MaxContent
                || other == LengthKind::FitContent
                || other == LengthKind::FitContentLength
                || other == LengthKind::FitContentPercent) {
                measured.ignore_preferred_height = true;
            }
        } else {
            measured.known_height.reset();
            measured.available.height = available;
            measured.ignore_preferred_height = true;
            measured.ignore_min_max_height = true;
            const auto other = node.style.box.size.width.kind();
            if (other == LengthKind::MinContent || other == LengthKind::MaxContent
                || other == LengthKind::FitContent
                || other == LengthKind::FitContentLength
                || other == LengthKind::FitContentPercent) {
                measured.ignore_preferred_width = true;
            }
        }
        const auto fragment = layout_size(node.id, measured, depth);
        return horizontal ? fragment.width : fragment.height;
    };
    if (kind == LengthKind::MinContent) return probe(AvailableSpace::min_content());
    if (kind == LengthKind::MaxContent) return probe(AvailableSpace::max_content());
    // Taffy 0.14 measures a fit-content(<length-percentage>) Block/Flex
    // container at max-content, while Grid resolves its supplied limit.
    if ((kind == LengthKind::FitContentLength
            || kind == LengthKind::FitContentPercent)
        && node.style.display != Display::Grid) {
        return probe(AvailableSpace::max_content());
    }
    const Unit minimum = probe(AvailableSpace::min_content());
    const Unit maximum = probe(AvailableSpace::max_content());
    const auto available = horizontal ? constraints.available.width
        : constraints.available.height;
    std::optional<Unit> limit = kind == LengthKind::FitContent
        ? available.definite_value()
        : value.resolve_fit_content_limit(horizontal
            ? constraints.containing_width : constraints.containing_height);
    if (!limit) return maximum;
    if (kind != LengthKind::FitContent) {
        const auto decoration = horizontal
            ? box.horizontal_decoration() : box.vertical_decoration();
        if (!decoration) {
            throw LayoutFailure(LayoutErrorCode::UnresolvedPercentage, node.id,
                "Intrinsic container decoration has no definite containing inline size");
        }
        *limit = box.box_sizing == BoxSizing::ContentBox
            ? checked(static_cast<double>(std::max(*limit, Unit{0})) + *decoration, node.id)
            : std::max(*limit, *decoration);
    }
    return std::max(minimum, std::min(maximum, *limit));
}

Measurement Engine::measure(const LayoutNode& node, const MeasureRequest& request) {
    if (!node.measure) return {};
    validate_measure_request(request);
    if (const auto* hit = cache_.find(node.cache, request)) return *hit;
    if (++measurements_ > limits_.maximum_measurements) {
        throw LayoutFailure(LayoutErrorCode::LimitExceeded, node.id, "CSS measurement limit exceeded");
    }
    const auto result = node.measure(request);
    try { validate_measurement(result); }
    catch (const std::invalid_argument& error) {
        throw LayoutFailure(LayoutErrorCode::InvalidMeasurement, node.id, error.what());
    }
    cache_.stage(node.cache, request, result);
    return result;
}

Fragment Engine::hidden(const LayoutNode& node, std::size_t depth) {
    if (depth > limits_.maximum_depth) {
        throw LayoutFailure(LayoutErrorCode::LimitExceeded, node.id, "CSS layout depth exceeded");
    }
    Fragment fragment;
    fragment.root = NodeLayout{node.id, node.parent};
    if (detail_ == FragmentDetail::Full) fragment.reserve_children(node.children.size());
    for (const auto child : node.children) {
        auto nested = hidden(get(child), depth + 1);
        if (detail_ == FragmentDetail::Full) {
            fragment.append_nodes(std::move(nested));
        }
    }
    return fragment;
}

} // namespace axisly::css::detail
