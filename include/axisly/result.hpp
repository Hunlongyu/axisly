#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <axisly/geometry.hpp>
#include <axisly/grid.hpp>

namespace axisly {

enum class GridErrorCode : std::uint8_t {
    NotComputed,
    None,
    InvalidContainerSize,
    InvalidTrack,
    InvalidGap,
    InvalidPadding,
    InvalidMargin,
    InvalidPlacement,
    InvalidSpan,
    InvalidAlignment,
    DuplicateItemId,
    InvalidMeasurement,
    NumericOverflow,
    UnsupportedFeature,
    InvalidPreferredSize,
    InvalidRounding,
    PlacementLimit,
    TrackLimit,
    TreeLimit
};

struct GridError {
    GridErrorCode code{GridErrorCode::NotComputed};
    std::string message;
    // Zero is a valid ItemId; nullopt denotes a grid-level error.
    std::optional<ItemId> item_id;
};

struct ItemLayout {
    ItemId id{};
    Rect rect{};
    // The full allocated span, including internal gaps, before margins/alignment.
    Rect cell_rect{};
    // Resolved placement, useful when an input axis was automatic.
    std::uint32_t column{}, row{}, column_span{1}, row_span{1};
};

struct GridResult {
    // Occupied track extent plus padding; may be smaller or larger than available.
    Size size{};
    std::vector<ItemLayout> items;
    GridError error{};
    // Actual callback invocations, including invalid results before a failure.
    // Available in all build modes; zero when no callback is provided.
    std::size_t measurement_count{};

    [[nodiscard]] bool succeeded() const noexcept { return error.code == GridErrorCode::None; }
};

} // namespace axisly
