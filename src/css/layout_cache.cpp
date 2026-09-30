#include "layout_cache.hpp"

#include <algorithm>

namespace axisly::css::detail {
namespace {
template<class Arena>
std::pair<std::size_t, std::size_t> oldest(const Arena& arena, const CacheList& list) {
    auto previous = no_cache_entry;
    auto index = list.head;
    while (arena[index].next != no_cache_entry) {
        previous = index;
        index = arena[index].next;
    }
    return {previous, index};
}

template<class Arena>
void evict(Arena& arena, CacheList& list, std::size_t previous, std::size_t index) {
    if (previous == no_cache_entry) list.head = no_cache_entry;
    else arena[previous].next = no_cache_entry;
    arena.erase(index);
    --list.count;
}
} // namespace

LayoutCacheKey LayoutCacheKey::from(const Constraints& c, std::size_t depth) noexcept {
    const auto bit = [](bool value, unsigned shift) { return std::uint32_t(value) << shift; };
    return {{c.known_width.value_or(0), c.known_height.value_or(0),
             c.containing_width.value_or(0), c.containing_height.value_or(0),
             c.available.width.value, c.available.height.value},
        bit(c.known_width.has_value(), 0) | bit(c.known_height.has_value(), 1)
        | bit(c.containing_width.has_value(), 2) | bit(c.containing_height.has_value(), 3)
        | bit(c.known_width_is_final, 4) | bit(c.known_height_is_final, 5)
        | bit(c.known_width_is_definite, 6) | bit(c.known_height_is_definite, 7)
        | bit(c.ignore_preferred_width, 8) | bit(c.ignore_preferred_height, 9)
        | bit(c.ignore_min_max_width, 10) | bit(c.ignore_min_max_height, 11)
        | bit(c.content_based_width, 12) | bit(c.preserve_known_grid_size, 13)
        | (static_cast<std::uint32_t>(c.sizing_mode) << 14)
        | (static_cast<std::uint32_t>(c.detail) << 15)
        | (static_cast<std::uint32_t>(c.available.width.kind) << 17)
        | (static_cast<std::uint32_t>(c.available.height.kind) << 20), depth};
}

void LayoutCache::invalidate(NodeCache& node) noexcept {
    for (auto index = node.layouts.head; index != no_cache_entry;) {
        const auto next = layouts_[index].next;
        layout_count_ -= layouts_[index].value.cost();
        layouts_.erase(index);
        index = next;
    }
    for (auto index = node.measurements.head; index != no_cache_entry;) {
        const auto next = measurements_[index].next;
        measurements_.erase(index);
        --measurement_count_;
        index = next;
    }
    node.layouts = {};
    node.measurements = {};
}

void LayoutCache::retain_layout(NodeCache& node, std::size_t index) noexcept {
    constexpr std::size_t entries_per_node = 8;
    const auto [previous, victim] = node.layouts.count == entries_per_node
        ? oldest(layouts_, node.layouts) : std::pair{no_cache_entry, no_cache_entry};
    const auto old_count = victim != no_cache_entry
        ? layouts_[victim].value.cost() : 0;
    const auto new_count = layouts_[index].value.cost();
    if (new_count > limits_.maximum_cached_layouts - (layout_count_ - old_count)) {
        layouts_.erase(index);
        return;
    }
    if (victim != no_cache_entry) {
        evict(layouts_, node.layouts, previous, victim);
        layout_count_ -= old_count;
    }
    layouts_[index].next = node.layouts.head;
    node.layouts.head = index;
    ++node.layouts.count;
    layout_count_ += new_count;
}

void LayoutCache::retain_measurement(NodeCache& node, std::size_t index) noexcept {
    constexpr std::size_t entries_per_node = 16;
    if (node.measurements.count == entries_per_node) {
        const auto [previous, victim] = oldest(measurements_, node.measurements);
        evict(measurements_, node.measurements, previous, victim);
        --measurement_count_;
    } else if (measurement_count_ >= limits_.maximum_cached_measurements) {
        measurements_.erase(index);
        return;
    }
    measurements_[index].next = node.measurements.head;
    node.measurements.head = index;
    ++node.measurements.count;
    ++measurement_count_;
}

CacheTransaction::~CacheTransaction() {
    for (const auto& entry : layouts_) {
        if (entry.index != no_cache_entry) resident_.layouts_.erase(entry.index);
    }
    for (const auto& entry : measurements_) {
        if (entry.index != no_cache_entry) resident_.measurements_.erase(entry.index);
    }
    for (auto* node : touched_) {
        node->pending_layouts = no_cache_entry;
        node->pending_measurements = no_cache_entry;
    }
}

void CacheTransaction::touch(NodeCache& node) {
    if (node.pending_layouts == no_cache_entry && node.pending_measurements == no_cache_entry) {
        touched_.push_back(&node);
    }
}

const Fragment* CacheTransaction::find(const NodeCache& node, const LayoutCacheKey& key) const {
    for (auto index = node.pending_layouts; index != no_cache_entry; index = resident_.layouts_[index].next) {
        if (resident_.layouts_[index].value.key == key) return &resident_.layouts_[index].value.fragment;
    }
    for (auto index = node.layouts.head; index != no_cache_entry; index = resident_.layouts_[index].next) {
        if (resident_.layouts_[index].value.key == key) return &resident_.layouts_[index].value.fragment;
    }
    return nullptr;
}

const Measurement* CacheTransaction::find(const NodeCache& node, const MeasureRequest& request) const {
    for (auto index = node.pending_measurements; index != no_cache_entry; index = resident_.measurements_[index].next) {
        if (resident_.measurements_[index].value.request == request) return &resident_.measurements_[index].value.result;
    }
    for (auto index = node.measurements.head; index != no_cache_entry; index = resident_.measurements_[index].next) {
        if (resident_.measurements_[index].value.request == request) return &resident_.measurements_[index].value.result;
    }
    return nullptr;
}

void CacheTransaction::stage(NodeCache& node, const LayoutCacheKey& key, const Fragment& fragment) {
    constexpr std::size_t maximum_fragment_nodes = 512;
    const auto count = std::max(std::size_t{1}, fragment.node_count());
    if (count > maximum_fragment_nodes
        || count > resident_.limits_.maximum_cached_layouts - layout_count_) return;
    if (layouts_.empty()) {
        const auto hint = std::min(reserve_hint_, resident_.limits_.maximum_cached_layouts);
        resident_.layouts_.reserve(hint);
        layouts_.reserve(hint);
    }
    touch(node);
    layouts_.push_back({&node});
    const auto index = resident_.layouts_.insert({key, fragment}, node.pending_layouts);
    layouts_.back().index = index;
    node.pending_layouts = index;
    layout_count_ += count;
}

void CacheTransaction::stage(NodeCache& node, const MeasureRequest& request,
                             const Measurement& measurement) {
    if (measurements_.size() >= resident_.limits_.maximum_cached_measurements) return;
    if (measurements_.empty()) {
        const auto hint = std::min(reserve_hint_, resident_.limits_.maximum_cached_measurements);
        resident_.measurements_.reserve(hint);
        measurements_.reserve(hint);
    }
    touch(node);
    measurements_.push_back({&node});
    const auto index = resident_.measurements_.insert({request, measurement}, node.pending_measurements);
    measurements_.back().index = index;
    node.pending_measurements = index;
}

void CacheTransaction::commit() noexcept {
    // Queries enter a transaction only after a resident/pending miss. There
    // cannot be an identical resident key to search for again during commit.
    for (auto& entry : layouts_) {
        resident_.retain_layout(*entry.owner, entry.index);
        entry.index = no_cache_entry;
    }
    for (auto& entry : measurements_) {
        resident_.retain_measurement(*entry.owner, entry.index);
        entry.index = no_cache_entry;
    }
}

} // namespace axisly::css::detail
