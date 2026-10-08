#include "upstream_fixture_runner.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {
using namespace axisly;
using namespace axisly::css;
bool rounded_output{};

// Measurement model from Taffy 0.14's tests/common/src/lib.rs at the pinned
// upstream commit. Ahem glyphs are 10x10, and U+200B marks break opportunities.
// No font or text layout dependency enters the production library.
// Copyright (c) 2018 Visly Inc.; Copyright (c) 2026 Taffy Authors.
// Adapted under the MIT license in TAFFY_UPSTREAM_LICENSE.txt.
Size measure_ahem(const std::vector<std::size_t>& segments, bool vertical,
    std::optional<Unit> known_inline, std::optional<Unit> known_block,
    AvailableSpace available_inline) {
    constexpr Unit glyph = 10;
    std::size_t minimum_length = 0, maximum_length = 0;
    for (const auto length : segments) {
        minimum_length = std::max(minimum_length, length);
        maximum_length += length;
    }
    const Unit minimum = static_cast<Unit>(minimum_length) * glyph;
    const Unit maximum = static_cast<Unit>(maximum_length) * glyph;
    const Unit queried = available_inline.kind == AvailableKind::MinContent
        ? minimum : available_inline.definite_value().value_or(maximum);
    const Unit inline_size = std::max(minimum,
        known_inline.value_or(std::min(queried, maximum)));
    const auto capacity = static_cast<std::size_t>(std::floor(inline_size / glyph));
    std::size_t line_count = 1, current_length = 0;
    for (const auto length : segments) {
        if (current_length + length > capacity) {
            if (current_length > 0) ++line_count;
            current_length = length;
        } else current_length += length;
    }
    const Unit block_size = known_block.value_or(static_cast<Unit>(line_count) * glyph);
    return vertical ? Size{block_size, inline_size} : Size{inline_size, block_size};
}

std::size_t index_argument(std::string_view value) {
    std::size_t result{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size()) {
        throw std::invalid_argument("Invalid upstream fixture range");
    }
    return result;
}

void write_string(std::ostream& out, std::string_view value) {
    constexpr char hex[] = "0123456789abcdef";
    out << '"';
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte == '"' || byte == '\\') out << '\\' << character;
        else if (byte < 32) out << "\\u00" << hex[byte >> 4] << hex[byte & 15];
        else out << character;
    }
    out << '"';
}
} // namespace

axisly::css::MeasureFunction ahem_measurement(std::string text, bool vertical) {
    constexpr std::string_view separator = "\342\200\213";
    std::vector<std::size_t> segments;
    std::size_t start = 0;
    for (;;) {
        const auto end = text.find(separator, start);
        if (end == std::string::npos) {
            segments.push_back(text.size() - start);
            break;
        }
        segments.push_back(end - start);
        start = end + separator.size();
    }
    return [segments = std::move(segments), vertical](const MeasureRequest& request) {
        const auto available = vertical ? request.available_height : request.available_width;
        const auto measured = measure_ahem(segments, vertical,
            vertical ? request.known_content_height : request.known_content_width,
            vertical ? request.known_content_width : request.known_content_height, available);
        const auto natural = measure_ahem(segments, vertical, {}, {}, available);
        return Measurement{{request.known_content_width.value_or(measured.width),
            request.known_content_height.value_or(measured.height)}, {}, {}, natural};
    };
}

const axisly::css::LayoutResult& compute_fixture(axisly::css::LayoutTree& tree,
    axisly::css::NodeId root, axisly::css::AvailableSize available) {
    return rounded_output ? tree.compute(root, available, axisly::PixelRounding{}) : tree.compute(root, available);
}

int main(int argc, char** argv) {
    try {
        const auto fixtures = upstream_fixtures();
        std::size_t first = 0, count = fixtures.size();
        for (int argument = 1; argument < argc; ++argument) {
            if (std::string_view(argv[argument]) == "--rounding") { rounded_output = true; continue; }
            if (argument + 1 >= argc) throw std::invalid_argument("Missing fixture range value");
            const std::string_view key = argv[argument];
            const auto value = index_argument(argv[++argument]);
            if (key == "--first") first = value;
            else if (key == "--count") count = value;
            else throw std::invalid_argument("Unknown fixture argument");
        }
        if (first >= fixtures.size() || count == 0 || count > fixtures.size() - first) {
            throw std::invalid_argument("Upstream fixture range out of bounds");
        }
        std::cout << "{\"protocol\":\"AXISLY_UPSTREAM_NATIVE_V1\",\"cases\":[";
        for (std::size_t index = first; index < first + count; ++index) {
            if (index != first) std::cout << ',';
            const auto& fixture = fixtures[index];
            std::ostringstream output;
            try {
                fixture.write(output);
                std::cout << output.str();
            } catch (const std::exception& error) {
                std::cout << "{\"name\":";
                write_string(std::cout, fixture.name);
                std::cout << ",\"error\":";
                write_string(std::cout, error.what());
                std::cout << '}';
            }
        }
        std::cout << "]}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
