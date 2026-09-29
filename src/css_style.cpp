#include <axisly/css_style.hpp>

#include <cmath>
#include <stdexcept>

namespace axisly::css {
namespace {
bool numeric(const Length& value) {
    return value.kind() == LengthKind::Length || value.kind() == LengthKind::Percent
        || value.kind() == LengthKind::Calc;
}
template <typename Enum>
void enumeration(Enum value, Enum last, const char* message) {
    const auto raw = static_cast<int>(value);
    if (raw < 0 || raw > static_cast<int>(last)) {
        throw std::invalid_argument(message);
    }
}
void item_alignment(const std::optional<ItemAlignment>& value) {
    if (!value) return;
    enumeration(value->position, ItemPosition::Stretch, "Invalid CSS item alignment");
    enumeration(value->safety, AlignmentSafety::Safe, "Invalid CSS alignment safety");
}
void content_alignment(const std::optional<ContentAlignment>& value) {
    if (!value) return;
    enumeration(value->position, ContentPosition::SpaceAround,
        "Invalid CSS content alignment");
    enumeration(value->safety, AlignmentSafety::Safe, "Invalid CSS alignment safety");
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
    enumeration(style.display, Display::None, "Invalid CSS display");
    enumeration(style.position, Position::Absolute, "Invalid CSS position");
    enumeration(style.overflow_x, Overflow::Scroll, "Invalid CSS overflow");
    enumeration(style.overflow_y, Overflow::Scroll, "Invalid CSS overflow");
    enumeration(style.float_side, FloatSide::Right, "Invalid CSS float");
    enumeration(style.clear, ClearSide::Both, "Invalid CSS clear");
    enumeration(style.contain, Contain::Content, "Invalid CSS containment");
    enumeration(style.text_align, TextAlign::LegacyCenter, "Invalid CSS text alignment");
    enumeration(style.flex_direction, FlexDirection::ColumnReverse,
        "Invalid CSS flex direction");
    enumeration(style.flex_wrap, FlexWrap::BalanceReverse, "Invalid CSS flex wrap");
    enumeration(style.box.box_sizing, BoxSizing::ContentBox, "Invalid CSS box sizing");
    enumeration(style.box.direction, Direction::Rtl, "Invalid CSS direction");
    item_alignment(style.align_items);
    item_alignment(style.align_self);
    item_alignment(style.justify_items);
    item_alignment(style.justify_self);
    content_alignment(style.align_content);
    content_alignment(style.justify_content);
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
