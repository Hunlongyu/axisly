// Development-only whitespace protocol. No JSON/parser dependency in gridly.
#include <gridly/gridly.hpp>

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
using namespace gridly;

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

void run_case(std::uint32_t index) {
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
    std::vector<GridItem> items;
    std::map<ItemId, MeasureResult> metrics;
    for (std::uint32_t n = 0; n < item_count; ++n) {
        GridItem item;
        item.id = number<ItemId>();
        item.column = count(15);
        item.row = count(15);
        item.column_span = count(16, false);
        item.row_span = count(16, false);
        item.margin = insets();
        item.horizontal_alignment = static_cast<HorizontalAlignment>(count(3));
        item.vertical_alignment = static_cast<VerticalAlignment>(count(3));
        metrics[item.id] = {size(), size()};
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
        std::cout << std::setprecision(std::numeric_limits<gridly::Unit>::max_digits10);
        if (token() != "GRIDLY_REFERENCE_V1") throw std::runtime_error("Unsupported protocol version");
        const auto cases = count(10000, false);
        for (; index < cases; ++index) run_case(index);
        std::string extra;
        if (std::cin >> extra) throw std::runtime_error("Unexpected trailing input");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "gridly reference case " << index << ": " << error.what() << '\n';
        return 1;
    }
}
