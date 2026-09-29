#include <axisly/css_measurement.hpp>

#include <cmath>
#include <stdexcept>

namespace axisly::css {
namespace {
void validate_known(std::optional<Unit> value) {
    if (value && (!std::isfinite(*value) || *value < 0)) {
        throw std::invalid_argument("Known content dimension must be finite and nonnegative");
    }
}

void validate_available(AvailableSpace space) {
    if (space.kind == AvailableKind::Definite && (!std::isfinite(space.value) || space.value < 0)) {
        throw std::invalid_argument("Definite available space must be finite and nonnegative");
    }
}

void validate_baseline(std::optional<Unit> baseline, Unit height) {
    if (baseline && (!std::isfinite(*baseline) || *baseline < 0 || *baseline > height)) {
        throw std::invalid_argument("Content baseline must be within measured height");
    }
}
} // namespace

void validate_measure_request(const MeasureRequest& request) {
    validate_known(request.known_content_width);
    validate_known(request.known_content_height);
    validate_available(request.available_width);
    validate_available(request.available_height);
}

void validate_measurement(const Measurement& measurement) {
    if (!std::isfinite(measurement.content_size.width)
        || !std::isfinite(measurement.content_size.height)
        || measurement.content_size.width < 0 || measurement.content_size.height < 0) {
        throw std::invalid_argument("Measured content size must be finite and nonnegative");
    }
    validate_baseline(measurement.first_baseline, measurement.content_size.height);
    validate_baseline(measurement.last_baseline, measurement.content_size.height);
}
} // namespace axisly::css
