#include <gridly/tree.hpp>
#include <gridly/cache.hpp>
#include "detail/rounding.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace gridly {
namespace {
NodeId allocate_id() {
    static std::atomic<std::uint64_t> next{1};
    auto candidate = next.load(std::memory_order_relaxed);
    for (;;) {
        if (candidate == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("GridTree node identities exhausted");
        if (next.compare_exchange_weak(candidate, candidate + 1, std::memory_order_relaxed)) return {candidate};
    }
}
struct Abort { GridError error; NodeId node; };
}

const NodeLayout* TreeResult::find(NodeId node) const noexcept {
    const auto found = std::find_if(items.begin(), items.end(), [&](const NodeLayout& layout) { return layout.node == node; });
    return found == items.end() ? nullptr : &*found;
}

struct GridTree::Impl {
    struct Node {
        NodeId id;
        GridItem item;
        GridSpec spec;
        std::optional<NodeId> parent;
        std::vector<NodeId> children;
        std::unique_ptr<GridCache> cache;
        MeasureCallback measure{};
        void* context{};
        bool dirty{true}, inputs_dirty{true};
    };
    std::unordered_map<std::uint64_t, std::unique_ptr<Node>> nodes;
    TreeLimits limits;
    TreeResult result;
    NodeId last_root;
    Size last_available{};
    std::optional<PixelRounding> last_rounding;
    bool published{}, computing{};
    std::size_t depth{}, work{};
    TreeResult* pending{};

    explicit Impl(TreeLimits value) : limits(value) {
        if (limits.maximum_depth == 0 || limits.maximum_layouts == 0)
            throw std::invalid_argument("Tree depth and layout limits must be positive");
    }
    void idle() const {
        if (computing) throw std::logic_error("GridTree cannot compute or mutate inside its measurement callback");
    }
    Node& get(NodeId id) const {
        const auto found = nodes.find(id.value);
        if (found == nodes.end()) throw std::out_of_range("Node handle is removed, foreign or invalid");
        return *found->second;
    }
    Node& grid(NodeId id) const {
        auto& node = get(id);
        if (!node.cache) throw std::invalid_argument("Expected a grid node");
        return node;
    }
    void invalidate_path(Node& node, bool clear_self) {
        published = false;
        node.dirty = true;
        if (clear_self && node.cache) node.cache->invalidate();
        auto* child = &node;
        while (child->parent) {
            auto& p = get(*child->parent);
            p.dirty = true;
            p.cache->invalidate_item(child->id.value);
            child = &p;
        }
    }
    std::vector<NodeId> subtree(NodeId root) const {
        std::vector<NodeId> result_nodes{root};
        for (std::size_t i = 0; i < result_nodes.size(); ++i) {
            const auto& children = get(result_nodes[i]).children;
            result_nodes.insert(result_nodes.end(), children.begin(), children.end());
        }
        return result_nodes;
    }
    void sync(Node& node) {
        if (!node.inputs_dirty) return;
        node.cache->set_spec(node.spec);
        std::vector<GridItem> items;
        items.reserve(node.children.size());
        for (const auto id : node.children) {
            auto item = get(id).item;
            item.id = id.value; // Stable internal identity, independent of host IDs.
            items.push_back(item);
        }
        const auto previous = node.cache->items();
        const bool same_order = previous.size() == items.size()
            && std::equal(previous.begin(), previous.end(), items.begin(), [](const GridItem& a, const GridItem& b) { return a.id == b.id; });
        if (same_order) {
            for (std::size_t i = 0; i < items.size(); ++i) node.cache->set_item(i, items[i]);
        } else node.cache->set_items(items);
        node.inputs_dirty = false;
    }
    [[noreturn]] void fail(GridErrorCode code, const char* message, NodeId id) const {
        throw Abort{{code, message, get(id).item.id}, id};
    }
    struct Depth {
        Impl& tree;
        Depth(Impl& t, NodeId id) : tree(t) {
            if (tree.depth >= tree.limits.maximum_depth) tree.fail(GridErrorCode::TreeLimit, "Tree depth limit exceeded", id);
            ++tree.depth;
        }
        ~Depth() { --tree.depth; }
    };
    const GridResult& solve(Node& node, Size available) {
        if (work >= limits.maximum_layouts) fail(GridErrorCode::TreeLimit, "Tree layout work limit exceeded", node.id);
        ++work;
        sync(node);
        const auto computed = node.cache->compute(available);
        if (computed.cache_hit) ++pending->cache_hits;
        else ++pending->layout_count;
        if (!computed.result.succeeded()) {
            auto error = computed.result.error;
            NodeId error_node = node.id;
            if (error.item_id && nodes.contains(*error.item_id)) error_node = {*error.item_id};
            error.item_id = get(error_node).item.id;
            throw Abort{std::move(error), error_node};
        }
        return computed.result;
    }
    MeasureResult measure(NodeId id, const MeasureInput& input) {
        auto& node = get(id);
        if (!node.cache) {
            if (!node.measure) return {{}, node.item.preferred_size, node.item.baseline};
            ++pending->measurement_count;
            return node.measure(node.item.id, input, node.context);
        }
        const Depth scope{*this, id};
        const Size available{input.known_width.value_or(input.available.width), input.known_height.value_or(input.available.height)};
        // Copy sizes before another compute mutates this node's borrowed result.
        const Size preferred = solve(node, available).size;
        const Size height_floor = available.height == 0 ? preferred : solve(node, {available.width, 0}).size;
        const Size width_floor = available.width == 0 ? height_floor : solve(node, {}).size;
        return {{std::min(width_floor.width, preferred.width), std::min(height_floor.height, preferred.height)}, preferred, {}};
    }
    Unit coordinate(double value, NodeId node) const {
        if (!std::isfinite(value) || value < 0 || value > std::numeric_limits<Unit>::max())
            fail(GridErrorCode::NumericOverflow, "Tree coordinates exceed finite Unit range", node);
        return static_cast<Unit>(value);
    }
    Rect translated(Rect local, double x, double y, NodeId id) const {
        (void)coordinate(x + local.x + local.width, id);
        (void)coordinate(y + local.y + local.height, id);
        return {coordinate(x + local.x, id), coordinate(y + local.y, id), local.width, local.height};
    }
    Size arrange(Node& node, Size available, double x, double y, bool root) {
        const Depth scope{*this, node.id};
        const auto& solved = solve(node, available);
        const Size extent = solved.size;
        (void)coordinate(x + extent.width, node.id);
        (void)coordinate(y + extent.height, node.id);
        if (root) pending->items.push_back({node.id, node.item.id, {0, 0, extent.width, extent.height}, {0, 0, extent.width, extent.height}});
        // Child calls operate on other caches, so this result's storage remains stable.
        for (const auto& item : solved.items) {
            auto& child = get({item.id});
            pending->items.push_back({child.id, child.item.id, translated(item.rect, x, y, child.id),
                translated(item.cell_rect, x, y, child.id)});
            if (child.cache) (void)arrange(child, {item.rect.width, item.rect.height}, x + item.rect.x, y + item.rect.y, false);
        }
        return extent;
    }
};

GridTree::GridTree(TreeLimits limits) : impl_(std::make_unique<Impl>(limits)) {}
GridTree::~GridTree() = default;
NodeId GridTree::create_grid(GridSpec spec, GridItem item) {
    impl_->idle();
    auto node = std::make_unique<Impl::Node>();
    node->id = allocate_id(); node->spec = std::move(spec); node->item = item;
    node->cache = std::make_unique<GridCache>(node->spec);
    node->cache->enable_measurement_cache();
    node->cache->set_measure(+[](ItemId id, const MeasureInput& input, void* context) {
        return static_cast<Impl*>(context)->measure({id}, input);
    }, impl_.get());
    const auto id = node->id;
    impl_->nodes.emplace(id.value, std::move(node));
    return id;
}
NodeId GridTree::create_leaf(GridItem item, MeasureCallback measure, void* user_data) {
    impl_->idle();
    auto node = std::make_unique<Impl::Node>();
    node->id = allocate_id(); node->item = item; node->measure = measure; node->context = measure ? user_data : nullptr;
    const auto id = node->id;
    impl_->nodes.emplace(id.value, std::move(node));
    return id;
}
bool GridTree::contains(NodeId node) const noexcept { return impl_->nodes.contains(node.value); }
bool GridTree::is_grid(NodeId node) const { return static_cast<bool>(impl_->get(node).cache); }
bool GridTree::is_dirty(NodeId node) const { return impl_->get(node).dirty; }
const GridItem& GridTree::item(NodeId node) const { return impl_->get(node).item; }
const GridSpec& GridTree::spec(NodeId grid) const { return impl_->grid(grid).spec; }
std::span<const NodeId> GridTree::children(NodeId grid) const { return impl_->grid(grid).children; }
std::optional<NodeId> GridTree::parent(NodeId node) const { return impl_->get(node).parent; }
void GridTree::set_item(NodeId id, GridItem item) {
    impl_->idle(); auto& node = impl_->get(id);
    if (node.item == item) return;
    node.item = item;
    if (node.parent) impl_->get(*node.parent).inputs_dirty = true;
    impl_->invalidate_path(node, false);
}
void GridTree::set_spec(NodeId id, const GridSpec& spec) {
    impl_->idle(); auto& node = impl_->grid(id);
    if (node.spec == spec) return;
    auto next = spec; node.spec = std::move(next); node.inputs_dirty = true;
    impl_->invalidate_path(node, true);
}
void GridTree::set_measure(NodeId id, MeasureCallback measure, void* user_data) {
    impl_->idle(); auto& node = impl_->get(id);
    if (node.cache) throw std::invalid_argument("Only leaves have host measurement callbacks");
    if (!measure) user_data = nullptr;
    if (node.measure == measure && node.context == user_data) return;
    node.measure = measure; node.context = user_data;
    impl_->invalidate_path(node, false);
}
void GridTree::insert_child(NodeId parent, std::size_t index, NodeId child) {
    impl_->idle(); auto& p = impl_->grid(parent); auto& c = impl_->get(child);
    for (auto current = std::optional<NodeId>{parent}; current; current = impl_->get(*current).parent)
        if (*current == child) throw std::invalid_argument("Attaching an ancestor would create a cycle");
    auto next = p.children;
    std::erase(next, child);
    if (index > next.size()) throw std::out_of_range("Child insertion index is outside the destination");
    next.insert(next.begin() + static_cast<std::ptrdiff_t>(index), child);
    if (next == p.children && c.parent == parent) return;
    // All allocation/validation precedes topology mutation.
    if (c.parent && *c.parent != parent) {
        auto& old = impl_->get(*c.parent);
        std::erase(old.children, child); old.inputs_dirty = true;
        impl_->invalidate_path(old, true);
    }
    p.children.swap(next); c.parent = parent; p.inputs_dirty = true;
    impl_->invalidate_path(p, true);
}
void GridTree::append_child(NodeId parent, NodeId child) {
    impl_->idle(); const auto& p = impl_->grid(parent); const auto& c = impl_->get(child);
    insert_child(parent, p.children.size() - (c.parent == parent ? 1u : 0u), child);
}
void GridTree::detach(NodeId child) {
    impl_->idle(); auto& c = impl_->get(child);
    if (!c.parent) return;
    auto& old = impl_->get(*c.parent);
    std::erase(old.children, child); c.parent.reset(); old.inputs_dirty = true;
    impl_->invalidate_path(old, true);
}
void GridTree::remove(NodeId id) {
    impl_->idle(); (void)impl_->get(id);
    const auto removed = impl_->subtree(id);
    detach(id);
    impl_->published = false;
    for (const auto node : removed) impl_->nodes.erase(node.value);
}
void GridTree::mark_dirty(NodeId id) {
    impl_->idle(); auto& node = impl_->get(id);
    const auto affected = impl_->subtree(id);
    for (const auto current : affected) {
        auto& dirty = impl_->get(current); dirty.dirty = true;
        if (dirty.cache) dirty.cache->invalidate();
    }
    impl_->invalidate_path(node, false);
}

const TreeResult& GridTree::compute(NodeId root, Size available, std::optional<PixelRounding> rounding) & {
    auto& tree = *impl_;
    tree.idle(); auto& node = tree.grid(root);
    if (tree.published && tree.last_root == root && tree.last_available == available && tree.last_rounding == rounding) {
        tree.result.layout_count = tree.result.measurement_count = tree.result.cache_hits = 0;
        tree.result.cache_hit = true;
        return tree.result;
    }
    tree.published = false; tree.computing = true;
    struct Guard {
        Impl& tree;
        bool completed{};
        ~Guard() {
            if (!completed) {
                // A host exception or allocation failure must not publish partial
                // geometry or reuse measurements from an interrupted update.
                tree.result = {};
                for (auto& [id, value] : tree.nodes) {
                    (void)id; value->dirty = true;
                    if (value->cache) value->cache->invalidate();
                }
            }
            tree.computing = false; tree.pending = nullptr; tree.depth = 0;
        }
    } guard{tree};
    TreeResult next;
    tree.pending = &next; tree.depth = tree.work = 0;
    try {
        if (!std::isfinite(available.width) || !std::isfinite(available.height) || available.width < 0 || available.height < 0)
            tree.fail(GridErrorCode::InvalidContainerSize, "Tree available size must be finite and non-negative", root);
        if (rounding && !detail::valid_rounding(*rounding)) tree.fail(GridErrorCode::InvalidRounding, "Invalid pixel grid", root);
        next.size = tree.arrange(node, available, 0, 0, true);
        if (rounding) {
            if (!detail::snap_size(next.size, *rounding)) tree.fail(GridErrorCode::NumericOverflow, "Rounded tree size overflow", root);
            for (auto& layout : next.items) {
                if (!detail::snap_rect(layout.rect, *rounding) || !detail::snap_rect(layout.cell_rect, *rounding))
                    tree.fail(GridErrorCode::NumericOverflow, "Rounded tree geometry overflow", layout.node);
            }
        }
        for (const auto& layout : next.items) tree.get(layout.node).dirty = false;
        next.error.code = GridErrorCode::None;
        tree.published = true;
        tree.last_root = root; tree.last_available = available; tree.last_rounding = rounding;
    } catch (const Abort& error) {
        next.items.clear(); next.size = {}; next.error = error.error; next.error_node = error.node;
    }
    tree.result = std::move(next);
    guard.completed = true;
    return tree.result;
}
}
