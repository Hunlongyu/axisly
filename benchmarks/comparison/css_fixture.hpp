#pragma once

// Shared development-only input for the CSS tree and Yoga runners.
#include <axisly/geometry.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace axisly::bench::comparison {
inline void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
struct Track { char kind{}; float value{}; };
struct Node {
    int parent{-1};
    char display{'F'}, direction{'R'};
    bool wrap{};
    float width{-1}, height{-1}, basis{-1}, grow{}, shrink{1};
    float padding{}, margin{}, gap{};
    int column{}, row{};
    unsigned column_span{1}, row_span{1};
    unsigned measure{}; // 0: none, 1: fixed metrics, 2: wrapping metrics.
    Size preferred{};
    std::vector<Track> columns, rows;
};
struct Fixture {
    std::string name;
    std::vector<Node> nodes;
    std::vector<Size> sizes; // Height -1 means max-content.
    std::vector<std::size_t> measured;
};
inline Fixture read_fixture(const char* path) {
    std::ifstream input(path);
    Fixture f;
    std::string magic;
    std::size_t count{}, frames{};
    input >> magic >> f.name >> count >> frames;
    require(input.good() && magic == "CSSCMP1" && count > 1 && count <= 100'001
        && frames >= 2 && frames <= 100, "Invalid CSS comparison header");
    auto tracks = [&input](std::size_t count) {
        require(count <= 1000, "Too many tracks");
        std::vector<Track> values(count);
        for (auto& t : values) {
            input >> t.kind >> t.value;
            require((t.kind == 'F' || t.kind == 'W' || t.kind == 'A')
                && std::isfinite(t.value) && t.value >= 0, "Invalid track");
        }
        return values;
    };
    f.nodes.resize(count);
    std::vector<int> ancestors;
    std::vector<bool> has_children(count);
    for (std::size_t i = 0; i < count; ++i) {
        auto& n = f.nodes[i];
        unsigned wrap{};
        std::size_t cols{}, rows{};
        input >> n.parent >> n.display >> n.direction >> wrap
            >> n.width >> n.height >> n.basis >> n.grow >> n.shrink
            >> n.padding >> n.margin >> n.gap >> n.column >> n.column_span
            >> n.row >> n.row_span >> n.measure >> n.preferred.width
            >> n.preferred.height >> cols >> rows;
        require(!input.fail() && (i == 0 ? n.parent == -1 : n.parent >= 0
            && static_cast<std::size_t>(n.parent) < i)
            && (n.display == 'F' || n.display == 'G' || n.display == 'B')
            && (n.direction == 'R' || n.direction == 'C') && wrap <= 1
            && n.measure <= 2 && n.column >= 0 && n.column <= 1000
            && n.row >= 0 && n.row <= 1000 && n.column_span > 0
            && n.column_span <= 1000 && n.row_span > 0 && n.row_span <= 1000,
            "Invalid CSS node");
        for (const auto v : {n.width, n.height, n.basis})
            require(std::isfinite(v) && (v >= 0 || v == -1 || v == -2), "Invalid dimension");
        for (const auto v : {n.grow, n.shrink, n.padding, n.margin, n.gap,
                            n.preferred.width, n.preferred.height})
            require(std::isfinite(v) && v >= 0, "Invalid numeric property");
        if (i) {
            while (!ancestors.empty() && ancestors.back() != n.parent) ancestors.pop_back();
            require(!ancestors.empty(), "Fixture must use depth-first node order");
            has_children[static_cast<std::size_t>(n.parent)] = true;
        }
        ancestors.push_back(static_cast<int>(i));
        n.wrap = wrap != 0;
        n.columns = tracks(cols);
        n.rows = tracks(rows);
        if (n.measure) f.measured.push_back(i);
    }
    for (const auto index : f.measured)
        require(!has_children[index], "Measured node must be a leaf");
    f.sizes.resize(frames);
    for (auto& s : f.sizes) {
        input >> s.width >> s.height;
        require(!input.fail() && std::isfinite(s.width) && s.width >= 0
            && std::isfinite(s.height) && (s.height >= 0 || s.height == -1),
            "Invalid viewport");
    }
    std::string extra;
    require(!(input >> extra), "Trailing fixture data");
    return f;
}
// Identical arithmetic in the Rust runner. This is not font shaping.
inline Size measure(const Node& n, std::size_t revision,
                    std::optional<float> known_width,
                    std::optional<float> known_height,
                    std::optional<float> limit, bool min_content) {
    const float preferred = n.preferred.width + static_cast<float>(revision % 7) * 8;
    const float minimum = n.measure == 2 ? std::min(preferred, 40.0F) : preferred;
    float width = min_content ? minimum : preferred;
    if (limit) width = std::min(width, *limit);
    if (known_width) width = *known_width;
    const float lines = n.measure == 2 ? std::max(1.0F,
        std::ceil(preferred / std::max(width, 1.0F))) : 1.0F;
    return {width, known_height.value_or(lines * n.preferred.height)};
}
inline double fingerprint(Rect r) {
    return r.x + static_cast<double>(r.y) * .5
        + static_cast<double>(r.width) * .25 + static_cast<double>(r.height) * .125;
}
} // namespace axisly::bench::comparison
