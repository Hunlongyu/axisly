#pragma once

#include <axisly/css_values.hpp>

#include <optional>

namespace axisly::css {

enum class BoxSizing { BorderBox, ContentBox };
enum class Direction { Ltr, Rtl };

struct Dimensions {
    Length width{Length::automatic()};
    Length height{Length::automatic()};
    bool operator==(const Dimensions&) const = default;
};

struct EdgeValues {
    Length left{Length::length(0)};
    Length top{Length::length(0)};
    Length right{Length::length(0)};
    Length bottom{Length::length(0)};
    bool operator==(const EdgeValues&) const = default;
};

struct BoxStyle {
    BoxSizing box_sizing{BoxSizing::BorderBox};
    Direction direction{Direction::Ltr};
    bool replaced{};
    Dimensions size;
    Dimensions min_size;
    Dimensions max_size;
    std::optional<Unit> aspect_ratio;
    EdgeValues margin;
    EdgeValues padding;
    EdgeValues border;
    bool operator==(const BoxStyle&) const = default;
};

enum class EdgeKind { Numeric, Auto, UnresolvedPercentage };

struct ResolvedEdge {
    EdgeKind kind{EdgeKind::Numeric};
    Unit value{};
    [[nodiscard]] std::optional<Unit> numeric() const noexcept;
    bool operator==(const ResolvedEdge&) const = default;
};

struct ResolvedEdges {
    ResolvedEdge left, top, right, bottom;
    [[nodiscard]] std::optional<Unit> horizontal() const;
    [[nodiscard]] std::optional<Unit> vertical() const;
    bool operator==(const ResolvedEdges&) const = default;
};

struct ResolvedDimensions {
    std::optional<Unit> width, height;
    bool operator==(const ResolvedDimensions&) const = default;
};

struct ResolvedBox {
    ResolvedEdges margin, padding, border;
    // These are border-box sizes. An absent value remains under algorithm control:
    // auto, intrinsic keywords and percentages with an indefinite basis differ.
    ResolvedDimensions preferred, minimum, maximum;
    std::optional<Unit> aspect_ratio;
    BoxSizing box_sizing{BoxSizing::BorderBox};
    Direction direction{Direction::Ltr};
    bool replaced{};
    [[nodiscard]] std::optional<Unit> horizontal_decoration() const;
    [[nodiscard]] std::optional<Unit> vertical_decoration() const;
};

// Percentage padding, border and margin use the containing block's inline size.
// Width and height percentages use the corresponding containing-block axis.
// Unknown bases stay explicit, including calc expressions containing percentages.
[[nodiscard]] ResolvedBox resolve_box(const BoxStyle& style,
    std::optional<Unit> containing_width, std::optional<Unit> containing_height);

// Resolve min/max in CSS order: max size first, then min size, so min wins if
// min exceeds max. The caller supplies the content-derived border-box size.
[[nodiscard]] Size constrain_size(Size candidate, const ResolvedBox& box);

} // namespace axisly::css
