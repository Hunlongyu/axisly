#pragma once

#include <axisly/css_tree.hpp>
#include "layout_fragment.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

namespace axisly::css::detail {

// Size still carries the metadata needed by Block flow (collapsed margins
// and a local root). SizeOnly is the independent, scalar query contract.
enum class FragmentDetail { Size, Baseline, Full, SizeOnly };
enum class SizingMode { Inherent, Content };
struct LayoutNode;

struct Constraints {
    std::optional<Unit> known_width, known_height; // border-box
    bool known_width_is_final{}, known_height_is_final{};
    // Content-derived dimensions do not establish a definite percentage basis.
    bool known_width_is_definite{true}, known_height_is_definite{true};
    AvailableSize available;
    std::optional<Unit> containing_width, containing_height;
    bool ignore_preferred_width{}, ignore_preferred_height{};
    bool ignore_min_max_width{}, ignore_min_max_height{};
    bool content_based_width{};
    bool preserve_known_grid_size{};
    SizingMode sizing_mode{SizingMode::Inherent};
    bool operator==(const Constraints&) const = default;
};

// Keep query stages and definiteness explicit, without the optional objects
// and padding of the algorithm-facing constraint structure in every cache row.
struct LayoutCacheKey {
    // Canonical zero bits preserve floating equality. Non-definite available
    // space has no numeric payload; its kind alone identifies that axis.
    std::array<std::uint32_t, 6> values;
    std::uint32_t flags;
    std::size_t depth{};
    [[nodiscard]] static LayoutCacheKey from(const LayoutNode& node,
        const Constraints& input, FragmentDetail detail, std::size_t depth);
    bool operator==(const LayoutCacheKey&) const = default;
};

inline constexpr auto no_cache_entry = std::numeric_limits<std::size_t>::max();
inline constexpr auto inline_cache_entry = no_cache_entry - 1;

// Size queries have no geometry ownership. Full results live in a separate
// arena, so probing, moving and evicting a size does not touch Fragment handles.
struct CacheEntry {
    LayoutCacheKey key;
    Size size;
    std::size_t fragment{no_cache_entry};
    std::size_t node_count{1};
    [[nodiscard]] std::size_t cost() const noexcept { return node_count; }
};
struct MeasurementCacheEntry {
    MeasureRequest request;
    Measurement result;
};

// A leaf owns no child trees, transformations or Grid reports. Its cached
// geometry is plain data; replacing it requires no shared-owner teardown.
struct LeafLayoutEntry {
    LayoutCacheKey key;
    Size size;
    LayoutGeometry root;
    std::optional<BlockMargins> block_margins;
};
static_assert(std::is_trivially_copyable_v<LeafLayoutEntry>);

template<std::size_t Count>
struct CacheSlots {
    std::array<std::size_t, Count> indices;
    std::array<std::uint32_t, Count> fingerprints;
    std::size_t count{}, next{};
    // Only [0, count) is live. Empty nodes need no array initialization, and
    // reset never reads stale slots. Cache state belongs to its stable node.
    CacheSlots() noexcept {}
    CacheSlots(const CacheSlots&) = delete;
    CacheSlots& operator=(const CacheSlots&) = delete;
    void clear() noexcept { count = next = 0; }
    [[nodiscard]] std::size_t replacement() const noexcept { return count < Count ? count : next; }
    [[nodiscard]] std::size_t evicted() const noexcept {
        return count < Count ? no_cache_entry : indices[next];
    }
    void put(std::size_t index, std::uint32_t fingerprint) noexcept {
        const auto slot = replacement();
        indices[slot] = index;
        fingerprints[slot] = fingerprint;
        if (count < Count) ++count;
        else next = (next + 1) % Count;
    }
};
struct NodeCache {
    // One-query nodes need no arena allocation. Spill slots use bounded,
    // compact fingerprints and constant-time eviction. Hash collisions never
    // discard a live query or substitute for full key equality.
    std::optional<CacheEntry> layout;
    std::optional<MeasurementCacheEntry> measurement;
    CacheSlots<7> layouts;
    CacheSlots<15> measurements;
    std::size_t pending_layouts{no_cache_entry};
    std::size_t pending_measurements{no_cache_entry};
    std::size_t leaf{no_cache_entry}, pending_leaves{no_cache_entry};
};

// Slots are shared by the whole tree. Inserting one query does not allocate a
// hash node and a separate per-node vector. Indices survive arena growth.
template<class Value>
class CacheArena {
public:
    explicit CacheArena(std::pmr::memory_resource* memory = std::pmr::new_delete_resource())
        : slots_(memory) {}
    struct Slot {
        Value value;
        std::size_t next{no_cache_entry};
    };
    [[nodiscard]] const Slot& operator[](std::size_t index) const noexcept { return slots_[index]; }
    [[nodiscard]] Slot& operator[](std::size_t index) noexcept { return slots_[index]; }
    void reserve(std::size_t count) { slots_.reserve(count); }
    [[nodiscard]] std::size_t insert(Value&& value, std::size_t next) {
        if (free_ != no_cache_entry) {
            const auto index = free_;
            free_ = slots_[index].next;
            slots_[index] = Slot{std::move(value), next};
            return index;
        }
        slots_.push_back(Slot{std::move(value), next});
        return slots_.size() - 1;
    }
    void erase(std::size_t index) noexcept {
        // A free record is unreachable through any live cache key. Plain
        // sizes and measurements own no resources, so leave their bytes for
        // the next insertion instead of zeroing data that cannot be read.
        // Geometry handles still release their owned storage immediately.
        if constexpr (!std::is_trivially_copyable_v<Value>) slots_[index].value = Value{};
        slots_[index].next = free_;
        free_ = index;
    }
private:
    std::pmr::vector<Slot> slots_;
    std::size_t free_{no_cache_entry};
};

class CacheTransaction;
struct CacheJournal {
    enum class Kind { Layout, Leaf, Measurement };
    struct Pending {
        NodeCache* owner;
        std::size_t index{no_cache_entry};
        Kind kind{Kind::Layout};
    };
    std::pmr::vector<Pending> entries;
    explicit CacheJournal(std::pmr::memory_resource* memory) : entries(memory) {}
};
template<class Workspace> class WorkspacePool;
struct FlexWorkspace;
struct BlockWorkspace;
struct BoxResolutionCache;
class LayoutBox;
class LayoutCache {
    friend class CacheTransaction;
public:
    explicit LayoutCache(TreeLimits limits, std::pmr::memory_resource* memory = std::pmr::new_delete_resource());
    ~LayoutCache();
    void invalidate(NodeCache& node) noexcept;
    [[nodiscard]] FragmentRootPool& root_pool() noexcept { return roots_; }
    [[nodiscard]] std::pmr::memory_resource* memory() const noexcept { return roots_.memory(); }
    [[nodiscard]] PooledValue<LayoutBox> resolve_box(const BoxStyle& style,
        std::optional<Unit> width, std::optional<Unit> height, bool container, Size scrollbar);
private:
    // Cached branches release their handles before the translation pool. The
    // pool is tree-local, with the same exclusive-access lifetime as caches.
    FragmentTranslationPool translations_;
    // Working roots borrow this pool for the lifetime of a computation.
    FragmentRootPool roots_;
    std::unique_ptr<WorkspacePool<FlexWorkspace>> flex_workspace_;
    std::unique_ptr<WorkspacePool<BlockWorkspace>> block_workspace_;
    std::unique_ptr<BoxResolutionCache> boxes_;
    TreeLimits limits_;
    CacheArena<CacheEntry> layouts_;
    CacheArena<FragmentSnapshot> fragments_;
    CacheArena<LeafLayoutEntry> leaves_;
    CacheArena<MeasurementCacheEntry> measurements_;
    // Reuse allocation capacity between computations. Transactions clear all
    // borrowed node pointers on both commit and failure before tree mutation.
    CacheJournal journal_;
    std::size_t layout_count_{}; // Number of stored node layouts, not queries.
    std::size_t measurement_count_{};
    void retain_layout(NodeCache& node, std::size_t index) noexcept;
    void retain_measurement(NodeCache& node, std::size_t index) noexcept;
    void retain_leaf(NodeCache& node, std::size_t index) noexcept;
    void release_geometry(const CacheEntry& entry) noexcept;
    void erase_layout(std::size_t index) noexcept;
};

// Computation sees its staged queries immediately, while resident entries are
// changed only by commit(). Unwinding a failed layout clears all pending links.
class CacheTransaction {
public:
    explicit CacheTransaction(LayoutCache& resident) : resident_(resident),
        entries_(resident.journal_.entries) {}
    ~CacheTransaction();
    CacheTransaction(const CacheTransaction&) = delete;
    CacheTransaction& operator=(const CacheTransaction&) = delete;
    void reserve_hint(std::size_t nodes) noexcept {
        reserve_hint_ = nodes;
        resident_.roots_.reserve_hint(nodes);
        resident_.translations_.reserve_hint(nodes);
    }
    [[nodiscard]] const CacheEntry* find(const NodeCache& node, const LayoutCacheKey& key) const;
    [[nodiscard]] Fragment restore(const CacheEntry& entry, FragmentRootPool& roots) const;
    [[nodiscard]] const LeafLayoutEntry* find_leaf(const NodeCache& node, const LayoutCacheKey& key) const;
    [[nodiscard]] Fragment restore(const LeafLayoutEntry& entry, FragmentRootPool& roots) const;
    [[nodiscard]] const Size* find_size(const NodeCache& node, const LayoutCacheKey& key) const;
    [[nodiscard]] const Measurement* find(const NodeCache& node, const MeasureRequest& request) const;
    void stage(NodeCache& node, const LayoutCacheKey& key, const Fragment& fragment);
    void stage_size(NodeCache& node, const LayoutCacheKey& key, Size size);
    void stage_leaf(NodeCache& node, const LayoutCacheKey& key, const Fragment& fragment);
    void stage(NodeCache& node, const MeasureRequest& request, const Measurement& measurement);
    void commit() noexcept;
    [[nodiscard]] WorkspacePool<FlexWorkspace>& flex_workspace();
    [[nodiscard]] WorkspacePool<BlockWorkspace>& block_workspace();
    [[nodiscard]] FragmentTranslationPool& translation_pool() noexcept { return resident_.translations_; }
private:
    LayoutCache& resident_;
    std::pmr::vector<CacheJournal::Pending>& entries_;
    std::size_t layout_count_{};
    std::size_t measurement_count_{};
    std::size_t reserve_hint_{};
    [[nodiscard]] const CacheEntry* find_entry(const NodeCache& node, const LayoutCacheKey& key) const;
    void stage_entry(NodeCache& node, CacheEntry entry);
};

} // namespace axisly::css::detail
