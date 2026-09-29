#include <axisly/css_style.hpp>

#include <cmath>
#include <stdexcept>

namespace axisly::css {
namespace {
bool numeric(const Length& value) {
    return value.kind() == LengthKind::Length || value.kind() == LengthKind::Percent
        || value.kind() == LengthKind::Calc;
}
void dimension(const Length& value, bool auto_allowed) {
    switch (value.kind()) {
    case LengthKind::Length:
    case LengthKind::Percent:
    case LengthKind::MinContent:
    case LengthKind::MaxContent:
    case LengthKind::FitContent:
    case LengthKind::Stretch:
    case LengthKind::Content:
    case LengthKind::FitContentLength:
    case LengthKind::FitContentPercent:
    case LengthKind::Calc:
        return;
    case LengthKind::Auto:
        if (auto_allowed) return;
        break;
    }
    throw std::invalid_argument("Invalid CSS dimension keyword");
}
void edge(const Length& value, bool auto_allowed) {
    if (numeric(value) || (auto_allowed && value.kind() == LengthKind::Auto)) return;
    throw std::invalid_argument("CSS edge requires length, percent, calc or allowed auto");
}
void edges(const EdgeValues& values, bool auto_allowed) {
    edge(values.left, auto_allowed);
    edge(values.top, auto_allowed);
    edge(values.right, auto_allowed);
    edge(values.bottom, auto_allowed);
}
} // namespace

void validate_style(const Style& style) {
    dimension(style.box.size.width, true);
    dimension(style.box.size.height, true);
    edge(style.box.min_size.width, true);
    edge(style.box.min_size.height, true);
    edge(style.box.max_size.width, true);
    edge(style.box.max_size.height, true);
    edges(style.box.margin, true);
    edges(style.box.padding, false);
    edges(style.box.border, false);
    edges(style.inset, true);
    edge(style.gap.width, false);
    edge(style.gap.height, false);
    dimension(style.flex_basis, true);
    if (style.box.aspect_ratio && (!std::isfinite(*style.box.aspect_ratio)
        || *style.box.aspect_ratio <= 0)) {
        throw std::invalid_argument("CSS aspect ratio must be finite and positive");
    }
    if (!std::isfinite(style.scrollbar_width) || style.scrollbar_width < 0
        || !std::isfinite(style.flex_grow) || style.flex_grow < 0
        || !std::isfinite(style.flex_shrink) || style.flex_shrink < 0
        || style.flex_line_count == 0) {
        throw std::invalid_argument("CSS style numeric factor is invalid");
    }
    validate_grid_style(style.grid);
}
} // namespace axisly::css
