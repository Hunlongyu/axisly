#pragma once

#include <cstddef>
#include <memory>
#include <gridly/measurement.hpp>

namespace gridly {

// Optional bounded LRU per item. Keys include all known/available dimensions.
// The host must invalidate an item when text/font/DPI or other external content
// changes. Binding changes clear all entries; invalid results are never cached.
// Owns entries, not the callback context. Not thread-safe or reentrant.
class MeasurementCache {
public:
    explicit MeasurementCache(std::size_t entries_per_item = 8);
    ~MeasurementCache();
    MeasurementCache(const MeasurementCache&) = delete;
    MeasurementCache& operator=(const MeasurementCache&) = delete;
    MeasurementCache(MeasurementCache&&) = delete;
    MeasurementCache& operator=(MeasurementCache&&) = delete;

    void set_measure(MeasureCallback measure, void* user_data = nullptr);
    [[nodiscard]] MeasureResult measure(ItemId id, const MeasureInput& input);
    void invalidate(ItemId id);
    void clear();
    void reset_statistics();
    [[nodiscard]] std::size_t measurement_count() const noexcept;
    [[nodiscard]] std::size_t hit_count() const noexcept;
    [[nodiscard]] std::size_t entry_count() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
