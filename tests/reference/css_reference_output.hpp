#pragma once

#include <axisly/css_tree.hpp>

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <ostream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace axisly::css::reference {

using IdentifiedNode = std::pair<int, NodeId>;

inline void write_json_string(std::ostream& out, std::string_view value) {
    constexpr char hex[] = "0123456789abcdef";
    out << '"';
    for (const unsigned char byte : value) {
        if (byte == '"' || byte == '\\') out << '\\' << static_cast<char>(byte);
        else if (byte < 0x20) out << "\\u00" << hex[byte >> 4] << hex[byte & 15];
        else out << static_cast<char>(byte);
    }
    out << '"';
}

inline void write_case(std::ostream& out, std::string_view name,
                       const LayoutResult& result,
                       const std::vector<IdentifiedNode>& nodes,
                       bool include_grid_details = false) {
    if (!result.succeeded()) throw std::runtime_error(result.error.message);
    out << std::setprecision(9) << "{\"name\":\"" << name << "\",\"nodes\":[";
    bool first = true;
    for (const auto& [id, handle] : nodes) {
        const auto* layout = result.find(handle);
        if (!layout) throw std::runtime_error("CSS node missing from result");
        if (!first) out << ',';
        first = false;
        const auto& rect = layout->border_box;
        const auto& margin = layout->margin;
        const auto& border = layout->border;
        const auto& padding = layout->padding;
        const auto& overflow = layout->scrollable_overflow_rect;
        out << "{\"id\":" << id << ",\"rect\":[" << rect.x << ',' << rect.y
            << ',' << rect.width << ',' << rect.height << "],\"margin\":["
            << margin.left << ',' << margin.top << ',' << margin.right << ','
            << margin.bottom << "],\"padding\":[" << padding.left << ','
            << padding.top << ',' << padding.right << ',' << padding.bottom
            << "],\"border\":[" << border.left << ',' << border.top << ','
            << border.right << ',' << border.bottom
            << "],\"scrollbar\":[" << layout->scrollbar.width << ','
            << layout->scrollbar.height << "],\"overflow\":["
            << overflow.x << ',' << overflow.y << ','
            << overflow.x + overflow.width << ','
            << overflow.y + overflow.height << ']';
        if (include_grid_details) {
            const auto grid = std::find_if(result.grid_tracks.begin(),
                result.grid_tracks.end(), [handle](const GridTracks& entry) {
                    return entry.node == handle;
                });
            out << ",\"grid\":";
            if (grid == result.grid_tracks.end()) {
                out << "null";
            } else {
                const auto write_axis = [&out](std::int32_t origin,
                    std::size_t explicit_count,
                    const std::vector<GridTrackPosition>& positions,
                    const std::vector<std::vector<std::string>>& line_names) {
                    if (origin > 0) {
                        throw std::runtime_error("Invalid CSS Grid track origin");
                    }
                    const auto negative = static_cast<std::size_t>(-
                        static_cast<std::int64_t>(origin));
                    if (positions.size() < negative + explicit_count) {
                        throw std::runtime_error("Invalid CSS Grid track range");
                    }
                    out << "{\"negative\":" << negative
                        << ",\"explicit\":" << explicit_count
                        << ",\"positive\":"
                        << positions.size() - negative - explicit_count
                        << ",\"positions\":[";
                    bool first_position = true;
                    for (const auto& position : positions) {
                        if (!first_position) out << ',';
                        first_position = false;
                        out << '[' << position.start << ',' << position.end << ']';
                    }
                    out << "],\"line_names\":[";
                    for (std::size_t line = 0; line < line_names.size(); ++line) {
                        if (line) out << ',';
                        out << '[';
                        for (std::size_t name = 0; name < line_names[line].size(); ++name) {
                            if (name) out << ',';
                            write_json_string(out, line_names[line][name]);
                        }
                        out << ']';
                    }
                    out << "]}";
                };
                out << "{\"rows\":";
                write_axis(grid->row_origin, grid->explicit_rows, grid->row_positions,
                    grid->row_line_names);
                out << ",\"columns\":";
                write_axis(grid->column_origin, grid->explicit_columns,
                    grid->column_positions, grid->column_line_names);
                out << ",\"items\":[";
                bool first_item = true;
                for (const auto& item : grid->items) {
                    if (!first_item) out << ',';
                    first_item = false;
                    out << '[' << item.row_start - grid->row_origin + 1 << ','
                        << item.row_end - grid->row_origin + 1 << ','
                        << item.column_start - grid->column_origin + 1 << ','
                        << item.column_end - grid->column_origin + 1 << ']';
                }
                out << "]}";
            }
        }
        out << '}';
    }
    out << "]}";
}

} // namespace axisly::css::reference
