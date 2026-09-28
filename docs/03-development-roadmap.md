# 03 — Detailed Development Roadmap

## Current implementation

The project is named **gridly**. M0 through M10 and the listed 1.1+ feature extensions
are implemented in `0.1.0`. See [completion and validation](22-roadmap-completion.md)
and [API contracts](23-api-contract.md). The older milestone sections below retain
their original implementation history.
The core supports fixed, weighted, Content and Range tracks, numeric limits with redistribution,
explicit placement and spans, padding, gaps, margins, independent item alignment, and validated rectangle output.
Callbacks supply minimum/preferred sizes and are called again at the final item width before row sizing.
See [initialization decisions](08-initialization.md), [fixed-layout semantics](09-fixed-layout.md),
[weighted-layout semantics](10-weighted-layout.md), [alignment semantics](11-alignment-and-margin.md),
[measurement and Content semantics](12-measurement-and-content.md),
[spanning semantics](13-spanning-layout.md),
[Range semantics](14-range-layout.md),
[nested integration](15-nested-grid-integration.md),
[reference validation](16-reference-validation.md),
[SDL3 playground](17-sdl3-playground.md),
and the root README for the implemented API and build commands.
M10 now has opt-in timing/allocation executables, 14 deterministic workloads and a measured ID-validation optimization.
See [performance benchmarks](18-performance-benchmarks.md) for methodology and competitor benchmark boundaries.
An additional opt-in [same-workload comparison](19-comparative-benchmarks.md) runs gridly, Taffy and Yoga with geometry checks before timing.
The optional [GridCache](20-layout-cache.md) reuses the last successful layout in O(1), with owned input snapshots and explicit content invalidation.
It also [retains scratch/output capacity and validated span topology](21-workspace-optimization.md) across cache misses, while still measuring fresh content.
Per-item measurement caches, retained trees and dirty propagation are now implemented,
along with rounding, automatic placement, implicit tracks, percentages and first
baseline alignment. API contracts and sanitizer CI are provided. `0.1.0` is the
current development version; milestone headings below are not published releases
or a claim of full browser behavior or broad production qualification.

## Phase M0 — Freeze the data model

Implement public value types only:

- Geometry
- `Track`
- `GridSpec`
- `GridItem`
- alignments
- measurement contracts
- result types
- error/status types

Acceptance criteria:

The API can describe:

- 2x2 and 3x3 grids
- fixed and weighted tracks
- content tracks
- gaps and padding
- explicit placement
- spans
- alignment

No layout algorithm yet.

---

## Phase M1 — Fixed tracks

Implement:

- fixed rows
- fixed columns
- explicit row/column placement
- container padding
- row/column gap
- output cell rectangles

Acceptance tests:

- one cell
- multiple columns
- multiple rows
- asymmetric padding
- gaps
- zero-size container

At this point the engine is already useful as a deterministic table/grid geometry calculator.

---

## Phase M2 — Weighted tracks

Implement `Track::weighted(weight)`.

Implemented in `0.0.3`. Optional numeric limits are expressed as
`Track::weighted(weight, minimum, maximum)`; existing one-argument calls remain valid.
Full `Track::range(...)` combinations are implemented in M6.

Algorithm:

1. subtract container padding
2. subtract all gaps
3. allocate fixed tracks
4. determine remaining free space
5. distribute it using track weights
6. enforce minimum/maximum constraints if present
7. redistribute after constrained tracks are frozen

Important test cases:

- 1:1
- 1:2
- 1:2:3
- no free space
- insufficient space
- one track reaches its minimum
- one track reaches its maximum

---

## Phase M3 — Alignment and margin

Implemented in `0.0.4` using static `GridItem::preferred_size`; results expose
both the aligned `rect` and original `cell_rect`. M4 adds callback-based measurement.

Implement per-item:

- Start
- Center
- End
- Stretch
- margin

Keep horizontal and vertical alignment independent.

Acceptance criteria:

Given a resolved cell rectangle and preferred item size, alignment produces deterministic final item bounds.

---

## Phase M4 — Measurement + Content tracks

Implemented in `0.0.5`. Successful callback-based layouts make exactly two calls per item.
`GridResult::measurement_count` is available in every build configuration and also retains
the count on returned errors. Content tracks use preferred sizes, shrinking proportionally
to available capacity down to measured minima; fixed/weighted numeric constraints remain authoritative.
Columns use intrinsic widths; rows use heights measured at the final aligned item width.
The original three-argument API uses static preferred sizes and zero item minima.

Add item measurement.

Implement:

- minimum size
- preferred size
- `Track::content()`
- content contribution to column sizing
- width-constrained remeasurement before final row sizing

This is the first major complexity step.

Instrument the engine with a debug measurement counter to track accidental excessive remeasurement.

---

## Phase M5 — Spanning items

Implemented in `0.0.6` in the dedicated `src/detail/span_resolution` module.
Merged cells include internal gaps. Shorter spans are resolved before longer spans;
equal-length groups merge growth from a shared snapshot, independent of input order.
Content absorbs spanning deficits first; otherwise spanning minima raise weighted floors
proportionally within numeric maxima. Preferences only grow Content. Hard sizes/caps remain
authoritative when an item's requirements cannot be met. Remeasurement uses the final aligned
width of the entire span, and remains two callback calls per item.

Implement:

- column span
- row span
- contribution of a spanning item's minimum/preferred size to affected tracks

Do not simply divide the deficit evenly in all cases.

Track growth policy should account for:

- Fixed tracks: normally frozen
- Content tracks: grow from content contribution
- Weighted tracks: can absorb flexible space
- Range tracks: grow only within bounds

Keep span logic in a dedicated module.

---

## Phase M6 — Range tracks

Implemented in `0.0.7`. All six legal lower/upper combinations are resolved through
internal track states. Fixed lower bounds supply numeric minima; Content lower bounds
read measured minima. Fixed upper bounds cap content-driven growth, Content targets
preferred size, and Weighted shares remaining space. Fixed caps clip conflicting
intrinsic minima; fixed floors survive smaller preferences. Spanning contributions
freeze capped content tracks and redistribute remaining minimum deficits to weighted tracks.

Add a native range abstraction, e.g.:

```cpp
Track::range(
    Track::fixed(100),
    Track::weighted(1));
```

Typical use:

```text
minimum width 100
then participate in remaining-space distribution
```

Required behavior:

- clamp
- freeze constrained tracks
- redistribute remaining space
- stable handling of impossible constraints

---

## Phase M7 — Nested-grid integration

Implemented in `0.0.8` as `examples/nested_grid_adapter.hpp`, a host-owned reference
adapter outside the installed library. It probes finite minimum/preferred sizes,
recomputes descendants at their final aligned allocation, and returns root-relative
rectangles with scoped item paths. Active-ancestor cycles, depth, total layout work,
child mappings and coordinate overflow are checked. Failed runs expose no partial
geometry; user exceptions propagate. Capturing/move-only measurement callables are
also supported by the installed `compute_grid` convenience overload.

No special nested-grid primitive is required in the core.

The host adapter measures a child grid recursively:

```text
parent needs child's size
       |
       v
adapter calls compute_grid(child)
       |
       v
child result becomes measurement
```

Add recursion guards and clear ownership conventions.

---

## Phase M8 — Reference/differential validation

Implemented in `0.0.9` under `tests/reference`, behind `GRIDLY_BUILD_REFERENCE_TESTS=OFF`
by default. The locked Taffy 0.14.0 tool and standard-library Python driver compare
30 fixtures plus 750 deterministic random cases. Input-only eligibility rules limit
the comparison to fixed tracks, flexible tracks with numeric floors, explicit spans,
fitting alignment/margins and unconstrained Content sizing. Incompatible CSS behavior
is rejected explicitly. Results include occupied extents, item rectangles and grid areas;
pixel rounding is disabled and the tolerance is documented. Mismatches produce replayable
JSON fixtures. Reference tools are never linked to or installed with the production library.

Build a development-only adapter that maps the common feature subset to Taffy.

Pipeline:

```text
random/native test case
        |
        +------ C++ gridly ------+
        |                            |
        +------ Taffy reference -----+
                                     |
                               compare rects
```

Keep Taffy/Rust completely out of the production library.

Tolerance for floating-point comparison should be explicit and small.

---

## Phase M9 — SDL3 visual playground

Implemented in `0.0.10` under `examples/playground`. SDL3, SDL_ttf and FreeType are
optional consumer dependencies. Ten scenes use public geometry and the M7 host adapter;
keyboard/mouse navigation, overlays, track/item inspection, narrow-canvas controls and
resize handling are available. Text uses an installed monospace font with antialiasing.
Scene contracts and hidden SDL render/event tests complement the core suite.

Create a standalone executable that visualizes layout results.

Required scenes:

1. fixed tracks
2. weighted 1:1
3. weighted 1:2:3
4. fixed + weighted
5. content-sized labels
6. column span
7. row span
8. all alignments
9. range/min constraint
10. nested grid

Controls:

- `1..0`: switch scene
- left/right: previous/next scene
- `G`: toggle grid-line overlay
- `B`: toggle item bounding boxes
- `M`: toggle measurements/labels
- `R`: reset window size
- resizable window: continuous layout recalculation

The demo must use the same public API a real third-party UI integration would use.

---

## Phase M10 — Performance and retained layer

Implemented: separate timing/allocation runs, callback and nested-work counters,
up to 99,856 items, repeatability checks, CSV reports, contiguous ID validation,
and an optional single-entry `GridCache` with zero-allocation, zero-measurement hits.
Scratch/output vector reuse, fixed-axis contribution bypass and immutable-input
validation/span preparation are implemented; warmed unchanged-capacity cache
misses have dedicated zero-allocation checks.
The retained layer and item measurements are implemented in 0.1.0. Dedicated tests
cover targeted invalidation, unrelated subtree reuse and randomized edits.

Only after correctness:

- `std::pmr`: conditional investigation; warmed core-cache allocation checks pass,
  so no allocator API is introduced without a measured need
- implemented: per-item measurement cache, full constraints and content invalidation
- implemented: retained `GridTree`
- implemented: ancestor dirty propagation and unrelated subtree reuse

Do not optimize algorithmic code before golden/differential tests are established.

---

# Version plan

## 0.1

- geometry
- fixed tracks
- weighted tracks
- gap/padding
- explicit placement
- SDL3 demo with first four scenes

## 0.2

- alignment
- measurement
- content tracks
- demo inspector overlay

## 0.3

- spans
- ranged tracks
- regression suite

## 0.4

- nested grids
- randomized tests
- differential testing against Taffy common subset

## 0.5

- API stabilization
- sanitizer CI
- benchmark suite

## 1.0

Production-ready core:

- Fixed / Weighted / Content / Range
- row/column spans
- gap/padding/margin
- Start/Center/End/Stretch
- measurement callback
- nested integration
- optional rounding
- C++20 / STL-only
- MSVC / Clang / GCC CI

## 1.1+

Originally scheduled for later evaluation; implemented in 0.1.0:

- auto-placement
- implicit tracks
- measurement cache/dirty tree
- percentage tracks
- baseline alignment
