#pragma once

#include <axisly/result.hpp>

namespace axisly {

struct PixelRounding {
    Unit scale{1};
    // Absolute logical origin. Snapping uses the same device grid for nested UI.
    Unit origin_x{}, origin_y{};
    bool operator==(const PixelRounding&) const = default;
};

// Snap edges, then derive sizes. Measurement always remains in logical units.
// Accepts a result by value: move a temporary or copy a cached snapshot.
// Invalid scale/origin or overflow returns an error with no partial geometry.
[[nodiscard]] GridResult round_layout(GridResult result, PixelRounding rounding = {});

} // namespace axisly
