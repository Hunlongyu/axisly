#pragma once

#include <axisly/css_values.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace axisly::css {

enum class TrackBreadthKind : std::uint8_t {
    Length, Percent, Auto, MinContent, MaxContent,
    FitContentLength, FitContentPercent, Fraction, Calc,
};

class TrackBreadth {
public:
    [[nodiscard]] static TrackBreadth length(Unit value);
    [[nodiscard]] static TrackBreadth percent(Unit fraction);
    [[nodiscard]] static TrackBreadth automatic() noexcept;
    [[nodiscard]] static TrackBreadth min_content() noexcept;
    [[nodiscard]] static TrackBreadth max_content() noexcept;
    [[nodiscard]] static TrackBreadth fit_content_length(Unit limit);
    [[nodiscard]] static TrackBreadth fit_content_percent(Unit fraction);
    [[nodiscard]] static TrackBreadth fraction(Unit factor);
    [[nodiscard]] static TrackBreadth calc(Calculation expression);

    [[nodiscard]] TrackBreadthKind kind() const noexcept { return kind_; }
    [[nodiscard]] Unit fraction_factor() const noexcept { return factor_; }
    [[nodiscard]] std::optional<Unit> resolve_numeric(std::optional<Unit> basis) const;
    [[nodiscard]] std::optional<Unit> resolve_fit_content_limit(std::optional<Unit> basis) const;
    [[nodiscard]] bool operator==(const TrackBreadth& other) const noexcept;

private:
    TrackBreadthKind kind_{TrackBreadthKind::Auto};
    Length value_{Length::automatic()};
    Unit factor_{};
    explicit TrackBreadth(TrackBreadthKind kind) noexcept : kind_(kind) {}
};

struct TrackSizing {
    TrackBreadth minimum{TrackBreadth::automatic()};
    TrackBreadth maximum{TrackBreadth::automatic()};
    [[nodiscard]] static TrackSizing fixed(Unit value);
    [[nodiscard]] static TrackSizing percent(Unit fraction);
    [[nodiscard]] static TrackSizing automatic() noexcept;
    [[nodiscard]] static TrackSizing min_content() noexcept;
    [[nodiscard]] static TrackSizing max_content() noexcept;
    [[nodiscard]] static TrackSizing fraction(Unit factor);
    [[nodiscard]] static TrackSizing fit_content_length(Unit limit);
    [[nodiscard]] static TrackSizing fit_content_percent(Unit fraction);
    [[nodiscard]] static TrackSizing minmax(TrackBreadth minimum, TrackBreadth maximum);
    bool operator==(const TrackSizing&) const = default;
};

enum class RepeatKind : std::uint8_t { Count, AutoFill, AutoFit };
struct GridRepeat {
    RepeatKind kind{RepeatKind::Count};
    std::uint16_t count{1};
    std::vector<TrackSizing> tracks;
    // Empty, or exactly tracks.size()+1 sets of names.
    std::vector<std::vector<std::string>> line_names;
    bool operator==(const GridRepeat&) const = default;
};
using TemplateTrack = std::variant<TrackSizing, GridRepeat>;

enum class GridLineKind : std::uint8_t { Auto, Index, NamedIndex, Span, NamedSpan };
class GridLine {
public:
    [[nodiscard]] static GridLine automatic() noexcept;
    [[nodiscard]] static GridLine index(std::int16_t value);
    [[nodiscard]] static GridLine named(std::string name, std::int16_t occurrence = 1);
    [[nodiscard]] static GridLine span(std::uint16_t count);
    [[nodiscard]] static GridLine named_span(std::string name, std::uint16_t count = 1);
    [[nodiscard]] GridLineKind kind() const noexcept { return kind_; }
    [[nodiscard]] std::int16_t index_value() const noexcept { return index_; }
    [[nodiscard]] std::uint16_t span_count() const noexcept { return span_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    bool operator==(const GridLine&) const = default;
private:
    GridLineKind kind_{GridLineKind::Auto};
    std::int16_t index_{};
    std::uint16_t span_{};
    std::string name_;
    explicit GridLine(GridLineKind kind) noexcept : kind_(kind) {}
};

struct GridPlacement {
    GridLine start{GridLine::automatic()};
    GridLine end{GridLine::automatic()};
    bool operator==(const GridPlacement&) const = default;
};

enum class GridAutoFlow : std::uint8_t { Row, Column, RowDense, ColumnDense };

struct GridStyle {
    std::vector<TemplateTrack> template_rows, template_columns;
    std::vector<TrackSizing> auto_rows, auto_columns;
    GridAutoFlow auto_flow{GridAutoFlow::Row};
    // Positional line-name sets for non-repeated template components.
    std::vector<std::vector<std::string>> row_line_names, column_line_names;
    // Each row names areas by column; "." means an unnamed cell.
    std::optional<std::vector<std::vector<std::string>>> template_areas;
    GridPlacement row, column;
    bool operator==(const GridStyle&) const = default;
};

void validate_grid_style(const GridStyle& style);

} // namespace axisly::css
