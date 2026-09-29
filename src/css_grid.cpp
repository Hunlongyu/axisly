#include <axisly/css_grid.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace axisly::css {
namespace {
Unit finite(Unit value) {
    if (!std::isfinite(value)) throw std::invalid_argument("Grid number must be finite");
    return value;
}
void valid_name(const std::string& name) {
    if (name.empty() || name == ".") throw std::invalid_argument("Grid name is empty or reserved");
}
void valid_names(const std::vector<std::vector<std::string>>& lines) {
    for (const auto& names : lines) for (const auto& name : names) valid_name(name);
}
void valid_track(const TrackSizing& track) {
    if (track.minimum.kind() == TrackBreadthKind::Fraction
        || track.minimum.kind() == TrackBreadthKind::FitContentLength
        || track.minimum.kind() == TrackBreadthKind::FitContentPercent) {
        throw std::invalid_argument("Grid minimum track function cannot be fr or fit-content");
    }
}
bool fixed_component(const TrackSizing& track) {
    const auto fixed = [](TrackBreadthKind kind) {
        return kind == TrackBreadthKind::Length || kind == TrackBreadthKind::Percent
            || kind == TrackBreadthKind::Calc;
    };
    return fixed(track.minimum.kind()) || fixed(track.maximum.kind());
}
} // namespace

TrackBreadth TrackBreadth::length(Unit value) {
    TrackBreadth result(TrackBreadthKind::Length);
    result.value_ = Length::length(finite(value));
    return result;
}
TrackBreadth TrackBreadth::percent(Unit fraction) {
    TrackBreadth result(TrackBreadthKind::Percent);
    result.value_ = Length::percent(finite(fraction));
    return result;
}
TrackBreadth TrackBreadth::automatic() noexcept { return TrackBreadth(TrackBreadthKind::Auto); }
TrackBreadth TrackBreadth::min_content() noexcept { return TrackBreadth(TrackBreadthKind::MinContent); }
TrackBreadth TrackBreadth::max_content() noexcept { return TrackBreadth(TrackBreadthKind::MaxContent); }
TrackBreadth TrackBreadth::fit_content_length(Unit limit) {
    TrackBreadth result(TrackBreadthKind::FitContentLength);
    result.value_ = Length::fit_content_length(finite(limit));
    return result;
}
TrackBreadth TrackBreadth::fit_content_percent(Unit fraction) {
    TrackBreadth result(TrackBreadthKind::FitContentPercent);
    result.value_ = Length::fit_content_percent(finite(fraction));
    return result;
}
TrackBreadth TrackBreadth::fraction(Unit factor) {
    if (finite(factor) < 0) throw std::invalid_argument("Grid fr factor must be nonnegative");
    TrackBreadth result(TrackBreadthKind::Fraction);
    result.factor_ = factor;
    return result;
}
TrackBreadth TrackBreadth::calc(Calculation expression) {
    TrackBreadth result(TrackBreadthKind::Calc);
    result.value_ = Length::calc(std::move(expression));
    return result;
}
std::optional<Unit> TrackBreadth::resolve_numeric(std::optional<Unit> basis) const {
    return value_.resolve_numeric(basis);
}
std::optional<Unit> TrackBreadth::resolve_fit_content_limit(std::optional<Unit> basis) const {
    return value_.resolve_fit_content_limit(basis);
}
bool TrackBreadth::operator==(const TrackBreadth& other) const noexcept {
    return kind_ == other.kind_ && value_ == other.value_ && factor_ == other.factor_;
}

TrackSizing TrackSizing::fixed(Unit value) {
    const auto breadth = TrackBreadth::length(value);
    return {breadth, breadth};
}
TrackSizing TrackSizing::percent(Unit fraction) {
    const auto breadth = TrackBreadth::percent(fraction);
    return {breadth, breadth};
}
TrackSizing TrackSizing::automatic() noexcept { return {}; }
TrackSizing TrackSizing::min_content() noexcept {
    const auto breadth = TrackBreadth::min_content();
    return {breadth, breadth};
}
TrackSizing TrackSizing::max_content() noexcept {
    const auto breadth = TrackBreadth::max_content();
    return {breadth, breadth};
}
TrackSizing TrackSizing::fraction(Unit factor) {
    return {TrackBreadth::automatic(), TrackBreadth::fraction(factor)};
}
TrackSizing TrackSizing::fit_content_length(Unit limit) {
    return {TrackBreadth::automatic(), TrackBreadth::fit_content_length(limit)};
}
TrackSizing TrackSizing::fit_content_percent(Unit fraction) {
    return {TrackBreadth::automatic(), TrackBreadth::fit_content_percent(fraction)};
}
TrackSizing TrackSizing::minmax(TrackBreadth minimum, TrackBreadth maximum) {
    TrackSizing result{std::move(minimum), std::move(maximum)};
    valid_track(result);
    return result;
}

GridLine GridLine::automatic() noexcept { return GridLine(GridLineKind::Auto); }
GridLine GridLine::index(std::int16_t value) {
    if (!value) throw std::invalid_argument("Grid line zero does not exist");
    GridLine result(GridLineKind::Index);
    result.index_ = value;
    return result;
}
GridLine GridLine::named(std::string name, std::int16_t occurrence) {
    valid_name(name);
    if (!occurrence) throw std::invalid_argument("Named grid line occurrence cannot be zero");
    GridLine result(GridLineKind::NamedIndex);
    result.name_ = std::move(name);
    result.index_ = occurrence;
    return result;
}
GridLine GridLine::span(std::uint16_t count) {
    if (!count) throw std::invalid_argument("Grid span must be positive");
    GridLine result(GridLineKind::Span);
    result.span_ = count;
    return result;
}
GridLine GridLine::named_span(std::string name, std::uint16_t count) {
    valid_name(name);
    if (!count) throw std::invalid_argument("Named grid span must be positive");
    GridLine result(GridLineKind::NamedSpan);
    result.name_ = std::move(name);
    result.span_ = count;
    return result;
}

void validate_grid_style(const GridStyle& style) {
    const auto validate_template = [](const std::vector<TemplateTrack>& tracks,
                                      const std::vector<std::vector<std::string>>& names) {
        if (!names.empty() && names.size() != tracks.size() + 1) {
            throw std::invalid_argument("Grid line names must have one set per template boundary");
        }
        valid_names(names);
        std::size_t auto_repeats = 0;
        bool all_fixed = true;
        for (const auto& component : tracks) {
            if (const auto* single = std::get_if<TrackSizing>(&component)) {
                valid_track(*single);
                all_fixed &= fixed_component(*single);
                continue;
            }
            const auto& repeat = std::get<GridRepeat>(component);
            if (repeat.kind != RepeatKind::Count && repeat.kind != RepeatKind::AutoFill
                && repeat.kind != RepeatKind::AutoFit) {
                throw std::invalid_argument("Invalid grid repetition kind");
            }
            if (repeat.tracks.empty() || (repeat.kind == RepeatKind::Count && repeat.count == 0)) {
                throw std::invalid_argument("Grid repetition needs tracks and a positive count");
            }
            if (repeat.kind != RepeatKind::Count) ++auto_repeats;
            if (!repeat.line_names.empty()
                && repeat.line_names.size() != repeat.tracks.size() + 1) {
                throw std::invalid_argument("Repeated grid line names have the wrong size");
            }
            valid_names(repeat.line_names);
            for (const auto& track : repeat.tracks) {
                valid_track(track);
                all_fixed &= fixed_component(track);
            }
        }
        if (auto_repeats > 1 || (auto_repeats && !all_fixed)) {
            throw std::invalid_argument(
                "Grid auto-repeat requires one repetition and fixed track components");
        }
    };
    validate_template(style.template_rows, style.row_line_names);
    validate_template(style.template_columns, style.column_line_names);
    for (const auto& track : style.auto_rows) valid_track(track);
    for (const auto& track : style.auto_columns) valid_track(track);
    if (style.auto_flow != GridAutoFlow::Row && style.auto_flow != GridAutoFlow::Column
        && style.auto_flow != GridAutoFlow::RowDense
        && style.auto_flow != GridAutoFlow::ColumnDense) {
        throw std::invalid_argument("Invalid grid auto-flow");
    }
    if (style.template_areas) {
        const auto& rows = *style.template_areas;
        if (rows.empty() || rows.front().empty()) {
            throw std::invalid_argument("Grid template areas require nonempty rows and columns");
        }
        std::unordered_map<std::string, std::array<std::size_t, 4>> bounds;
        for (std::size_t row = 0; row < rows.size(); ++row) {
            if (rows[row].size() != rows.front().size()) {
                throw std::invalid_argument("Grid template areas must be rectangular");
            }
            for (std::size_t column = 0; column < rows[row].size(); ++column) {
                const auto& name = rows[row][column];
                if (name == ".") continue;
                valid_name(name);
                auto [it, inserted] = bounds.try_emplace(name,
                    std::array<std::size_t, 4>{row, row + 1, column, column + 1});
                if (!inserted) {
                    auto& box = it->second;
                    box[0] = std::min(box[0], row);
                    box[1] = std::max(box[1], row + 1);
                    box[2] = std::min(box[2], column);
                    box[3] = std::max(box[3], column + 1);
                }
            }
        }
        for (const auto& [name, box] : bounds) {
            for (auto row = box[0]; row < box[1]; ++row) {
                for (auto column = box[2]; column < box[3]; ++column) {
                    if (rows[row][column] != name) {
                        throw std::invalid_argument("Named grid area must form a rectangle");
                    }
                }
            }
        }
    }
}

} // namespace axisly::css
