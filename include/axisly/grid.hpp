#pragma once

#include <cstdint>
#include <cstddef>
#include <limits>
#include <optional>
#include <vector>

#include <axisly/geometry.hpp>
#include <axisly/track.hpp>

namespace axisly {

using ItemId = std::uint64_t;

// Use on either or both axes, with GridSpec::auto_flow enabled.
inline constexpr std::uint32_t auto_position = std::numeric_limits<std::uint32_t>::max();
enum class AutoFlow : std::uint8_t { Explicit, Row, Column, RowDense, ColumnDense };

enum class HorizontalAlignment : std::uint8_t {
    Start,
    Center,
    End,
    Stretch
};

enum class VerticalAlignment : std::uint8_t {
    Start,
    Center,
    End,
    Stretch,
    Baseline
};

struct GridSpec {
    std::vector<Track> columns;
    std::vector<Track> rows;
    Unit column_gap{};
    Unit row_gap{};
    Insets padding{};
    AutoFlow auto_flow{AutoFlow::Explicit};
    bool allow_implicit_tracks{};
    Track implicit_column{Track::content()};
    Track implicit_row{Track::content()};
    // Growth per axis beyond explicit tracks; search/occupancy work is bounded.
    std::size_t max_implicit_tracks{4096};
    std::size_t max_placement_work{1'000'000};

    bool operator==(const GridSpec&) const = default;
};

struct GridItem {
    ItemId id{};
    std::uint32_t column{};
    std::uint32_t row{};
    // Positive track counts; merged cells include internal gaps. Growth beyond
    // explicit tracks requires allow_implicit_tracks.
    std::uint32_t column_span{1};
    std::uint32_t row_span{1};
    Insets margin{};
    HorizontalAlignment horizontal_alignment{HorizontalAlignment::Stretch};
    VerticalAlignment vertical_alignment{VerticalAlignment::Stretch};
    // Fallback desired size without a measurement callback; defaults to zero.
    Size preferred_size{};
    // First baseline from the top of preferred_size; absent means bottom edge.
    std::optional<Unit> baseline{};

    bool operator==(const GridItem&) const = default;
};

} // namespace axisly
