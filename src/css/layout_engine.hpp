#pragma once

#include <axisly/css_tree.hpp>
#include "layout_fragment.hpp"
#include "layout_cache.hpp"
#include "node_store.hpp"
#include "layout_box.hpp"

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
struct GridStructure;
struct GridStructureKey;


[[nodiscard]] ResolvedDimensions requested_size(const Constraints& constraints,
    const LayoutBox& box);

struct SourceBinding {
    std::span<const NodeId> children;
    void* context{};
    bool dirty{true};
    std::uint64_t epoch{};
};
struct LayoutNode {
    template<class StyleSource>
    LayoutNode(StyleSource&& node_style, MeasureFunction&& node_measure)
        : owned_style(std::forward<StyleSource>(node_style)), style(owned_style), measure(std::move(node_measure)) {}
    struct Borrowed {};
    LayoutNode(Borrowed, const Style& node_style) : style(node_style) {}
    NodeId id;
    Style owned_style;
    const Style& style;
    MeasureFunction measure;
    std::shared_ptr<void> context;
    std::optional<NodeId> parent;
    std::vector<NodeId> children;
    SourceBinding* borrowed{};
    [[nodiscard]] bool is_leaf() const noexcept {
        return borrowed ? borrowed->children.empty() : children.empty();
    }
    [[nodiscard]] std::span<const NodeId> layout_children() const noexcept {
        return borrowed ? borrowed->children : std::span<const NodeId>(children);
    }
    [[nodiscard]] void* measure_context() const noexcept { return borrowed ? borrowed->context : context.get(); }
    // Intrusive work list for allocation-free invalidation/destruction. It is
    // used only by guarded tree mutations, never by layout or host callbacks.
    NodeId next_visit{};
    FragmentRootPool* roots{};
    LayoutCache* cache_owner{};
    bool box_depends_on_width{}, box_depends_on_height{};
    bool margin_depends_on_width{}, vertical_edges_depend_on_width{};
    bool has_relative_inset{};
    bool grid_columns_auto_repeat{}, grid_rows_auto_repeat{};
    mutable std::shared_ptr<const GridStructure> grid_structure;
    struct BoxCache {
        std::optional<Unit> width, height;
        PooledValue<LayoutBox> box;
        bool container{};
    };
    mutable std::optional<BoxCache> resolved_box;
    mutable NodeCache cache;
    void reset_style_cache();
};
struct SourceNode : LayoutNode {
    SourceBinding binding;
    explicit SourceNode(const Style& source_style) : LayoutNode(Borrowed{}, source_style) {
        borrowed = &binding;
    }
};
class LayoutNodes : public NodeStore<LayoutNode> {
    using Owned = NodeStore<LayoutNode>;
    struct Borrowed {
        const LayoutSource* source{};
        LayoutCache* cache{};
        TreeLimits limits;
        std::uint64_t epoch{1};
        std::unordered_map<std::uint64_t, std::unique_ptr<SourceNode>> entries;
    };
    std::unique_ptr<Borrowed> borrowed_;
    [[nodiscard]] const LayoutNode* find_borrowed(std::uint64_t id) const;
public:
    using Owned::Owned;
    [[nodiscard]] bool is_external() const noexcept { return bool(borrowed_); }
    [[nodiscard]] const LayoutNode* find(std::uint64_t id) const {
        return borrowed_ ? find_borrowed(id) : Owned::find(id);
    }
    [[nodiscard]] LayoutNode* find(std::uint64_t id) {
        return const_cast<LayoutNode*>(std::as_const(*this).find(id));
    }
    template<class Visitor> void for_each(Visitor&& visitor) {
        if (borrowed_) {
            for (auto& [id, node] : borrowed_->entries) { (void)id; visitor(*node); }
        } else Owned::for_each(std::forward<Visitor>(visitor));
    }
    void attach(const LayoutSource& source, LayoutCache& cache, TreeLimits limits);
    void begin_compute();
    void invalidate_source(std::optional<NodeId> node);
};

struct BlockContext;
struct PositionedItemInput;
struct FlexItem;
struct FlexItemSizingInput;
struct LeafSizingResult {
    Size size;
    Measurement measured;
    bool has_content{};
};

[[nodiscard]] inline Unit edge(const ResolvedEdge& value, NodeId id) {
    if (value.kind == EdgeKind::Numeric) return value.value;
    if (value.kind == EdgeKind::Auto) return 0;
    unresolved_layout_edge(id);
}
[[nodiscard]] inline Insets edges(const ResolvedEdges& values, NodeId id) {
    return {edge(values.left, id), edge(values.top, id),
            edge(values.right, id), edge(values.bottom, id)};
}
[[nodiscard]] inline Insets edges(const Insets& values, NodeId) noexcept { return values; }
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
[[nodiscard]] inline const LayoutBox& resolve_layout_box(const LayoutNode& node,
    std::optional<Unit> containing_width, std::optional<Unit> containing_height) {
    const auto width_key = node.box_depends_on_width ? containing_width : std::nullopt;
    const auto height_key = node.box_depends_on_height ? containing_height : std::nullopt;
    if (!node.resolved_box || node.resolved_box->width != width_key
        || node.resolved_box->height != height_key
        || node.resolved_box->container != !node.is_leaf()) {
        update_layout_box_cache(node, containing_width, containing_height);
    }
    return node.resolved_box->box.read();
}
[[nodiscard]] inline Unit content_origin_x(const LayoutBox& box, Insets border,
                      Insets padding, NodeId node) {
    return checked(static_cast<double>(padding.left) + border.left
        + (box.direction == Direction::Rtl ? box.scrollbar.width : 0), node);
}
void set_fragment_border_size(Fragment& fragment, Size size, const LayoutBox& box);
void publish_item_box(Fragment& fragment, const LayoutNode& node, const LayoutBox& box);
[[nodiscard]] FragmentRoot make_layout(const LayoutNode& node, Size size,
    const LayoutBox& resolved, std::optional<Unit> first_baseline = std::nullopt,
    std::optional<Unit> last_baseline = std::nullopt);
[[nodiscard]] inline std::optional<Unit> child_baseline(const Fragment& fragment, const Style& style) {
    if (!fragment.root) return std::nullopt;
    const auto baseline = fragment.root_layout().first_baseline;
    if (style.overflow_y == Overflow::Hidden || style.overflow_y == Overflow::Scroll) {
        return std::clamp(baseline.value_or(fragment.size.height),
            Unit{0}, fragment.size.height);
    }
    return baseline;
}
void compute_scrollable_overflow(Fragment& fragment, const LayoutNode& node,
    const LayoutNodes& nodes);
[[nodiscard]] std::pair<Unit, Unit> relative_offset(const LayoutNode& node,
    Size containing, Direction containing_direction, NodeId parent);

class Engine {
    template<bool> friend class FlexLayout;
    template<bool> friend class FlexItemSizer;
public:
    Engine(const LayoutNodes& nodes, TreeLimits limits, LayoutCache& cache,
        const MeasureProvider* measure_provider = nullptr, bool collect_grid_tracks = true);
    [[nodiscard]] LayoutResult compute(NodeId root, AvailableSize available,
        std::vector<NodeLayout> output, PublicationContext publication,
        std::optional<PixelRounding> rounding = std::nullopt,
        Fragment* retained = nullptr, bool publish_nodes = true);
    void commit_caches() { cache_.commit(); }

private:
    const LayoutNodes& nodes_;
    CacheTransaction cache_;
    TreeLimits limits_;
    std::size_t measurements_{};
    std::size_t layouts_{};
    std::size_t hits_{};
    bool root_hit_{};
    const MeasureProvider* measure_provider_{};
    bool collect_grid_tracks_{true};
    [[nodiscard]] bool has_measure(const LayoutNode& node) const noexcept {
        return bool(node.measure) || measure_provider_;
    }
    FragmentDetail detail_{FragmentDetail::Full};
    std::unordered_map<std::uint64_t, bool> float_subtree_;
    [[nodiscard]] const LayoutNode& get(NodeId id) const {
        const auto* node = nodes_.find(id.value);
        if (!node) throw LayoutFailure(LayoutErrorCode::InvalidRoot, id, "CSS node is absent");
        return *node;
    }
    [[nodiscard]] bool has_float_subtree(NodeId id, std::size_t depth);
    void translate(Fragment& fragment, Unit x, Unit y, NodeId node);
    void apply_relative_position(Fragment& fragment, const LayoutNode& node,
        Size containing, Direction containing_direction, NodeId parent) {
        if (!node.has_relative_inset) return;
        const auto [x, y] = relative_offset(node, containing, containing_direction, parent);
        translate(fragment, x, y, node.id);
    }
    [[nodiscard]] Fragment layout(const LayoutNode& node, const Constraints& constraints, std::size_t depth,
                                  BlockContext* block_context = nullptr);
    [[nodiscard]] Fragment compute_container(const LayoutNode& node, const Constraints& constraints,
        LayoutBox box, std::size_t depth, BlockContext* block_context = nullptr);
    [[nodiscard]] Fragment layout_sizing(const LayoutNode& node, const Constraints& constraints,
                                         std::size_t depth);
    [[nodiscard]] Size layout_size(const LayoutNode& node, const Constraints& constraints,
                                       std::size_t depth);
    [[nodiscard]] Fragment layout_container(const LayoutNode& node, const Constraints& constraints,
        const LayoutBox& box, std::size_t depth, BlockContext* block_context);
    [[nodiscard]] LayoutBox query_box(const LayoutNode& node, const Constraints& constraints) const;
    [[nodiscard]] Fragment leaf(const LayoutNode& node, const Constraints& constraints,
                                const LayoutBox& box);
    [[nodiscard]] LeafSizingResult size_leaf(const LayoutNode& node, const Constraints& constraints,
                                             const LayoutBox& box, bool include_content);
    [[nodiscard]] std::optional<Unit> intrinsic_container_size(const LayoutNode& node,
        const Constraints& constraints, const LayoutBox& box,
        std::size_t depth, bool horizontal);
    [[nodiscard]] Fragment block(const LayoutNode& node, const Constraints& constraints,
                                 const LayoutBox& box, std::size_t depth,
                                 BlockContext* block_context);
    [[nodiscard]] Unit block_content_based_width(const LayoutNode& node,
        AvailableSpace available, ResolvedDimensions percentage_basis, std::size_t depth);
    [[nodiscard]] Fragment flex(const LayoutNode& node, const Constraints& constraints,
                                const LayoutBox& box, std::size_t depth);
    void prepare_flex_item(FlexItem& item, std::pmr::vector<LayoutBox>& item_boxes, const LayoutNode& node,
        const LayoutNode& child, const Constraints& constraints,
        const FlexItemSizingInput& input, std::size_t depth);
    [[nodiscard]] Fragment grid(const LayoutNode& node, const Constraints& constraints,
                                const LayoutBox& box, std::size_t depth);
    [[nodiscard]] std::shared_ptr<const GridStructure> resolve_grid_structure(
        const LayoutNode& node, GridStructureKey key) const;
    [[nodiscard]] GridAxisContribution grid_axis_contribution(NodeId id,
        const GridContributionInput& input, std::size_t depth);
    [[nodiscard]] std::pmr::vector<GridBaselineItem> measure_grid_baselines(const LayoutNode& parent,
        std::span<const Placement> placements, ResolvedDimensions container_inner,
        std::size_t depth);
    [[nodiscard]] Fragment hidden(const LayoutNode& node, std::size_t depth);
    void append_fragment(Fragment& parent, Fragment&& child);
    [[nodiscard]] Fragment absolute(const LayoutNode& parent, NodeId child,
        Size parent_size, const LayoutBox& parent_box, std::size_t depth,
        std::optional<Rect> containing_override = std::nullopt,
        std::optional<Unit> static_block_y = std::nullopt);
    [[nodiscard]] Fragment positioned_item(const LayoutNode& child,
        const LayoutBox& box, const PositionedItemInput& input, std::size_t depth);
    [[nodiscard]] Measurement measure(const LayoutNode& node, const MeasureRequest& request);
};

} // namespace axisly::css::detail
