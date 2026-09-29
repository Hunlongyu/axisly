"""Intrinsic width keywords across Block, Flex and Grid against Taffy 0.14."""

import argparse
import itertools
import json
import subprocess

from grid_detail_compare import compare_grid_details
from test_flex_matrix import compare


DISPLAYS = ("Block", "Flex", "Grid")
WIDTHS = ("min-content", "max-content", "fit-content",
          "fit-content(15px)", "fit-content(55px)",
          "fit-content(95px)", "fit-content(50%)")
AVAILABLE = (160, "max-content", "min-content")


def make_cases():
    for axis, display, dimension, query, direction in itertools.product(
        range(2), range(3), range(7), range(3), range(2)
    ):
        main, cross = ("width", "height") if axis == 0 else ("height", "width")
        start, end = ("left", "right") if axis == 0 else ("top", "bottom")
        root = {
            "display": DISPLAYS[display],
            "size": {main: WIDTHS[dimension], cross: 60},
            "direction": "Rtl" if direction else "Ltr",
            "padding": {start: 4, end: 4},
            "gap": {main: 4},
            "align_items": "Start", "justify_items": "Start",
        }
        if axis == 1:
            root["flex_direction"] = "Column"
        if display == 2:
            root.update({"grid_template_columns" if axis == 0 else "grid_template_rows":
                             "auto auto",
                         "grid_template_rows" if axis == 0 else "grid_template_columns":
                             "auto"})
        nodes = [{"id": 1, "parent": None, "style": root}]
        for index, (minimum, maximum) in enumerate(((20, 60), (30, 40))):
            measured_min = [minimum, 12] if axis == 0 else [12, minimum]
            measured_max = [maximum, 12] if axis == 0 else [12, maximum]
            nodes.append({
                "id": index + 2, "parent": 0,
                "style": {"size": {cross: 12}, "flex_shrink": 0},
                "measure": {"minimum": measured_min,
                            "preferred": measured_max},
            })
        yield {
            "name": f"intrinsic-size-{axis}-{display}-{dimension}-{query}-{direction}",
            "available": [AVAILABLE[query], 60] if axis == 0
                         else [60, AVAILABLE[query]], "nodes": nodes,
        }
    for axis, parent_display, child_display, dimension, direction, constraint in itertools.product(
        range(2), range(3), range(3), range(7), range(2), range(3)
    ):
        main, cross = ("width", "height") if axis == 0 else ("height", "width")
        start, end = ("left", "right") if axis == 0 else ("top", "bottom")
        outer = {
            "display": DISPLAYS[parent_display],
            "size": {main: 160, cross: 80},
            "direction": "Rtl" if direction else "Ltr",
            "align_items": "Start", "justify_items": "Start",
        }
        if axis == 1:
            outer["flex_direction"] = "Column"
        if parent_display == 2:
            outer.update({"grid_template_columns" if axis == 0 else "grid_template_rows":
                              "120px",
                          "grid_template_rows" if axis == 0 else "grid_template_columns":
                              "auto"})
        inner = {
            "display": DISPLAYS[child_display],
            "size": {main: WIDTHS[dimension], cross: 60},
            "padding": {start: 4, end: 4},
            "gap": {main: 4}, "flex_shrink": 0,
            "align_items": "Start", "justify_items": "Start",
        }
        if constraint == 1:
            inner["min_size"] = {main: 80}
        elif constraint == 2:
            inner["max_size"] = {main: 70}
        if axis == 1:
            inner["flex_direction"] = "Column"
        if child_display == 2:
            inner.update({"grid_template_columns" if axis == 0 else "grid_template_rows":
                              "auto auto",
                          "grid_template_rows" if axis == 0 else "grid_template_columns":
                              "auto"})
        nodes = [{"id": 1, "parent": None, "style": outer},
                 {"id": 2, "parent": 0, "style": inner}]
        for index, (minimum, maximum) in enumerate(((20, 60), (30, 40))):
            measured_min = [minimum, 12] if axis == 0 else [12, minimum]
            measured_max = [maximum, 12] if axis == 0 else [12, maximum]
            nodes.append({
                "id": index + 3, "parent": 1,
                "style": {"size": {cross: 12}, "flex_shrink": 0},
                "measure": {"minimum": measured_min,
                            "preferred": measured_max},
            })
        yield {
            "name": f"intrinsic-nested-{axis}-{parent_display}-{child_display}-{dimension}-{direction}-{constraint}",
            "available": [160, 80] if axis == 0 else [80, 160], "nodes": nodes,
        }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--axisly", required=True)
    parser.add_argument("--taffy", required=True)
    parser.add_argument("--details", action="store_true")
    args = parser.parse_args()
    cases = list(make_cases())
    protocol = "AXISLY_TREE_GRID_DETAILS_V1" if args.details else "AXISLY_TREE_V1"
    oracle = json.loads(subprocess.run(
        [args.taffy], input=json.dumps({"protocol": protocol, "cases": cases}),
        text=True, capture_output=True, check=True,
    ).stdout)
    assert oracle["taffy"] == "0.14.0"
    native = json.loads(subprocess.run(
        [args.axisly] + (["--details"] if args.details else []),
        text=True, capture_output=True, check=True,
    ).stdout)
    assert native["protocol"] == "AXISLY_INTRINSIC_SIZE_V1"
    assert len(native["cases"]) == len(oracle["cases"]) == len(cases) == 1008
    failures = []
    for actual, expected in zip(native["cases"], oracle["cases"], strict=True):
        try:
            compare(actual, expected)
            if args.details:
                compare_grid_details(actual, expected)
        except AssertionError as error:
            failures.append(error.args[0])
    assert not failures, (len(failures), failures[:12])


if __name__ == "__main__":
    main()
