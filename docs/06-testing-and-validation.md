# 06 — Testing and Validation

## Testing priority

For a layout engine, correctness is more important than code volume.

Recommended effort distribution:

```text
algorithm implementation       ~35%
tests/reference validation     ~45%
benchmarks/tooling             ~20%
```

## Unit tests

Separate by behavior:

```text
tests/
  fixed/
  weighted/
  content/
  range/
  spans/
  alignment/
  measurement/
  nested/
  regression/
```

Never make visual SDL output the only test.

M9 adds `gridly.playground.scenes` (50 scene/container contracts plus semantic checks)
and `gridly.playground.runtime` (30 rendered scene/size combinations and scripted SDL
events). These exist only with `GRIDLY_BUILD_PLAYGROUND=ON`. CTest uses a hidden dummy
video driver and software renderer; running `gridly_playground --smoke-test <directory>`
without those driver overrides exercises the native hidden window/renderer. Saved PNGs
are inspected separately; a render pass is not a pixel-perfect golden-image comparison.

## Golden tests

Every fixed bug becomes a permanent test case containing:

- GridSpec
- items
- measurement responses
- expected rectangles

## Invariants

Random tests should assert:

- no NaN
- no infinity
- no negative final width/height unless explicitly supported
- stable deterministic output
- no invalid ItemId returned
- alignment result remains inside the cell unless overflow is intentionally permitted

## Differential testing

M8 implements this as optional tooling in `tests/reference`, with `reference-debug`
and `reference-release` CMake presets. The default suite remains dependency-free C++.
See [M8 reference validation](16-reference-validation.md) for the enforced common
subset, pinned version, tolerance, fixtures, replay commands and current local results.

Use Taffy only in developer tooling.

Map the feature intersection:

```text
gridly Fixed      -> equivalent fixed length
gridly Weighted   -> equivalent flexible fraction
gridly Content    -> compatible intrinsic sizing case
gridly Span       -> equivalent grid span
```

Compare final rectangles with a documented epsilon.

A mismatch should emit a standalone reproducible test fixture.

## Fuzzing

0.1.0 adds deterministic randomized placement checked against an independent
bitmap oracle, 6,931 shared-edge rounding layouts, randomized retained-tree edits
checked against fresh stateless output, and nested-tree checks against the M7
host adapter. Cache keys, exceptions, reentry, node lifetime, topology, baseline
caps and percentage resize are tested separately. The new features are not
silently added to the older Taffy common-subset eligibility set.

A separate V2 suite now adds 913 default Taffy cases with explicit admission rules,
engine-native placement/track diagnostics, exact placement comparisons and 12
harness checks. The original 780 V1 cases and their replay protocol remain intact.
See [extended validation](24-ci-and-extended-validation.md) for the baseline
MinContent boundary and the sparse spanning-placement regression it found.

The `GRIDLY_ENABLE_SANITIZERS` option and `scripts/build-sanitizers.ps1` instrument
local developer builds. CI has separate Linux Clang and GCC ASan+UBSan jobs.
Exact local results are recorded in [the completion report](22-roadmap-completion.md);
configured remote jobs are not a claim of an executed remote CI run.

Candidate inputs:

- track counts
- track types
- weights
- fixed sizes
- constraints
- gaps/padding
- item positions
- spans
- measurement responses
- container sizes

Use sanitizers in CI:

- ASan
- UBSan where supported
- MSVC runtime checks in debug configurations as useful

## Benchmarks

The optional complete-layout cache has dedicated invalidation, lifetime, failure
recovery and randomized update tests. The separate `gridly_cache_allocations`
executable checks that hits allocate nothing and make no measurement callbacks,
including a 10,000-item grid. `comparison-release` checks every cached rectangle
against the same independent geometry oracle as the stateless and competitor paths.
See [the cache contract and results](20-layout-cache.md).

Workspace reuse additionally checks growing/shrinking content, fixed/mixed axis
transitions, input error priority, second-pass failures and randomized span edits.
Allocation probes cover warmed resize, content-version changes, explicit
invalidation and same-capacity item updates for 400/10,000-item grids, comparing
each result to a fresh stateless calculation outside the allocation window.
The comparison runner optionally accepts `--baseline-native` to run a preserved
pre-optimization binary with the same output-retirement timing contract.
See [workspace validation and measurements](21-workspace-optimization.md).

The first implementation is available through `GRIDLY_BUILD_BENCHMARKS` and the
`benchmark-release` / `benchmark-debug` presets. See [M10 benchmark methodology](18-performance-benchmarks.md).
CI runs only correctness smoke tests and allocation-counter contracts; noisy shared-runner timings are not pass/fail gates.
Taffy correctness comparisons do not measure performance. Its upstream Grid bench covers a larger CSS feature set;
matching node counts alone is not an equivalent workload or evidence of relative speed.

Track at least:

- total layout time
- allocations
- number of measure callbacks
- track count
- item count

Scenarios:

- 3x3 settings form
- 20x20 medium grid
- 50x50/100x100 stress grid
- repeated window resizing
- many content-measured items
- many spanning items

Do not benchmark only one synthetic case.
