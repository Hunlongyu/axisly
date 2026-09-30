#include "css_reference_output.hpp"

#include <array>
#include <iostream>
#include <string>

namespace {
using namespace axisly;
using namespace axisly::css;
Calculation sum(Unit length, Unit percent) {
    return Calculation::add(Calculation::length(length), Calculation::percent(percent));
}
Length calc(Unit length, Unit percent) { return Length::calc(sum(length, percent)); }
std::vector<TrackSizing> tracks(int variant) {
    const auto track = [](Calculation a, Calculation b) {
        return TrackSizing::minmax(TrackBreadth::calc(a), TrackBreadth::calc(b));
    };
    switch (variant) {
    case 0: return {track(sum(20, .1f), sum(20, .1f)), track(sum(35, .05f), sum(35, .05f))};
    case 1: return {track(sum(10, .05f), sum(80, .1f)), track(sum(15, .02f), sum(60, .08f))};
    case 2: {
        auto lower = Calculation::clamp(Calculation::length(25), Calculation::percent(.1f), Calculation::length(10));
        return {track(lower, lower), track(sum(15, .1f), sum(15, .1f))};
    }
    case 3: {
        auto zero = Calculation::maximum(Calculation::length(0), sum(0, -.1f));
        return {track(zero, zero), track(sum(20, .1f), sum(20, .1f))};
    }
    default: return {
        TrackSizing::minmax(TrackBreadth::length(10), TrackBreadth::calc(sum(80, .1f))),
        TrackSizing::minmax(TrackBreadth::length(15), TrackBreadth::calc(sum(60, .08f)))};
    }
}
void run(int variant, int space, bool definite, bool rows, int repeat, bool rtl, bool implicit) {
    LayoutTree tree;
    Style style;
    style.display = Display::Grid;
    style.box.direction = rtl ? Direction::Rtl : Direction::Ltr;
    if (definite) style.box.size = {calc(400, 0), calc(180, 0)};
    style.gap = {calc(3, .02f), calc(2, .01f)};
    auto& axis = rows ? style.grid.template_rows : style.grid.template_columns;
    const auto sizing = tracks(variant);
    if (repeat == 0) for (const auto& value : sizing) axis.push_back(value);
    else axis.push_back(GridRepeat{repeat == 1 ? RepeatKind::Count
        : repeat == 2 ? RepeatKind::AutoFill : RepeatKind::AutoFit, 2, sizing, {}});
    (rows ? style.grid.template_columns : style.grid.template_rows) = {TrackSizing::fixed(60)};
    const auto a = TrackSizing::minmax(TrackBreadth::calc(sum(15, .05f)), TrackBreadth::calc(sum(15, .05f)));
    const auto clipped = Calculation::maximum(Calculation::length(0), sum(50, -.05f));
    const auto b = TrackSizing::minmax(TrackBreadth::calc(clipped), TrackBreadth::calc(clipped));
    style.grid.auto_columns = {a, b};
    style.grid.auto_rows = {a, b};
    const auto root = tree.create_node(style);
    std::vector<reference::IdentifiedNode> identities{{1, root}};
    constexpr std::array displays{Display::Block, Display::Flex, Display::Grid};
    for (std::size_t i = 0; i < displays.size(); ++i) {
        style = {};
        style.display = displays[i];
        style.box.direction = rtl ? Direction::Rtl : Direction::Ltr;
        style.box.min_size = {calc(0, 0), calc(0, 0)};
        if (implicit && i != 1) {
            (rows ? style.grid.row : style.grid.column).start = GridLine::index(i == 0 ? -12 : 12);
        }
        const auto parent = tree.create_node(style);
        tree.append_child(root, parent);
        identities.emplace_back(static_cast<int>(2 + i * 3), parent);
        for (std::size_t j = 0; j < 2; ++j) {
            style = {};
            style.display = Display::Block;
            style.box.direction = rtl ? Direction::Rtl : Direction::Ltr;
            style.box.size.height = calc(10, .1f);
            style.box.min_size.width = calc(0, 0);
            const Size content{j == 0 ? 50.0f : 30.0f, j == 0 ? 10.0f : 15.0f};
            const auto leaf = tree.create_node(style, [content](const MeasureRequest&) {
                return Measurement{content, {}, {}, content};
            });
            tree.append_child(parent, leaf);
            identities.emplace_back(static_cast<int>(3 + i * 3 + j), leaf);
        }
    }
    const auto available = space == 0
        ? AvailableSize{AvailableSpace::definite(500), AvailableSpace::definite(250)}
        : space == 1 ? AvailableSize{AvailableSpace::max_content(), AvailableSpace::max_content()}
            : AvailableSize{AvailableSpace::min_content(), AvailableSpace::min_content()};
    const auto name = "calc-tracks-" + std::to_string(variant) + "-" + std::to_string(space)
        + "-" + std::to_string(definite) + "-" + std::to_string(rows) + "-" + std::to_string(repeat)
        + "-" + std::to_string(rtl) + "-" + std::to_string(implicit);
    reference::write_case(std::cout, name, tree.compute(root, available), identities, true);
}
} // namespace
int main() {
    try {
        std::cout << "{\"protocol\":\"AXISLY_CALC_TRACKS_V1\",\"cases\":[";
        bool first = true;
        for (int variant = 0; variant < 5; ++variant)
        for (int space = 0; space < 3; ++space)
        for (int definite = 0; definite < 2; ++definite)
        for (int rows = 0; rows < 2; ++rows)
        for (int repeat = 0; repeat < 4; ++repeat)
        for (int rtl = 0; rtl < 2; ++rtl)
        for (int implicit = 0; implicit < 2; ++implicit) {
            if (!first) std::cout << ',';
            first = false;
            run(variant, space, definite != 0, rows != 0, repeat, rtl != 0, implicit != 0);
        }
        std::cout << "]}";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
