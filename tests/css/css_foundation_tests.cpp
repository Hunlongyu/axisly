#include <axisly/css_box.hpp>
#include <axisly/css_measurement.hpp>

#include <cmath>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace axisly;
using namespace axisly::css;

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

template<class Callable>
void expect_invalid(Callable&& call) {
    try { call(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("Expected invalid_argument");
}

void values() {
    const auto expression = Calculation::clamp(Calculation::length(10),
        Calculation::add(Calculation::percent(0.5f), Calculation::length(5)),
        Calculation::length(60));
    require(!expression.resolve(std::nullopt), "Unknown percentage basis must remain unknown");
    require(expression.resolve(0.0f) == 10, "A known zero basis must resolve before clamping");
    require(expression.resolve(50.0f) == 30, "calc must use the supplied percentage basis");
    require(expression.resolve(200.0f) == 60, "calc clamp must enforce its upper limit");
    require(expression == Calculation::clamp(Calculation::length(10),
        Calculation::add(Calculation::percent(0.5f), Calculation::length(5)),
        Calculation::length(60)), "Equivalent expression trees must compare equal");
    require(Length::percent(0.25f).resolve_numeric(0.0f) == 0, "Percentage zero is definite");
    require(!Length::percent(0.25f).resolve_numeric(std::nullopt), "Unknown percentage is deferred");
    require(Length::fit_content_percent(0.5f).resolve_fit_content_limit(40.0f) == 20,
        "fit-content limit must use its containing basis");
    require(!Length::min_content().resolve_numeric(20.0f), "Intrinsic keywords need a layout algorithm");
    require(AvailableSpace::definite(0).definite_value() == 0, "Zero available space is definite");
    require(!AvailableSpace::indefinite().definite_value(), "Unknown available space is not zero");
    require(AvailableSpace::min_content() != AvailableSpace::max_content(),
        "Distinct intrinsic queries must remain distinct");
    expect_invalid([] { (void)Length::length(std::numeric_limits<Unit>::quiet_NaN()); });
    expect_invalid([] { (void)AvailableSpace::definite(-1); });
    for (const auto value : {std::numeric_limits<Unit>::infinity(),
            -std::numeric_limits<Unit>::infinity(), std::numeric_limits<Unit>::quiet_NaN()}) {
        expect_invalid([=] { (void)AvailableSpace::definite(value); });
    }
    require(AvailableSpace::definite(-0.0F).definite_value() == 0
        && AvailableSpace::definite(std::numeric_limits<Unit>::max()).definite_value()
            == std::numeric_limits<Unit>::max(), "Valid definite-space endpoints were rejected");
    expect_invalid([] { (void)Calculation::percent(0.5f).resolve(-1.0f); });
    bool overflow = false;
    try {
        (void)Calculation::scale(Calculation::length(std::numeric_limits<Unit>::max()), 2).resolve(0.0f);
    } catch (const std::overflow_error&) { overflow = true; }
    require(overflow, "Overflowing calculation must be rejected");
}

void value_ownership() {
    require(Length::length(0.0f) == Length::length(-0.0f), "Signed zero lengths compare equal");
    require(Length::percent(0.0f) == Length::percent(-0.0f), "Signed zero percentages compare equal");
    auto original = Length::calc(Calculation::add(Calculation::length(5), Calculation::percent(0.5f)));
    const auto equivalent = Length::calc(Calculation::add(Calculation::length(5), Calculation::percent(0.5f)));
    require(original == equivalent, "Distinct equivalent calculation payloads compare equal");
    auto alias = original;
    original = Length::length(123);
    require(alias.resolve_numeric(100) == 55, "A calculation copy owns its expression independently");
    const auto& self = alias;
    alias = self;
    require(alias.resolve_numeric(100) == 55, "Self assignment retains the calculation");
    auto moved = std::move(alias);
    alias = Length::percent(0.25f);
    require(moved == equivalent && alias.resolve_numeric(100) == 25,
        "Moving a calculation transfers ownership and leaves the source assignable");
    std::vector<Length> values;
    for (int i = 0; i < 1000; ++i) {
        values.push_back(i % 2 ? moved : Length::length(static_cast<Unit>(i)));
    }
    moved = Length::automatic();
    for (std::size_t i = 0; i < values.size(); ++i) {
        require(values[i].resolve_numeric(100) == (i % 2 ? 55 : static_cast<Unit>(i)),
            "Vector relocation preserves both numeric and calculation values");
    }
    values[0] = std::move(values[1]);
    require(values[0] == equivalent, "Move assignment releases the old value and adopts the expression");
    auto* self_move = &values[0];
    values[0] = std::move(*self_move);
    require(values[0] == equivalent, "Self move retains a valid calculation");
    require(!values[0].resolve_numeric(std::nullopt), "Length calculations still require a definite basis");
    values[0] = Length::calc(Calculation::length(7));
    require(values[0].resolve_numeric(0) == 7 && values[3] == equivalent,
        "Replacing one expression leaves the other aliases intact");
}

void box() {
    BoxStyle style;
    style.box_sizing = BoxSizing::ContentBox;
    style.direction = Direction::Rtl;
    style.size.width = Length::percent(0.5f);
    style.size.height = Length::length(20);
    style.min_size.width = Length::length(70);
    style.max_size.width = Length::length(50);
    style.margin.left = Length::length(-5);
    style.margin.right = Length::automatic();
    style.padding.left = Length::percent(0.1f);
    style.padding.right = Length::length(2);
    style.border.left = Length::length(1);
    style.border.right = Length::length(1);
    const auto result = resolve_box(style, 100.0f, 200.0f);
    require(result.direction == Direction::Rtl, "Box direction must be retained");
    require(result.margin.left.numeric() == -5 && result.margin.right.kind == EdgeKind::Auto,
        "Negative and auto margins must remain distinct");
    require(result.horizontal_decoration() == 14, "Padding percentages use inline basis");
    require(result.preferred.width == 64 && result.preferred.height == 20,
        "Content-box dimensions must add padding and border");
    require(constrain_size({90, 20}, result) == Size{84, 20}, "Minimum must win over maximum");
    const auto unresolved = resolve_box(style, std::nullopt, 200.0f);
    require(unresolved.padding.left.kind == EdgeKind::UnresolvedPercentage,
        "Unknown inline basis must not turn percentage padding into zero");
    require(!unresolved.preferred.width, "Unresolved padding prevents a definite border-box size");
    const auto intrinsic = resolve_box(style, std::nullopt, 200.0f,
        IndefiniteEdgeBasis::Zero);
    require(intrinsic.padding.left.numeric() == 0
        && intrinsic.horizontal_decoration() == 4
        && !intrinsic.preferred.width,
        "Intrinsic edge resolution must keep percentage dimensions indefinite");
    require(resolve_box(style, 0.0f, 0.0f).preferred.width == 4,
        "Known zero basis must resolve percentage width and padding");
    style.size.height = Length::automatic();
    style.aspect_ratio = 2.0f;
    require(resolve_box(style, 100.0f, 100.0f).preferred.height == 25,
        "Content-box aspect ratio must transfer using content width");
    style.padding.top = Length::length(-1);
    expect_invalid([&] { (void)resolve_box(style, 100.0f, 100.0f); });
}

void measurement() {
    MeasureRequest min_query;
    min_query.known_content_width = 0.0f;
    min_query.available_width = AvailableSpace::min_content();
    validate_measure_request(min_query);
    require(min_query.known_content_width == 0 && min_query.available_width.kind == AvailableKind::MinContent,
        "Known zero and min-content query are independent");
    Measurement result{{0, 30}, 20.0f, 25.0f};
    validate_measurement(result);
    result.last_baseline = 31.0f;
    expect_invalid([&] { validate_measurement(result); });
    result.last_baseline = 25.0f;
    result.scrollable_content_size = Size{0, 50};
    validate_measurement(result);
    result.scrollable_content_size = Size{-1, 50};
    expect_invalid([&] { validate_measurement(result); });
    min_query.known_content_height = -1.0f;
    expect_invalid([&] { validate_measure_request(min_query); });
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected test case name");
        const std::string_view name = argv[1];
        if (name == "values") values();
        else if (name == "value_ownership") value_ownership();
        else if (name == "box") box();
        else if (name == "measurement") measurement();
        else throw std::runtime_error("Unknown test case");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
