#pragma once
#include <axisly/rounding.hpp>

namespace axisly::detail {
[[nodiscard]] bool valid_rounding(PixelRounding rounding) noexcept;
[[nodiscard]] bool snap_rect(Rect& rect, PixelRounding rounding) noexcept;
[[nodiscard]] bool snap_size(Size& size, PixelRounding rounding) noexcept;
}
