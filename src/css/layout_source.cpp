#include <axisly/css_source.hpp>
#include "layout_engine.hpp"

namespace axisly::css::detail {
void LayoutNodes::attach(const LayoutSource& source, LayoutCache& cache, TreeLimits limits) {
    borrowed_ = std::make_unique<Borrowed>();
    borrowed_->source = &source;
    borrowed_->cache = &cache;
    borrowed_->limits = limits;
}

void LayoutNodes::begin_compute() {
    if (!borrowed_) return;
    if (++borrowed_->epoch == 0) {
        borrowed_->epoch = 1;
        for (auto& [id, node] : borrowed_->entries) { (void)id; node->binding.epoch = 0; }
    }
}

const LayoutNode* LayoutNodes::find_borrowed(std::uint64_t id) const {
    if (!id) return nullptr;
    auto& state = *borrowed_;
    auto found = state.entries.find(id);
    if (found != state.entries.end() && found->second->binding.epoch == state.epoch) return found->second.get();
    const auto view = state.source->node({id});
    if (!view) return nullptr;
    if (found == state.entries.end() || &found->second->style != &view->style) {
        if (found == state.entries.end() && state.entries.size() >= state.limits.maximum_nodes) {
            throw LayoutFailure(LayoutErrorCode::LimitExceeded, {id}, "CSS source node limit exceeded");
        }
        auto node = std::make_unique<SourceNode>(view->style);
        node->id = {id};
        node->roots = &state.cache->root_pool();
        node->cache_owner = state.cache;
        if (found == state.entries.end()) found = state.entries.emplace(id, std::move(node)).first;
        else {
            state.cache->invalidate(found->second->cache);
            found->second = std::move(node);
        }
    }
    auto& node = *found->second;
    if (node.binding.dirty) {
        try { validate_style(view->style); }
        catch (const std::invalid_argument& error) {
            throw LayoutFailure(LayoutErrorCode::InvalidStyle, node.id, error.what());
        }
        node.reset_style_cache();
        node.binding.dirty = false;
    }
    node.parent = view->parent;
    node.binding.children = view->children;
    node.binding.context = view->context;
    node.binding.epoch = state.epoch;
    return &node;
}

void LayoutNodes::invalidate_source(std::optional<NodeId> node) {
    auto& state = *borrowed_;
    const auto clear = [&] {
        for (auto& [id, entry] : state.entries) { (void)id; state.cache->invalidate(entry->cache); }
        state.entries.clear();
    };
    if (!node) { clear(); return; }
    try {
        std::size_t depth = 0;
        while (node) {
            if (++depth > state.limits.maximum_nodes) throw std::invalid_argument("CSS source parent cycle");
            if (const auto found = state.entries.find(node->value); found != state.entries.end()) {
                state.cache->invalidate(found->second->cache);
                // mark_dirty names the edited node. Ancestors need layout
                // invalidation, not style validation or template compilation.
                if (depth == 1) found->second->binding.dirty = true;
                // Borrowed styles have no old value to compare: conservatively
                // invalidate the edited node's and direct parent's placement.
                if (depth <= 2) found->second->grid_structure.reset();
                found->second->binding.epoch = 0;
            }
            const auto view = state.source->node(*node);
            if (!view) throw std::invalid_argument("Unknown CSS source node");
            node = view->parent;
        }
    } catch (...) { clear(); throw; }
}
} // namespace axisly::css::detail

namespace axisly::css {
LayoutSession::LayoutSession(const LayoutSource& source, TreeLimits limits,
                             std::shared_ptr<LayoutMemory> memory) : tree_(limits, std::move(memory)) {
    tree_.attach_source(source);
}
void LayoutSession::mark_dirty(NodeId node) { tree_.source_dirty(node); }
void LayoutSession::invalidate() { tree_.source_dirty(std::nullopt); }
const LayoutResult& LayoutSession::compute(NodeId root, AvailableSize available, LayoutOptions options) & {
    return tree_.compute(root, available, options);
}
std::optional<NodeLayout> LayoutSession::layout(NodeId node, CoordinateSpace space) const {
    return tree_.layout(node, space);
}
std::optional<NodeLayout> LayoutSession::unrounded_layout(NodeId node, CoordinateSpace space) const {
    return tree_.unrounded_layout(node, space);
}
} // namespace axisly::css
