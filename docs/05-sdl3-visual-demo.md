# 05 — SDL3 Visual Demo

Implemented in M9. Build instructions and validation details are in
[M9 SDL3 playground](17-sdl3-playground.md).

SDL3 is a consumer of axisly. The installed core remains C++20 / STL-only;
neither SDL nor font types enter its public headers or link interface.

## Scenes

The scene list follows the M9 roadmap. Column and row spans are separate scenes;
the old proposed stress scene belongs to the M10 benchmark work.

1. Fixed: columns 140 / 220 / 100; rows 80 / 120.
2. Weighted 1:1.
3. Weighted 1:2:3.
4. Fixed 180 + Weighted 1:2.
5. Content: synthetic labels and final-width remeasurement.
6. Column span: a header across three columns.
7. Row span: a sidebar across two rows.
8. All 16 independent horizontal/vertical alignment combinations.
9. Range: 120..220, Fixed 160..Weighted, and an unconstrained weighted track.
10. Nested: host-owned root and child grids, scoped paths and translated geometry.

## Interface

A header contains clickable overlay toggles. The left navigation shows ten scenes
and keyboard shortcuts. The canvas shows cell outlines, colored allocated items,
and a gold preferred-size footprint clipped to the item. Full preferred dimensions
remain available in the inspector. A green outline identifies the inspected item.

Below the canvas, root track sizes and item details expose public layout results.
Every root track has a nonspanning item, so its public cell rectangle supplies the
track diagnostic without adding probe items or calling internal sizing functions.
The status line shows available -> occupied size, last host layout time, measurement
count (M), layout call count (L), and whether host clipping hides overflowing content.

Font rendering uses SDL_ttf with local monospace fonts (Consolas by default on
Windows), antialiasing, a bounded texture cache and output-density-aware rasterization.
Scene 5 keeps synthetic measurements so sizing is independent of the display font.

## Interaction

| Input | Action |
| --- | --- |
| 1..0 / scene click | Direct scene selection |
| Left / Right | Previous / next, wrapping |
| G / B / M / header click | Cell lines / item borders / measurements |
| Hover / Up / Down | Inspect item, descendants take hover precedence |
| [ / ] | Canvas width from 10% through 100% |
| Resize window | Recompute with current canvas dimensions |
| R | Restore 1280 x 840 window and full canvas width |
| Esc / window close | Exit |

The minimum window size is 960 x 720. Narrower layout constraints can be explored
with the canvas-width controls while the inspector remains readable. Layout runs
when the scene or container changes; idle event waiting avoids continuous busy drawing.
Initialization/rendering errors produce a visible dialog in interactive mode.
