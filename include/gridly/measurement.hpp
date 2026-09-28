#pragma once

#include <optional>

#include <gridly/geometry.hpp>
#include <gridly/grid.hpp>

namespace gridly {

struct MeasureInput {
    // nullopt means unknown; a present zero is a known zero constraint.
    std::optional<Unit> known_width;
    std::optional<Unit> known_height;
    // Advisory space: container minus padding and this item's margins, clamped
    // to zero. In the second pass width is replaced by the final item width.
    Size available{};
    bool operator==(const MeasureInput&) const = default;
};

struct MeasureResult {
    // Both sizes must be finite, non-negative, with minimum <= preferred per axis.
    // Content uses both bounds; Range selects intrinsic or numeric bounds.
    // Spans can raise track minima within caps. Fixed sizes and numeric maxima
    // take priority over item requirements.
    Size minimum{};
    Size preferred{};
    // First baseline offset from the top, in [0, preferred.height].
    std::optional<Unit> baseline{};
};

// The host owns both the item and user_data throughout a synchronous layout call.
// Do not mutate layout inputs during callbacks. No context is retained afterward.
using MeasureCallback = MeasureResult (*)(ItemId, const MeasureInput&, void* user_data);

} // namespace gridly
