// Development-only whitespace protocol. No JSON/parser dependency in axisly.
#include <axisly/axisly.hpp>

#include <charconv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <map>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace {
using namespace axisly;

std::string token() {
    std::string value;
    if (!(std::cin >> value)) throw std::runtime_error("Truncated input");
    return value;
}

template<class T> T number() {
    const auto text = token();
    T value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) throw std::runtime_error("Invalid numeric token");
    if constexpr (std::is_floating_point_v<T>) {
        if (!std::isfinite(value) || value < 0 || value > 1'000'000) throw std::runtime_error("Numeric input outside reference limits");
    }
    return value;
}

std::uint32_t count(std::uint32_t maximum, bool allow_zero = true) {
    const auto value = number<std::uint32_t>();
    if (value > maximum || (!allow_zero && value == 0)) throw std::runtime_error("Count outside reference limits");
    return value;
}

Size size() { return {number<Unit>(), number<Unit>()}; }
Insets insets() { return {number<Unit>(), number<Unit>(), number<Unit>(), number<Unit>()}; }

std::vector<Track> tracks(std::uint32_t length) {
    std::vector<Track> result;
    for (std::uint32_t index = 0; index < length; ++index) {
        const auto kind = token();
        if (kind == "F") result.push_back(Track::fixed(number<Unit>()));
        else if (kind == "P") result.push_back(Track::percentage(number<Unit>()));
        else if (kind == "C") result.push_back(Track::content());
        else if (kind == "W" || kind == "R") {
            const auto weight = number<Unit>();
            const auto minimum = number<Unit>();
            if (weight < 1) throw std::runtime_error("Reference weights must be at least one");
            result.push_back(kind == "W" ? Track::weighted(weight, minimum)
                : Track::range(Track::fixed(minimum), Track::weighted(weight)));
        } else throw std::runtime_error("Unknown track token");
    }
    return result;
}

void rect(Rect value) {
    std::cout << '[' << value.x << ',' << value.y << ',' << value.width << ',' << value.height << ']';
}

void run_case(std::uint32_t index, bool advanced) {
    const auto available = size();
    GridSpec spec;
    spec.padding = insets();
    spec.column_gap = number<Unit>();
    spec.row_gap = number<Unit>();
    const auto columns = count(16, false);
    const auto rows = count(16, false);
    const auto item_count = count(128);
    spec.columns = tracks(columns);
    spec.rows = tracks(rows);
    if (advanced) {
        spec.auto_flow = static_cast<AutoFlow>(count(4));
        spec.allow_implicit_tracks = count(1) != 0;
        spec.implicit_column = Track::fixed(number<Unit>());
        spec.implicit_row = Track::fixed(number<Unit>());
        spec.max_implicit_tracks = 4096;
    }
    std::vector<GridItem> items;
    std::map<ItemId, MeasureResult> metrics;
    for (std::uint32_t n = 0; n < item_count; ++n) {
        GridItem item;
        item.id = number<ItemId>();
        const auto position = [advanced]() {
            const auto value = number<std::uint32_t>();
            if (value > 15 && !(advanced && value == auto_position)) throw std::runtime_error("Invalid position");
            return value;
        };
        item.column = position();
        item.row = position();
        item.column_span = count(16, false);
        item.row_span = count(16, false);
        item.margin = insets();
        item.horizontal_alignment = static_cast<HorizontalAlignment>(count(3));
        item.vertical_alignment = static_cast<VerticalAlignment>(count(advanced ? 4 : 3));
        metrics[item.id] = {size(), size()};
        if (advanced && count(1) != 0) metrics[item.id].baseline = number<Unit>();
        items.push_back(item);
    }
    const auto result = compute_grid(spec, items, available,
        [&](ItemId id, const MeasureInput&) { return metrics.at(id); });
    if (!result.succeeded()) throw std::runtime_error(result.error.message);
    std::cout << "{\"case\":" << index << ",\"size\":[" << result.size.width << ',' << result.size.height << "],\"items\":[";
    bool first = true;
    for (const auto& item : result.items) {
        if (!first) std::cout << ',';
        first = false;
        std::cout << "{\"id\":" << item.id << ",\"rect\":";
        rect(item.rect);
        std::cout << ",\"cell\":";
        rect(item.cell_rect);
        if (advanced) {
            std::cout << ",\"at\":[" << item.column << ',' << item.row << ',' << item.column_span << ',' << item.row_span << ']';
        }
        std::cout << '}';
    }
    std::cout << "]}\n";
}
} // namespace

int main() {
    std::uint32_t index = 0;
    try {
        std::cin.imbue(std::locale::classic());
        std::cout.imbue(std::locale::classic());
        std::cout << std::setprecision(std::numeric_limits<axisly::Unit>::max_digits10);
        const auto protocol = token();
        if (protocol != "AXISLY_REFERENCE_V1" && protocol != "AXISLY_REFERENCE_V2") throw std::runtime_error("Unsupported protocol version");
        const auto cases = count(10000, false);
        for (; index < cases; ++index) run_case(index, protocol == "AXISLY_REFERENCE_V2");
        std::string extra;
        if (std::cin >> extra) throw std::runtime_error("Unexpected trailing input");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "axisly reference case " << index << ": " << error.what() << '\n';
        return 1;
    }
}
