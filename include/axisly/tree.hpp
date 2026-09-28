#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <axisly/measurement.hpp>
#include <axisly/rounding.hpp>

namespace axisly {

// Process-unique, never-reused identity. Zero is an invalid handle. Handles from
// other trees and removed nodes are rejected even if storage is later reused.
struct NodeId {
    std::uint64_t value{};
    bool operator==(const NodeId&) const = default;
};
struct TreeLimits {
    std::size_t maximum_depth{64};
    std::size_t maximum_layouts{100'000};
};
struct NodeLayout {
    NodeId node;
    ItemId item_id{};
    // Root-relative logical coordinates, optionally snapped after the whole tree.
    Rect rect{}, cell_rect{};
};
struct TreeResult {
    Size size{};
    std::vector<NodeLayout> items; // Root first, then children in depth-first order.
    GridError error;
    std::optional<NodeId> error_node;
    std::size_t layout_count{};      // Actual grid recomputations this call.
    std::size_t measurement_count{}; // Actual host leaf callbacks this call.
    std::size_t cache_hits{};        // Reused grid results this call.
    bool cache_hit{};               // Entire published tree reused.
    [[nodiscard]] bool succeeded() const noexcept { return error.code == GridErrorCode::None; }
    [[nodiscard]] const NodeLayout* find(NodeId node) const noexcept;
};

// Optional retained layer. Owns nodes, styles, topology and caches; never widgets
// or callback contexts. Changes invalidate the affected node and ancestors.
// Not thread-safe. Callback mutation/reentry into this tree throws logic_error.
class GridTree {
public:
    explicit GridTree(TreeLimits limits = {});
    ~GridTree();
    GridTree(const GridTree&) = delete;
    GridTree& operator=(const GridTree&) = delete;
    GridTree(GridTree&&) = delete;
    GridTree& operator=(GridTree&&) = delete;

    [[nodiscard]] NodeId create_grid(GridSpec spec = {}, GridItem item = {});
    [[nodiscard]] NodeId create_leaf(GridItem item = {}, MeasureCallback measure = nullptr, void* user_data = nullptr);
    [[nodiscard]] bool contains(NodeId node) const noexcept;
    [[nodiscard]] bool is_grid(NodeId node) const;
    [[nodiscard]] bool is_dirty(NodeId node) const;
    [[nodiscard]] const GridItem& item(NodeId node) const;
    [[nodiscard]] const GridSpec& spec(NodeId grid) const;
    [[nodiscard]] std::span<const NodeId> children(NodeId grid) const;
    [[nodiscard]] std::optional<NodeId> parent(NodeId node) const;

    void set_item(NodeId node, GridItem item);
    void set_spec(NodeId grid, const GridSpec& spec);
    void set_measure(NodeId leaf, MeasureCallback measure, void* user_data = nullptr);
    // A child has at most one parent. Reparenting and reordering are supported;
    // index is in the destination list AFTER removing this child, if present.
    void insert_child(NodeId grid, std::size_t index, NodeId child);
    void append_child(NodeId grid, NodeId child);
    void detach(NodeId child);
    void remove(NodeId node); // Removes the entire subtree; handles stay invalid.
    // Call after external text/font/DPI/content changes. A grid invalidates its
    // complete subtree; a leaf invalidates only its path to the root.
    void mark_dirty(NodeId node);

    // root must be a grid. Finite nested measurement probes use available size,
    // zero-height at the same width, and zero-space width (native, not CSS).
    // Borrowed until next compute/mutation/destruction. Copy for a snapshot.
    // Errors publish no partial geometry; user callback exceptions propagate.
    [[nodiscard]] const TreeResult& compute(NodeId root, Size available,
        std::optional<PixelRounding> rounding = std::nullopt) &;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
