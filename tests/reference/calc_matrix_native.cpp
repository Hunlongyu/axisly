#include "css_reference_output.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace axisly;
using namespace axisly::css;
constexpr std::array displays{Display::Block, Display::Flex, Display::Grid};

Length calc(Unit fixed, Unit fraction) {
    return Length::calc(Calculation::add(Calculation::length(fixed),
        Calculation::percent(fraction)));
}

Style common(Display display, bool content_box, bool rtl) {
    Style style;
    style.display = display;
    style.box.box_sizing = content_box ? BoxSizing::ContentBox : BoxSizing::BorderBox;
    style.box.direction = rtl ? Direction::Rtl : Direction::Ltr;
    return style;
}

void run(int outer, int middle, int inner, int space, int variant,
         bool content_box, bool replaced, bool rtl) {
    LayoutTree tree;
    auto root_style = common(displays[static_cast<std::size_t>(outer)], content_box, rtl);
    root_style.gap = {calc(3, .1f), calc(2, .05f)};
    if (variant == 0) root_style.box.size = {calc(30, .7f), calc(20, .3f)};
    else if (variant == 3) {
        root_style.box.size.width = Length::calc(Calculation::clamp(
            Calculation::length(150), Calculation::percent(.5f), Calculation::length(100)));
    }
    const auto root = tree.create_node(root_style);
    auto parent_style = common(displays[static_cast<std::size_t>(middle)], content_box, rtl);
    parent_style.box.padding.left = calc(2, .02f);
    parent_style.box.padding.top = calc(1, .01f);
    parent_style.box.border.left = Length::calc(Calculation::length(1));
    parent_style.box.min_size.width = calc(10, .05f);
    parent_style.box.max_size.width = calc(140, .1f);
    if (variant == 0) parent_style.box.size.width = calc(20, .5f);
    parent_style.gap.width = calc(2, .05f);
    const auto parent = tree.create_node(parent_style);
    tree.append_child(root, parent);
    auto child_style = common(displays[static_cast<std::size_t>(inner)], content_box, rtl);
    if (variant == 2) child_style.box.size.width = Length::min_content();
    else child_style.box.size.width = calc(10, .5f);
    child_style.box.size.height = calc(15, .25f);
    child_style.box.margin.left = calc(variant == 3 ? -2.0f : 2.0f, .01f);
    child_style.flex_grow = .5f;
    const auto child = tree.create_node(child_style);
    tree.append_child(parent, child);
    auto leaf_style = common(Display::Block, content_box, rtl);
    leaf_style.box.replaced = replaced;
    leaf_style.box.aspect_ratio = 2.0f;
    if (variant == 1) leaf_style.box.size.width = calc(30, .75f);
    else if (variant == 0) leaf_style.box.size = {calc(10, .6f), calc(10, 0)};
    else leaf_style.box.size.height = Length::calc(Calculation::maximum(
        Calculation::length(0), Calculation::subtract(Calculation::length(15),
            Calculation::percent(.05f))));
    leaf_style.box.max_size.width = calc(120, -.1f);
    const auto measured = [](Size content) {
        return [content](const MeasureRequest&) {
            return Measurement{content, {}, {}, content};
        };
    };
    const auto leaf = tree.create_node(leaf_style, measured({70, 20}));
    tree.append_child(child, leaf);
    auto tail_style = common(Display::Block, content_box, rtl);
    tail_style.box.size = {calc(10, .05f), calc(8, 0)};
    const auto tail = tree.create_node(tail_style, measured({10, 8}));
    tree.append_child(root, tail);
    const auto available = space == 0
        ? AvailableSize{AvailableSpace::definite(250), AvailableSpace::definite(100)}
        : space == 1
            ? AvailableSize{AvailableSpace::max_content(), AvailableSpace::max_content()}
            : AvailableSize{AvailableSpace::min_content(), AvailableSpace::min_content()};
    const std::string name = "calc-matrix-" + std::to_string(outer) + "-"
        + std::to_string(middle) + "-" + std::to_string(inner) + "-" + std::to_string(space)
        + "-" + std::to_string(variant) + "-" + std::to_string(content_box)
        + "-" + std::to_string(replaced) + "-" + std::to_string(rtl);
    reference::write_case(std::cout, name, tree.compute(root, available),
        {{1, root}, {2, parent}, {3, child}, {4, leaf}, {5, tail}});
}
} // namespace

int main() {
    try {
        std::cout << "{\"protocol\":\"AXISLY_CALC_MATRIX_V1\",\"cases\":[";
        bool first = true;
        for (int outer = 0; outer < 3; ++outer)
        for (int middle = 0; middle < 3; ++middle)
        for (int inner = 0; inner < 3; ++inner)
        for (int space = 0; space < 3; ++space)
        for (int variant = 0; variant < 4; ++variant)
        for (int content_box = 0; content_box < 2; ++content_box)
        for (int replaced = 0; replaced < 2; ++replaced)
        for (int rtl = 0; rtl < 2; ++rtl) {
            if (!first) std::cout << ',';
            first = false;
            run(outer, middle, inner, space, variant, content_box != 0, replaced != 0, rtl != 0);
        }
        std::cout << "]}";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
