#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

#include <axisly/geometry.hpp>
#include <axisly/grid.hpp>

namespace axisly {

// Typed layout values. Percentages are fractions (0.5 means 50%).
struct FlexLength {
    enum class Kind : std::uint8_t { Auto, Fixed, Percent, Content, MinContent, MaxContent, FitContent };
    Kind kind{Kind::Auto};
    Unit value{};
    [[nodiscard]] static constexpr FlexLength fixed(Unit n) noexcept { return {Kind::Fixed, n}; }
    [[nodiscard]] static constexpr FlexLength percent(Unit fraction) noexcept { return {Kind::Percent, fraction}; }
    [[nodiscard]] static constexpr FlexLength content() noexcept { return {Kind::Content, 0}; }
    [[nodiscard]] static constexpr FlexLength min_content() noexcept { return {Kind::MinContent, 0}; }
    [[nodiscard]] static constexpr FlexLength max_content() noexcept { return {Kind::MaxContent, 0}; }
    [[nodiscard]] static constexpr FlexLength fit_content() noexcept { return {Kind::FitContent, 0}; }
    bool operator==(const FlexLength&) const = default;
};

enum class FlexDirection : std::uint8_t { Row, RowReverse, Column, ColumnReverse };
enum class FlexWrap : std::uint8_t { NoWrap, Wrap, WrapReverse };
enum class WritingMode : std::uint8_t { HorizontalTb, VerticalRl, VerticalLr, SidewaysRl, SidewaysLr };
enum class TextDirection : std::uint8_t { Ltr, Rtl };
enum class FlexPosition : std::uint8_t { Normal, Absolute };
enum class FlexVisibility : std::uint8_t { Visible, Collapse };
enum class FlexOverflow : std::uint8_t { Visible, Scroll, Hidden, Clip };
enum class FlexBoxSizing : std::uint8_t { ContentBox, BorderBox };

// FlexStart/FlexEnd follow the flex flow; Start/End follow the writing mode.
enum class FlexAlign : std::uint8_t {
    Auto, Normal, Start, End, Center, FlexStart, FlexEnd, Stretch,
    FirstBaseline, LastBaseline, SpaceBetween, SpaceAround, SpaceEvenly
};
struct FlexAlignment {
    FlexAlign value{FlexAlign::Normal};
    bool safe{}; // Overflow alignment for positional values.
    bool operator==(const FlexAlignment&) const = default;
};

struct FlexEdges {
    FlexLength left{}, top{}, right{}, bottom{};
    bool operator==(const FlexEdges&) const = default;
};

struct FlexSpec {
    FlexDirection direction{FlexDirection::Row};
    FlexWrap wrap{FlexWrap::NoWrap};
    WritingMode writing_mode{WritingMode::HorizontalTb};
    TextDirection text_direction{TextDirection::Ltr};
    FlexAlignment justify_content{};
    FlexAlignment align_items{FlexAlign::Stretch};
    FlexAlignment align_content{FlexAlign::Stretch};
    Unit column_gap{}, row_gap{};
    Insets padding{}, border{};
    bool operator==(const FlexSpec&) const = default;
};

struct FlexItem {
    ItemId id{};
    int order{};
    Unit grow{};
    Unit shrink{1};
    FlexLength basis{};
    FlexLength width{}, height{};
    FlexLength minimum_width{}, minimum_height{};
    FlexLength maximum_width{}, maximum_height{};
    FlexEdges margin{FlexLength::fixed(0), FlexLength::fixed(0),
                     FlexLength::fixed(0), FlexLength::fixed(0)};
    Insets padding{}, border{};
    FlexAlignment align_self{FlexAlign::Auto};
    FlexPosition position{FlexPosition::Normal};
    FlexVisibility visibility{FlexVisibility::Visible};
    FlexOverflow overflow{FlexOverflow::Visible};
    FlexBoxSizing box_sizing{FlexBoxSizing::ContentBox};
    // Physical offsets for absolute-positioned items; auto leaves static position.
    FlexEdges inset{};
    std::optional<Unit> aspect_ratio; // Intrinsic width / height.
    bool replaced{}; // Image-like content uses the replaced auto-minimum rule.
    // Static fallback in content-box units when no callback is supplied.
    Size min_content{}, max_content{}, preferred_size{};
    std::optional<Unit> baseline{};
    bool operator==(const FlexItem&) const = default;
};

struct FlexMeasureInput {
    // The layout engine requests a content-box measurement. Present zero differs
    // from an unknown dimension. The host may reflow height at a known width.
    std::optional<Unit> known_content_width, known_content_height;
    std::optional<Unit> available_content_width, available_content_height;
    WritingMode writing_mode{WritingMode::HorizontalTb};
};
struct FlexMeasureResult {
    Size min_content{}, max_content{}, preferred{};
    // First and last baseline offsets from the content box's physical top.
    std::optional<Unit> first_baseline, last_baseline;
};
using FlexMeasureCallback = FlexMeasureResult (*)(ItemId, const FlexMeasureInput&, void*);

enum class FlexErrorCode : std::uint8_t {
    None, InvalidContainerSize, InvalidContainerStyle, InvalidItemStyle,
    DuplicateItemId, InvalidMeasurement, NumericOverflow
};
struct FlexError {
    FlexErrorCode code{FlexErrorCode::None};
    std::string message;
    std::optional<ItemId> item_id;
};
struct FlexLineLayout {
    std::size_t first_item{}, item_count{};
    Rect rect{};
};
struct FlexItemLayout {
    ItemId id{};
    Rect rect{}; // Physical border box relative to container border box.
    Rect content_rect{};
    Rect static_position_rect{}; // For absolute items, hypothetical static box.
    std::size_t line_index{};
    bool absolute{};
    bool collapsed{};
};
struct FlexResult {
    Size size{};
    Rect occupied_bounds{};
    std::vector<FlexItemLayout> items; // Input order, including absolute items.
    std::vector<FlexLineLayout> lines;
    std::vector<ItemId> paint_order; // Stable order-modified document order.
    FlexError error{};
    std::size_t measurement_count{};
    [[nodiscard]] bool succeeded() const noexcept { return error.code == FlexErrorCode::None; }
};

// Computes finite, definite container geometry. Flex items are borrowed for the
// synchronous call; no host widgets or callback state are retained. Failure is
// atomic and callback exceptions propagate.
[[nodiscard]] FlexResult compute_flex(const FlexSpec& spec, std::span<const FlexItem> items, Size available);
[[nodiscard]] FlexResult compute_flex(const FlexSpec& spec, std::span<const FlexItem> items,
    Size available, FlexMeasureCallback measure, void* user_data = nullptr);

template<class Measure>
    requires std::is_invocable_r_v<FlexMeasureResult, Measure&, ItemId, const FlexMeasureInput&>
[[nodiscard]] FlexResult compute_flex(const FlexSpec& spec, std::span<const FlexItem> items,
    Size available, Measure&& measure) {
    auto invoke = [&measure](ItemId id, const FlexMeasureInput& input) -> FlexMeasureResult {
        return std::invoke(measure, id, input);
    };
    return compute_flex(spec, items, available,
        +[](ItemId id, const FlexMeasureInput& input, void* context) -> FlexMeasureResult {
            return (*static_cast<decltype(invoke)*>(context))(id, input);
        }, std::addressof(invoke));
}
} // namespace axisly
