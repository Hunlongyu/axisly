#pragma once

#include <axisly/css_values.hpp>

#include <functional>
#include <optional>

namespace axisly::css {

struct MeasureRequest {
    // Known content-box dimensions override the corresponding measured result.
    // A present zero remains distinct from an unknown dimension.
    std::optional<Unit> known_content_width, known_content_height;
    AvailableSpace available_width{AvailableSpace::indefinite()};
    AvailableSpace available_height{AvailableSpace::indefinite()};
    bool operator==(const MeasureRequest&) const = default;
};

struct Measurement {
    Size content_size{};
    // Offsets from the content box's top. Both are optional because a host may
    // have no text baseline; layout algorithms may synthesize one from the box.
    std::optional<Unit> first_baseline, last_baseline;
    // The painted content extent can differ from a constrained layout size.
    // When omitted, content_size is also used for scrollable overflow.
    std::optional<Size> scrollable_content_size;
};

using MeasureFunction = std::function<Measurement(const MeasureRequest&)>;

void validate_measure_request(const MeasureRequest& request);
void validate_measurement(const Measurement& measurement);

} // namespace axisly::css
