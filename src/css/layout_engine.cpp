#include "layout_engine.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <utility>

namespace axisly::css::detail {
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
    // Style dimensions have already applied content-box sizing in resolve_box.
    // Parent-supplied dimensions have algorithm-specific ratio handling.
    return {
        constraints.known_width ? constraints.known_width : box.preferred.width,
        constraints.known_height ? constraints.known_height : box.preferred.height,
    };
}

ResolvedBox resolve_layout_box(const LayoutNode& node,
                               std::optional<Unit> containing_width,
                               std::optional<Unit> containing_height) {
    auto resolved = resolve_box(node.style.box, containing_width, containing_height,
        IndefiniteEdgeBasis::Zero);
    // Taffy transfers a container's max-size through aspect-ratio. Its leaf
    // algorithm retains independent maximum bounds, so keep that distinction
    // at the node-resolution boundary.
    if (!node.children.empty()) {
        resolved.maximum = transfer_aspect_ratio(resolved.maximum, resolved);
    }
    resolved.scrollbar.width = node.style.overflow_y == Overflow::Scroll
        ? node.style.scrollbar_width : Unit{0};
    resolved.scrollbar.height = node.style.overflow_x == Overflow::Scroll
        ? node.style.scrollbar_width : Unit{0};
    return resolved;
}

Unit content_origin_x(const ResolvedBox& box, Insets border,
                      Insets padding, NodeId node) {
    return checked(static_cast<double>(padding.left) + border.left
        + (box.direction == Direction::Rtl ? box.scrollbar.width : 0), node);
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
    auto& layout = fragment.nodes.front();
    layout.border_box.width = size.width;
    layout.border_box.height = size.height;
    const auto padding = edges(box.padding, layout.node);
    const auto border = edges(box.border, layout.node);
    layout.content_box.width = std::max(Unit{0}, content_axis(size.width,
        padding.left, padding.right, border.left, border.right) - box.scrollbar.width);
    layout.content_box.height = std::max(Unit{0}, content_axis(size.height,
        padding.top, padding.bottom, border.top, border.bottom) - box.scrollbar.height);
}

void translate(Fragment& fragment, Unit x, Unit y, NodeId node) {
    for (auto& layout : fragment.nodes) {
        layout.border_box.x = checked(static_cast<double>(layout.border_box.x) + x, node);
        layout.border_box.y = checked(static_cast<double>(layout.border_box.y) + y, node);
        layout.content_box.x = checked(static_cast<double>(layout.content_box.x) + x, node);
        layout.content_box.y = checked(static_cast<double>(layout.content_box.y) + y, node);
    }
}

std::optional<Unit> child_baseline(const Fragment& fragment, const Style& style) {
    if (fragment.nodes.empty()) return std::nullopt;
    const auto baseline = fragment.nodes.front().first_baseline;
    if (style.overflow_y == Overflow::Hidden || style.overflow_y == Overflow::Scroll) {
        return std::clamp(baseline.value_or(fragment.size.height),
            Unit{0}, fragment.size.height);
    }
    return baseline;
}

Engine::Engine(const std::unordered_map<std::uint64_t, LayoutNode>& nodes,
               const CacheMap& cache,
               const MeasurementCacheMap& measurement_cache, TreeLimits limits)
    : nodes_(nodes), cache_(cache), measurement_cache_(measurement_cache),
      limits_(limits) {}

void Engine::stage(NodeId id, const Constraints& constraints, const Fragment& fragment) {
    // Fragment caching is bounded because storing every nested subtree in a
    // deep chain otherwise takes quadratic space.
    constexpr std::size_t maximum_fragment_nodes = 512;
    constexpr std::size_t entries_per_node = 8;
    const auto count = fragment.nodes.size();
    const auto existing = updates_.find(id.value);
    std::optional<std::size_t> match_index;
    if (existing != updates_.end()) {
        for (std::size_t index = 0; index < existing->second.size(); ++index) {
            if (existing->second[index].constraints == constraints) {
                match_index = index;
                break;
            }
        }
    }
    const auto old_count = match_index
        ? existing->second[*match_index].fragment.nodes.size()
        : existing != updates_.end() && existing->second.size() == entries_per_node
            ? existing->second.front().fragment.nodes.size() : 0;
    if (count > maximum_fragment_nodes
        || count > limits_.maximum_cached_layouts - (pending_layouts_ - old_count)) return;
    auto& entries = updates_[id.value];
    if (match_index) {
        entries[*match_index] = CacheEntry{constraints, fragment};
    } else {
        if (entries.size() == entries_per_node) entries.erase(entries.begin());
        entries.push_back(CacheEntry{constraints, fragment});
    }
    pending_layouts_ = pending_layouts_ - old_count + count;
}

const LayoutNode& Engine::get(NodeId id) const {
    const auto iterator = nodes_.find(id.value);
    if (iterator == nodes_.end()) throw LayoutFailure(LayoutErrorCode::InvalidRoot, id, "CSS node is absent");
    return iterator->second;
}

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
        result.size = fragment.size;
        result.nodes = std::move(fragment.nodes);
        result.grid_tracks = std::move(fragment.grid_tracks);
        if (get(root).style.box.direction == Direction::Rtl
            && available.width.kind == AvailableKind::Definite) {
            const Unit root_x = checked(static_cast<double>(available.width.value)
                - result.size.width, root);
            for (auto& layout : result.nodes) {
                layout.border_box.x = checked(static_cast<double>(layout.border_box.x)
                    + root_x, layout.node);
            }
        }
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
    constraints.detail = detail_;
    if (!block_context) {
        const auto lookup = [&](const CacheMap& source) -> const CacheEntry* {
            const auto entries = source.find(id.value);
            if (entries == source.end()) return nullptr;
            if (const auto match = std::find_if(entries->second.begin(), entries->second.end(),
                    [&](const CacheEntry& entry) { return entry.constraints == constraints; });
                match != entries->second.end()) {
                return &*match;
            }
            return nullptr;
        };
        const auto* hit = lookup(updates_);
        if (!hit) hit = lookup(cache_);
        // A richer cached fragment can answer a smaller query. The reverse
        // would publish incomplete descendants or omit a needed baseline.
        for (int detail = static_cast<int>(detail_) + 1;
             !hit && detail <= static_cast<int>(FragmentDetail::Full); ++detail) {
            constraints.detail = static_cast<FragmentDetail>(detail);
            hit = lookup(updates_);
            if (!hit) hit = lookup(cache_);
        }
        constraints.detail = detail_;
        if (hit) {
            ++hits_;
            if (depth == 1) root_hit_ = true;
            if (hit->constraints.detail != detail_) {
                Fragment compact;
                compact.size = hit->fragment.size;
                compact.nodes.push_back(hit->fragment.nodes.front());
                compact.block_margins = hit->fragment.block_margins;
                return compact;
            }
            return hit->fragment;
        }
    }
    ++layouts_;
    Fragment fragment;
    if (node.style.display == Display::None) {
        fragment = hidden(node, depth);
        if (!block_context) stage(id, constraints, fragment);
        return fragment;
    }
    ResolvedBox box;
    try {
        box = resolve_layout_box(node, constraints.containing_width, constraints.containing_height);
    } catch (const std::invalid_argument& error) {
        throw LayoutFailure(LayoutErrorCode::InvalidStyle, id, error.what());
    }
    if (constraints.sizing_mode == SizingMode::Content) {
        // Flex resolves its item size before asking the child for its content.
        // Containers retain min/max constraints; measured leaves ignore them
        // and defer aspect-ratio effects to the parent item calculation.
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
    if (node.children.empty()) {
        fragment = leaf(node, constraints, box);
        if (suppresses_baseline(node.style.contain)) {
            fragment.nodes.front().first_baseline.reset();
            fragment.nodes.front().last_baseline.reset();
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
        if (!block_context) stage(id, constraints, fragment);
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
        fragment.nodes.front().first_baseline.reset();
        fragment.nodes.front().last_baseline.reset();
    }
    if (!block_context) stage(id, constraints, fragment);
    return fragment;
}

Fragment Engine::layout_sizing(NodeId id, Constraints constraints,
                               std::size_t depth) {
    ScopedDetail scope(detail_, FragmentDetail::Baseline);
    return layout(id, constraints, depth);
}

Fragment Engine::layout_size(NodeId id, Constraints constraints,
                             std::size_t depth) {
    ScopedDetail scope(detail_, FragmentDetail::Size);
    return layout(id, constraints, depth);
}

std::optional<Unit> Engine::intrinsic_container_size(const LayoutNode& node,
    const Constraints& constraints, const ResolvedBox& box,
    std::size_t depth, bool horizontal) {
    // Grid and Flex content queries use the parent's selected measurement
    // constraint. Block still interprets its intrinsic sizing style while
    // choosing a content-based outer inline axis.
    if (constraints.sizing_mode == SizingMode::Content
        && node.style.display != Display::Block
        && node.style.display != Display::FlowRoot) return std::nullopt;
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
        return horizontal ? fragment.size.width : fragment.size.height;
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
    const auto lookup = [&](const MeasurementCacheMap& source) -> const Measurement* {
        const auto entries = source.find(node.id.value);
        if (entries == source.end()) return nullptr;
        const auto match = std::find_if(entries->second.begin(), entries->second.end(),
            [&](const MeasurementCacheEntry& entry) {
                return entry.request == request;
            });
        return match == entries->second.end() ? nullptr : &match->result;
    };
    if (const auto* hit = lookup(measurement_updates_)) return *hit;
    if (const auto* hit = lookup(measurement_cache_)) return *hit;
    if (++measurements_ > limits_.maximum_measurements) {
        throw LayoutFailure(LayoutErrorCode::LimitExceeded, node.id, "CSS measurement limit exceeded");
    }
    const auto result = node.measure(request);
    try { validate_measurement(result); }
    catch (const std::invalid_argument& error) {
        throw LayoutFailure(LayoutErrorCode::InvalidMeasurement, node.id, error.what());
    }
    constexpr std::size_t entries_per_node = 16;
    auto found = measurement_updates_.find(node.id.value);
    if (found != measurement_updates_.end()
        && found->second.size() == entries_per_node) {
        found->second.erase(found->second.begin());
        found->second.push_back({request, result});
    } else if (pending_measurements_ < limits_.maximum_cached_measurements) {
        measurement_updates_[node.id.value].push_back({request, result});
        ++pending_measurements_;
    }
    return result;
}

Fragment Engine::hidden(const LayoutNode& node, std::size_t depth) {
    if (depth > limits_.maximum_depth) {
        throw LayoutFailure(LayoutErrorCode::LimitExceeded, node.id, "CSS layout depth exceeded");
    }
    Fragment fragment;
    fragment.nodes.push_back({node.id, node.parent});
    for (const auto child : node.children) {
        auto nested = hidden(get(child), depth + 1);
        if (detail_ == FragmentDetail::Full) {
            fragment.nodes.insert(fragment.nodes.end(),
                nested.nodes.begin(), nested.nodes.end());
        }
    }
    return fragment;
}

} // namespace axisly::css::detail
