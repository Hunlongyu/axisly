#pragma once

#include <axisly/css_tree.hpp>
#include "layout_fragment.hpp"
#include "layout_cache.hpp"
#include "node_store.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace axisly::css::detail {

struct GridContributionInput;
struct GridAxisContribution;
struct GridBaselineItem;
struct Placement;


[[nodiscard]] ResolvedDimensions requested_size(const Constraints& constraints,
    const ResolvedBox& box);

struct LayoutNode {
    NodeId id;
    Style style;
    MeasureFunction measure;
    std::optional<NodeId> parent;
    std::vector<NodeId> children;
    bool box_depends_on_width{}, box_depends_on_height{};
    struct BoxCache {
        std::optional<Unit> width, height;
        ResolvedBox box;
    };
    mutable std::optional<BoxCache> resolved_box;
    mutable NodeCache cache;
    void reset_style_cache();
};
using LayoutNodes = NodeStore<LayoutNode>;

struct BlockContext;
struct PositionedItemInput;
struct FlexItem;
struct FlexItemSizingInput;
struct LeafSizingResult {
    Size size;
    Measurement measured;
    bool has_content{};
};

class LayoutFailure final : public std::runtime_error {
public:
    LayoutFailure(LayoutErrorCode code, NodeId node, const char* message)
        : std::runtime_error(message), code(code), node(node) {}
    LayoutErrorCode code;
    NodeId node;
};

[[noreturn]] void layout_numeric_overflow(NodeId id);
[[noreturn]] void unresolved_layout_edge(NodeId id);

[[nodiscard]] inline Unit checked(double value, NodeId id) {
    if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<Unit>::max()) {
        layout_numeric_overflow(id);
    }
    return static_cast<Unit>(value);
}
[[nodiscard]] inline Unit edge(const ResolvedEdge& value, NodeId id) {
    if (value.kind == EdgeKind::Numeric) return value.value;
    if (value.kind == EdgeKind::Auto) return 0;
    unresolved_layout_edge(id);
}
[[nodiscard]] inline Insets edges(const ResolvedEdges& values, NodeId id) {
    return {edge(values.left, id), edge(values.top, id),
            edge(values.right, id), edge(values.bottom, id)};
}
[[nodiscard]] inline Unit content_axis(Unit border_size, Unit first_padding, Unit last_padding,
                  Unit first_border, Unit last_border) {
    return std::max(Unit{0}, border_size - first_padding - last_padding - first_border - last_border);
}
[[nodiscard]] inline Unit constrain_axis(Unit value, std::optional<Unit> minimum,
                    std::optional<Unit> maximum) {
    if (maximum) value = std::min(value, *maximum);
    if (minimum) value = std::max(value, *minimum);
    return value;
}
[[nodiscard]] inline AvailableSpace subtract_decoration(AvailableSpace space, Unit amount) {
    if (space.kind != AvailableKind::Definite) return space;
    return AvailableSpace::definite(std::max(Unit{0}, space.value - amount));
}
void update_layout_box_cache(const LayoutNode& node,
    std::optional<Unit> containing_width, std::optional<Unit> containing_height);
[[nodiscard]] inline ResolvedBox resolve_layout_box(const LayoutNode& node,
    std::optional<Unit> containing_width, std::optional<Unit> containing_height) {
    const auto width_key = node.box_depends_on_width ? containing_width : std::nullopt;
    const auto height_key = node.box_depends_on_height ? containing_height : std::nullopt;
    if (!node.resolved_box || node.resolved_box->width != width_key
        || node.resolved_box->height != height_key) {
        update_layout_box_cache(node, containing_width, containing_height);
    }
    auto resolved = node.resolved_box->box;
    // Maximum ratio transfer depends on whether the node is a container.
    if (!node.children.empty()) resolved.maximum = transfer_aspect_ratio(resolved.maximum, resolved);
    // Ratio bounds use padding/border decoration before scrollbars reserve
    // content space. The cached box retains this pre-scrollbar state.
    resolved.scrollbar.width = node.style.overflow_y == Overflow::Scroll
        ? node.style.scrollbar_width : Unit{0};
    resolved.scrollbar.height = node.style.overflow_x == Overflow::Scroll
        ? node.style.scrollbar_width : Unit{0};
    return resolved;
}
[[nodiscard]] inline Unit content_origin_x(const ResolvedBox& box, Insets border,
                      Insets padding, NodeId node) {
    return checked(static_cast<double>(padding.left) + border.left
        + (box.direction == Direction::Rtl ? box.scrollbar.width : 0), node);
}
void set_fragment_border_size(Fragment& fragment, Size size, const ResolvedBox& box);
void publish_item_box(Fragment& fragment, const LayoutNode& node, const ResolvedBox& box);
[[nodiscard]] NodeLayout make_layout(const LayoutNode& node, Size size,
    const ResolvedBox& resolved, std::optional<Unit> first_baseline = std::nullopt,
    std::optional<Unit> last_baseline = std::nullopt);
void translate(Fragment& fragment, Unit x, Unit y, NodeId node);
[[nodiscard]] inline std::optional<Unit> child_baseline(const Fragment& fragment, const Style& style) {
    if (!fragment.root) return std::nullopt;
    const auto baseline = fragment.root_layout().first_baseline;
    if (style.overflow_y == Overflow::Hidden || style.overflow_y == Overflow::Scroll) {
        return std::clamp(baseline.value_or(fragment.size.height),
            Unit{0}, fragment.size.height);
    }
    return baseline;
}
void compute_scrollable_overflow(std::vector<NodeLayout>& layouts,
    const LayoutNodes& nodes);
[[nodiscard]] std::pair<Unit, Unit> relative_offset(const LayoutNode& node,
    Size containing, Direction containing_direction, NodeId parent);


class Engine {
    friend class FlexLayout;
public:
    Engine(const LayoutNodes& nodes, TreeLimits limits, LayoutCache& cache);
    [[nodiscard]] LayoutResult compute(NodeId root, AvailableSize available);
    void commit_caches() { cache_.commit(); }

private:
    const LayoutNodes& nodes_;
    CacheTransaction cache_;
    TreeLimits limits_;
    std::size_t measurements_{};
    std::size_t layouts_{};
    std::size_t hits_{};
    bool root_hit_{};
    FragmentDetail detail_{FragmentDetail::Full};
    std::unordered_map<std::uint64_t, bool> float_subtree_;
    [[nodiscard]] const LayoutNode& get(NodeId id) const {
        const auto* node = nodes_.find(id.value);
        if (!node) throw LayoutFailure(LayoutErrorCode::InvalidRoot, id, "CSS node is absent");
        return *node;
    }
    [[nodiscard]] bool has_float_subtree(NodeId id, std::size_t depth);
    [[nodiscard]] Fragment layout(NodeId id, Constraints constraints, std::size_t depth,
                                  BlockContext* block_context = nullptr);
    [[nodiscard]] Fragment layout_sizing(NodeId id, Constraints constraints,
                                         std::size_t depth);
    [[nodiscard]] Size layout_size(NodeId id, Constraints constraints,
                                       std::size_t depth);
    [[nodiscard]] ResolvedBox query_box(const LayoutNode& node, const Constraints& constraints) const;
    [[nodiscard]] Fragment leaf(const LayoutNode& node, const Constraints& constraints,
                                const ResolvedBox& box);
    [[nodiscard]] LeafSizingResult size_leaf(const LayoutNode& node, const Constraints& constraints,
                                             const ResolvedBox& box, bool include_content);
    [[nodiscard]] std::optional<Unit> intrinsic_container_size(const LayoutNode& node,
        const Constraints& constraints, const ResolvedBox& box,
        std::size_t depth, bool horizontal);
    [[nodiscard]] Fragment block(const LayoutNode& node, const Constraints& constraints,
                                 const ResolvedBox& box, std::size_t depth,
                                 BlockContext* block_context);
    [[nodiscard]] Unit block_content_based_width(const LayoutNode& node,
        AvailableSpace available, ResolvedDimensions percentage_basis, std::size_t depth);
    [[nodiscard]] Fragment flex(const LayoutNode& node, const Constraints& constraints,
                                const ResolvedBox& box, std::size_t depth);
    [[nodiscard]] FlexItem prepare_flex_item(const LayoutNode& node,
        const LayoutNode& child, const Constraints& constraints,
        const FlexItemSizingInput& input, std::size_t depth);
    [[nodiscard]] Fragment grid(const LayoutNode& node, const Constraints& constraints,
                                const ResolvedBox& box, std::size_t depth);
    [[nodiscard]] GridAxisContribution grid_axis_contribution(NodeId id,
        const GridContributionInput& input, std::size_t depth);
    [[nodiscard]] std::vector<GridBaselineItem> measure_grid_baselines(const LayoutNode& parent,
        const std::vector<Placement>& placements, ResolvedDimensions container_inner,
        std::size_t depth);
    [[nodiscard]] Fragment hidden(const LayoutNode& node, std::size_t depth);
    [[nodiscard]] Fragment absolute(const LayoutNode& parent, NodeId child,
        Size parent_size, const ResolvedBox& parent_box, std::size_t depth,
        std::optional<Rect> containing_override = std::nullopt,
        std::optional<Unit> static_block_y = std::nullopt);
    [[nodiscard]] Fragment positioned_item(const LayoutNode& child,
        const ResolvedBox& box, const PositionedItemInput& input, std::size_t depth);
    [[nodiscard]] Measurement measure(const LayoutNode& node, const MeasureRequest& request);
};

} // namespace axisly::css::detail
