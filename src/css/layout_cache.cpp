#include "layout_cache.hpp"
#include "layout_engine.hpp"
#include "flex_workspace.hpp"
#include "grid_workspace.hpp"
#include "block_workspace.hpp"
#include "layout_box.hpp"

#include <algorithm>
#include <bit>
#include <cassert>

namespace axisly::css::detail {
// Pure style resolution is independent of node identity, measurement and
// placement. A bounded tree-local cache lets siblings share that computation.
// Expressions use the normal resolver so cached keys never borrow a calc AST.
struct BoxResolutionCache {
    explicit BoxResolutionCache(std::pmr::memory_resource* memory) : pool(memory) {}
    struct Key {
        std::array<std::uint64_t, 21> words{};
        bool operator==(const Key&) const = default;
    };
    struct Slot {
        Key key;
        PooledValue<LayoutBox> value;
        std::uint64_t fingerprint{};
    };
    // Slots and node handles must release their references before this pool.
    ValuePool<LayoutBox> pool;
    std::array<Slot, 16> slots;
    std::size_t count{}, next{}, last{slots.size()};
    std::size_t misses{}, bypass{};

    static std::optional<Key> key_for(const BoxStyle& style,
        std::optional<Unit> width, std::optional<Unit> height, bool container, Size scrollbar) {
        Key key;
        std::size_t i = 0;
        for (const auto* value : {&style.size.width, &style.size.height,
                &style.min_size.width, &style.min_size.height,
                &style.max_size.width, &style.max_size.height,
                &style.margin.left, &style.margin.top, &style.margin.right, &style.margin.bottom,
                &style.padding.left, &style.padding.top, &style.padding.right, &style.padding.bottom,
                &style.border.left, &style.border.top, &style.border.right, &style.border.bottom}) {
            if (value->kind() == LengthKind::Calc) return std::nullopt;
            key.words[i++] = value->representation_;
        }
        const auto bits = [](Unit value) { return std::bit_cast<std::uint32_t>(value); };
        key.words[18] = std::uint64_t(bits(width.value_or(0)))
            | std::uint64_t(bits(height.value_or(0))) << 32;
        key.words[19] = std::uint64_t(bits(style.aspect_ratio.value_or(0))) << 32
            | std::uint64_t(style.aspect_ratio.has_value()) << 16
            | std::uint64_t(width.has_value()) << 17
            | std::uint64_t(height.has_value()) << 18
            | std::uint64_t(container) << 19
            | std::uint64_t(style.box_sizing) << 8
            | std::uint64_t(style.direction) << 4 | std::uint64_t(style.replaced);
        key.words[20] = std::uint64_t(bits(scrollbar.width))
            | std::uint64_t(bits(scrollbar.height)) << 32;
        return key;
    }

    PooledValue<LayoutBox> resolve(const BoxStyle& style,
        std::optional<Unit> width, std::optional<Unit> height, bool container, Size scrollbar) {
        const auto create = [&] {
            auto value = resolve_numeric_box(style, width, height);
            // Container ratio bounds use padding/border before scrollbar
            // gutters. The published box is immutable and shared by nodes;
            // replacing a lookup slot never invalidates their borrowed views.
            if (container) value.maximum = transfer_aspect_ratio(value.maximum, value);
            value.scrollbar = scrollbar;
            return pool.create(value);
        };
        // A shared-style cache is counterproductive for a stream of unique
        // styles. Periodically probe it again so a later homogeneous run can
        // resume sharing; node-local resolved boxes remain cached throughout.
        if (bypass) { --bypass; return create(); }
        const auto key = key_for(style, width, height, container, scrollbar);
        if (!key) return create();
        // Consecutive siblings use the last result directly. Keep associative
        // lookup for alternating styles, with a position-sensitive fingerprint
        // that has no serial multiply/rotate chain. This is only a filter:
        // every hit still compares the complete key, including all edges.
        if (last < count && slots[last].key == *key) { misses = 0; return slots[last].value; }
        std::uint64_t hash = 0;
        for (std::size_t i = 0; i < key->words.size(); ++i) {
            hash ^= std::rotl(key->words[i], static_cast<int>(3 * i));
        }
        for (std::size_t i = 0; i < count; ++i) {
            if (slots[i].fingerprint == hash && slots[i].key == *key) {
                last = i;
                misses = 0;
                return slots[i].value;
            }
        }
        auto result = create();
        last = count < slots.size() ? count++ : next;
        if (count == slots.size()) next = (last + 1) % slots.size();
        slots[last] = {*key, result, hash};
        if (misses < slots.size()) ++misses;
        if (misses == slots.size()) bypass = 32;
        return result;
    }
};

LayoutCache::LayoutCache(TreeLimits limits, std::pmr::memory_resource* memory)
    : translations_(memory), roots_(memory), limits_(limits), layouts_(memory), fragments_(memory),
      leaves_(memory), measurements_(memory), journal_(memory) {}
LayoutCache::~LayoutCache() = default;

PooledValue<LayoutBox> LayoutCache::resolve_box(const BoxStyle& style,
    std::optional<Unit> width, std::optional<Unit> height, bool container, Size scrollbar) {
    if (!boxes_) boxes_ = std::make_unique<BoxResolutionCache>(memory());
    return boxes_->resolve(style, width, height, container, scrollbar);
}

WorkspacePool<FlexWorkspace>& CacheTransaction::flex_workspace() {
    if (!resident_.flex_workspace_) resident_.flex_workspace_ = std::make_unique<WorkspacePool<FlexWorkspace>>(resident_.memory());
    return *resident_.flex_workspace_;
}

WorkspacePool<GridWorkspace>& CacheTransaction::grid_workspace() {
    if (!resident_.grid_workspace_) resident_.grid_workspace_ = std::make_unique<WorkspacePool<GridWorkspace>>(resident_.memory());
    return *resident_.grid_workspace_;
}

WorkspacePool<BlockWorkspace>& CacheTransaction::block_workspace() {
    if (!resident_.block_workspace_) resident_.block_workspace_ = std::make_unique<WorkspacePool<BlockWorkspace>>(resident_.memory());
    return *resident_.block_workspace_;
}

namespace {
std::uint32_t numeric_bits(Unit value) noexcept {
    // Equality treats the two signed zeros alike.
    return value == 0 ? 0 : std::bit_cast<std::uint32_t>(value);
}
std::uint32_t fingerprint(const LayoutCacheKey& key) noexcept {
    // These fingerprints only reject unequal entries in a short slot array;
    // every hit still checks the whole key. Independent, position-sensitive
    // rotations avoid a serial multiply chain on each probe and replacement.
    return key.flags ^ static_cast<std::uint32_t>(key.depth)
        ^ std::rotl(key.values[0], 3) ^ std::rotl(key.values[1], 8)
        ^ std::rotl(key.values[2], 13) ^ std::rotl(key.values[3], 18)
        ^ std::rotl(key.values[4], 23) ^ std::rotl(key.values[5], 28);
}
std::uint32_t fingerprint(const MeasureRequest& request) noexcept {
    const auto flags = static_cast<std::uint32_t>(request.available_width.kind)
        | (static_cast<std::uint32_t>(request.available_height.kind) << 4)
        | (static_cast<std::uint32_t>(request.known_content_width.has_value()) << 8)
        | (static_cast<std::uint32_t>(request.known_content_height.has_value()) << 9);
    return flags ^ std::rotl(numeric_bits(request.known_content_width.value_or(0)), 3)
        ^ std::rotl(numeric_bits(request.known_content_height.value_or(0)), 10)
        ^ std::rotl(numeric_bits(request.available_width.value), 17)
        ^ std::rotl(numeric_bits(request.available_height.value), 24);
}
} // namespace

LayoutCacheKey LayoutCacheKey::from(const LayoutNode& node, const Constraints& c,
    FragmentDetail detail, std::size_t depth) {
    // Canonicalize only cache inputs. Layout constraints remain immutable;
    // building a key never copies and edits the entire query structure.
    const auto containing_width = node.box_depends_on_width ? c.containing_width : std::nullopt;
    const auto containing_height = node.box_depends_on_height ? c.containing_height : std::nullopt;
    auto available = c.available;
    if ((node.is_leaf() && c.known_width && c.known_height
            && (!node.box_depends_on_width || containing_width))
        || (!node.is_leaf() && c.sizing_mode == SizingMode::Content
            && (node.style.display == Display::Flex || node.style.display == Display::Grid))) {
        if (c.known_width) available.width = AvailableSpace::definite(*c.known_width);
        if (c.known_height) available.height = AvailableSpace::definite(*c.known_height);
    }
    const auto bit = [](bool value, unsigned shift) { return std::uint32_t(value) << shift; };
    return {{numeric_bits(c.known_width.value_or(0)), numeric_bits(c.known_height.value_or(0)),
             numeric_bits(containing_width.value_or(0)), numeric_bits(containing_height.value_or(0)),
             numeric_bits(available.width.kind == AvailableKind::Definite ? available.width.value : 0),
             numeric_bits(available.height.kind == AvailableKind::Definite ? available.height.value : 0)},
        bit(c.known_width.has_value(), 0) | bit(c.known_height.has_value(), 1)
        | bit(containing_width.has_value(), 2) | bit(containing_height.has_value(), 3)
        | bit(c.known_width_is_final, 4) | bit(c.known_height_is_final, 5)
        | bit(!c.known_width || c.known_width_is_definite, 6)
        | bit(!c.known_height || c.known_height_is_definite, 7)
        | bit(c.ignore_preferred_width, 8) | bit(c.ignore_preferred_height, 9)
        | bit(c.ignore_min_max_width, 10) | bit(c.ignore_min_max_height, 11)
        | bit(c.content_based_width, 12) | bit(c.preserve_known_grid_size, 13)
        | (static_cast<std::uint32_t>(c.sizing_mode) << 14)
        | (static_cast<std::uint32_t>(detail) << 15)
        | (static_cast<std::uint32_t>(available.width.kind) << 17)
        | (static_cast<std::uint32_t>(available.height.kind) << 20), depth};
}

void LayoutCache::invalidate(NodeCache& node) noexcept {
    if (!node.layout && !node.measurement && node.leaf == no_cache_entry
        && !node.layouts.count && !node.measurements.count) return;
    if (node.layout) {
        layout_count_ -= node.layout->cost();
        release_geometry(*node.layout);
    }
    if (node.measurement) --measurement_count_;
    if (node.leaf != no_cache_entry) {
        leaves_.erase(node.leaf);
        node.leaf = no_cache_entry;
        --layout_count_;
    }
    node.layout.reset();
    node.measurement.reset();
    for (std::size_t slot = 0; slot < node.layouts.count; ++slot) {
        const auto index = node.layouts.indices[slot];
        layout_count_ -= layouts_[index].value.cost();
        erase_layout(index);
    }
    for (std::size_t slot = 0; slot < node.measurements.count; ++slot) {
        const auto index = node.measurements.indices[slot];
        measurements_.erase(index);
        --measurement_count_;
    }
    node.layouts.clear();
    node.measurements.clear();
}

void LayoutCache::release_geometry(const CacheEntry& entry) noexcept {
    if (entry.fragment != no_cache_entry) fragments_.erase(entry.fragment);
}

void LayoutCache::erase_layout(std::size_t index) noexcept {
    release_geometry(layouts_[index].value);
    layouts_.erase(index);
}

void LayoutCache::retain_layout(NodeCache& node, std::size_t index) noexcept {
    if (index == inline_cache_entry) {
        const auto count = node.layout->cost();
        if (count <= limits_.maximum_cached_layouts - layout_count_) layout_count_ += count;
        else { release_geometry(*node.layout); node.layout.reset(); }
        return;
    }
    const auto new_count = layouts_[index].value.cost();
    if (!node.layout) {
        if (new_count <= limits_.maximum_cached_layouts - layout_count_) {
            node.layout.emplace(std::move(layouts_[index].value));
            layout_count_ += new_count;
        } else release_geometry(layouts_[index].value);
        layouts_.erase(index);
        return;
    }
    const auto slot = node.layouts.evicted();
    const auto old_count = slot == no_cache_entry ? 0 : layouts_[slot].value.cost();
    if (new_count <= limits_.maximum_cached_layouts - (layout_count_ - old_count)) {
        if (slot != no_cache_entry) erase_layout(slot);
        // Keep the newest query inline and move its predecessor into the
        // bounded FIFO, reusing the staged query's arena record.
        const auto hash = fingerprint(node.layout->key);
        std::swap(*node.layout, layouts_[index].value);
        node.layouts.put(index, hash);
        layout_count_ = layout_count_ - old_count + new_count;
    } else {
        const auto primary_count = node.layout->cost();
        if (new_count <= limits_.maximum_cached_layouts - (layout_count_ - primary_count)) {
            release_geometry(*node.layout);
            *node.layout = std::move(layouts_[index].value);
            layout_count_ = layout_count_ - primary_count + new_count;
        } else release_geometry(layouts_[index].value);
        layouts_.erase(index);
    }
}

void LayoutCache::retain_measurement(NodeCache& node, std::size_t index) noexcept {
    if (index == inline_cache_entry) {
        if (measurement_count_ < limits_.maximum_cached_measurements) ++measurement_count_;
        else node.measurement.reset();
        return;
    }
    if (!node.measurement) {
        if (measurement_count_ < limits_.maximum_cached_measurements) {
            node.measurement.emplace(std::move(measurements_[index].value));
            ++measurement_count_;
        }
        measurements_.erase(index);
        return;
    }
    const auto slot = node.measurements.evicted();
    if (slot != no_cache_entry || measurement_count_ < limits_.maximum_cached_measurements) {
        if (slot != no_cache_entry) measurements_.erase(slot);
        else ++measurement_count_;
        const auto hash = fingerprint(node.measurement->request);
        std::swap(*node.measurement, measurements_[index].value);
        node.measurements.put(index, hash);
    } else {
        *node.measurement = std::move(measurements_[index].value);
        measurements_.erase(index);
    }
}

void LayoutCache::retain_leaf(NodeCache& node, std::size_t index) noexcept {
    if (node.leaf != no_cache_entry) leaves_.erase(node.leaf);
    else if (layout_count_ < limits_.maximum_cached_layouts) ++layout_count_;
    else { leaves_.erase(index); return; }
    node.leaf = index;
}

CacheTransaction::~CacheTransaction() {
    for (const auto& entry : entries_) {
        if (entry.kind == CacheJournal::Kind::Layout) {
            if (entry.index == inline_cache_entry) {
                resident_.release_geometry(*entry.owner->layout);
                entry.owner->layout.reset();
            } else if (entry.index != no_cache_entry) resident_.erase_layout(entry.index);
        } else if (entry.kind == CacheJournal::Kind::Leaf) {
            if (entry.index != no_cache_entry) resident_.leaves_.erase(entry.index);
        } else {
            if (entry.index == inline_cache_entry) entry.owner->measurement.reset();
            else if (entry.index != no_cache_entry) resident_.measurements_.erase(entry.index);
        }
        entry.owner->pending_layouts = no_cache_entry;
        entry.owner->pending_measurements = no_cache_entry;
        entry.owner->pending_leaves = no_cache_entry;
    }
    entries_.clear();
}

const CacheEntry* CacheTransaction::find_entry(const NodeCache& node, const LayoutCacheKey& key) const {
    if (node.layout && node.layout->key == key) return &*node.layout;
    for (auto index = node.pending_layouts; index != no_cache_entry; index = resident_.layouts_[index].next) {
        if (resident_.layouts_[index].value.key == key) return &resident_.layouts_[index].value;
    }
    if (!node.layouts.count) return nullptr;
    const auto hash = fingerprint(key);
    for (std::size_t slot = 0; slot < node.layouts.count; ++slot) {
        if (node.layouts.fingerprints[slot] != hash) continue;
        const auto index = node.layouts.indices[slot];
        if (resident_.layouts_[index].value.key == key) return &resident_.layouts_[index].value;
    }
    return nullptr;
}

const CacheEntry* CacheTransaction::find(const NodeCache& node, const LayoutCacheKey& key) const {
    return find_entry(node, key);
}

Fragment CacheTransaction::restore(const CacheEntry& entry, FragmentRootPool& roots) const {
    assert(entry.fragment != no_cache_entry);
    return resident_.fragments_[entry.fragment].value.restore(roots);
}

const LeafLayoutEntry* CacheTransaction::find_leaf(const NodeCache& node, const LayoutCacheKey& key) const {
    if (node.leaf != no_cache_entry && resident_.leaves_[node.leaf].value.key == key) {
        return &resident_.leaves_[node.leaf].value;
    }
    for (auto index = node.pending_leaves; index != no_cache_entry; index = resident_.leaves_[index].next) {
        if (resident_.leaves_[index].value.key == key) return &resident_.leaves_[index].value;
    }
    return nullptr;
}

Fragment CacheTransaction::restore(const LeafLayoutEntry& entry, FragmentRootPool& roots) const {
    Fragment result;
    result.size = entry.size;
    result.root = roots.create(entry.root);
    result.block_margins = entry.block_margins;
    return result;
}

const Size* CacheTransaction::find_size(const NodeCache& node, const LayoutCacheKey& key) const {
    const auto* entry = find_entry(node, key);
    return entry ? &entry->size : nullptr;
}

const Measurement* CacheTransaction::find(const NodeCache& node, const MeasureRequest& request) const {
    if (node.measurement && node.measurement->request == request) return &node.measurement->result;
    for (auto index = node.pending_measurements; index != no_cache_entry; index = resident_.measurements_[index].next) {
        if (resident_.measurements_[index].value.request == request) return &resident_.measurements_[index].value.result;
    }
    if (!node.measurements.count) return nullptr;
    const auto hash = fingerprint(request);
    for (std::size_t slot = 0; slot < node.measurements.count; ++slot) {
        if (node.measurements.fingerprints[slot] != hash) continue;
        const auto index = node.measurements.indices[slot];
        if (resident_.measurements_[index].value.request == request) return &resident_.measurements_[index].value.result;
    }
    return nullptr;
}

void CacheTransaction::stage(NodeCache& node, const LayoutCacheKey& key, const Fragment& fragment) {
    constexpr std::size_t maximum_fragment_nodes = 512;
    const auto count = std::max(std::size_t{1}, fragment.node_count());
    if (count > maximum_fragment_nodes
        || count > resident_.limits_.maximum_cached_layouts - layout_count_) return;
    resident_.fragments_.reserve(reserve_hint_);
    const auto geometry = resident_.fragments_.insert(FragmentSnapshot(fragment), no_cache_entry);
    stage_entry(node, {key, fragment.size, geometry, count});
}

void CacheTransaction::stage_leaf(NodeCache& node, const LayoutCacheKey& key, const Fragment& fragment) {
    assert(fragment.root && !fragment.descendants && !fragment.translation
        && !fragment.grid_reports && !fragment.overflow_dirty);
    if (layout_count_ >= resident_.limits_.maximum_cached_layouts) return;
    if (entries_.empty()) entries_.reserve(std::min(reserve_hint_, resident_.limits_.maximum_cached_layouts));
    entries_.push_back({&node, no_cache_entry, CacheJournal::Kind::Leaf});
    resident_.leaves_.reserve(reserve_hint_);
    const auto index = resident_.leaves_.insert({key, fragment.size, fragment.root.read(), fragment.block_margins},
        node.pending_leaves);
    entries_.back().index = index;
    node.pending_leaves = index;
    ++layout_count_;
}

void CacheTransaction::stage_size(NodeCache& node, const LayoutCacheKey& key, Size size) {
    if (layout_count_ >= resident_.limits_.maximum_cached_layouts) return;
    stage_entry(node, {key, size});
}

void CacheTransaction::stage_entry(NodeCache& node, CacheEntry entry) {
    try {
        if (entries_.empty()) {
            const auto hint = std::min(reserve_hint_, resident_.limits_.maximum_cached_layouts);
            entries_.reserve(hint);
        }
        entries_.push_back({&node, no_cache_entry, CacheJournal::Kind::Layout});
        if (!node.layout) {
            node.layout.emplace(entry);
            entries_.back().index = inline_cache_entry;
        } else {
            resident_.layouts_.reserve(reserve_hint_);
            const auto index = resident_.layouts_.insert(std::move(entry), node.pending_layouts);
            entries_.back().index = index;
            node.pending_layouts = index;
        }
        layout_count_ += entry.cost();
    } catch (...) {
        resident_.release_geometry(entry);
        throw;
    }
}

void CacheTransaction::stage(NodeCache& node, const MeasureRequest& request,
                             const Measurement& measurement) {
    if (measurement_count_ >= resident_.limits_.maximum_cached_measurements) return;
    if (entries_.empty()) {
        const auto hint = std::min(reserve_hint_, resident_.limits_.maximum_cached_measurements);
        entries_.reserve(hint);
    }
    entries_.push_back({&node, no_cache_entry, CacheJournal::Kind::Measurement});
    if (!node.measurement) {
        node.measurement.emplace(MeasurementCacheEntry{request, measurement});
        entries_.back().index = inline_cache_entry;
    } else {
        resident_.measurements_.reserve(reserve_hint_);
        const auto index = resident_.measurements_.insert({request, measurement}, node.pending_measurements);
        entries_.back().index = index;
        node.pending_measurements = index;
    }
    ++measurement_count_;
}

void CacheTransaction::commit() noexcept {
    // Queries enter a transaction only after a resident/pending miss. There
    // cannot be an identical resident key to search for again during commit.
    // The same journal also owns link cleanup. Committing needs one pass and
    // leaves the destructor with no work; failure releases only staged data.
    for (const auto& entry : entries_) {
        if (entry.kind == CacheJournal::Kind::Layout) {
            resident_.retain_layout(*entry.owner, entry.index);
        } else if (entry.kind == CacheJournal::Kind::Leaf) resident_.retain_leaf(*entry.owner, entry.index);
        else resident_.retain_measurement(*entry.owner, entry.index);
        entry.owner->pending_layouts = no_cache_entry;
        entry.owner->pending_measurements = no_cache_entry;
        entry.owner->pending_leaves = no_cache_entry;
    }
    entries_.clear();
}

} // namespace axisly::css::detail
