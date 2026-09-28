#include <axisly/track.hpp>

#include <cmath>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace axisly {
namespace {

TrackBound as_bound(const Track& track) {
    return std::visit([](const auto& definition) -> TrackBound {
        using Type = std::decay_t<decltype(definition)>;
        if constexpr (std::is_same_v<Type, RangeTrack>) {
            throw std::invalid_argument("A range bound must not be another range");
        } else {
            return definition;
        }
    }, track.definition());
}

} // namespace

Track::Track(Definition definition) noexcept : definition_(std::move(definition)) {}

Track Track::fixed(Unit size) {
    if (!std::isfinite(size) || size < 0.0f) {
        throw std::invalid_argument("Fixed track size must be finite and non-negative");
    }
    return Track{FixedTrack{size}};
}

Track Track::weighted(float weight, Unit minimum, std::optional<Unit> maximum) {
    if (!std::isfinite(weight) || weight <= 0.0f) {
        throw std::invalid_argument("Track weight must be finite and positive");
    }
    if (!std::isfinite(minimum) || minimum < 0.0f) {
        throw std::invalid_argument("Weighted track minimum must be finite and non-negative");
    }
    if (maximum && (!std::isfinite(*maximum) || *maximum < minimum)) {
        throw std::invalid_argument("Weighted track maximum must be finite and at least its minimum");
    }
    return Track{WeightedTrack{weight, minimum, maximum}};
}

Track Track::content() noexcept {
    return Track{ContentTrack{}};
}

Track Track::percentage(float fraction) {
    if (!std::isfinite(fraction) || fraction < 0.0f) {
        throw std::invalid_argument("Percentage fraction must be finite and non-negative");
    }
    return Track{PercentageTrack{fraction}};
}

Track Track::range(const Track& minimum, const Track& maximum) {
    const auto lower = as_bound(minimum);
    const auto upper = as_bound(maximum);
    if (std::holds_alternative<WeightedTrack>(lower)) {
        throw std::invalid_argument("A range minimum must be fixed, percentage or content-sized");
    }
    if (const auto* weighted = std::get_if<WeightedTrack>(&upper);
        weighted && (weighted->minimum != 0.0f || weighted->maximum.has_value())) {
        throw std::invalid_argument("A range's weighted upper bound must not carry separate numeric limits");
    }
    if (const auto* fixed_lower = std::get_if<FixedTrack>(&lower)) {
        if (const auto* fixed_upper = std::get_if<FixedTrack>(&upper);
            fixed_upper && fixed_lower->size > fixed_upper->size) {
            throw std::invalid_argument("A fixed range maximum must not be smaller than its minimum");
        }
    }
    if (const auto* percent_lower = std::get_if<PercentageTrack>(&lower)) {
        if (const auto* percent_upper = std::get_if<PercentageTrack>(&upper);
            percent_upper && percent_lower->fraction > percent_upper->fraction) {
            throw std::invalid_argument("A percentage range maximum must not be smaller than its minimum");
        }
    }
    return Track{RangeTrack{lower, upper}};
}

TrackKind Track::kind() const noexcept {
    return std::visit([](const auto& definition) {
        using Type = std::decay_t<decltype(definition)>;
        if constexpr (std::is_same_v<Type, FixedTrack>) {
            return TrackKind::Fixed;
        } else if constexpr (std::is_same_v<Type, WeightedTrack>) {
            return TrackKind::Weighted;
        } else if constexpr (std::is_same_v<Type, ContentTrack>) {
            return TrackKind::Content;
        } else if constexpr (std::is_same_v<Type, PercentageTrack>) {
            return TrackKind::Percentage;
        } else {
            return TrackKind::Range;
        }
    }, definition_);
}

} // namespace axisly
