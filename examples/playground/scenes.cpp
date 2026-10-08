#include "scenes.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace axisly::playground {
namespace {
namespace css = axisly::css;
struct Node {
    css::Style style;
    Size preferred{96, 36};
    std::string label;
    std::size_t parent{};
    bool container{};
};
struct Definition {
    std::vector<Node> nodes;
    std::vector<std::string> columns, rows;
    bool wrap{};
};
css::Style grid() {
    css::Style s; s.display = css::Display::Grid;
    s.box.padding = {css::Length::length(16), css::Length::length(16), css::Length::length(16), css::Length::length(16)};
    s.gap = {css::Length::length(12), css::Length::length(12)};
    return s;
}
std::size_t add(Definition& d, int col, int row, std::string label, int cs = 1, int rs = 1, std::size_t parent = 0) {
    css::Style s;
    s.grid.column = {css::GridLine::index(static_cast<std::int16_t>(col + 1)), css::GridLine::span(static_cast<std::uint16_t>(cs))};
    s.grid.row = {css::GridLine::index(static_cast<std::int16_t>(row + 1)), css::GridLine::span(static_cast<std::uint16_t>(rs))};
    s.box.margin = {css::Length::length(6), css::Length::length(6), css::Length::length(6), css::Length::length(6)};
    s.box.min_size = {css::Length::length(0), css::Length::length(0)};
    d.nodes.push_back({s, {96, 36}, std::move(label), parent});
    return d.nodes.size() - 1;
}
Definition define(std::size_t index) {
    using T = css::TrackSizing; using B = css::TrackBreadth;
    Definition d;
    auto s = grid(); s.grid.template_rows = {T::fixed(92), T::fraction(1)};
    d.rows = {"92px", "1fr"}; d.nodes.push_back({s, {}, "Root", 0, true});
    auto& root = d.nodes[0].style.grid; // Used only before children grow nodes.
    switch (index) {
    case 0:
        root.template_columns = {T::fixed(140), T::fixed(220), T::fixed(100)};
        root.template_rows = {T::fixed(80), T::fixed(120)};
        d.columns = {"140px", "220px", "100px"}; d.rows = {"80px", "120px"}; break;
    case 1:
        root.template_columns = {T::fraction(1), T::fraction(1)}; d.columns = {"1fr", "1fr"}; break;
    case 2:
        root.template_columns = {T::fraction(1), T::fraction(2), T::fraction(3)}; d.columns = {"1fr", "2fr", "3fr"}; break;
    case 3:
        root.template_columns = {T::fixed(180), T::fraction(1), T::fraction(2)}; d.columns = {"180px", "1fr", "2fr"}; break;
    case 4:
        root.template_columns = {T::automatic(), T::fraction(1)}; root.template_rows.assign(3, T::automatic());
        d.columns = {"auto", "1fr"}; d.rows.assign(3, "auto"); d.wrap = true;
        for (int row = 0; row < 3; ++row) {
            add(d, 0, row, std::array{"Name", "Server URL", "Very long label"}[row]);
            d.nodes.back().preferred = {std::array{64.F, 96.F, 160.F}[row], 32};
            add(d, 1, row, std::array{"axisly", "https://example.test", "Wraps at the final item width"}[row]);
            d.nodes.back().preferred = {std::array{140.F, 300.F, 620.F}[row], 32};
        } return d;
    case 5:
        root.template_columns.assign(3, T::fraction(1)); root.template_rows = {T::fixed(76), T::fraction(1), T::fraction(1)};
        d.columns.assign(3, "1fr"); d.rows = {"76px", "1fr", "1fr"}; add(d, 0, 0, "Header / span 3 columns", 3);
        for (int row = 1; row < 3; ++row) for (int col = 0; col < 3; ++col) add(d, col, row, "Card");
        return d;
    case 6:
        root.template_columns = {T::fixed(180), T::fraction(1), T::fraction(1)}; root.template_rows.assign(2, T::fraction(1));
        d.columns = {"180px", "1fr", "1fr"}; d.rows.assign(2, "1fr"); add(d, 0, 0, "Sidebar / span 2 rows", 1, 2);
        for (int row = 0; row < 2; ++row) for (int col = 1; col < 3; ++col) add(d, col, row, "Panel");
        return d;
    case 7: {
        root.template_columns.assign(4, T::fraction(1)); root.template_rows.assign(4, T::fraction(1));
        d.columns.assign(4, "1fr"); d.rows.assign(4, "1fr");
        const std::array positions{css::ItemPosition::Start, css::ItemPosition::Center, css::ItemPosition::End, css::ItemPosition::Stretch};
        const std::array names{"S", "C", "E", "F"};
        for (int row = 0; row < 4; ++row) for (int col = 0; col < 4; ++col) {
            add(d, col, row, std::string(names[col]) + "/" + names[row]);
            auto& item = d.nodes.back(); item.preferred = {72, 32};
            item.style.justify_self = css::ItemAlignment{positions[col]}; item.style.align_self = css::ItemAlignment{positions[row]};
        } return d;
    }
    case 8:
        root.template_columns = {T::minmax(B::length(120), B::length(220)), T::minmax(B::length(160), B::fraction(1)), T::fraction(1)};
        d.columns = {"minmax(120px,220px)", "minmax(160px,1fr)", "1fr"}; break;
    case 9: {
        root.template_columns = {T::fixed(160), T::fraction(1)}; root.template_rows = {T::fraction(1)};
        d.columns = {"160px", "1fr"}; d.rows = {"1fr"}; add(d, 0, 0, "Sidebar");
        const auto parent = add(d, 1, 0, "Nested CSS Grid");
        auto child = grid(); child.grid.column.start = css::GridLine::index(2);
        child.grid.template_columns = {T::fraction(1), T::fraction(2)}; child.grid.template_rows = {T::fixed(60), T::fraction(1)};
        child.box.padding.top = css::Length::length(38); d.nodes[parent].style = child; d.nodes[parent].container = true;
        add(d, 0, 0, "Child A", 1, 1, parent); add(d, 1, 0, "Child B", 1, 1, parent); add(d, 0, 1, "Child / span 2", 2, 1, parent);
        return d;
    }
    default: throw std::out_of_range("Unknown scene");
    }
    const auto rows = d.rows.size(), columns = d.columns.size();
    for (std::size_t row = 0; row < rows; ++row) for (std::size_t col = 0; col < columns; ++col)
        add(d, static_cast<int>(col), static_cast<int>(row), std::string(1, static_cast<char>('A' + d.nodes.size() - 1)));
    return d;
}
} // namespace

Frame arrange_scene(std::size_t index, Size available) {
    const auto d = define(index);
    css::LayoutTree tree; std::vector<css::NodeId> nodes;
    for (std::size_t i = 0; i < d.nodes.size(); ++i) {
        auto style = d.nodes[i].style;
        if (!i) style.box.size = {css::Length::length(available.width),
            d.wrap ? css::Length::automatic() : css::Length::length(available.height)};
        const auto measure = [&, i](const css::MeasureRequest& request) {
            const auto preferred = d.nodes[i].preferred;
            float width = request.known_content_width.value_or(request.available_width.kind == css::AvailableKind::MinContent ? (d.wrap ? 32.F : 0.F) : preferred.width);
            if (!request.known_content_width && request.available_width.definite_value()) width = std::min(width, request.available_width.value);
            float height = preferred.height;
            if (d.wrap) height *= std::ceil(preferred.width / std::max(1.F, width));
            return css::Measurement{{width, request.known_content_height.value_or(height)}};
        };
        nodes.push_back(d.nodes[i].container ? tree.create_node(style) : tree.create_node(style, measure));
        if (i) tree.append_child(nodes[d.nodes[i].parent], nodes.back());
    }
    const auto& result = tree.compute(nodes[0], {css::AvailableSpace::definite(available.width),
        d.wrap ? css::AvailableSpace::max_content() : css::AvailableSpace::definite(available.height)});
    Frame frame; frame.available = available; frame.occupied = result.size;
    frame.column_rules = d.columns; frame.row_rules = d.rows; frame.measurements = result.measurement_count; frame.layouts = result.layout_count;
    if (!result.succeeded()) { frame.error = result.error.message; return frame; }
    const auto overflow = result.nodes.front().scrollable_overflow_rect;
    frame.occupied = {std::max(result.size.width, overflow.x + overflow.width), std::max(result.size.height, overflow.y + overflow.height)};
    for (const auto& report : result.grid_tracks) if (report.node == nodes[0]) { frame.columns = report.columns; frame.rows = report.rows; }
    for (std::size_t i = 1; i < nodes.size(); ++i) {
        const auto& spec = d.nodes[i]; const auto* geometry = result.find(nodes[i]); const auto* parent = result.find(nodes[spec.parent]);
        if (!geometry || !parent) throw std::runtime_error("Missing scene geometry");
        css::GridItemPlacement placement; Rect cell;
        for (const auto& report : result.grid_tracks) if (report.node == nodes[spec.parent]) {
            for (const auto& item : report.items) if (item.node == nodes[i]) {
                placement = item;
                const auto c0 = static_cast<std::size_t>(item.column_start - report.column_origin), c1 = static_cast<std::size_t>(item.column_end - report.column_origin - 1);
                const auto r0 = static_cast<std::size_t>(item.row_start - report.row_origin), r1 = static_cast<std::size_t>(item.row_end - report.row_origin - 1);
                const auto x = report.column_positions.at(c0).start, y = report.row_positions.at(r0).start;
                cell = {parent->border_box.x + x, parent->border_box.y + y, report.column_positions.at(c1).end - x, report.row_positions.at(r1).end - y};
            }
        }
        frame.items.push_back({"/" + std::to_string(i), spec.label, placement, geometry->border_box, cell, spec.preferred, spec.parent ? std::size_t{1} : std::size_t{0}, !spec.container});
    }
    return frame;
}
} // namespace axisly::playground
