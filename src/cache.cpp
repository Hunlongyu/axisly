#include <axisly/cache.hpp>
#include <axisly/measurement_cache.hpp>

#include "detail/layout_workspace.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace axisly {

GridCache::GridCache(GridSpec spec, std::span<const GridItem> items)
    : spec_(std::move(spec)), items_(items.begin(), items.end()),
      workspace_(std::make_unique<detail::LayoutWorkspace>()) {}

GridCache::~GridCache() = default;

void GridCache::require_idle() const {
    if (computing_) throw std::logic_error("GridCache cannot compute or mutate during its measurement callback");
}

void GridCache::set_spec(const GridSpec& spec) {
    require_idle();
    if (spec_ == spec) return;
    auto next = spec;
    spec_ = std::move(next);
    if (measurement_cache_) measurement_cache_->clear();
    workspace_->invalidate_inputs();
    valid_ = false;
}

void GridCache::set_items(std::span<const GridItem> items) {
    require_idle();
    if (std::ranges::equal(items_, items)) return;
    // Copy first: the supplied span may refer to part of items_.
    std::vector<GridItem> next(items.begin(), items.end());
    items_.swap(next);
    if (measurement_cache_) measurement_cache_->clear();
    workspace_->invalidate_inputs();
    valid_ = false;
}

void GridCache::set_item(std::size_t index, GridItem item) {
    require_idle();
    auto& current = items_.at(index);
    if (current == item) return;
    if (measurement_cache_) {
        measurement_cache_->invalidate(current.id);
        measurement_cache_->invalidate(item.id);
    }
    current = item;
    workspace_->invalidate_inputs();
    valid_ = false;
}

void GridCache::set_measure(MeasureCallback measure, void* user_data) {
    require_idle();
    if (!measure) user_data = nullptr;
    if (measure_ == measure && user_data_ == user_data) return;
    measure_ = measure;
    user_data_ = user_data;
    if (measurement_cache_) measurement_cache_->set_measure(measure_, user_data_);
    valid_ = false;
}

void GridCache::invalidate() {
    require_idle();
    if (measurement_cache_) measurement_cache_->clear();
    valid_ = false;
}

void GridCache::enable_measurement_cache(bool enabled) {
    require_idle();
    if (enabled == static_cast<bool>(measurement_cache_)) return;
    if (enabled) {
        auto next = std::make_unique<MeasurementCache>();
        next->set_measure(measure_, user_data_);
        measurement_cache_ = std::move(next);
    } else measurement_cache_.reset();
    valid_ = false;
}
bool GridCache::measurement_cache_enabled() const noexcept { return static_cast<bool>(measurement_cache_); }
void GridCache::invalidate_item(ItemId id) {
    require_idle();
    if (measurement_cache_) measurement_cache_->invalidate(id);
    valid_ = false;
}

CachedGridResult GridCache::compute(Size available, std::uint64_t content_version) & {
    require_idle();
    if (valid_ && available_ == available && content_version_ == content_version) {
        return {result_, true, 0};
    }

    valid_ = false;
    computing_ = true;
    struct Guard {
        bool& computing;
        MeasurementCache* measurements;
        bool completed{};
        ~Guard() {
            // Callback exceptions unwind their own measurement guard first.
            // Clear partial entries without intercepting/rethrowing the exception.
            if (!completed && measurements) measurements->clear();
            computing = false;
        }
    } guard{computing_, measurement_cache_.get()};

    if (measurement_cache_) {
        if (content_version != content_version_) measurement_cache_->clear();
        measurement_cache_->reset_statistics();
    }
    const auto cached_measure = +[](ItemId id, const MeasureInput& input, void* context) {
        return static_cast<MeasurementCache*>(context)->measure(id, input);
    };
    detail::compute_grid_into(spec_, items_, available,
        measure_ && measurement_cache_ ? cached_measure : measure_,
        measure_ && measurement_cache_ ? measurement_cache_.get() : user_data_, *workspace_, result_, true);
    if (measurement_cache_ && measure_) {
        result_.measurement_count = measurement_cache_->measurement_count();
        if (!result_.succeeded()) measurement_cache_->clear();
    }
    available_ = available;
    content_version_ = content_version;
    valid_ = result_.succeeded();
    guard.completed = true;
    return {result_, false, result_.measurement_count};
}

} // namespace axisly
