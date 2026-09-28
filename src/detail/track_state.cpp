#include "track_state.hpp"

#include <algorithm>
#include <limits>
#include <variant>

namespace axisly::detail {

TrackState initialize_track(const Track& track, double percentage_basis) noexcept {
    constexpr double unbounded = std::numeric_limits<double>::infinity();
    const auto& definition = track.definition();
    if (const auto* fixed = std::get_if<FixedTrack>(&definition)) {
        return {TrackGrowth::Fixed, fixed->size, fixed->size, fixed->size, 0, false, false};
    }
    if (const auto* percent = std::get_if<PercentageTrack>(&definition)) {
        const double size = percentage_basis * percent->fraction;
        return {TrackGrowth::Fixed, size, size, size, 0, false, false};
    }
    if (const auto* weighted = std::get_if<WeightedTrack>(&definition)) {
        const double maximum = weighted->maximum ? static_cast<double>(*weighted->maximum) : unbounded;
        return {TrackGrowth::Weighted, weighted->minimum, weighted->minimum, maximum, weighted->weight, false, false};
    }
    if (std::holds_alternative<ContentTrack>(definition)) {
        return {TrackGrowth::Content, 0, 0, unbounded, 0, true, true};
    }

    const auto& range = std::get<RangeTrack>(definition);
    const auto* fixed_minimum = std::get_if<FixedTrack>(&range.minimum);
    const auto* fixed_maximum = std::get_if<FixedTrack>(&range.maximum);
    const auto* weighted = std::get_if<WeightedTrack>(&range.maximum);
    const auto* percent_minimum = std::get_if<PercentageTrack>(&range.minimum);
    const auto* percent_maximum = std::get_if<PercentageTrack>(&range.maximum);
    const double minimum = fixed_minimum ? fixed_minimum->size
        : percent_minimum ? percentage_basis * percent_minimum->fraction : 0.0;
    // A resolved numeric lower bound wins if mixed units invert the range.
    const double maximum = std::max(minimum, fixed_maximum ? fixed_maximum->size
        : percent_maximum ? percentage_basis * percent_maximum->fraction : unbounded);
    return {weighted ? TrackGrowth::Weighted : TrackGrowth::Content,
            minimum, minimum, maximum, weighted ? weighted->weight : 0.0,
            fixed_minimum == nullptr && percent_minimum == nullptr, weighted == nullptr};
}

} // namespace axisly::detail
