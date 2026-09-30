#include "grid_internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace axisly::css::detail {
namespace {

Unit definite_track_size(const TrackSizing& track, Unit basis, NodeId id) {
    const auto minimum = track.minimum.resolve_numeric(basis);
    const auto maximum = track.maximum.resolve_numeric(basis);
    if (!minimum && !maximum) {
        throw LayoutFailure(LayoutErrorCode::InvalidStyle, id,
            "Grid auto-repeat track lacks a fixed sizing component");
    }
    return std::max(Unit{0}, std::max(minimum.value_or(0), maximum.value_or(0)));
}

std::uint16_t auto_repeat_count(const std::vector<TemplateTrack>& components,
                                std::optional<Unit> available, Unit gap,
                                bool minimum_strategy, NodeId id) {
    const GridRepeat* automatic = nullptr;
    std::uint32_t fixed_count = 0;
    std::uint32_t insertion = 0;
    for (const auto& component : components) {
        if (std::holds_alternative<TrackSizing>(component)) {
            fixed_count = std::min(fixed_count + 1,
                static_cast<std::uint32_t>(grid_track_limit));
            if (!automatic) insertion = fixed_count;
            continue;
        }
        const auto& repeat = std::get<GridRepeat>(component);
        if (repeat.kind == RepeatKind::Count) {
            const auto count = static_cast<std::uint32_t>(repeat.count)
                * static_cast<std::uint32_t>(std::min(repeat.tracks.size(),
                    static_cast<std::size_t>(grid_track_limit)));
            fixed_count = std::min(fixed_count + count,
                static_cast<std::uint32_t>(grid_track_limit));
            if (!automatic) insertion = fixed_count;
        } else automatic = &repeat;
    }
    if (!automatic) return 0;
    const auto per_repeat = static_cast<std::uint32_t>(std::min(
        automatic->tracks.size(), static_cast<std::size_t>(
            std::numeric_limits<std::uint16_t>::max())));
    if (insertion >= static_cast<std::uint32_t>(grid_track_limit)) return 0;
    const auto maximum_repeats = (static_cast<std::uint32_t>(grid_track_limit)
        - insertion + per_repeat - 1) / per_repeat;
    if (!available) return 1;
    Unit fixed_space = 0;
    Unit repeated_space = 0;
    for (const auto& component : components) {
        if (const auto* single = std::get_if<TrackSizing>(&component)) {
            fixed_space += definite_track_size(*single, *available, id);
        } else {
            const auto& repeat = std::get<GridRepeat>(component);
            Unit group = 0;
            for (const auto& track : repeat.tracks) {
                group += definite_track_size(track, *available, id);
            }
            if (repeat.kind == RepeatKind::Count) fixed_space += repeat.count * group;
            else repeated_space = group;
        }
    }
    const Unit first = fixed_space + repeated_space
        + static_cast<Unit>(std::max(0u, fixed_count + per_repeat - 1)) * gap;
    if (first > *available) return 1;
    const Unit step = repeated_space + static_cast<Unit>(per_repeat) * gap;
    if (step <= 0) return static_cast<std::uint16_t>(maximum_repeats);
    const Unit extra = (*available - first) / step;
    const Unit rounded = minimum_strategy ? std::ceil(extra) : std::floor(extra);
    return static_cast<std::uint16_t>(std::clamp(rounded + Unit{1}, Unit{1},
        static_cast<Unit>(maximum_repeats)));
}

} // namespace

ExpandedTemplate expand_grid_template(const std::vector<TemplateTrack>& template_tracks,
    const std::vector<std::vector<std::string>>& outer_names,
    std::optional<Unit> available, Unit gap, bool minimum_strategy, NodeId id) {
    ExpandedTemplate result;
    result.line_names.emplace_back();
    const auto automatic_count = auto_repeat_count(template_tracks, available,
        gap, minimum_strategy, id);
    const auto limit = static_cast<std::size_t>(grid_track_limit);
    std::size_t component_index = 0;
    for (; component_index < template_tracks.size()
        && result.tracks.size() < limit; ++component_index) {
        const auto& component = template_tracks[component_index];
        if (!outer_names.empty()) {
            auto& boundary = result.line_names.back();
            boundary.insert(boundary.end(), outer_names[component_index].begin(),
                outer_names[component_index].end());
        }
        if (const auto* single = std::get_if<TrackSizing>(&component)) {
            result.tracks.push_back(*single);
            result.auto_fit.push_back(false);
            result.line_names.emplace_back();
        } else {
            const auto& repetition = std::get<GridRepeat>(component);
            const auto count = repetition.kind == RepeatKind::Count
                ? repetition.count : automatic_count;
            for (std::uint16_t repeat = 0; repeat < count
                && result.tracks.size() < limit; ++repeat) {
                if (!repetition.line_names.empty()) {
                    auto& boundary = result.line_names.back();
                    boundary.insert(boundary.end(), repetition.line_names.front().begin(),
                        repetition.line_names.front().end());
                }
                for (std::size_t i = 0; i < repetition.tracks.size()
                    && result.tracks.size() < limit; ++i) {
                    result.tracks.push_back(repetition.tracks[i]);
                    result.auto_fit.push_back(repetition.kind == RepeatKind::AutoFit);
                    result.line_names.push_back(repetition.line_names.empty()
                        ? std::vector<std::string>{} : repetition.line_names[i + 1]);
                }
            }
        }
    }
    if (!outer_names.empty() && component_index == template_tracks.size()) {
        auto& boundary = result.line_names.back();
        boundary.insert(boundary.end(), outer_names.back().begin(), outer_names.back().end());
    }
    return result;
}

void apply_grid_template_areas(const GridStyle& style, ExpandedTemplate& rows, ExpandedTemplate& columns) {
    if (!style.template_areas) return;
    const auto& cells = *style.template_areas;
    const auto extend = [](ExpandedTemplate& axis, std::size_t minimum) {
        minimum = std::min(minimum, static_cast<std::size_t>(grid_track_limit));
        while (axis.tracks.size() < minimum) {
            axis.tracks.push_back(TrackSizing::automatic());
            axis.auto_fit.push_back(false);
            axis.line_names.emplace_back();
        }
    };
    extend(rows, cells.size());
    extend(columns, cells.front().size());
    std::unordered_map<std::string, std::size_t> area_indices;
    std::vector<std::pair<std::string, std::array<std::size_t, 4>>> bounds;
    for (std::size_t row = 0; row < cells.size(); ++row) {
        for (std::size_t column = 0; column < cells[row].size(); ++column) {
            const auto& name = cells[row][column];
            if (name == ".") continue;
            const auto [it, inserted] = area_indices.try_emplace(name, bounds.size());
            if (inserted) {
                bounds.emplace_back(name, std::array<std::size_t, 4>{row, row + 1, column, column + 1});
            } else {
                auto& box = bounds[it->second].second;
                box[0] = std::min(box[0], row);
                box[1] = std::max(box[1], row + 1);
                box[2] = std::min(box[2], column);
                box[3] = std::max(box[3], column + 1);
            }
        }
    }
    for (const auto& [name, box] : bounds) {
        rows.line_names[std::min(box[0], rows.tracks.size())].push_back(name + "-start");
        rows.line_names[std::min(box[1], rows.tracks.size())].push_back(name + "-end");
        columns.line_names[std::min(box[2], columns.tracks.size())].push_back(name + "-start");
        columns.line_names[std::min(box[3], columns.tracks.size())].push_back(name + "-end");
    }
}

std::vector<std::vector<std::string>> reported_grid_line_names(
    const ExpandedTemplate& definition, const TrackAxis& axis) {
    if (std::none_of(definition.line_names.begin(), definition.line_names.end(),
        [](const auto& names) { return !names.empty(); })) return {};
    std::vector<std::vector<std::string>> result(axis.tracks.size() + 1);
    const auto leading = static_cast<std::size_t>(-axis.origin);
    for (std::size_t line = 0; line < definition.line_names.size(); ++line) {
        auto& names = result.at(leading + line);
        for (const auto& name : definition.line_names[line]) {
            if (std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
        }
    }
    return result;
}

} // namespace axisly::css::detail
