#pragma once
#include <axisly/css_tree.hpp>
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

// Every completed query owns its root inline. Only Full queries collect
// descendants; size/baseline probes and their cache copies need no node buffer.
// A default fragment is an empty slot in a parent's child workspace.
struct Fragment {
    Size size{};
    std::optional<NodeLayout> root;
    std::vector<NodeLayout> descendants;
    std::vector<GridTracks> grid_tracks;
    std::optional<BlockMargins> block_margins;

    [[nodiscard]] NodeLayout& root_layout() { return root.value(); }
    [[nodiscard]] const NodeLayout& root_layout() const { return root.value(); }
    [[nodiscard]] std::size_t node_count() const noexcept;
    void append_nodes(Fragment&& child);
    [[nodiscard]] std::vector<NodeLayout> take_nodes() &&;
};
} // namespace axisly::css::detail
