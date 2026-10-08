#pragma once

#include <axisly/css_tree.hpp>

namespace axisly::css {

// Borrowed directly from the host's tree. Style and children storage must stay
// valid and unchanged throughout compute(). IDs are host-defined, nonzero,
// unique within the source, and must not alias a live node after reuse.
struct LayoutNodeView {
    const Style& style;
    std::span<const NodeId> children{};
    std::optional<NodeId> parent;
    void* context{};
};

class LayoutSource {
public:
    virtual ~LayoutSource() = default;
    [[nodiscard]] virtual std::optional<LayoutNodeView> node(NodeId id) const = 0;
};

// Owns caches and published geometry, while the host owns styles and topology.
// There is no copied Style/children tree to synchronize. A source must outlive
// its session. One session is exclusive; independent sessions may run in parallel.
class LayoutSession {
public:
    explicit LayoutSession(const LayoutSource& source, TreeLimits limits = {},
        std::shared_ptr<LayoutMemory> memory = {});
    // Call after a style/content edit. Only this node and ancestors are invalidated.
    void mark_dirty(NodeId node);
    // Call after topology changes, removals, ID reuse, or a whole-source update.
    // Drops node bindings/caches; the previous publication remains readable.
    void invalidate();
    [[nodiscard]] const LayoutResult& compute(NodeId root, AvailableSize available,
        LayoutOptions options = {}) &;
    [[nodiscard]] std::optional<NodeLayout> layout(NodeId node,
        CoordinateSpace space = CoordinateSpace::Root) const;
    [[nodiscard]] std::optional<NodeLayout> unrounded_layout(NodeId node,
        CoordinateSpace space = CoordinateSpace::Root) const;

private:
    LayoutTree tree_;
};

} // namespace axisly::css
