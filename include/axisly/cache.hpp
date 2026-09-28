#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <axisly/grid.hpp>
#include <axisly/measurement.hpp>
#include <axisly/result.hpp>

namespace axisly {

namespace detail { struct LayoutWorkspace; }
class MeasurementCache;

struct CachedGridResult {
    // Borrowed from GridCache; consume before the next mutation/compute or its
    // destruction. Copy result explicitly if an independent snapshot is needed.
    const GridResult& result;
    bool cache_hit{};
    // Calls made THIS time. result.measurement_count describes the computation
    // that produced the stored geometry and remains unchanged on a hit.
    std::size_t measurement_count{};
};

// Optional single-entry cache over compute_grid. Owns copies of the specification
// and items; unchanged compute calls are O(1), with no allocations or result copy.
// Misses reuse private scratch/output capacity and immutable input preparation;
// measurements run afresh unless measurement caching is explicitly enabled.
// Retained workspace capacity is released on destruction,
// not by invalidate() or by shrinking inputs. Larger inputs may allocate again.
// Not thread-safe. Non-copyable/non-movable to keep borrowed storage unambiguous.
class GridCache {
public:
    explicit GridCache(GridSpec spec = {}, std::span<const GridItem> items = {});
    ~GridCache();
    GridCache(const GridCache&) = delete;
    GridCache& operator=(const GridCache&) = delete;
    GridCache(GridCache&&) = delete;
    GridCache& operator=(GridCache&&) = delete;

    [[nodiscard]] const GridSpec& spec() const noexcept { return spec_; }
    [[nodiscard]] std::span<const GridItem> items() const noexcept { return items_; }

    // Compare on update, never on compute. Equal values preserve the cache.
    // Allocation failure leaves the previous inputs and cache intact.
    void set_spec(const GridSpec& spec);
    void set_items(std::span<const GridItem> items);
    // O(1) update by input index; out-of-range indices throw std::out_of_range.
    void set_item(std::size_t index, GridItem item);

    // Retains the function pointer and NON-OWNING context until replaced.
    // The host must keep them valid whenever compute may invoke the callback.
    // A changed callback or context invalidates the cache. With a null callback,
    // context is ignored and GridItem::preferred_size is used.
    void set_measure(MeasureCallback measure, void* user_data = nullptr);
    void invalidate();
    // Opt-in per-item measurement reuse. Disabled by default for compatibility.
    void enable_measurement_cache(bool enabled = true);
    [[nodiscard]] bool measurement_cache_enabled() const noexcept;
    // Invalidate only this item's measurement and the complete layout. Other
    // items may reuse measurements when every constraint still matches.
    void invalidate_item(ItemId id);

    // The host must change content_version (or invalidate) after text/font/DPI or
    // other external measurement data changes, including at the same address.
    // Both available dimensions are part of the key. Only success is cached.
    // Errors have compute_grid semantics; exceptions propagate and clear validity.
    // Recursive compute or mutation from this cache's callback throws logic_error;
    // computing with another cache or the stateless API is allowed.
    [[nodiscard]] CachedGridResult compute(Size available, std::uint64_t content_version = 0) &;

private:
    void require_idle() const;

    GridSpec spec_;
    std::vector<GridItem> items_;
    GridResult result_;
    // Private implementation: retained scratch storage and validated input plan.
    std::unique_ptr<detail::LayoutWorkspace> workspace_;
    std::unique_ptr<MeasurementCache> measurement_cache_;
    MeasureCallback measure_{};
    void* user_data_{};
    Size available_{};
    std::uint64_t content_version_{};
    bool valid_{};
    bool computing_{};
};

} // namespace axisly
