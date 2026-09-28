#include <gridly/gridly.hpp>

#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace {

using namespace gridly;

// Checks deliberately remain active in Release builds.
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template<class Function>
void require_invalid_argument(Function&& function) {
    try {
        function();
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error("Expected std::invalid_argument");
}

void geometry() {
    static_assert(std::is_same_v<Unit, float>);
    static_assert(std::is_same_v<ItemId, std::uint64_t>);
    static_assert(std::is_aggregate_v<Size> && std::is_aggregate_v<Rect> && std::is_aggregate_v<Insets>);
    static_assert(std::is_trivially_copyable_v<Size> && std::is_trivially_copyable_v<Rect>);
    require(Size{} == Size{0, 0}, "Size must default to zero");
    require(Rect{} == Rect{0, 0, 0, 0}, "Rect must default to zero");
    const Insets padding{1, 2, 3, 4};
    require(padding.left == 1 && padding.top == 2 && padding.right == 3 && padding.bottom == 4,
            "Insets must use left, top, right, bottom order");
}

void tracks() {
    const auto fixed = Track::fixed(180);
    const auto weighted = Track::weighted(2);
    const auto content = Track::content();
    require(fixed.kind() == TrackKind::Fixed && std::get<FixedTrack>(fixed.definition()).size == 180,
            "Fixed track must retain its size");
    require(weighted.kind() == TrackKind::Weighted && std::get<WeightedTrack>(weighted.definition()).weight == 2,
            "Weighted track must retain its weight");
    require(Track::weighted() == Track::weighted(1), "Default weight must be one");
    require(content.kind() == TrackKind::Content && std::holds_alternative<ContentTrack>(content.definition()),
            "Content track must retain its kind");
    require(std::get<FixedTrack>(Track::fixed(0).definition()).size == 0, "Zero fixed size must be allowed");
    auto copy = fixed;
    copy = weighted;
    require(fixed == Track::fixed(180) && copy == weighted, "Track copies must be independent values");
}

void invalid_tracks() {
    const auto infinity = std::numeric_limits<Unit>::infinity();
    const auto nan = std::numeric_limits<Unit>::quiet_NaN();
    for (const auto value : {-1.0f, infinity, -infinity, nan}) {
        require_invalid_argument([value] { (void)Track::fixed(value); });
        require_invalid_argument([value] { (void)Track::weighted(value); });
    }
    require_invalid_argument([] { (void)Track::weighted(0); });
    require_invalid_argument([] { (void)Track::weighted(-0.0f); });
    require(Track::fixed(std::numeric_limits<Unit>::max()).kind() == TrackKind::Fixed,
            "A finite size should be representable even when later arithmetic could overflow");
}

void ranges() {
    const auto minimum = Track::fixed(100);
    const auto maximum = Track::weighted(2);
    const auto ranged = Track::range(minimum, maximum);
    require(ranged.kind() == TrackKind::Range, "Range must have a distinct kind");
    const auto& bounds = std::get<RangeTrack>(ranged.definition());
    require(std::get<FixedTrack>(bounds.minimum).size == 100, "Range must retain its minimum");
    require(std::get<WeightedTrack>(bounds.maximum).weight == 2, "Range must retain its growth weight");
    require(minimum == Track::fixed(100) && maximum == Track::weighted(2), "Range must not mutate its inputs");
    require(Track::range(Track::fixed(100), Track::fixed(100)).kind() == TrackKind::Range,
            "Equal fixed bounds must be allowed");
    require(Track::range(Track::fixed(0), Track::content()).kind() == TrackKind::Range,
            "Content maximum must be representable");
    require(Track::range(Track::content(), Track::fixed(200)).kind() == TrackKind::Range,
            "Content minimum with a fixed maximum must be representable");
    require(Track::range(Track::content(), Track::weighted()).kind() == TrackKind::Range,
            "Content minimum with a weighted maximum must be representable");
    require(Track::range(Track::content(), Track::content()).kind() == TrackKind::Range,
            "Content bounds must be representable");
}

void invalid_ranges() {
    require_invalid_argument([] { (void)Track::range(Track::fixed(101), Track::fixed(100)); });
    require_invalid_argument([] { (void)Track::range(Track::weighted(), Track::fixed(100)); });
    require_invalid_argument([] { (void)Track::range(Track::weighted(), Track::weighted()); });
    const auto nested = Track::range(Track::fixed(100), Track::weighted());
    require_invalid_argument([&] { (void)Track::range(nested, Track::weighted()); });
    require_invalid_argument([&] { (void)Track::range(Track::fixed(0), nested); });
}

void grids() {
    const GridItem defaults{};
    require(defaults.column_span == 1 && defaults.row_span == 1, "Default item must occupy one cell");
    require(defaults.horizontal_alignment == HorizontalAlignment::Stretch
                && defaults.vertical_alignment == VerticalAlignment::Stretch,
            "Default item must stretch on both axes");

    const GridSpec two_by_two{
        .columns = {Track::fixed(100), Track::weighted()},
        .rows = {Track::content(), Track::fixed(40)},
        .column_gap = 8,
        .row_gap = 6,
        .padding = {1, 2, 3, 4}
    };
    require(two_by_two.columns.size() == 2 && two_by_two.rows.size() == 2, "Must describe a 2x2 grid");
    require(two_by_two.column_gap == 8 && two_by_two.row_gap == 6 && two_by_two.padding == Insets{1, 2, 3, 4},
            "Independent gaps and asymmetric padding must be retained");
    GridSpec three_by_three;
    three_by_three.columns = {Track::fixed(180), Track::weighted(1), Track::weighted(2)};
    three_by_three.rows = {Track::content(), Track::content(), Track::weighted()};
    require(three_by_three.columns.size() == 3 && three_by_three.rows.size() == 3, "Must describe a 3x3 grid");

    const std::vector<GridItem> items = {
        {.id = 0, .column = 0, .row = 0, .column_span = 2, .row_span = 2, .margin = {1, 2, 3, 4},
         .horizontal_alignment = HorizontalAlignment::Start, .vertical_alignment = VerticalAlignment::End},
        {.id = 2, .column = 2, .row = 0,
         .horizontal_alignment = HorizontalAlignment::Center, .vertical_alignment = VerticalAlignment::Center},
        {.id = 3, .column = 0, .row = 2,
         .horizontal_alignment = HorizontalAlignment::End, .vertical_alignment = VerticalAlignment::Start},
        {.id = 4, .column = 2, .row = 2}
    };
    const std::span<const GridItem> view{items};
    require(view.size() == 4 && view.front().id == 0, "Items must support a borrowed span and ID zero");
    require(view[0].column_span == 2 && view[0].row_span == 2 && view[0].margin == Insets{1, 2, 3, 4},
            "Placement, spans and margins must be independent of UI ownership");
}

struct HostState {
    ItemId last_id{};
    int calls{};
};

MeasureResult measure_item(ItemId id, const MeasureInput& input, void* user_data) {
    auto& state = *static_cast<HostState*>(user_data);
    state.last_id = id;
    ++state.calls;
    return {{10, 20}, {input.known_width.value_or(100.0f), input.known_height.value_or(60.0f)}};
}

void measurement() {
    HostState host;
    const MeasureCallback callback = &measure_item;
    MeasureInput input{.available = {300, 200}};
    require(!input.known_width && !input.known_height, "Unknown dimensions must be absent");
    const auto unconstrained = callback(42, input, &host);
    require(unconstrained.minimum == Size{10, 20} && unconstrained.preferred == Size{100, 60},
            "Callback must expose minimum and preferred dimensions");
    input.known_width = 0.0f;
    const auto zero_width = callback(0, input, &host);
    require(zero_width.preferred.width == 0 && zero_width.preferred.height == 60,
            "Known zero width must be distinct from unknown width");
    input.known_width = 150.0f;
    input.known_height = 80.0f;
    require(callback(7, input, &host).preferred == Size{150, 80}, "Known dimensions must reach the host");
    require(host.calls == 3 && host.last_id == 7, "Callback must preserve host context and opaque item IDs");
}

void results() {
    GridResult result;
    require(!result.succeeded() && result.error.code == GridErrorCode::NotComputed,
            "An uncomputed result must not report success");
    require(result.measurement_count == 0, "An uncomputed result must not report any measurements");
    result.error = {.code = GridErrorCode::None};
    result.size = {300, 200};
    result.items = {{42, {10, 20, 100, 40}}};
    require(result.succeeded() && result.items.front().id == 42, "A successful result must preserve item IDs");
    require(result.items.front().rect == Rect{10, 20, 100, 40}, "Result must carry native rectangles");
    result.error = {.code = GridErrorCode::InvalidSpan, .message = "Span must be positive", .item_id = 0};
    require(!result.succeeded() && result.error.item_id.has_value() && *result.error.item_id == 0,
            "Error context must distinguish item zero from no item");
}

struct TestCase {
    std::string_view name;
    void (*run)();
};

} // namespace

int main(int argc, char** argv) {
    const std::array cases{
        TestCase{"geometry", geometry}, TestCase{"tracks", tracks},
        TestCase{"invalid_tracks", invalid_tracks}, TestCase{"ranges", ranges},
        TestCase{"invalid_ranges", invalid_ranges}, TestCase{"grids", grids},
        TestCase{"measurement", measurement}, TestCase{"results", results}
    };
    if (argc != 2) {
        std::cerr << "Provide one contract test name\n";
        return 2;
    }
    for (const auto& test : cases) {
        if (test.name == argv[1]) {
            try {
                test.run();
                std::cout << "PASS " << test.name << '\n';
                return 0;
            } catch (const std::exception& error) {
                std::cerr << "FAIL " << test.name << ": " << error.what() << '\n';
                return 1;
            }
        }
    }
    std::cerr << "Unknown contract test: " << argv[1] << '\n';
    return 2;
}
