#pragma once
#include <gridly/rounding.hpp>

namespace gridly::detail {
[[nodiscard]] bool valid_rounding(PixelRounding rounding) noexcept;
[[nodiscard]] bool snap_rect(Rect& rect, PixelRounding rounding) noexcept;
[[nodiscard]] bool snap_size(Size& size, PixelRounding rounding) noexcept;
}
