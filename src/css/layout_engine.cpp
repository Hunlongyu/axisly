#include "layout_engine.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <utility>

namespace axisly::css::detail {
Unit checked(double value, NodeId id) {
    if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<Unit>::max()) {
        throw LayoutFailure(LayoutErrorCode::NumericOverflow, id, "CSS layout numeric overflow");
    }
    return static_cast<Unit>(value);
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

Unit edge(const ResolvedEdge& value, NodeId id) {
    if (value.kind == EdgeKind::Numeric) return value.value;
    if (value.kind == EdgeKind::Auto) return 0;
    throw LayoutFailure(LayoutErrorCode::UnresolvedPercentage, id,
        "CSS percentage edge has no definite containing inline size");
}

Insets edges(const ResolvedEdges& values, NodeId id) {
    return {edge(values.left, id), edge(values.top, id),
            edge(values.right, id), edge(values.bottom, id)};
}

Unit content_axis(Unit border_size, Unit first_padding, Unit last_padding,
                  Unit first_border, Unit last_border) {
    return std::max(Unit{0}, border_size - first_padding - last_padding - first_border - last_border);
}

Unit constrain_axis(Unit value, std::optional<Unit> minimum,
                    std::optional<Unit> maximum) {
    if (maximum) value = std::min(value, *maximum);
    if (minimum) value = std::max(value, *minimum);
    return value;
}

AvailableSpace subtract_decoration(AvailableSpace space, Unit amount) {
    if (space.kind != AvailableKind::Definite) return space;
    return AvailableSpace::definite(std::max(Unit{0}, space.value - amount));
}

ResolvedDimensions requested_size(const Constraints& constraints,
                                  const ResolvedBox& box) {
    return transfer_aspect_ratio({
        constraints.known_width ? constraints.known_width : box.preferred.width,
        constraints.known_height ? constraints.known_height : box.preferred.height,
    }, box);
}

ResolvedBox resolve_layout_box(const LayoutNode& node,
                               std::optional<Unit> containing_width,
                               std::optional<Unit> containing_height) {
    auto resolved = resolve_box(node.style.box, containing_width, containing_height);
    resolved.scrollbar.width = node.style.overflow_y == Overflow::Scroll
        ? node.style.scrollbar_width : Unit{0};
    resolved.scrollbar.height = node.style.overflow_x == Overflow::Scroll
        ? node.style.scrollbar_width : Unit{0};
    if (resolved.box_sizing == BoxSizing::ContentBox) {
        for (auto* value : {&resolved.preferred.width, &resolved.minimum.width,
                            &resolved.maximum.width}) {
            if (*value) *value = checked(static_cast<double>(**value)
                + resolved.scrollbar.width, node.id);
        }
        for (auto* value : {&resolved.preferred.height, &resolved.minimum.height,
                            &resolved.maximum.height}) {
            if (*value) *value = checked(static_cast<double>(**value)
                + resolved.scrollbar.height, node.id);
        }
    }
    return resolved;
}

NodeLayout make_layout(const LayoutNode& node, Size size, const ResolvedBox& resolved,
                       std::optional<Unit> first_baseline,
                       std::optional<Unit> last_baseline) {
    const auto margin = edges(resolved.margin, node.id);
    const auto padding = edges(resolved.padding, node.id);
    const auto border = edges(resolved.border, node.id);
    const Unit left = padding.left + border.left;
    const Unit top = padding.top + border.top;
    const Unit width = std::max(Unit{0}, content_axis(size.width,
        padding.left, padding.right, border.left, border.right) - resolved.scrollbar.width);
    const Unit height = std::max(Unit{0}, content_axis(size.height,
        padding.top, padding.bottom, border.top, border.bottom) - resolved.scrollbar.height);
    return {node.id, node.parent, {0, 0, size.width, size.height},
        {left, top, width, height}, margin, padding, border, resolved.scrollbar,
        first_baseline ? std::optional<Unit>(*first_baseline + top) : std::nullopt,
        last_baseline ? std::optional<Unit>(*last_baseline + top) : std::nullopt};
}

void translate(Fragment& fragment, Unit x, Unit y, NodeId node) {
    for (auto& layout : fragment.nodes) {
        layout.border_box.x = checked(static_cast<double>(layout.border_box.x) + x, node);
        layout.border_box.y = checked(static_cast<double>(layout.border_box.y) + y, node);
        layout.content_box.x = checked(static_cast<double>(layout.content_box.x) + x, node);
        layout.content_box.y = checked(static_cast<double>(layout.content_box.y) + y, node);
    }
}

Engine::Engine(const std::unordered_map<std::uint64_t, LayoutNode>& nodes,
               const CacheMap& cache, TreeLimits limits)
    : nodes_(nodes), cache_(cache), limits_(limits) {}

void Engine::stage(NodeId id, const Constraints& constraints, const Fragment& fragment) {
    // Fragment caching is bounded because storing every nested subtree in a
    // deep chain otherwise takes quadratic space.
    constexpr std::size_t maximum_fragment_nodes = 512;
    const auto count = fragment.nodes.size();
    const auto existing = updates_.find(id.value);
    const auto old_count = existing == updates_.end()
        ? 0 : existing->second.fragment.nodes.size();
    if (count > maximum_fragment_nodes
        || count > limits_.maximum_cached_layouts - (pending_layouts_ - old_count)) return;
    updates_.insert_or_assign(id.value, CacheEntry{constraints, fragment});
    pending_layouts_ = pending_layouts_ - old_count + count;
}

const LayoutNode& Engine::get(NodeId id) const {
    const auto iterator = nodes_.find(id.value);
    if (iterator == nodes_.end()) throw LayoutFailure(LayoutErrorCode::InvalidRoot, id, "CSS node is absent");
    return iterator->second;
}

LayoutResult Engine::compute(NodeId root, AvailableSize available) {
    LayoutResult result;
    try {
        Constraints constraints;
        constraints.available = available;
        constraints.containing_width = available.width.definite_value();
        constraints.containing_height = available.height.definite_value();
        auto fragment = layout(root, constraints, 1);
        result.size = fragment.size;
        result.nodes = std::move(fragment.nodes);
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

Fragment Engine::layout(NodeId id, Constraints constraints, std::size_t depth) {
    if (depth > limits_.maximum_depth) {
        throw LayoutFailure(LayoutErrorCode::LimitExceeded, id, "CSS layout depth exceeded");
    }
    const auto& node = get(id);
    const auto cached = updates_.find(id.value);
    if (cached != updates_.end() && cached->second.constraints == constraints) {
        ++hits_;
        if (depth == 1) root_hit_ = true;
        return cached->second.fragment;
    }
    const auto previous = cache_.find(id.value);
    if (previous != cache_.end() && previous->second.constraints == constraints) {
        ++hits_;
        if (depth == 1) root_hit_ = true;
        return previous->second.fragment;
    }
    ++layouts_;
    Fragment fragment;
    if (node.style.display == Display::None) {
        fragment = hidden(node, depth);
        stage(id, constraints, fragment);
        return fragment;
    }
    ResolvedBox box;
    try {
        box = resolve_layout_box(node, constraints.containing_width, constraints.containing_height);
    } catch (const std::invalid_argument& error) {
        throw LayoutFailure(LayoutErrorCode::InvalidStyle, id, error.what());
    }
    if (constraints.ignore_preferred_width) box.preferred.width.reset();
    if (constraints.ignore_preferred_height) box.preferred.height.reset();
    if (node.children.empty()) {
        fragment = leaf(node, constraints, box);
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
        stage(id, constraints, fragment);
        return fragment;
    }
    switch (node.style.display) {
    case Display::Block:
    case Display::FlowRoot: fragment = block(node, constraints, box, depth); break;
    case Display::Flex: fragment = flex(node, constraints, box, depth); break;
    case Display::Grid: fragment = grid(node, constraints, box, depth); break;
    case Display::None: break;
    default:
        throw LayoutFailure(LayoutErrorCode::InvalidStyle, id,
            "CSS display value is invalid");
    }
    stage(id, constraints, fragment);
    return fragment;
}

Measurement Engine::measure(const LayoutNode& node, const MeasureRequest& request) {
    if (!node.measure) return {};
    if (++measurements_ > limits_.maximum_measurements) {
        throw LayoutFailure(LayoutErrorCode::LimitExceeded, node.id, "CSS measurement limit exceeded");
    }
    validate_measure_request(request);
    const auto result = node.measure(request);
    try { validate_measurement(result); }
    catch (const std::invalid_argument& error) {
        throw LayoutFailure(LayoutErrorCode::InvalidMeasurement, node.id, error.what());
    }
    return result;
}

Fragment Engine::leaf(const LayoutNode& node, const Constraints& constraints,
                      const ResolvedBox& box) {
    const auto decoration_width = box.horizontal_decoration();
    const auto decoration_height = box.vertical_decoration();
    if (!decoration_width || !decoration_height) {
        throw LayoutFailure(LayoutErrorCode::UnresolvedPercentage, node.id,
            "CSS box decoration has no definite containing inline size");
    }
    const auto intrinsic = [&](const Length& value, bool horizontal) -> std::optional<Unit> {
        const auto kind = value.kind();
        if (kind == LengthKind::Auto || kind == LengthKind::Content
            || kind == LengthKind::Length || kind == LengthKind::Percent
            || kind == LengthKind::Calc) return std::nullopt;
        const Unit decoration = horizontal ? *decoration_width : *decoration_height;
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
    const auto requested = requested_size(constraints, box);
    auto width = requested.width;
    auto height = requested.height;
    if (!width && !constraints.ignore_preferred_width) {
        width = intrinsic(node.style.box.size.width, true);
    }
    if (!height && !constraints.ignore_preferred_height) {
        height = intrinsic(node.style.box.size.height, false);
    }
    const auto transferred = transfer_aspect_ratio({width, height}, box);
    width = transferred.width;
    height = transferred.height;
    MeasureRequest request;
    request.known_content_width = width
        ? std::optional<Unit>(std::max(Unit{0}, *width - *decoration_width)) : std::nullopt;
    request.known_content_height = height
        ? std::optional<Unit>(std::max(Unit{0}, *height - *decoration_height)) : std::nullopt;
    request.available_width = subtract_decoration(constraints.available.width, *decoration_width);
    request.available_height = subtract_decoration(constraints.available.height, *decoration_height);
    const auto measured = measure(node, request);
    Size candidate{
        width ? *width : checked(static_cast<double>(measured.content_size.width) + *decoration_width, node.id),
        height ? *height : checked(static_cast<double>(measured.content_size.height) + *decoration_height, node.id),
    };
    candidate = constrain_size(candidate, box);
    if (box.aspect_ratio) {
        candidate.height = std::max(candidate.height,
            checked(static_cast<double>(candidate.width) / *box.aspect_ratio,
                node.id));
    }
    Fragment fragment;
    fragment.size = candidate;
    fragment.nodes.push_back(make_layout(node, candidate, box,
        measured.first_baseline, measured.last_baseline));
    const auto padding = edges(box.padding, node.id);
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
    fragment.nodes.front().scrollable_overflow_rect = {
        0, 0,
        checked(static_cast<double>(inline_start) + scrollable_content.width
            + (scroll_container ? inline_end : 0), node.id),
        checked(static_cast<double>(padding.top) + scrollable_content.height
            + (scroll_container ? padding.bottom : 0), node.id)};
    return fragment;
}

Fragment Engine::hidden(const LayoutNode& node, std::size_t depth) {
    if (depth > limits_.maximum_depth) {
        throw LayoutFailure(LayoutErrorCode::LimitExceeded, node.id, "CSS layout depth exceeded");
    }
    Fragment fragment;
    fragment.nodes.push_back({node.id, node.parent});
    for (const auto child : node.children) {
        auto nested = hidden(get(child), depth + 1);
        fragment.nodes.insert(fragment.nodes.end(), nested.nodes.begin(), nested.nodes.end());
    }
    return fragment;
}

} // namespace axisly::css::detail
