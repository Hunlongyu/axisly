#pragma once

#include <axisly/css_tree.hpp>
#include "layout_fragment.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace axisly::css::detail {

enum class FragmentDetail { Size, Baseline, Full, LeafSize };
enum class SizingMode { Inherent, Content };

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
    FragmentDetail detail{FragmentDetail::Full};
    bool operator==(const Constraints&) const = default;
};

// Keep query stages and definiteness explicit, without the optional objects
// and padding of the algorithm-facing constraint structure in every cache row.
struct LayoutCacheKey {
    std::array<Unit, 6> values;
    std::uint32_t flags;
    std::size_t depth{};
    [[nodiscard]] static LayoutCacheKey from(const Constraints& input, std::size_t depth) noexcept;
    bool operator==(const LayoutCacheKey&) const = default;
};

struct CacheEntry {
    LayoutCacheKey key;
    Fragment fragment;
    [[nodiscard]] std::size_t cost() const noexcept {
        const auto nodes = fragment.node_count();
        return nodes ? nodes : 1;
    }
};
struct MeasurementCacheEntry {
    MeasureRequest request;
    Measurement result;
};

inline constexpr auto no_cache_entry = std::numeric_limits<std::size_t>::max();
struct CacheList {
    std::size_t head{no_cache_entry};
    std::size_t count{};
};
struct NodeCache {
    CacheList layouts, measurements;
    std::size_t pending_layouts{no_cache_entry};
    std::size_t pending_measurements{no_cache_entry};
};

// Slots are shared by the whole tree. Inserting one query does not allocate a
// hash node and a separate per-node vector. Indices survive arena growth.
template<class Value>
class CacheArena {
public:
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
        slots_[index] = Slot{Value{}, free_};
        free_ = index;
    }
private:
    std::vector<Slot> slots_;
    std::size_t free_{no_cache_entry};
};

class CacheTransaction;
class LayoutCache {
    friend class CacheTransaction;
public:
    explicit LayoutCache(TreeLimits limits) : limits_(limits) {}
    void invalidate(NodeCache& node) noexcept;
private:
    TreeLimits limits_;
    CacheArena<CacheEntry> layouts_;
    CacheArena<MeasurementCacheEntry> measurements_;
    std::size_t layout_count_{}; // Number of stored node layouts, not queries.
    std::size_t measurement_count_{};
    void retain_layout(NodeCache& node, std::size_t index) noexcept;
    void retain_measurement(NodeCache& node, std::size_t index) noexcept;
};

// Computation sees its staged queries immediately, while resident entries are
// changed only by commit(). Unwinding a failed layout clears all pending links.
class CacheTransaction {
public:
    explicit CacheTransaction(LayoutCache& resident) : resident_(resident) {}
    ~CacheTransaction();
    CacheTransaction(const CacheTransaction&) = delete;
    CacheTransaction& operator=(const CacheTransaction&) = delete;
    void reserve_hint(std::size_t nodes) noexcept { reserve_hint_ = nodes; }
    [[nodiscard]] const Fragment* find(const NodeCache& node, const LayoutCacheKey& key) const;
    [[nodiscard]] const Measurement* find(const NodeCache& node, const MeasureRequest& request) const;
    void stage(NodeCache& node, const LayoutCacheKey& key, const Fragment& fragment);
    void stage(NodeCache& node, const MeasureRequest& request, const Measurement& measurement);
    void commit() noexcept;
private:
    struct Pending {
        NodeCache* owner;
        std::size_t index{no_cache_entry};
    };
    LayoutCache& resident_;
    std::vector<NodeCache*> touched_;
    std::vector<Pending> layouts_;
    std::vector<Pending> measurements_;
    std::size_t layout_count_{};
    std::size_t reserve_hint_{};
    void touch(NodeCache& node);
};

} // namespace axisly::css::detail
