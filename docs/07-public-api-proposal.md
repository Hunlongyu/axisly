# 07 — Public API Proposal

The following is the historical native C++ proposal. The current 0.1.0 API adds
percentages, automatic placement, first baselines, rounding and optional retained
types; see [the current API contract](23-api-contract.md) and installed headers.

This document is the original API proposal. The implementation uses the
`axisly` namespace and `<axisly/axisly.hpp>`. It also defines range bounds and
error/result status; see [initialization decisions](08-initialization.md).
M7 provides `compute_grid(spec, items, available)` for fixed, weighted, Content and Range tracks,
with explicit row/column spans, item margins and independent alignment from static `preferred_size`. The overload
`compute_grid(spec, items, available, measure, user_data)` supports intrinsic measurement
and height remeasurement at the final item width (`user_data` defaults to null). See
[Range semantics](14-range-layout.md) for current supported features and
failure behavior. A constrained template also accepts a two-argument capturing callable
as `compute_grid(spec, items, available, measure)`, borrowed synchronously without copying.
The host-side nested adapter is an example rather than a new installed grid/tree type;
see [nested integration](15-nested-grid-integration.md). The headers are the source of truth for the current API.

M8 (`0.0.9`) adds optional development-only reference tools and makes no changes to
the exported layout API or its dependencies.

## Geometry

```cpp
using Unit = float;

struct Size {
    Unit width{};
    Unit height{};
};

struct Rect {
    Unit x{};
    Unit y{};
    Unit width{};
    Unit height{};
};

struct Insets {
    Unit left{};
    Unit top{};
    Unit right{};
    Unit bottom{};
};
```

## Track

```cpp
enum class TrackKind : std::uint8_t {
    Fixed,
    Weighted,
    Content,
    Range
};

class Track {
public:
    static Track fixed(Unit value);
    static Track weighted(
        float weight = 1.0f,
        Unit minimum = 0.0f,
        std::optional<Unit> maximum = std::nullopt);
    static Track content();

    // Fixed/Content minimum; Fixed/Content/Weighted upper bound or growth rule.
    static Track range(const Track& minimum, const Track& maximum);
};
```

## Alignment

```cpp
enum class HorizontalAlignment : std::uint8_t {
    Start,
    Center,
    End,
    Stretch
};

enum class VerticalAlignment : std::uint8_t {
    Start,
    Center,
    End,
    Stretch
};
```

## Grid spec

```cpp
struct GridSpec {
    std::vector<Track> columns;
    std::vector<Track> rows;
    Unit column_gap{};
    Unit row_gap{};
    Insets padding{};
};
```

## Item

```cpp
using ItemId = std::uint64_t;

struct GridItem {
    ItemId id{};
    std::uint32_t column{};
    std::uint32_t row{};
    std::uint32_t column_span{1};
    std::uint32_t row_span{1};
    Insets margin{};
    HorizontalAlignment horizontal_alignment{HorizontalAlignment::Stretch};
    VerticalAlignment vertical_alignment{VerticalAlignment::Stretch};
    Size preferred_size{};
};
```

## Measurement

```cpp
struct MeasureInput {
    std::optional<Unit> known_width;
    std::optional<Unit> known_height;
    Size available{};
};

struct MeasureResult {
    Size minimum{};
    Size preferred{};
};

using MeasureCallback = MeasureResult (*)(ItemId, const MeasureInput&, void* user_data);
```

## Result

```cpp
struct ItemLayout {
    ItemId id{};
    Rect rect{};
    Rect cell_rect{};
};

struct GridResult {
    Size size{};
    std::vector<ItemLayout> items;
    GridError error{};
    std::size_t measurement_count{};
};
```

## Example

```cpp
GridSpec spec;
spec.columns = {
    Track::fixed(180),
    Track::weighted(1),
    Track::weighted(2)
};

spec.rows = {
    Track::content(),
    Track::content(),
    Track::weighted(1)
};

spec.column_gap = 8;
spec.row_gap = 8;
spec.padding = {12, 12, 12, 12};

std::vector<GridItem> items = {
    {.id = 1, .column = 0, .row = 0},
    {.id = 2, .column = 1, .row = 0, .column_span = 2},
    {.id = 3, .column = 0, .row = 2, .column_span = 3}
};
```

The exact callable interface should be frozen only after M1/M2 prototypes are used by both the SDL3 demo and at least one real UI adapter.
