#pragma once
#include <gridly/measurement.hpp>
#include <cmath>

namespace gridly::detail {
inline bool valid_measurement(const MeasureResult& r) noexcept {
    const auto valid = [](Unit x) { return std::isfinite(x) && x >= 0; };
    return valid(r.minimum.width) && valid(r.minimum.height)
        && valid(r.preferred.width) && valid(r.preferred.height)
        && r.minimum.width <= r.preferred.width && r.minimum.height <= r.preferred.height
        && (!r.baseline || (valid(*r.baseline) && *r.baseline <= r.preferred.height));
}
}
