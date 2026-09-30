#pragma once
#include <axisly/css_tree.hpp>
#include <memory>
#include <optional>
#include <vector>

namespace axisly::css::detail {
struct MarginStrut {
    Unit positive{}, negative{};
    [[nodiscard]] static MarginStrut from(Unit value);
    [[nodiscard]] MarginStrut merged(MarginStrut other) const;
    [[nodiscard]] Unit resolve(NodeId node) const;
};
struct BlockMargins {
    MarginStrut top, bottom;
    bool through{};
};

struct FragmentChildren;
struct FragmentTranslation {
    Unit x{}, y{};
    NodeId node;
    std::shared_ptr<const FragmentTranslation> previous;
};

// Every completed query owns its root inline. Full queries share immutable
// descendants; cache hits copy handles instead of the whole subtree. Parent
// translations are replayed in order when publishing, retaining float rounding
// and overflow checks at every original coordinate transformation.
// A default fragment is an empty slot in a parent's child workspace.
struct Fragment {
    Size size{};
    std::optional<NodeLayout> root;
    std::shared_ptr<FragmentChildren> descendants;
    std::shared_ptr<const FragmentTranslation> translation;
    std::vector<GridTracks> grid_tracks;
    std::optional<BlockMargins> block_margins;

    [[nodiscard]] NodeLayout& root_layout() { return root.value(); }
    [[nodiscard]] const NodeLayout& root_layout() const { return root.value(); }
    [[nodiscard]] std::size_t node_count() const noexcept;
    void reserve_children(std::size_t count);
    void append_nodes(Fragment&& child);
    void translate_descendants(Unit x, Unit y, NodeId node);
    [[nodiscard]] std::vector<NodeLayout> take_nodes() &&;
};

struct FragmentBranch {
    NodeLayout root;
    std::shared_ptr<const FragmentChildren> descendants;
    std::shared_ptr<const FragmentTranslation> translation;
};

struct FragmentChildren {
    // Leaf-only children remain contiguous, allowing a large flat root to
    // publish its existing buffer. Mixed subtrees retain shared branches.
    std::vector<NodeLayout> leaves;
    std::vector<FragmentBranch> branches;
    std::size_t node_count{};
    std::size_t reserve_hint{};
    bool branched{};
};
} // namespace axisly::css::detail
