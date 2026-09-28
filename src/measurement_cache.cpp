#include <gridly/measurement_cache.hpp>
#include "detail/measurement_validation.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace gridly {
struct MeasurementCache::Impl {
    struct Entry { MeasureInput input; MeasureResult result; };
    std::unordered_map<ItemId, std::vector<Entry>> items;
    std::size_t capacity, calls{}, hits{}, entries{};
    MeasureCallback callback{};
    void* context{};
    bool measuring{};
    explicit Impl(std::size_t size) : capacity(size) {}
    void idle() const {
        if (measuring) throw std::logic_error("MeasurementCache cannot recurse or mutate in its callback");
    }
};

MeasurementCache::MeasurementCache(std::size_t entries_per_item) {
    if (entries_per_item == 0 || entries_per_item > 64)
        throw std::invalid_argument("MeasurementCache entries per item must be in [1, 64]");
    impl_ = std::make_unique<Impl>(entries_per_item);
}
MeasurementCache::~MeasurementCache() = default;
void MeasurementCache::set_measure(MeasureCallback measure, void* user_data) {
    impl_->idle();
    if (!measure) user_data = nullptr;
    if (measure == impl_->callback && user_data == impl_->context) return;
    clear();
    impl_->callback = measure;
    impl_->context = user_data;
}
MeasureResult MeasurementCache::measure(ItemId id, const MeasureInput& input) {
    auto& state = *impl_;
    state.idle();
    const auto valid = [](Unit x) { return std::isfinite(x) && x >= 0; };
    if (!valid(input.available.width) || !valid(input.available.height)
        || (input.known_width && !valid(*input.known_width))
        || (input.known_height && !valid(*input.known_height)))
        throw std::invalid_argument("MeasurementCache requires finite non-negative constraints");
    if (const auto found = state.items.find(id); found != state.items.end()) {
        auto& entries = found->second;
        const auto hit = std::find_if(entries.begin(), entries.end(), [&](const Impl::Entry& e) { return e.input == input; });
        if (hit != entries.end()) {
            const auto result = hit->result;
            std::rotate(entries.begin(), hit, hit + 1);
            ++state.hits;
            return result;
        }
    }
    if (!state.callback) return {};
    state.measuring = true;
    struct Guard { bool& value; ~Guard() { value = false; } } guard{state.measuring};
    ++state.calls;
    const auto result = state.callback(id, input, state.context);
    if (detail::valid_measurement(result)) {
        auto& entries = state.items[id];
        if (entries.empty()) entries.reserve(state.capacity);
        if (entries.size() == state.capacity) entries.pop_back();
        else ++state.entries;
        entries.insert(entries.begin(), {input, result});
    }
    return result;
}
void MeasurementCache::invalidate(ItemId id) {
    impl_->idle();
    if (const auto found = impl_->items.find(id); found != impl_->items.end()) {
        impl_->entries -= found->second.size();
        impl_->items.erase(found);
    }
}
void MeasurementCache::clear() { impl_->idle(); impl_->items.clear(); impl_->entries = 0; }
void MeasurementCache::reset_statistics() { impl_->idle(); impl_->calls = impl_->hits = 0; }
std::size_t MeasurementCache::measurement_count() const noexcept { return impl_->calls; }
std::size_t MeasurementCache::hit_count() const noexcept { return impl_->hits; }
std::size_t MeasurementCache::entry_count() const noexcept { return impl_->entries; }
}
