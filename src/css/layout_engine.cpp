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
    grid_structure.reset();
    const auto has_auto_repeat = [](const std::vector<TemplateTrack>& tracks) {
        return std::any_of(tracks.begin(), tracks.end(), [](const TemplateTrack& component) {
            const auto* repeat = std::get_if<GridRepeat>(&component);
            return repeat && repeat->kind != RepeatKind::Count;
        });
    };
    grid_columns_auto_repeat = has_auto_repeat(style.grid.template_columns);
    grid_rows_auto_repeat = has_auto_repeat(style.grid.template_rows);
    const auto dependent = [](const Length& value) {
        return value.kind() == LengthKind::Percent
            || value.kind() == LengthKind::FitContentPercent
            || value.kind() == LengthKind::Calc;
    };
    const auto& box = style.box;
    margin_depends_on_width = dependent(box.margin.left) || dependent(box.margin.right)
        || dependent(box.margin.top) || dependent(box.margin.bottom);
    vertical_edges_depend_on_width = dependent(box.padding.top) || dependent(box.padding.bottom)
        || dependent(box.border.top) || dependent(box.border.bottom);
    has_relative_inset = style.position == Position::Relative
        && (style.inset.left.kind() != LengthKind::Auto
            || style.inset.right.kind() != LengthKind::Auto
            || style.inset.top.kind() != LengthKind::Auto
            || style.inset.bottom.kind() != LengthKind::Auto);
    box_depends_on_width = dependent(box.size.width) || dependent(box.min_size.width)
        || dependent(box.max_size.width);
    box_depends_on_height = dependent(box.size.height) || dependent(box.min_size.height)
        || dependent(box.max_size.height);
    for (const auto* edges : {&box.margin, &box.padding, &box.border}) {
        box_depends_on_width |= dependent(edges->left) || dependent(edges->right)
            || dependent(edges->top) || dependent(edges->bottom);
    }
}

ResolvedDimensions requested_size(const Constraints& constraints,
                                  const LayoutBox& box) {
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
    const bool container = !node.is_leaf();
    const Size scrollbar{
        node.style.overflow_y == Overflow::Scroll ? node.style.scrollbar_width : Unit{0},
        node.style.overflow_x == Overflow::Scroll ? node.style.scrollbar_width : Unit{0}};
    PooledValue<LayoutBox> resolved;
    try {
        resolved = node.cache_owner->resolve_box(node.style.box, width_key, height_key, container, scrollbar);
    } catch (const std::invalid_argument& error) {
        throw LayoutFailure(LayoutErrorCode::InvalidStyle, node.id, error.what());
    }
    node.resolved_box = LayoutNode::BoxCache{width_key, height_key, std::move(resolved), container};
}

FragmentRoot make_layout(const LayoutNode& node, Size size, const LayoutBox& resolved,
                       std::optional<Unit> first_baseline,
                       std::optional<Unit> last_baseline) {
    const auto margin = edges(resolved.margin, node.id);
    const auto padding = edges(resolved.padding(), node.id);
    const auto border = edges(resolved.border(), node.id);
    const Unit left = content_origin_x(resolved, border, padding, node.id);
    const Unit top = padding.top + border.top;
    const Unit width = std::max(Unit{0}, content_axis(size.width,
        padding.left, padding.right, border.left, border.right) - resolved.scrollbar.width);
    const Unit height = std::max(Unit{0}, content_axis(size.height,
        padding.top, padding.bottom, border.top, border.bottom) - resolved.scrollbar.height);
    return node.roots->create(LayoutGeometry{NodeLayout{node.id, node.parent, {0, 0, size.width, size.height},
        {left, top, width, height}, margin, padding, border,
        resolved.scrollbar,
        first_baseline ? std::optional<Unit>(*first_baseline + top) : std::nullopt,
        last_baseline ? std::optional<Unit>(*last_baseline + top) : std::nullopt}, border});
}

void set_fragment_border_size(Fragment& fragment, Size size, const LayoutBox& box) {
    fragment.overflow_dirty |= fragment.size != size;
    fragment.size = size;
    auto& layout = fragment.mutable_root_layout();
    layout.border_box.width = size.width;
    layout.border_box.height = size.height;
    const auto padding = edges(box.padding(), layout.node);
    const auto border = edges(box.border(), layout.node);
    layout.content_box.width = std::max(Unit{0}, content_axis(size.width,
        padding.left, padding.right, border.left, border.right) - box.scrollbar.width);
    layout.content_box.height = std::max(Unit{0}, content_axis(size.height,
        padding.top, padding.bottom, border.top, border.bottom) - box.scrollbar.height);
}

void publish_item_box(Fragment& fragment, const LayoutNode& node, const LayoutBox& box) {
    auto& root = fragment.mutable_root_layout();
    const auto padding = edges(box.padding(), node.id);
    const auto border = edges(box.border(), node.id);
    fragment.overflow_dirty |= root.padding != padding;
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

void Engine::translate(Fragment& fragment, Unit x, Unit y, NodeId node) {
    if (x == 0 && y == 0) return;
    auto move = [&](NodeLayout& layout) {
        layout.border_box.x = checked(static_cast<double>(layout.border_box.x) + x, node);
        layout.border_box.y = checked(static_cast<double>(layout.border_box.y) + y, node);
        layout.content_box.x = checked(static_cast<double>(layout.content_box.x) + x, node);
        layout.content_box.y = checked(static_cast<double>(layout.content_box.y) + y, node);
    };
    if (fragment.root) move(fragment.mutable_root_layout());
    fragment.translate_descendants(x, y, node, cache_.translation_pool());
}

Engine::Engine(const LayoutNodes& nodes, TreeLimits limits, LayoutCache& cache,
               const MeasureProvider* measure_provider, bool collect_grid_tracks)
    : nodes_(nodes), cache_(cache), limits_(limits), measure_provider_(measure_provider),
      collect_grid_tracks_(collect_grid_tracks) {}

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
        for (const auto child : node.layout_children()) {
            if (has_float_subtree(child, depth + 1)) {
                has_float = true;
                break;
            }
        }
    }
    float_subtree_.emplace(id.value, has_float);
    return has_float;
}

LayoutResult Engine::compute(NodeId root, AvailableSize available,
                             std::vector<NodeLayout> output, PublicationContext publication,
                             std::optional<PixelRounding> rounding,
                             Fragment* retained, bool publish_nodes) {
    LayoutResult result;
    result.nodes = std::move(output);
    result.nodes.clear();
    try {
        Constraints constraints;
        constraints.available = available;
        constraints.containing_width = available.width.definite_value();
        constraints.containing_height = available.height.definite_value();
        const auto& root_node = get(root);
        cache_.reserve_hint(root_node.layout_children().size() + 1);
        if (root_node.style.display == Display::Block
            || root_node.style.display == Display::FlowRoot) {
            const auto root_box = resolve_layout_box(root_node,
                constraints.containing_width, constraints.containing_height);
            const auto margin = edges(root_box.margin, root);
            const auto padding = edges(root_box.padding(), root);
            const auto border = edges(root_box.border(), root);
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
        auto fragment = layout(root_node, constraints, 1);
        if (get(root).style.box.direction == Direction::Rtl
            && available.width.kind == AvailableKind::Definite) {
            const Unit root_x = checked(static_cast<double>(available.width.value)
                - fragment.size.width, root);
            translate(fragment, root_x, 0, root);
        }
        result.size = fragment.size;
        if (!publish_nodes) {
            if (rounding) fragment.validate_rounding(*rounding);
            else fragment.validate_nodes();
        }
        // For an unrounded flat tree at the origin, published coordinates are
        // already local coordinates. Its output is itself the query snapshot,
        // so the leaf buffer can still transfer directly into LayoutResult.
        const auto& root_layout = fragment.root_layout();
        const bool flat_snapshot = !rounding && !fragment.translation
            && (!fragment.descendants || !fragment.descendants->branched)
            && root_layout.border_box.x == 0 && root_layout.border_box.y == 0;
        if (retained && publish_nodes && !flat_snapshot) {
            retained->size = fragment.size;
            retained->root = root_node.roots->create(fragment.root.read());
            retained->descendants = fragment.descendants;
            retained->translation = fragment.translation;
            // Queries need geometry only. Sharing Grid reports here would force
            // the report publisher to copy otherwise uniquely owned vectors.
        }
        if (publish_nodes && rounding) {
            result.nodes = fragment.rounded_nodes(std::move(result.nodes), *rounding);
            if (!result.nodes.empty()) {
                result.size = {result.nodes.front().border_box.width, result.nodes.front().border_box.height};
            }
        } else if (publish_nodes) {
            result.nodes = std::move(fragment).take_nodes(std::move(result.nodes), publication);
        }
        if (collect_grid_tracks_) result.grid_tracks = std::move(fragment).take_grid_tracks();
        if (retained && !publish_nodes) {
            if (rounding) {
                const auto rounded = round_node_layout(fragment.root_layout(), {}, {}, *rounding, true);
                result.size = {rounded.border_box.width, rounded.border_box.height};
            }
            *retained = std::move(fragment);
        }
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
    result.published_nodes_reused = publication.reused_nodes;
    if (!result.succeeded()) {
        result.size = {};
        result.nodes.clear();
        result.grid_tracks.clear();
        result.published_nodes_reused = 0;
    }
    return result;
}

Fragment Engine::layout(const LayoutNode& node, const Constraints& constraints, std::size_t depth,
                        BlockContext* block_context) {
    const auto id = node.id;
    if (depth > limits_.maximum_depth) {
        throw LayoutFailure(LayoutErrorCode::LimitExceeded, id, "CSS layout depth exceeded");
    }
    const bool trivial_size = detail_ == FragmentDetail::Size
        && ((constraints.known_width && constraints.known_height)
            || (node.is_leaf() && !has_measure(node)));
    // A content query with both leaf axes supplied has no sizing work to
    // retain. The independent measurement cache owns content and baselines;
    // reconstructing this leaf's geometry avoids retaining a second result
    // whose only remaining work is decoration and parent placement.
    const bool trivial_leaf = node.is_leaf()
        && constraints.sizing_mode == SizingMode::Content
        && constraints.known_width && constraints.known_height;
    const bool cacheable = !block_context && !trivial_size && !trivial_leaf;
    const auto cache_key = cacheable
        ? LayoutCacheKey::from(node, constraints, detail_, depth) : LayoutCacheKey{};
    if (cacheable) {
        // Query stages have distinct semantics: a full Grid layout can rerun
        // tracks after the size-query exit, changing its intrinsic result.
        // Cache only the requested stage, even when richer output exists.
        if (node.is_leaf()) {
            if (const auto* hit = cache_.find_leaf(node.cache, cache_key)) {
                ++hits_;
                if (depth == 1) root_hit_ = true;
                return cache_.restore(*hit, *node.roots);
            }
        } else if (const auto* hit = cache_.find(node.cache, cache_key)) {
            ++hits_;
            if (depth == 1) root_hit_ = true;
            return cache_.restore(*hit, *node.roots);
        }
    }
    // Leaves stay in this dispatch frame. Container sizing and geometry share
    // the same computation after resolving the query box exactly once.
    auto fragment = [&]() -> Fragment {
        if (node.style.display == Display::None) {
            ++layouts_;
            return hidden(node, depth);
        }
        if (node.is_leaf()) {
            ++layouts_;
            return leaf(node, constraints, resolve_layout_box(node,
                constraints.containing_width, constraints.containing_height));
        }
        return compute_container(node, constraints, query_box(node, constraints), depth, block_context);
    }();
    if (cacheable) {
        // Leaf geometry follows the parent's current dimensions. Replace its
        // most recent result instead of filling the multi-query FIFO with
        // final rectangles; intrinsic sizes and host measurements still use
        // their multi-query retention paths.
        if (node.is_leaf()) cache_.stage_leaf(node.cache, cache_key, fragment);
        else cache_.stage(node.cache, cache_key, fragment);
    }
    return fragment;
}

Fragment Engine::compute_container(const LayoutNode& node, const Constraints& constraints,
    LayoutBox box, std::size_t depth, BlockContext* block_context) {
    ++layouts_;
    if (node.parent) {
        if (const auto intrinsic = intrinsic_container_size(node, constraints, box, depth, true)) {
            box.preferred.width = *intrinsic;
        }
        if (const auto intrinsic = intrinsic_container_size(node, constraints, box, depth, false)) {
            box.preferred.height = *intrinsic;
        }
    }
    auto fragment = layout_container(node, constraints, box, depth, block_context);
    if (fragment.root && suppresses_baseline(node.style.contain)) {
        auto& root_layout = fragment.mutable_root_layout();
        root_layout.first_baseline.reset();
        root_layout.last_baseline.reset();
    }
    if (detail_ == FragmentDetail::Full) compute_scrollable_overflow(fragment, node, nodes_);
    return fragment;
}

Fragment Engine::layout_container(const LayoutNode& node, const Constraints& constraints,
    const LayoutBox& box, std::size_t depth, BlockContext* block_context) {
    // Return directly into the caller's fragment, without a default geometry
    // followed by a second move assignment at the algorithm dispatch boundary.
    switch (node.style.display) {
    case Display::Block:
    case Display::FlowRoot: return block(node, constraints, box, depth, block_context);
    case Display::Flex: return flex(node, constraints, box, depth);
    case Display::Grid: return grid(node, constraints, box, depth);
    default:
        throw LayoutFailure(LayoutErrorCode::InvalidStyle, node.id,
            "CSS display value is invalid");
    }
}

void Engine::append_fragment(Fragment& parent, Fragment&& child) {
    if (child.overflow_dirty && child.root) {
        compute_scrollable_overflow(child, get(child.root_layout().node), nodes_);
    }
    parent.append_nodes(std::move(child));
    parent.append_grid_reports(std::move(child));
}

Fragment Engine::layout_sizing(const LayoutNode& node, const Constraints& constraints,
                               std::size_t depth) {
    ScopedDetail scope(detail_, FragmentDetail::Baseline);
    return layout(node, constraints, depth);
}

LayoutBox Engine::query_box(const LayoutNode& node, const Constraints& constraints) const {
    LayoutBox box;
    try {
        box = resolve_layout_box(node, constraints.containing_width, constraints.containing_height);
    } catch (const std::invalid_argument& error) {
        throw LayoutFailure(LayoutErrorCode::InvalidStyle, node.id, error.what());
    }
    if (constraints.sizing_mode == SizingMode::Content) {
        box.preferred = {};
        if (node.is_leaf()) {
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

Size Engine::layout_size(const LayoutNode& node, const Constraints& constraints,
                             std::size_t depth) {
    const auto id = node.id;
    if (depth > limits_.maximum_depth) {
        throw LayoutFailure(LayoutErrorCode::LimitExceeded, id, "CSS layout depth exceeded");
    }
    if (node.style.display == Display::None) return {};
    // Parent-resolved content dimensions already determine a leaf's size.
    // Geometry/content queries still use leaf(); a size query needs neither
    // its box cache nor a Measurement payload in this case.
    if (node.is_leaf() && constraints.sizing_mode == SizingMode::Content
        && constraints.known_width && constraints.known_height) {
        ++layouts_;
        return validate_border_size({*constraints.known_width, *constraints.known_height});
    }
    std::optional<LayoutCacheKey> leaf_key;
    if (node.is_leaf() && has_measure(node)
        && !(constraints.known_width && constraints.known_height)) {
        // A cached intrinsic result already includes box resolution and
        // clamping. Do not reconstruct those inputs before returning a hit.
        leaf_key = LayoutCacheKey::from(node, constraints, FragmentDetail::SizeOnly, depth);
        if (const auto* cached = cache_.find_size(node.cache, *leaf_key)) {
            ++hits_;
            return *cached;
        }
    }
    if (node.is_leaf()) {
        const auto& box = resolve_layout_box(node,
            constraints.containing_width, constraints.containing_height);
        ++layouts_;
        const auto sized = size_leaf(node, constraints, box, false);
        if (has_measure(node) && sized.has_content) {
            if (!leaf_key) leaf_key = LayoutCacheKey::from(node, constraints, FragmentDetail::SizeOnly, depth);
            cache_.stage_size(node.cache, *leaf_key, sized.size);
        }
        return sized.size;
    }
    // A scalar container query does not consume a root, margin metadata or
    // geometry ownership. Probe before resolving the box and restore only its
    // size. Block flow still uses layout() and its separate fragment cache.
    const bool cacheable = !(constraints.known_width && constraints.known_height);
    const auto key = cacheable
        ? LayoutCacheKey::from(node, constraints, FragmentDetail::SizeOnly, depth) : LayoutCacheKey{};
    if (cacheable) {
        if (const auto* cached = cache_.find_size(node.cache, key)) {
            ++hits_;
            return *cached;
        }
    }
    const auto box = query_box(node, constraints);
    const auto requested = requested_size(constraints, box);
    if (requested.width && requested.height
        && (node.style.display == Display::Flex || node.style.display == Display::Grid)) {
        ++layouts_;
        auto size = constrain_size({*requested.width, *requested.height}, box);
        if (node.style.display == Display::Grid && constraints.preserve_known_grid_size) {
            if (constraints.known_width) size.width = *constraints.known_width;
            if (constraints.known_height) size.height = *constraints.known_height;
        }
        return size;
    }
    ScopedDetail scope(detail_, FragmentDetail::Size);
    const auto size = compute_container(node, constraints, box, depth).size;
    if (cacheable) cache_.stage_size(node.cache, key, size);
    return size;
}

std::optional<Unit> Engine::intrinsic_container_size(const LayoutNode& node,
    const Constraints& constraints, const LayoutBox& box,
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
        const auto fragment = layout_size(node, measured, depth);
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
        *limit = box.box_sizing == BoxSizing::ContentBox
            ? checked(static_cast<double>(std::max(*limit, Unit{0})) + decoration, node.id)
            : std::max(*limit, decoration);
    }
    return std::max(minimum, std::min(maximum, *limit));
}

Measurement Engine::measure(const LayoutNode& node, const MeasureRequest& request) {
    if (!has_measure(node)) return {};
    if (const auto* hit = cache_.find(node.cache, request)) return *hit;
    // Only successfully validated requests enter the measurement cache.
    validate_measure_request(request);
    if (++measurements_ > limits_.maximum_measurements) {
        throw LayoutFailure(LayoutErrorCode::LimitExceeded, node.id, "CSS measurement limit exceeded");
    }
    const auto result = node.measure ? node.measure(request)
        : measure_provider_->measure(node.id, node.style, node.measure_context(), request);
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
    fragment.root = node.roots->create(LayoutGeometry{NodeLayout{node.id, node.parent}});
    if (detail_ == FragmentDetail::Full) fragment.reserve_children(node.layout_children().size());
    for (const auto child : node.layout_children()) {
        auto nested = hidden(get(child), depth + 1);
        if (detail_ == FragmentDetail::Full) {
            fragment.append_nodes(std::move(nested));
        }
    }
    return fragment;
}

} // namespace axisly::css::detail
