#pragma once

#include <cstdint>
#include <optional>
#include <variant>

#include <axisly/geometry.hpp>

namespace axisly {

enum class TrackKind : std::uint8_t {
    Fixed,
    Weighted,
    Content,
    Range,
    Percentage
};

struct FixedTrack {
    Unit size{};
    bool operator==(const FixedTrack&) const = default;
};

struct WeightedTrack {
    float weight{1.0f};
    Unit minimum{};
    std::optional<Unit> maximum;
    bool operator==(const WeightedTrack&) const = default;
};

struct ContentTrack {
    bool operator==(const ContentTrack&) const = default;
};

struct PercentageTrack {
    // 0.25 is 25% of the available inner extent, before subtracting gaps.
    float fraction{};
    bool operator==(const PercentageTrack&) const = default;
};

// Range bounds cannot themselves contain another range.
using TrackBound = std::variant<FixedTrack, WeightedTrack, ContentTrack, PercentageTrack>;

struct RangeTrack {
    TrackBound minimum;
    TrackBound maximum;
    bool operator==(const RangeTrack&) const = default;
};

class Track {
public:
    using Definition = std::variant<FixedTrack, WeightedTrack, ContentTrack, RangeTrack, PercentageTrack>;

    // Invalid numbers or range combinations throw std::invalid_argument.
    [[nodiscard]] static Track fixed(Unit size);
    // minimum is a floor on the proportional size, not a base added to it.
    // nullopt maximum means no user-specified upper limit.
    [[nodiscard]] static Track weighted(
        float weight = 1.0f, Unit minimum = 0.0f, std::optional<Unit> maximum = std::nullopt);
    [[nodiscard]] static Track content() noexcept;
    // Fractions above 1 are permitted and may overflow the available container.
    [[nodiscard]] static Track percentage(float fraction);
    // Fixed/Percentage/Content lower; Fixed/Percentage/Content/Weighted upper.
    // With mixed numeric units, a resolved lower bound wins over a smaller upper.
    // A Fixed upper bound caps content-sized growth; Content targets preferred
    // size; Weighted shares available space. Fixed minima survive smaller
    // preferences, while fixed maxima clip larger content minima.
    // A weighted maximum must use the unconstrained weighted(weight) form.
    [[nodiscard]] static Track range(const Track& minimum, const Track& maximum);

    [[nodiscard]] TrackKind kind() const noexcept;
    [[nodiscard]] const Definition& definition() const noexcept { return definition_; }

    bool operator==(const Track&) const = default;

private:
    explicit Track(Definition definition) noexcept;
    Definition definition_;
};

} // namespace axisly
