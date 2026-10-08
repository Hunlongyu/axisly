#pragma once

#include <axisly/geometry.hpp>

namespace axisly {

struct PixelRounding {
    Unit scale{1};
    // Absolute logical origin. Snapping uses the same device grid for nested UI.
    Unit origin_x{}, origin_y{};
    bool operator==(const PixelRounding&) const = default;
};

} // namespace axisly
