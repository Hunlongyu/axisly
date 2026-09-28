# 04 — Third-party UI Integration

## Core principle

A third-party UI library should not need to inherit from gridly classes.

Integration is an **adapter**, not a framework dependency.

The host must provide only two operations:

```text
1. measure a child
2. apply the resulting rectangle to a child
```

## Generic adapter interface

Conceptually:

```cpp
class HostAdapter {
public:
    MeasureResult measure(
        ItemId id,
        const MeasureInput& input);

    void apply(
        ItemId id,
        const Rect& rect);
};
```

Layout sequence:

```cpp
auto result = compute_grid(
    spec,
    items,
    container_size,
    measure_callback,
    &adapter);

for (const auto& item : result.items) {
    adapter.apply(item.id, item.rect);
}
```

This means gridly never needs to know whether the item is a `QWidget*`, `HWND`, game entity, or custom widget.

---

# Qt adapter

Possible mapping:

```text
ItemId
  -> QWidget*

measure
  -> QWidget::minimumSizeHint()
  -> QWidget::sizeHint()

apply
  -> QWidget::setGeometry(...)
```

Pseudo code:

```cpp
MeasureResult QtGridAdapter::measure(
    ItemId id,
    const MeasureInput&) {

    QWidget* w = widgets_.at(id);

    const QSize minimum = w->minimumSizeHint();
    const QSize preferred = w->sizeHint();

    return {
        .minimum = {float(minimum.width()), float(minimum.height())},
        .preferred = {float(preferred.width()), float(preferred.height())}
    };
}

void QtGridAdapter::apply(ItemId id, const Rect& r) {
    widgets_.at(id)->setGeometry(
        qRound(r.x), qRound(r.y),
        qRound(r.width), qRound(r.height));
}
```

Qt remains a consumer, not a dependency.

---

# Win32 adapter

Possible mapping:

```text
ItemId
  -> HWND

measure
  -> control-specific ideal size
  -> DirectWrite measurement for custom text
  -> cached desired size

apply
  -> SetWindowPos / MoveWindow
```

Pseudo code:

```cpp
void Win32GridAdapter::apply(ItemId id, const Rect& r) {
    HWND hwnd = handles_.at(id);

    SetWindowPos(
        hwnd,
        nullptr,
        int(std::round(r.x)),
        int(std::round(r.y)),
        int(std::round(r.width)),
        int(std::round(r.height)),
        SWP_NOZORDER | SWP_NOACTIVATE);
}
```

A retained custom Win32 widget system can avoid HWND-per-item entirely and simply store the returned `Rect` in each widget.

---

# Immediate/custom renderer adapter

For SDL/Skia/Direct2D custom UI, `apply()` may be unnecessary.

The renderer can consume `GridResult` directly:

```cpp
for (const auto& layout : result.items) {
    renderer.draw_widget(layout.id, layout.rect);
}
```

---

# Recommended optional C++ convenience layer

Implemented in M7 (`0.0.8`) in `<gridly/layout.hpp>` and available through the umbrella header.
The low-level callback API remains available with its original signature.

The convenience overload accepts a callable returning `MeasureResult` from the two arguments:

```cpp
template<class Measure>
GridResult compute_grid(
    const GridSpec& spec,
    std::span<const GridItem> items,
    Size available,
    Measure&& measure);
```

The callable is borrowed synchronously, never copied or retained. Const callables,
function references, mutable and move-only closures are supported; callable exceptions
propagate unchanged. A successful core call still invokes it twice per item.

Third-party code can use lambdas without manually handling `void*`:

```cpp
auto result = compute_grid(
    grid,
    items,
    size,
    [&](ItemId id, const MeasureInput& input) {
        return adapter.measure(id, input);
    });
```

---

# Integration rule: keep widget lifetime outside gridly

Do not store:

```cpp
QWidget*
HWND
SDL_Texture*
```

inside public core node types.

Instead, keep an adapter-side mapping:

```cpp
std::unordered_map<ItemId, QWidget*> widgets;
```

or use the UI framework's existing stable handle/index as `ItemId`.

This avoids circular ownership and keeps the core reusable.

---

# Nested grids

For nested grids, the adapter can treat a child grid as a measurable item.

```text
Parent Grid
  |
  +-- ordinary widget
  |
  +-- Child Grid
         |
         +-- child widget
```

When the parent measures the child-grid item:

1. call the child's `compute_grid()` with the current constraints
2. map its occupied sizes into minimum/preferred measurements using an explicit host policy
3. after the parent resolves the final child rectangle, recompute/layout the child grid with that exact allocated size

This keeps nesting outside the core algorithm and prevents gridly from becoming a full UI tree framework.

M7 supplies a tested reference implementation in
[`examples/nested_grid_adapter.hpp`](../examples/nested_grid_adapter.hpp), with a runnable
[`nested_grid.cpp`](../examples/nested_grid.cpp) example. This adapter is source-only;
it is not an installed gridly API. Its finite-space probing policy, full-path identities,
ownership rules, error propagation, recursion/work limits and coordinate translation are
specified in [M7 integration semantics](15-nested-grid-integration.md).

Only apply rectangles after the entire nested result succeeds. Keep all referenced
grids, callback contexts and item storage alive and unchanged during calculation;
the core and adapter never own or destroy host widgets.
