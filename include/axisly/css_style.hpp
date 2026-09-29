#pragma once

#include <axisly/css_box.hpp>
#include <axisly/css_grid.hpp>

#include <cstdint>
#include <optional>

namespace axisly::css {

enum class Display : std::uint8_t { Block, FlowRoot, Flex, Grid, None };
enum class Position : std::uint8_t { Relative, Absolute };
enum class Overflow : std::uint8_t { Visible, Clip, Hidden, Scroll };
enum class FloatSide : std::uint8_t { None, Left, Right };
enum class ClearSide : std::uint8_t { None, Left, Right, Both };
enum class Contain : std::uint8_t { None, Layout, Paint, Content };
enum class TextAlign : std::uint8_t { Auto, LegacyLeft, LegacyRight, LegacyCenter };

enum class AlignmentSafety : std::uint8_t { Unsafe, Safe };
enum class ItemPosition : std::uint8_t {
    Start, End, FlexStart, FlexEnd, SelfStart, SelfEnd, Center, Baseline, Stretch,
};
struct ItemAlignment {
    ItemPosition position{ItemPosition::Stretch};
    AlignmentSafety safety{AlignmentSafety::Unsafe};
    bool operator==(const ItemAlignment&) const = default;
};

enum class ContentPosition : std::uint8_t {
    Start, End, FlexStart, FlexEnd, Center, Stretch,
    SpaceBetween, SpaceEvenly, SpaceAround,
};
struct ContentAlignment {
    ContentPosition position{ContentPosition::Stretch};
    AlignmentSafety safety{AlignmentSafety::Unsafe};
    bool operator==(const ContentAlignment&) const = default;
};

enum class FlexDirection : std::uint8_t { Row, RowReverse, Column, ColumnReverse };
enum class FlexWrap : std::uint8_t { NoWrap, Wrap, WrapReverse, Balance, BalanceReverse };

struct Style {
    // Taffy 0.14 with all layout features enabled defaults to Flex.
    Display display{Display::Flex};
    BoxStyle box;
    bool item_is_table{};
    Overflow overflow_x{Overflow::Visible};
    Overflow overflow_y{Overflow::Visible};
    Unit scrollbar_width{};
    Contain contain{Contain::None};
    FloatSide float_side{FloatSide::None};
    ClearSide clear{ClearSide::None};
    Position position{Position::Relative};
    EdgeValues inset{Length::automatic(), Length::automatic(),
                     Length::automatic(), Length::automatic()};

    std::optional<ItemAlignment> align_items, align_self, justify_items, justify_self;
    std::optional<ContentAlignment> align_content, justify_content;
    Dimensions gap{Length::length(0), Length::length(0)};
    TextAlign text_align{TextAlign::Auto};

    FlexDirection flex_direction{FlexDirection::Row};
    FlexWrap flex_wrap{FlexWrap::NoWrap};
    std::uint16_t flex_line_count{1};
    Length flex_basis{Length::automatic()};
    Unit flex_grow{};
    Unit flex_shrink{1};

    GridStyle grid;

    bool operator==(const Style&) const = default;
};

// Checks property domains independently of a particular layout algorithm.
// This does not imply all represented algorithms have an implementation yet.
void validate_style(const Style& style);

} // namespace axisly::css
