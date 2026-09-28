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

#include <axisly/measurement.hpp>

namespace axisly {

// Anchor percentages are fractions of the container's padded content box.
struct AnchorLength {
    enum class Kind : std::uint8_t { Fixed, Percent };
    Kind kind{Kind::Fixed};
    Unit value{};

    [[nodiscard]] static constexpr AnchorLength fixed(Unit value) noexcept { return {Kind::Fixed, value}; }
    [[nodiscard]] static constexpr AnchorLength percent(Unit fraction) noexcept { return {Kind::Percent, fraction}; }
    bool operator==(const AnchorLength&) const = default;
};

struct AnchorEdges {
    std::optional<AnchorLength> left, top, right, bottom;
    bool operator==(const AnchorEdges&) const = default;
};

// Auto resolves to Start, End, or Stretch according to the opposing anchors.
enum class OverlayAlign : std::uint8_t { Auto, Start, Center, End, Stretch };

struct OverlaySpec {
    Insets padding{};
    Insets border{}; // Geometry only; painting remains the host's responsibility.
    bool operator==(const OverlaySpec&) const = default;
};

struct OverlayItem {
    ItemId id{};
    AnchorEdges anchors{};
    Insets margin{};
    OverlayAlign horizontal_alignment{OverlayAlign::Auto};
    OverlayAlign vertical_alignment{OverlayAlign::Auto};
    Size minimum_size{};
    Size preferred_size{};
    std::optional<Unit> maximum_width, maximum_height;
    std::optional<Unit> width, height;
    bool operator==(const OverlayItem&) const = default;
};

enum class OverlayErrorCode : std::uint8_t {
    None, InvalidContainerSize, InvalidPadding, InvalidBorder, InvalidAnchor,
    InvalidMargin, InvalidSize, InvalidAlignment, DuplicateItemId,
    InvalidMeasurement, NumericOverflow
};

struct OverlayError {
    OverlayErrorCode code{OverlayErrorCode::None};
    std::string message;
    std::optional<ItemId> item_id;
};

struct OverlayItemLayout {
    ItemId id{};
    Rect rect{};        // Item's border box, relative to the container border box.
    Rect anchor_rect{}; // Space between resolved opposing anchors, before margin.
};

struct OverlayResult {
    Size size{}; // The requested container border-box size.
    Rect occupied_bounds{}; // Union of container and item rectangles; can overflow.
    std::vector<OverlayItemLayout> items;
    OverlayError error{};
    std::size_t measurement_count{};
    [[nodiscard]] bool succeeded() const noexcept { return error.code == OverlayErrorCode::None; }
};

// Overlay is a native UI algorithm, independent of CSS positioned layout.
// Both opposing anchors fill the axis unless a fixed item size is supplied.
// Min/max constraints can cause overflow; the start anchor wins in that case.
// Input order is retained, so the host can use it as its paint order.
// On error, no partial geometry is returned. Measurement callbacks are borrowed
// only during this synchronous call; callback exceptions propagate.
[[nodiscard]] OverlayResult compute_overlay(
    const OverlaySpec& spec, std::span<const OverlayItem> items, Size available);

[[nodiscard]] OverlayResult compute_overlay(
    const OverlaySpec& spec, std::span<const OverlayItem> items, Size available,
    MeasureCallback measure, void* user_data = nullptr);

template<class Measure>
    requires std::is_invocable_r_v<MeasureResult, Measure&, ItemId, const MeasureInput&>
[[nodiscard]] OverlayResult compute_overlay(
    const OverlaySpec& spec, std::span<const OverlayItem> items, Size available,
    Measure&& measure) {
    auto invoke = [&measure](ItemId id, const MeasureInput& input) -> MeasureResult {
        return std::invoke(measure, id, input);
    };
    return compute_overlay(spec, items, available,
        +[](ItemId id, const MeasureInput& input, void* context) -> MeasureResult {
            return (*static_cast<decltype(invoke)*>(context))(id, input);
        }, std::addressof(invoke));
}

} // namespace axisly
