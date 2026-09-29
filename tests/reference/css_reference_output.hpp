#pragma once

#include <axisly/css_tree.hpp>

#include <iomanip>
#include <ostream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace axisly::css::reference {

using IdentifiedNode = std::pair<int, NodeId>;

inline void write_case(std::ostream& out, std::string_view name,
                       const LayoutResult& result,
                       const std::vector<IdentifiedNode>& nodes) {
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
            << overflow.y + overflow.height << "]}";
    }
    out << "]}";
}

} // namespace axisly::css::reference
