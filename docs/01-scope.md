# 01 — Scope and Product Definition

## Product definition

gridly is a standalone native C++ layout algorithm library.

Its only responsibility is:

```text
Grid definition
+ items
+ available container size
+ optional item measurement
              |
              v
       layout computation
              |
              v
      x / y / width / height
```

It is not a browser engine and should not expose CSS syntax.

## V1 public concepts

- `Unit` — layout scalar, initially `float`
- `Size`, `Rect`, `Insets`
- `Track`
  - Fixed
  - Weighted
  - Content
  - Range
- `GridSpec`
  - columns
  - rows
  - gaps
  - padding
- `GridItem`
  - row / column
  - row span / column span
  - margin
  - horizontal / vertical alignment
- measurement callback
- `GridResult`

## Explicit non-goals

V1 must not contain:

- CSS parsing
- CSS property names
- string grammar such as `"200px 1fr auto"`
- stylesheet cascade
- selectors
- DOM
- Flexbox
- Block layout
- media queries
- CSS variables
- `calc()`
- subgrid
- masonry
- browser writing modes
- complete browser baseline semantics

## API naming policy

Use native layout terminology instead of CSS terminology.

Preferred:

```cpp
Track::fixed(180)
Track::weighted(1)
Track::content()
Track::range(...)
```

Avoid exposing:

```text
1fr
min-content
max-content
grid-template-columns
```

Internally, CSS Grid and Taffy can still be used as algorithmic references.

## V1 feature boundary

Recommended V1.0:

- explicit rows and columns
- fixed tracks
- weighted tracks
- content tracks
- ranged tracks
- row/column span
- gaps
- padding
- margins
- Start / Center / End / Stretch
- measurement callback
- nested-grid support through recursive measurement in the host adapter
- optional final pixel rounding
- optional complete-layout `GridCache` (implemented in 0.0.12; see [cache contract](20-layout-cache.md))

Originally deferred to 1.1+, now implemented in 0.1.0 at the user's request:

- automatic placement
- implicit tracks
- retained tree and per-item measurement cache
- dirty propagation
- percentage tracks
- baseline alignment

See [implemented semantics and validation](22-roadmap-completion.md). Percentage
bounds also work inside Range. Baseline support means a native first-baseline
group in the starting row, not all browser writing-mode/baseline behavior.
The feature-version headings in the original roadmap are milestones, not release
numbers or a claim that a production 1.0 release has already shipped.
