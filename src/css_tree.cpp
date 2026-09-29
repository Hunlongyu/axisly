#include <axisly/css_tree.hpp>

#include "css/layout_engine.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace axisly::css {
namespace {
NodeId allocate_node_id() {
    static std::atomic<std::uint64_t> next{1};
    auto value = next.load(std::memory_order_relaxed);
    while (true) {
        if (value == 0 || value == std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error("CSS node identity exhausted");
        }
        if (next.compare_exchange_weak(value, value + 1, std::memory_order_relaxed)) return {value};
    }
}

void validate_available(AvailableSize available) {
    const auto check = [](AvailableSpace value) {
        if (value.kind == AvailableKind::Definite && (!std::isfinite(value.value) || value.value < 0)) {
            throw std::invalid_argument("Definite layout space must be finite and nonnegative");
        }
    };
    check(available.width);
    check(available.height);
}
} // namespace

const NodeLayout* LayoutResult::find(NodeId node) const noexcept {
    for (const auto& layout : nodes) if (layout.node == node) return &layout;
    return nullptr;
}

struct LayoutTree::Impl {
    TreeLimits limits;
    std::unordered_map<std::uint64_t, detail::LayoutNode> nodes;
    detail::CacheMap cache;
    std::size_t cached_layouts{};
    LayoutResult published;
    bool computing{};

    explicit Impl(TreeLimits requested) : limits(requested) {
        if (!limits.maximum_depth || !limits.maximum_nodes || !limits.maximum_measurements
            || !limits.maximum_cached_layouts) {
            throw std::invalid_argument("CSS tree limits must be positive");
        }
    }
    void mutable_guard() const {
        if (computing) throw std::logic_error("CSS layout tree cannot change during compute");
    }
    detail::LayoutNode& get(NodeId id) {
        const auto iterator = nodes.find(id.value);
        if (!id.value || iterator == nodes.end()) throw std::invalid_argument("Unknown CSS node");
        return iterator->second;
    }
    const detail::LayoutNode& get(NodeId id) const {
        const auto iterator = nodes.find(id.value);
        if (!id.value || iterator == nodes.end()) throw std::invalid_argument("Unknown CSS node");
        return iterator->second;
    }
    void erase_cache(NodeId id) {
        const auto entry = cache.find(id.value);
        if (entry == cache.end()) return;
        cached_layouts -= entry->second.fragment.nodes.size();
        cache.erase(entry);
    }
    void invalidate_path(std::optional<NodeId> start) {
        for (auto id = start; id; id = get(*id).parent) erase_cache(*id);
    }
    void invalidate_subtree(NodeId root) {
        std::vector<NodeId> pending{root};
        while (!pending.empty()) {
            const auto id = pending.back();
            pending.pop_back();
            const auto& children = get(id).children;
            pending.insert(pending.end(), children.begin(), children.end());
            erase_cache(id);
        }
    }
};

LayoutTree::LayoutTree(TreeLimits limits) : impl_(std::make_unique<Impl>(limits)) {}
LayoutTree::~LayoutTree() = default;

NodeId LayoutTree::create_node(Style style, MeasureFunction measure) {
    impl_->mutable_guard();
    validate_style(style);
    if (impl_->nodes.size() >= impl_->limits.maximum_nodes) throw std::length_error("CSS tree node limit");
    const auto id = allocate_node_id();
    impl_->nodes.emplace(id.value, detail::LayoutNode{id, std::move(style), std::move(measure), {}, {}});
    return id;
}

bool LayoutTree::contains(NodeId node) const noexcept {
    return node.value && impl_->nodes.contains(node.value);
}
const Style& LayoutTree::style(NodeId node) const { return impl_->get(node).style; }
std::optional<NodeId> LayoutTree::parent(NodeId node) const { return impl_->get(node).parent; }
std::span<const NodeId> LayoutTree::children(NodeId node) const { return impl_->get(node).children; }

void LayoutTree::set_style(NodeId node, Style style) {
    impl_->mutable_guard();
    validate_style(style);
    auto& item = impl_->get(node);
    if (item.style == style) return;
    item.style = std::move(style);
    impl_->invalidate_path(node);
}
void LayoutTree::set_measure(NodeId node, MeasureFunction measure) {
    impl_->mutable_guard();
    impl_->get(node).measure = std::move(measure);
    impl_->invalidate_path(node);
}

void LayoutTree::insert_child(NodeId parent, std::size_t index, NodeId child) {
    impl_->mutable_guard();
    auto& destination = impl_->get(parent);
    auto& item = impl_->get(child);
    if (parent == child) throw std::invalid_argument("CSS node cannot parent itself");
    for (auto ancestor = std::optional<NodeId>{parent}; ancestor; ancestor = impl_->get(*ancestor).parent) {
        if (*ancestor == child) throw std::invalid_argument("CSS tree cycle");
    }
    const bool same_parent = item.parent == parent;
    const auto current = same_parent
        ? std::find(destination.children.begin(), destination.children.end(), child)
        : destination.children.end();
    if (same_parent && current == destination.children.end()) throw std::logic_error("CSS tree parent mismatch");
    const auto maximum_index = destination.children.size() - (same_parent ? 1 : 0);
    if (index > maximum_index) throw std::out_of_range("CSS child insertion index");
    if (!same_parent) destination.children.reserve(destination.children.size() + 1);
    const auto old_parent = item.parent;
    if (old_parent) {
        auto& old = impl_->get(*item.parent).children;
        old.erase(std::remove(old.begin(), old.end(), child), old.end());
    }
    destination.children.insert(destination.children.begin() + static_cast<std::ptrdiff_t>(index), child);
    item.parent = parent;
    impl_->invalidate_subtree(child);
    impl_->invalidate_path(old_parent);
    impl_->invalidate_path(parent);
}
void LayoutTree::append_child(NodeId parent, NodeId child) {
    auto index = impl_->get(parent).children.size();
    if (impl_->get(child).parent == parent) --index;
    insert_child(parent, index, child);
}
void LayoutTree::detach(NodeId child) {
    impl_->mutable_guard();
    auto& item = impl_->get(child);
    if (!item.parent) return;
    auto& siblings = impl_->get(*item.parent).children;
    siblings.erase(std::remove(siblings.begin(), siblings.end(), child), siblings.end());
    const auto old_parent = item.parent;
    item.parent.reset();
    impl_->invalidate_subtree(child);
    impl_->invalidate_path(old_parent);
}
void LayoutTree::remove(NodeId node) {
    impl_->mutable_guard();
    impl_->get(node);
    detach(node);
    std::vector<NodeId> pending{node};
    while (!pending.empty()) {
        const auto current = pending.back();
        pending.pop_back();
        const auto& children = impl_->get(current).children;
        pending.insert(pending.end(), children.begin(), children.end());
        impl_->nodes.erase(current.value);
    }
}
void LayoutTree::mark_dirty(NodeId node) {
    impl_->mutable_guard();
    impl_->get(node);
    impl_->invalidate_subtree(node);
    impl_->invalidate_path(impl_->get(node).parent);
}

const LayoutResult& LayoutTree::compute(NodeId root, AvailableSize available) & {
    impl_->mutable_guard();
    validate_available(available);
    struct Guard {
        bool& computing;
        explicit Guard(bool& value) : computing(value) { computing = true; }
        ~Guard() { computing = false; }
    } guard(impl_->computing);
    detail::Engine engine(impl_->nodes, impl_->cache, impl_->limits);
    auto candidate = engine.compute(root, available);
    if (candidate.succeeded()) {
        for (auto& [id, entry] : std::move(engine).take_updates()) {
            const auto existing = impl_->cache.find(id);
            const auto old_count = existing == impl_->cache.end()
                ? 0 : existing->second.fragment.nodes.size();
            const auto new_count = entry.fragment.nodes.size();
            if (new_count > impl_->limits.maximum_cached_layouts
                - (impl_->cached_layouts - old_count)) continue;
            impl_->cache.insert_or_assign(id, std::move(entry));
            impl_->cached_layouts = impl_->cached_layouts - old_count + new_count;
        }
    }
    impl_->published = std::move(candidate);
    return impl_->published;
}

} // namespace axisly::css
