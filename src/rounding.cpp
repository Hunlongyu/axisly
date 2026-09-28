#include <gridly/rounding.hpp>
#include "detail/rounding.hpp"

#include <cmath>
#include <limits>

namespace gridly::detail {
bool valid_rounding(PixelRounding r) noexcept {
    return std::isfinite(r.scale) && r.scale > 0
        && std::isfinite(r.origin_x) && std::isfinite(r.origin_y);
}
namespace {
bool fits(double x) noexcept {
    return std::isfinite(x) && x >= 0 && x <= std::numeric_limits<Unit>::max();
}
bool axis(Unit& start, Unit& size, Unit origin, Unit scale) noexcept {
    const double end = static_cast<double>(start) + size;
    if (!fits(start) || !fits(size) || !fits(end)) return false;
    // Public rectangles use Unit precision. Normalize a reconstructed end to
    // that same precision as a neighboring rectangle's published start before
    // snapping (e.g. six equal columns in five units share the edge at 2.5).
    const Unit end_at_unit_precision = static_cast<Unit>(end);
    const double base = std::round(static_cast<double>(origin) * scale);
    const double left = std::round((static_cast<double>(origin) + start) * scale);
    const double right = std::round((static_cast<double>(origin) + end_at_unit_precision) * scale);
    const double position = (left - base) / scale;
    const double extent = (right - left) / scale;
    if (!fits(position) || !fits(extent) || !fits(position + extent)) return false;
    start = static_cast<Unit>(position);
    size = static_cast<Unit>(extent);
    return true;
}
}
bool snap_rect(Rect& rect, PixelRounding r) noexcept {
    return axis(rect.x, rect.width, r.origin_x, r.scale)
        && axis(rect.y, rect.height, r.origin_y, r.scale);
}
bool snap_size(Size& size, PixelRounding r) noexcept {
    Rect rect{0, 0, size.width, size.height};
    if (!snap_rect(rect, r)) return false;
    size = {rect.width, rect.height};
    return true;
}
}

namespace gridly {
GridResult round_layout(GridResult result, PixelRounding rounding) {
    if (!result.succeeded()) return result;
    const auto fail = [&](GridErrorCode code) {
        result.size = {};
        result.items.clear();
        result.error = {code, "Pixel rounding needs finite geometry, origin and a positive finite scale", {}};
    };
    if (!detail::valid_rounding(rounding)) { fail(GridErrorCode::InvalidRounding); return result; }
    if (!detail::snap_size(result.size, rounding)) { fail(GridErrorCode::NumericOverflow); return result; }
    for (auto& item : result.items) {
        if (!detail::snap_rect(item.rect, rounding) || !detail::snap_rect(item.cell_rect, rounding)) {
            fail(GridErrorCode::NumericOverflow);
            break;
        }
    }
    return result;
}
}
