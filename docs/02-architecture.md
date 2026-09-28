# 02 — Architecture

## Layering

```text
+-------------------------------------------------+
|                Third-party UI                   |
| Win32 / Qt / SDL / Skia / game UI / custom UI |
+-------------------------+-----------------------+
                          |
                  Framework Adapter
          +---------------+---------------+
          | measure(item, constraints)     |
          | apply_rect(item, rect)         |
          +---------------+---------------+
                          |
                          v
+-------------------------------------------------+
|                axisly Core                  |
|                                                 |
| validation -> placement -> track sizing         |
| -> span resolution -> alignment -> result       |
+-------------------------------------------------+
```

## Core ownership rule

The layout engine does **not** own UI objects.

A `GridItem` stores only an opaque identifier:

```cpp
using ItemId = std::uint64_t;
```

The host framework maps this ID back to its own widget/node/window.

This makes it possible to adapt:

- `HWND`
- `QWidget*`
- custom retained widget handles
- ECS entity IDs
- game object IDs

without linking any UI framework into axisly.

## Stateless core first

The first public computation API should be conceptually:

```cpp
GridResult compute_grid(
    const GridSpec& spec,
    std::span<const GridItem> items,
    Size available,
    MeasureCallback measure,
    void* user_data);
```

Benefits:

- deterministic
- easy to unit-test
- no hidden state
- no widget ownership
- easy differential testing
- easy fuzzing
- easy benchmarking

## Runtime track state

Do not mutate public `Track` objects during layout. Convert them to an internal runtime representation:

```cpp
struct ResolvedTrack {
    Unit base_size{};
    Unit final_size{};
    Unit offset{};
    Unit min_size{};
    Unit max_size{};
    float weight{};
    TrackKind kind{};
};
```

## Suggested computation pipeline

```text
compute_grid()
    |
    +-- validate_input()
    +-- initialize_tracks()
    +-- place_explicit_items()
    +-- resolve_fixed_tracks()
    +-- resolve_content_contributions()
    +-- resolve_spanning_items()
    +-- distribute_weighted_space()
    +-- finalize_columns()
    +-- remeasure_height_dependent_items()
    +-- finalize_rows()
    +-- compute_track_offsets()
    +-- align_items()
    +-- optional_rounding()
    +-- build_result()
```

## Measurement boundary

The core does not know what text, images or widgets are.

Suggested callback:

```cpp
struct MeasureInput {
    std::optional<Unit> known_width;
    std::optional<Unit> known_height;
    Size available;
};

struct MeasureResult {
    Size minimum;
    Size preferred;
};

using MeasureCallback = MeasureResult (*)(
    ItemId,
    const MeasureInput&,
    void* user_data);
```

The adapter implements actual measurement with DirectWrite, QFontMetrics, SDL_ttf, Skia, etc.

## Why width-dependent remeasurement matters

Text can measure differently after the final column width is known:

```text
unconstrained: 400 x 20
final width  : 150
remeasured   : 150 x 60
```

Therefore column sizing and row sizing cannot be treated as completely independent one-pass operations.

## Optional complete-layout cache

`GridCache` (0.0.12) owns a specification, an item snapshot, and the most recent
successful `GridResult`. Setters invalidate changed inputs. Container width,
height and the host's content version complete the key. Hits borrow the result
in O(1), without measuring, scanning items or copying rectangles.

The host owns measurement context and must invalidate when external text/font/DPI
data changes. Only this optional layer retains state; `compute_grid` stays stateless.
By default it caches one grid result. Since 0.1.0, per-item measurement caching can
be explicitly enabled; each entry includes every known/available constraint.
See [the detailed contract](20-layout-cache.md) for lifetimes and failure behavior.

Since 0.0.13, each cache also owns a private layout workspace. Output, measurement,
track-sizing and span-growth vectors retain capacity between calls. Static input
validation and sorted span topology survive viewport/content changes; setters that
change the specification/items invalidate both. Dynamic contributions, callbacks
and numeric checks run afresh on every miss. Fixed axes bypass contribution
collection without bypassing measurement. The stateless API creates a local
workspace per call and releases completed validation/axis scratch before the
next phase; no shared or thread-local scratch is used. See
[workspace optimization](21-workspace-optimization.md) for allocation and timing results.

## Optional retained layer (implemented in 0.1.0)

`GridTree` owns nodes and composes per-grid caches above the stateless core:

```text
GridTree
  - node hierarchy
  - cached measurements
  - cached layouts
  - dirty propagation
       |
       v
axisly Core
```

This layer remains optional. Tree callbacks translate stable internal node IDs to
host ItemIds. Mutations invalidate the affected path; unchanged siblings retain
their measurements and layouts when constraints still match. A grid content
invalidation clears its subtree. Final layout uses each child's aligned box;
root-relative rounding happens after arrangement. Cycles, stale handles, depth,
work and absolute-coordinate overflow are checked. Publication is atomic.
See [the public contracts](23-api-contract.md).
