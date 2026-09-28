#pragma once

#include <axisly/track.hpp>

namespace axisly::detail {

enum class TrackGrowth { Fixed, Content, Weighted };

struct TrackState {
    TrackGrowth growth{TrackGrowth::Fixed};
    // Double preserves intrinsic sums larger than Unit until final allocation.
    double minimum{};
    double preferred{};
    // Infinity denotes no explicit cap; final geometry must still fit Unit.
    double maximum{};
    double weight{};
    bool intrinsic_minimum{};
    bool intrinsic_preferred{};
};

// Normalize validated public definitions without modifying or retaining them.
[[nodiscard]] TrackState initialize_track(const Track& track, double percentage_basis = 0) noexcept;

} // namespace axisly::detail
