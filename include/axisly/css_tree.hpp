#pragma once

#include <axisly/css_measurement.hpp>
#include <axisly/css_memory.hpp>
#include <axisly/css_style.hpp>
#include <axisly/rounding.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace axisly::css {
class LayoutSource;
class LayoutSession;

struct NodeId {
    std::uint64_t value{};
    bool operator==(const NodeId&) const = default;
};

struct AvailableSize {
    AvailableSpace width{AvailableSpace::indefinite()};
    AvailableSpace height{AvailableSpace::indefinite()};
    bool operator==(const AvailableSize&) const = default;
};

// Shared measurement policy for a computation. Per-node MeasureFunction takes
// precedence. The provider must remain alive for the call, and must not mutate
// the tree. Mark changed nodes dirty when its externally owned content changes.
class MeasureProvider {
public:
    virtual ~MeasureProvider() = default;
    [[nodiscard]] virtual Measurement measure(NodeId node, const Style& style,
        void* context, const MeasureRequest& request) const = 0;
};

enum class CoordinateSpace { Root, Parent };
struct LayoutOptions {
    std::optional<PixelRounding> rounding;
    bool collect_grid_tracks{true};
    bool publish_nodes{true};
    const MeasureProvider* measure_provider{};
    // Increment when changing shared measurement behavior without identifying
    // individual dirty nodes. A changed identity/revision invalidates caches.
    std::uint64_t measurement_revision{};
    bool operator==(const LayoutOptions&) const = default;
};

struct TreeLimits {
    std::size_t maximum_depth{64};
    std::size_t maximum_nodes{100'000};
    std::size_t maximum_measurements{100'000};
    std::size_t maximum_cached_layouts{100'000};
    std::size_t maximum_cached_measurements{100'000};
};

enum class LayoutErrorCode {
    None, InvalidRoot, InvalidStyle, Unsupported, UnresolvedPercentage,
    InvalidMeasurement, NumericOverflow, LimitExceeded,
};

struct LayoutError {
    LayoutErrorCode code{LayoutErrorCode::None};
    std::optional<NodeId> node;
    std::string message;
};

struct NodeLayout {
    NodeId node;
    std::optional<NodeId> parent;
    // Root-relative border and content rectangles; margin is outside border.
    Rect border_box{}, content_box{};
    Insets margin{}, padding{}, border{};
    Size scrollbar{};
    std::optional<Unit> first_baseline, last_baseline;
    // Node-local scrollable region; unlike border_box, it is not root-relative.
    Rect scrollable_overflow_rect{};
    bool operator==(const NodeLayout&) const = default;
};

struct GridTrackPosition {
    Unit start{}, end{}; // Grid border-box-relative physical coordinates.
    bool operator==(const GridTrackPosition&) const = default;
};

struct GridItemPlacement {
    NodeId node;
    // Origin-zero line numbers in the owning Grid's explicit coordinate system.
    std::int32_t row_start{}, row_end{}, column_start{}, column_end{};
    bool operator==(const GridItemPlacement&) const = default;
};

struct GridTracks {
    NodeId node;
    // First track's line number relative to the explicit grid's first line.
    std::int32_t row_origin{}, column_origin{};
    std::size_t explicit_rows{}, explicit_columns{};
    // Used track sizes and positions in implicit-grid order, including collapsed tracks.
    std::vector<Unit> rows, columns;
    std::vector<GridTrackPosition> row_positions, column_positions;
    // Names in implicit-grid line order (tracks + 1), or empty if all lines
    // are unnamed. Repeat boundaries retain source order without duplicates.
    std::vector<std::vector<std::string>> row_line_names, column_line_names;
    // In-flow children only; line zero is the explicit grid's first line.
    std::vector<GridItemPlacement> items;
    bool operator==(const GridTracks&) const = default;
};

struct LayoutResult {
    Size size{};
    std::vector<NodeLayout> nodes; // Root first, then depth-first order.
    std::vector<GridTracks> grid_tracks;
    LayoutError error;
    std::size_t measurement_count{};
    std::size_t layout_count{};
    std::size_t cache_hits{};
    // Nodes copied as previously validated coordinate ranges during publication.
    std::size_t published_nodes_reused{};
    bool cache_hit{};
    [[nodiscard]] bool succeeded() const noexcept { return error.code == LayoutErrorCode::None; }
    [[nodiscard]] const NodeLayout* find(NodeId node) const noexcept;
};

// Retained CSS layout tree. Host measurement callbacks are stored by value.
// Mutations and reentrant compute from a callback are rejected.
class LayoutTree {
public:
    // Retain memory outside temporary trees to reuse their large allocations.
    // An omitted resource uses ordinary system allocation. The tree shares
    // ownership of a supplied resource for the lifetime of its internal data.
    explicit LayoutTree(TreeLimits limits = {}, std::shared_ptr<LayoutMemory> memory = {});
    ~LayoutTree();
    LayoutTree(const LayoutTree&) = delete;
    LayoutTree& operator=(const LayoutTree&) = delete;
    LayoutTree(LayoutTree&&) = delete;
    LayoutTree& operator=(LayoutTree&&) = delete;

    // Copy a reusable style directly into its node, or transfer an owned style.
    [[nodiscard]] NodeId create_node(const Style& style = {}, MeasureFunction measure = {});
    [[nodiscard]] NodeId create_node(Style&& style, MeasureFunction measure = {});
    [[nodiscard]] NodeId create_node_with_children(Style style,
        std::span<const NodeId> children, MeasureFunction measure = {});
    [[nodiscard]] bool contains(NodeId node) const noexcept;
    [[nodiscard]] std::size_t node_count() const noexcept;
    [[nodiscard]] const Style& style(NodeId node) const;
    [[nodiscard]] std::optional<NodeId> parent(NodeId node) const;
    [[nodiscard]] std::span<const NodeId> children(NodeId node) const;
    [[nodiscard]] NodeId child_at(NodeId parent, std::size_t index) const;
    [[nodiscard]] std::size_t child_count(NodeId parent) const;
    void set_style(NodeId node, Style style);
    void set_measure(NodeId node, MeasureFunction measure);
    void set_context(NodeId node, std::shared_ptr<void> context);
    [[nodiscard]] const std::shared_ptr<void>& context(NodeId node) const;
    void insert_child(NodeId parent, std::size_t index, NodeId child);
    void append_child(NodeId parent, NodeId child);
    // All topology operations validate/allocate before changing the tree.
    // Moving existing nodes preserves their identities and contexts.
    void set_children(NodeId parent, std::span<const NodeId> children);
    [[nodiscard]] NodeId replace_child(NodeId parent, std::size_t index, NodeId child);
    [[nodiscard]] NodeId detach_child_at(NodeId parent, std::size_t index);
    void detach_children(NodeId parent, std::size_t first, std::size_t count);
    void detach(NodeId child);
    // remove() retains its original recursive semantics for source compatibility.
    void remove(NodeId node);
    void remove_subtree(NodeId node);
    // Delete only this node; direct children become roots and remain alive.
    void remove_node(NodeId node);
    void clear();
    // Invalidate this node and its ancestors. External changes to multiple
    // descendants require mark_subtree_dirty(), or each changed leaf individually.
    void mark_dirty(NodeId node);
    void mark_subtree_dirty(NodeId node);
    [[nodiscard]] bool dirty(NodeId node) const;

    // A failed computation publishes an empty result with an error. Host
    // callback exceptions propagate without publishing partial geometry.
    [[nodiscard]] const LayoutResult& compute(NodeId root, AvailableSize available) &;

    // Publish device-pixel geometry from the original logical layout. Scale
    // must be positive and finite; origins must be finite. Invalid options
    // throw invalid_argument before computing. The two-argument overload
    // remains unrounded. Both overloads share the same result lifetime.
    // Margins and Grid diagnostic tracks retain their logical values.
    [[nodiscard]] const LayoutResult& compute(NodeId root, AvailableSize available,
        PixelRounding rounding) &;
    [[nodiscard]] const LayoutResult& compute(NodeId root, AvailableSize available,
        LayoutOptions options) &;

    // Read a value from the last successful publication. With publish_nodes=false
    // this does not flatten the entire tree. Unrounded logical geometry remains
    // available even when the published snapshot uses pixel rounding.
    [[nodiscard]] std::optional<NodeLayout> layout(NodeId node,
        CoordinateSpace space = CoordinateSpace::Root) const;
    [[nodiscard]] std::optional<NodeLayout> unrounded_layout(NodeId node,
        CoordinateSpace space = CoordinateSpace::Root) const;

    // Find in the last published snapshot. The lazy index is reused while
    // node positions in the output remain valid; a changed order rebuilds it
    // on demand. No index is allocated by compute(). Identities absent from
    // the snapshot return nullptr. Tree mutations alone keep the old snapshot,
    // including nodes removed since that publication, until the next compute.
    // The pointer has the same lifetime as a node in compute().nodes.
    [[nodiscard]] const NodeLayout* find_layout(NodeId node) const;

private:
    friend class LayoutSession;
    void attach_source(const LayoutSource& source);
    void source_dirty(std::optional<NodeId> node);
    const LayoutResult& compute_impl(NodeId root, AvailableSize available,
        LayoutOptions options);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace axisly::css
