#pragma once
#include <axisly/css_tree.hpp>
#include "layout_numeric.hpp"
#include "pooled_value.hpp"
#include <algorithm>
#include <memory>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace axisly::css::detail {
struct MarginStrut {
    Unit positive{}, negative{};
    [[nodiscard]] static MarginStrut from(Unit value) noexcept {
        return {std::max(Unit{0}, value), std::min(Unit{0}, value)};
    }
    [[nodiscard]] MarginStrut merged(MarginStrut other) const noexcept {
        return {std::max(positive, other.positive), std::min(negative, other.negative)};
    }
    [[nodiscard]] Unit resolve(NodeId node) const {
        return checked(static_cast<double>(positive) + negative, node);
    }
};
struct BlockMargins {
    MarginStrut top, bottom;
    bool through{};
};

struct FragmentChildren;
struct FragmentGridReports;
struct LayoutPosition { Unit x{}, y{}; };
[[nodiscard]] NodeLayout round_node_layout(const NodeLayout& raw,
    LayoutPosition logical_parent, LayoutPosition pixel_parent,
    PixelRounding rounding, bool root);
struct PublicationContext {
    std::span<const NodeLayout> previous;
    std::uint64_t previous_epoch{}, next_epoch{};
    std::size_t reused_nodes{};
};
// Keep algorithm metadata with the pooled geometry, rather than inflating every
// transient Fragment as it passes through layout and cache queries.
struct LayoutGeometry {
    NodeLayout layout;
    Insets overflow_origin_border{};
    std::optional<Unit> overflow_coordinate_width;
};
using FragmentRoot = PooledValue<LayoutGeometry, false>;
using FragmentRootPool = ValuePool<LayoutGeometry, false>;

struct FragmentTranslation;
using FragmentTranslationHandle = PooledValue<FragmentTranslation>;
using FragmentTranslationPool = ValuePool<FragmentTranslation>;
struct FragmentTranslation {
    Unit x{}, y{};
    NodeId node;
    FragmentTranslationHandle previous;
};

// Subtrees are immutable while shared. Translation history preserves the
// rounding and overflow checks of each original coordinate transformation.
struct FragmentState {
    Size size{};
    std::shared_ptr<FragmentChildren> descendants;
    FragmentTranslationHandle translation;
    std::shared_ptr<FragmentGridReports> grid_reports;
    std::optional<BlockMargins> block_margins;
    bool overflow_dirty{};
};

// Working queries keep a compact root handle. Caches store a separate value
// snapshot, so admitting a query does not make parent placement detach it.
// A default fragment is an empty slot in a parent's child workspace.
struct Fragment : FragmentState {
    FragmentRoot root;

    // Root ownership is unique. Descendant subtrees remain immutable while
    // shared; a cached root is a separate snapshot of this value.
    [[nodiscard]] NodeLayout& mutable_root_layout() { return root.write().layout; }
    [[nodiscard]] const NodeLayout& root_layout() const noexcept { return root.read().layout; }
    [[nodiscard]] std::size_t node_count() const noexcept;
    void reserve_children(std::size_t count);
    void append_nodes(Fragment&& child);
    void translate_descendants(Unit x, Unit y, NodeId node, FragmentTranslationPool& pool);
    [[nodiscard]] std::vector<NodeLayout> take_nodes(std::vector<NodeLayout> output,
        PublicationContext& publication) &&;
    [[nodiscard]] std::vector<NodeLayout> rounded_nodes(std::vector<NodeLayout> output,
        PixelRounding rounding) const;
    [[nodiscard]] std::optional<NodeLayout> lookup(NodeId node,
        std::span<const NodeId> path, std::optional<PixelRounding> rounding,
        CoordinateSpace space = CoordinateSpace::Root) const;
    void validate_nodes() const;
    void validate_rounding(PixelRounding rounding) const;
    void add_grid_report(GridTracks&& report);
    void append_grid_reports(Fragment&& child);
    [[nodiscard]] std::vector<GridTracks> take_grid_tracks() &&;
};

struct FragmentSnapshot : FragmentState {
    std::optional<LayoutGeometry> root;
    FragmentSnapshot() = default;
    explicit FragmentSnapshot(const Fragment& fragment) noexcept
        : FragmentState(fragment),
          root(fragment.root ? std::optional<LayoutGeometry>(fragment.root.read()) : std::nullopt) {}
    [[nodiscard]] Fragment restore(FragmentRootPool& pool) const;
};

struct FragmentBranch {
    NodeLayout root;
    std::shared_ptr<const FragmentChildren> descendants;
    FragmentTranslationHandle translation;
};

struct FragmentChildren {
    FragmentChildren() = default;
    FragmentChildren(const FragmentChildren& other)
        : leaves(other.leaves), branches(other.branches), node_count(other.node_count),
          reserve_hint(other.reserve_hint), branched(other.branched) {}
    // Leaf-only children remain contiguous, allowing a large flat root to
    // publish its existing buffer. Mixed subtrees retain shared branches.
    std::vector<NodeLayout> leaves;
    std::vector<FragmentBranch> branches;
    std::size_t node_count{};
    std::size_t reserve_hint{};
    bool branched{};
    // Built only for value lookup. Ordered IDs need no permutation; arbitrary
    // host IDs and CSS order use offsets into the immutable child collection.
    struct LookupIndex { std::vector<std::size_t> order; };
    mutable std::unique_ptr<LookupIndex> lookup_index;
    [[nodiscard]] std::optional<std::size_t> find_child(NodeId node) const;
    // Coordinates in the last published snapshot. This is only a hint: both
    // snapshot identity and every translation must match before range reuse.
    // A copied or modified child collection always drops this hint.
    struct Publication {
        std::uint64_t epoch{};
        std::size_t offset{};
        std::vector<std::pair<std::uint32_t, std::uint32_t>> translations;
    };
    mutable Publication publication;
};

struct FragmentGridReports {
    std::vector<std::shared_ptr<GridTracks>> reports;
};
} // namespace axisly::css::detail
