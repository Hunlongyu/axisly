#include <axisly/css_tree.hpp>

#include "css/layout_engine.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

namespace axisly::css {
std::uint32_t detail::allocate_node_epoch() {
    static std::atomic<std::uint64_t> next{1};
    auto value = next.load(std::memory_order_relaxed);
    while (true) {
        if (value > std::numeric_limits<std::uint32_t>::max()) {
            throw std::overflow_error("CSS node identity exhausted");
        }
        if (next.compare_exchange_weak(value, value + 1, std::memory_order_relaxed)) {
            return static_cast<std::uint32_t>(value);
        }
    }
}

namespace {
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
    enum class Change { Content, Structure };
    TreeLimits limits;
    std::shared_ptr<LayoutMemory> memory;
    // Node-inline cache entries and arena entries both borrow the cache's
    // root pool. Destroy nodes before the cache and its pool.
    detail::LayoutCache cache;
    detail::LayoutNodes nodes;
    LayoutResult published;
    detail::Fragment published_fragment;
    LayoutOptions published_options, cache_options;
    // Keep the published snapshot intact until the next computation succeeds
    // or returns a layout error. A host exception must leave it unchanged.
    std::vector<NodeLayout> spare_nodes;
    std::optional<std::tuple<NodeId, AvailableSize, LayoutOptions>> published_key;
    bool computing{};
    std::uint64_t published_epoch{}, next_epoch{};
    std::uint64_t published_revision{}, indexed_revision{};
    std::vector<std::size_t> published_index;

    explicit Impl(TreeLimits requested, std::shared_ptr<LayoutMemory> resource)
        : limits(requested), memory(std::move(resource)),
          cache(requested, memory ? memory.get() : std::pmr::new_delete_resource()),
          nodes(cache.memory()) {
        if (!limits.maximum_depth || !limits.maximum_nodes || !limits.maximum_measurements
            || !limits.maximum_cached_layouts || !limits.maximum_cached_measurements) {
            throw std::invalid_argument("CSS tree limits must be positive");
        }
    }
    void mutable_guard() const {
        if (computing) throw std::logic_error("CSS layout tree cannot change during compute");
    }
    template<class StyleSource>
    NodeId create(StyleSource&& style, MeasureFunction&& measure) {
        mutable_guard();
        validate_style(style);
        if (nodes.size() >= limits.maximum_nodes) throw std::length_error("CSS tree node limit");
        const NodeId id{nodes.emplace(std::forward<StyleSource>(style), std::move(measure))};
        auto& node = get(id);
        node.roots = &cache.root_pool();
        node.cache_owner = &cache;
        node.reset_style_cache();
        return id;
    }
    detail::LayoutNode& get(NodeId id) {
        auto* node = nodes.find(id.value);
        if (!node) throw std::invalid_argument("Unknown CSS node");
        return *node;
    }
    template<class StyleSource>
    void set_style(NodeId id, StyleSource&& style) {
        mutable_guard();
        auto& item = get(id);
        // Stored styles have already passed validation. Avoid copying vectors,
        // area names and calc ownership when a host resubmits an unchanged style.
        if (&item.style == &style || item.style == style) return;
        validate_style(style);
        // Copying an lvalue may fail halfway through its Grid containers. Stage
        // it first so the old style and all caches remain coherent on failure.
        Style replacement(std::forward<StyleSource>(style));
        const bool placement_changed = item.style.display != replacement.display
            || item.style.position != replacement.position
            || item.style.grid.row != replacement.grid.row
            || item.style.grid.column != replacement.grid.column;
        static_assert(std::is_nothrow_move_assignable_v<Style>);
        item.owned_style = std::move(replacement);
        item.reset_style_cache();
        if (placement_changed && item.parent) get(*item.parent).grid_structure.reset();
        invalidate_path(id, Change::Content);
    }
    const detail::LayoutNode& get(NodeId id) const {
        const auto* node = nodes.find(id.value);
        if (!node) throw std::invalid_argument("Unknown CSS node");
        return *node;
    }
    void erase_cache(NodeId id, Change change = Change::Structure) {
        auto& node = get(id);
        cache.invalidate(node.cache);
        if (change == Change::Structure) node.grid_structure.reset();
    }
    void invalidate_path(std::optional<NodeId> start, Change change = Change::Structure) {
        published_key.reset();
        for (auto id = start; id; id = get(*id).parent) {
            erase_cache(*id, change);
            // Topology changes affect this container's placement, while its
            // ancestors only need new sizes. Auto-repeat inputs remain keyed.
            change = Change::Content;
        }
    }
    template<class Visitor>
    void visit_subtree(NodeId root, Visitor&& visitor) {
        auto pending = root;
        get(root).next_visit = {};
        while (pending.value) {
            const auto id = pending;
            auto& node = get(id);
            pending = node.next_visit;
            node.next_visit = {};
            for (const auto child : node.children) {
                get(child).next_visit = pending;
                pending = child;
            }
            visitor(id);
        }
    }
    void invalidate_subtree(NodeId root, Change change = Change::Structure) {
        published_key.reset();
        visit_subtree(root, [&](NodeId id) { erase_cache(id, change); });
    }
    std::optional<NodeLayout> lookup(NodeId id, CoordinateSpace space, bool unrounded) const {
        if (space != CoordinateSpace::Root && space != CoordinateSpace::Parent) {
            throw std::invalid_argument("Unknown CSS coordinate space");
        }
        if (!published.succeeded() || !published_fragment.root) return std::nullopt;
        std::array<NodeId, 64> path;
        auto first = path.size();
        // Unchanged topology permits an allocation-free root-to-node route.
        // After mutations, search the immutable snapshot, including removed nodes.
        if (published_key) {
            auto next = std::optional<NodeId>{id};
            while (next && first) {
                path[--first] = *next;
                if (*next == published_fragment.root_layout().node) break;
                const auto* node = nodes.find(next->value);
                next = node ? node->parent : std::nullopt;
            }
        }
        const auto route = first < path.size() && path[first] == published_fragment.root_layout().node
            ? std::span<const NodeId>(path).subspan(first) : std::span<const NodeId>{};
        return published_fragment.lookup(id, route,
            unrounded ? std::nullopt : published_options.rounding, space);
    }
};

LayoutTree::LayoutTree(TreeLimits limits, std::shared_ptr<LayoutMemory> memory)
    : impl_(std::make_unique<Impl>(limits, std::move(memory))) {}
LayoutTree::~LayoutTree() = default;

NodeId LayoutTree::create_node(const Style& style, MeasureFunction measure) {
    return impl_->create(style, std::move(measure));
}
NodeId LayoutTree::create_node(Style&& style, MeasureFunction measure) {
    return impl_->create(std::move(style), std::move(measure));
}
NodeId LayoutTree::create_node_with_children(Style style,
        std::span<const NodeId> children, MeasureFunction measure) {
    const auto node = create_node(std::move(style), std::move(measure));
    try { set_children(node, children); }
    catch (...) { impl_->nodes.erase(node.value); throw; }
    return node;
}

bool LayoutTree::contains(NodeId node) const noexcept {
    return impl_->nodes.find(node.value) != nullptr;
}
std::size_t LayoutTree::node_count() const noexcept { return impl_->nodes.size(); }
const Style& LayoutTree::style(NodeId node) const { return impl_->get(node).style; }
std::optional<NodeId> LayoutTree::parent(NodeId node) const { return impl_->get(node).parent; }
std::span<const NodeId> LayoutTree::children(NodeId node) const { return impl_->get(node).children; }
NodeId LayoutTree::child_at(NodeId parent, std::size_t index) const {
    return impl_->get(parent).children.at(index);
}
std::size_t LayoutTree::child_count(NodeId parent) const { return impl_->get(parent).children.size(); }

void LayoutTree::set_style(NodeId node, const Style& style) {
    impl_->set_style(node, style);
}
void LayoutTree::set_style(NodeId node, Style&& style) {
    impl_->set_style(node, std::move(style));
}
void LayoutTree::set_measure(NodeId node, MeasureFunction measure) {
    impl_->mutable_guard();
    impl_->get(node).measure = std::move(measure);
    impl_->invalidate_path(node, Impl::Change::Content);
}
void LayoutTree::set_context(NodeId node, std::shared_ptr<void> context) {
    impl_->mutable_guard();
    auto& item = impl_->get(node);
    if (item.context == context) return;
    item.context = std::move(context);
    impl_->invalidate_path(node, Impl::Change::Content);
}
const std::shared_ptr<void>& LayoutTree::context(NodeId node) const { return impl_->get(node).context; }

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
    if (!same_parent && destination.children.size() == destination.children.capacity()) {
        const auto capacity = destination.children.capacity();
        const auto maximum = destination.children.max_size();
        const auto grown = capacity > maximum / 2 ? maximum : std::max(std::size_t{4}, capacity * 2);
        destination.children.reserve(std::max(destination.children.size() + 1, grown));
    }
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
void LayoutTree::set_children(NodeId parent, std::span<const NodeId> children) {
    impl_->mutable_guard();
    auto& destination = impl_->get(parent);
    // Copy first: the caller may pass our own children() span. All allocations,
    // duplicate checks and cycle checks precede the nonthrowing commit phase.
    std::vector<NodeId> next(children.begin(), children.end());
    auto unique = next;
    std::sort(unique.begin(), unique.end(), [](NodeId a, NodeId b) { return a.value < b.value; });
    if (std::adjacent_find(unique.begin(), unique.end()) != unique.end()) {
        throw std::invalid_argument("Duplicate CSS child");
    }
    for (const auto child : next) {
        impl_->get(child);
        for (auto ancestor = std::optional<NodeId>{parent}; ancestor;
             ancestor = impl_->get(*ancestor).parent) {
            if (*ancestor == child) throw std::invalid_argument("CSS tree cycle");
        }
    }
    if (next == destination.children) return;
    for (const auto child : next) {
        impl_->invalidate_path(impl_->get(child).parent);
        impl_->invalidate_subtree(child);
    }
    for (const auto child : destination.children) {
        impl_->invalidate_subtree(child);
        impl_->get(child).parent.reset();
    }
    for (const auto child : next) {
        auto& item = impl_->get(child);
        if (item.parent) {
            auto& old = impl_->get(*item.parent).children;
            old.erase(std::remove(old.begin(), old.end(), child), old.end());
        }
        item.parent = parent;
    }
    destination.children = std::move(next);
    impl_->invalidate_path(parent);
}
NodeId LayoutTree::replace_child(NodeId parent, std::size_t index, NodeId child) {
    impl_->mutable_guard();
    const auto previous = child_at(parent, index);
    if (previous == child) return previous;
    auto next = impl_->get(parent).children;
    next[index] = child;
    set_children(parent, next);
    return previous;
}
NodeId LayoutTree::detach_child_at(NodeId parent, std::size_t index) {
    const auto child = child_at(parent, index);
    detach(child);
    return child;
}
void LayoutTree::detach_children(NodeId parent, std::size_t first, std::size_t count) {
    impl_->mutable_guard();
    auto& children = impl_->get(parent).children;
    if (first > children.size() || count > children.size() - first) {
        throw std::out_of_range("CSS child range");
    }
    if (!count) return;
    for (const auto child : std::span(children).subspan(first, count)) {
        impl_->invalidate_subtree(child);
        impl_->get(child).parent.reset();
    }
    children.erase(children.begin() + static_cast<std::ptrdiff_t>(first),
        children.begin() + static_cast<std::ptrdiff_t>(first + count));
    impl_->invalidate_path(parent);
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
void LayoutTree::remove_subtree(NodeId node) {
    impl_->mutable_guard();
    impl_->get(node);
    impl_->published_key.reset();
    detach(node);
    impl_->visit_subtree(node, [&](NodeId current) {
        impl_->erase_cache(current);
        impl_->nodes.erase(current.value);
    });
}
void LayoutTree::remove_node(NodeId node) {
    impl_->mutable_guard();
    auto& item = impl_->get(node);
    detach(node);
    for (const auto child : item.children) {
        impl_->get(child).parent.reset();
        impl_->invalidate_subtree(child);
    }
    impl_->published_key.reset();
    impl_->erase_cache(node);
    impl_->nodes.erase(node.value);
}
void LayoutTree::clear() {
    impl_->mutable_guard();
    impl_->published_key.reset();
    impl_->nodes.for_each([&](auto& node) {
        impl_->cache.invalidate(node.cache);
        impl_->nodes.erase(node.id.value);
    });
}
void LayoutTree::mark_subtree_dirty(NodeId node) {
    impl_->mutable_guard();
    impl_->get(node);
    impl_->invalidate_subtree(node, Impl::Change::Content);
    impl_->invalidate_path(impl_->get(node).parent, Impl::Change::Content);
}
void LayoutTree::mark_dirty(NodeId node) {
    impl_->mutable_guard();
    impl_->get(node);
    impl_->invalidate_path(node, Impl::Change::Content);
}
bool LayoutTree::dirty(NodeId node) const {
    const auto& cache = impl_->get(node).cache;
    return !cache.layout && cache.layouts.count == 0 && cache.leaf == detail::no_cache_entry;
}

const NodeLayout* LayoutTree::find_layout(NodeId node) const {
    if (impl_->published.nodes.empty() && impl_->published_fragment.root) {
        // Pointer lookup explicitly requests a flat, address-stable snapshot.
        // Value lookup through layout() leaves retained output unmaterialized.
        auto candidate = detail::FragmentSnapshot(impl_->published_fragment).restore(impl_->cache.root_pool());
        detail::PublicationContext publication;
        auto nodes = impl_->published_options.rounding
            ? candidate.rounded_nodes({}, *impl_->published_options.rounding)
            : std::move(candidate).take_nodes({}, publication);
        impl_->published.nodes = std::move(nodes);
    }
    if (impl_->nodes.is_external()) return impl_->published.find(node);
    const auto& layouts = impl_->published.nodes;
    const auto slot = detail::LayoutNodes::slot_index(node.value);
    const auto lookup = [&]() -> const NodeLayout* {
        if (slot >= impl_->published_index.size()) return nullptr;
        const auto index = impl_->published_index[slot];
        // The complete identity check covers slot reuse, partial-root layouts
        // and reordered output without a mandatory index pass every frame.
        return index < layouts.size() && layouts[index].node == node ? &layouts[index] : nullptr;
    };
    if (const auto* found = lookup()) return found;
    if (layouts.empty() || impl_->indexed_revision == impl_->published_revision) return nullptr;
    std::size_t size = 0;
    for (const auto& layout : layouts) {
        size = std::max(size, std::size_t{detail::LayoutNodes::slot_index(layout.node.value)} + 1);
    }
    impl_->published_index.assign(size, std::numeric_limits<std::size_t>::max());
    for (std::size_t i = 0; i < layouts.size(); ++i) {
        impl_->published_index[detail::LayoutNodes::slot_index(layouts[i].node.value)] = i;
    }
    impl_->indexed_revision = impl_->published_revision;
    return lookup();
}

const LayoutResult& LayoutTree::compute(NodeId root, AvailableSize available) & {
    return compute_impl(root, available, {});
}

const LayoutResult& LayoutTree::compute(NodeId root, AvailableSize available, PixelRounding rounding) & {
    LayoutOptions options;
    options.rounding = rounding;
    return compute_impl(root, available, options);
}
const LayoutResult& LayoutTree::compute(NodeId root, AvailableSize available, LayoutOptions options) & {
    return compute_impl(root, available, options);
}
std::optional<NodeLayout> LayoutTree::layout(NodeId node, CoordinateSpace space) const {
    if (!impl_->published.nodes.empty() && (space == CoordinateSpace::Root
        || (space == CoordinateSpace::Parent && !impl_->published_fragment.root))) {
        // A flat, unrounded snapshot at the origin has identical root/local
        // coordinates and needs no second copy of its leaf geometry.
        if (const auto* result = find_layout(node)) return *result;
        return std::nullopt;
    }
    return impl_->lookup(node, space, false);
}
std::optional<NodeLayout> LayoutTree::unrounded_layout(NodeId node, CoordinateSpace space) const {
    if (!impl_->published_options.rounding) return layout(node, space);
    return impl_->lookup(node, space, true);
}

const LayoutResult& LayoutTree::compute_impl(NodeId root, AvailableSize available,
                                            LayoutOptions options) {
    impl_->mutable_guard();
    validate_available(available);
    if (const auto& rounding = options.rounding; rounding
        && (!std::isfinite(rounding->scale) || rounding->scale <= 0
            || !std::isfinite(rounding->origin_x) || !std::isfinite(rounding->origin_y))) {
        throw std::invalid_argument("CSS pixel rounding needs a positive finite scale and finite origins");
    }
    const auto key = std::tuple{root, available, options};
    if (impl_->published_key == key) {
        impl_->published.measurement_count = 0;
        impl_->published.layout_count = 0;
        impl_->published.cache_hits = 1;
        impl_->published.cache_hit = true;
        impl_->published.published_nodes_reused = impl_->published.nodes.size();
        return impl_->published;
    }
    impl_->published_key.reset();
    if (options.measure_provider != impl_->cache_options.measure_provider
        || options.measurement_revision != impl_->cache_options.measurement_revision
        || options.collect_grid_tracks != impl_->cache_options.collect_grid_tracks) {
        impl_->nodes.for_each([&](auto& node) { impl_->cache.invalidate(node.cache); });
        impl_->cache_options = options;
    }
    struct Guard {
        bool& computing;
        explicit Guard(bool& value) : computing(value) { computing = true; }
        ~Guard() { computing = false; }
    } guard(impl_->computing);
    impl_->nodes.begin_compute();
    detail::Engine engine(impl_->nodes, impl_->limits, impl_->cache,
        options.measure_provider, options.collect_grid_tracks);
    if (impl_->next_epoch == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("CSS publication identity exhausted");
    }
    const auto epoch = ++impl_->next_epoch;
    detail::Fragment fragment;
    auto candidate = engine.compute(root, available, std::move(impl_->spare_nodes),
        {impl_->published.nodes, impl_->published_epoch, epoch}, options.rounding,
        &fragment, options.publish_nodes);
    if (candidate.succeeded()) {
        engine.commit_caches();
    }
    impl_->spare_nodes = std::move(impl_->published.nodes);
    impl_->spare_nodes.clear();
    impl_->published = std::move(candidate);
    impl_->published_fragment = impl_->published.succeeded() ? std::move(fragment) : detail::Fragment{};
    impl_->published_options = options;
    impl_->published_revision = epoch;
    // Rounded output must never be reused by the logical-coordinate publisher.
    impl_->published_epoch = impl_->published.succeeded() && !options.rounding && options.publish_nodes ? epoch : 0;
    if (impl_->published.succeeded()) impl_->published_key = key;
    return impl_->published;
}

void LayoutTree::attach_source(const LayoutSource& source) {
    impl_->nodes.attach(source, impl_->cache, impl_->limits);
}
void LayoutTree::source_dirty(std::optional<NodeId> node) {
    impl_->mutable_guard();
    impl_->published_key.reset();
    impl_->nodes.invalidate_source(node);
}

} // namespace axisly::css
