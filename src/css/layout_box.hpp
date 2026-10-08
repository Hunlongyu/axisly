#pragma once

#include "box_resolution.hpp"

namespace axisly::css::detail {

// The public resolver retains unresolved edge states. An algorithm's box has
// already resolved padding/border against its stage-specific basis. Store
// numeric insets and their checked sums once; keep decoration mutation behind
// one operation so later queries cannot observe stale cached sums.
class LayoutBox {
public:
    ResolvedEdges margin;
    Size scrollbar{};
    ResolvedDimensions preferred, minimum, maximum;
    ResolvedDimensions specified_minimum, specified_maximum;
    std::optional<Unit> aspect_ratio;
    BoxSizing box_sizing{BoxSizing::BorderBox};
    Direction direction{Direction::Ltr};
    bool replaced{};

    LayoutBox() = default;

    [[nodiscard]] const Insets& padding() const noexcept { return padding_; }
    [[nodiscard]] const Insets& border() const noexcept { return border_; }
    void set_decoration(Insets padding, Insets border) {
        const Size sums{
            add_box_extent(add_box_extent(padding.left, padding.right), add_box_extent(border.left, border.right)),
            add_box_extent(add_box_extent(padding.top, padding.bottom), add_box_extent(border.top, border.bottom))};
        padding_ = padding;
        border_ = border;
        padding_border_ = sums;
    }
    [[nodiscard]] Unit horizontal_padding_border() const noexcept { return padding_border_.width; }
    [[nodiscard]] Unit vertical_padding_border() const noexcept { return padding_border_.height; }
    [[nodiscard]] Unit horizontal_decoration() const {
        return add_box_extent(padding_border_.width, scrollbar.width);
    }
    [[nodiscard]] Unit vertical_decoration() const {
        return add_box_extent(padding_border_.height, scrollbar.height);
    }

private:
    Insets padding_{}, border_{};
    Size padding_border_{};
};

[[nodiscard]] ResolvedDimensions transfer_definite_aspect_ratio(ResolvedDimensions size, const LayoutBox& box);
[[nodiscard]] inline ResolvedDimensions transfer_aspect_ratio(ResolvedDimensions size, const LayoutBox& box) {
    if (!box.aspect_ratio || size.width.has_value() == size.height.has_value()) return size;
    return transfer_definite_aspect_ratio(size, box);
}

[[nodiscard]] inline Size validate_border_size(Size candidate) {
    if (!(candidate.width >= 0 && candidate.width <= std::numeric_limits<Unit>::max()
        && candidate.height >= 0 && candidate.height <= std::numeric_limits<Unit>::max())) {
        invalid_numeric_value("Candidate border-box size must be finite and nonnegative");
    }
    return candidate;
}

template<class Bounds>
[[nodiscard]] inline Size constrain_size(Size candidate, const Bounds& box) {
    candidate = validate_border_size(candidate);
    if (box.maximum.width) candidate.width = std::min(candidate.width, *box.maximum.width);
    if (box.maximum.height) candidate.height = std::min(candidate.height, *box.maximum.height);
    if (box.minimum.width) candidate.width = std::max(candidate.width, *box.minimum.width);
    if (box.minimum.height) candidate.height = std::max(candidate.height, *box.minimum.height);
    return candidate;
}

void resolve_box_dimensions(LayoutBox& box, const BoxStyle& style,
    std::optional<Unit> width, std::optional<Unit> height);
[[nodiscard]] LayoutBox resolve_numeric_box(const BoxStyle& style,
    std::optional<Unit> width, std::optional<Unit> height);

} // namespace axisly::css::detail
