#pragma once

#include "layout_engine.hpp"

#include <algorithm>
#include <variant>

namespace axisly::css::detail {
using SizingKeyword = std::variant<Unit, AvailableSpace>;

// Exact stretch sizes and intrinsic measurement constraints are distinct.
// The caller supplies the margin-box stretch budget and percentage basis.
[[nodiscard]] inline std::optional<SizingKeyword> sizing_keyword(const Length& style,
    std::optional<Unit> stretch, std::optional<Unit> basis) {
    switch (style.kind()) {
    case LengthKind::MinContent: return AvailableSpace::min_content();
    case LengthKind::MaxContent: return AvailableSpace::max_content();
    case LengthKind::Stretch:
        if (stretch) return std::max(Unit{0}, *stretch);
        break;
    case LengthKind::FitContent:
        if (stretch) return AvailableSpace::definite(std::max(Unit{0}, *stretch));
        break;
    case LengthKind::FitContentLength:
    case LengthKind::FitContentPercent:
        if (const auto limit = style.resolve_fit_content_limit(basis)) {
            return AvailableSpace::definite(std::max(Unit{0}, *limit));
        }
        break;
    default: break;
    }
    return std::nullopt;
}
} // namespace axisly::css::detail
