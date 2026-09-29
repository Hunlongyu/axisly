"""Mixed-container aspect-ratio constraints against Taffy 0.14."""

import argparse
import itertools
import json
import subprocess

from grid_detail_compare import compare_grid_details
from test_flex_matrix import compare


DISPLAYS = ("Block", "Flex", "Grid")


def make_cases():
    for scenario, parent_display, child_display, known_axis, box_sizing, bound, direction in itertools.product(
        range(3), range(3), range(3), range(2), range(2), range(6), range(2)
    ):
        outer = {
            "display": DISPLAYS[parent_display],
            "direction": "Rtl" if direction else "Ltr",
            "size": {**({"width": 160} if scenario != 2 else {}),
                     **({"height": 120} if scenario != 1 else {})},
            "align_items": "Start", "justify_items": "Start",
        }
        if parent_display == 2:
            outer.update({"grid_template_columns": "120px",
                          "grid_template_rows": "auto"})
        inner = {
            "display": DISPLAYS[child_display],
            "direction": outer["direction"],
            "box_sizing": "BorderBox" if box_sizing == 0 else "ContentBox",
            "aspect_ratio": 2,
            "size": {"width": 60} if known_axis == 0 else {"height": 40},
            "padding": {"left": 10, "right": 10, "top": 5, "bottom": 5},
            "flex_shrink": 0,
            "align_items": "Start", "justify_items": "Start",
        }
        if bound == 1:
            inner["min_size"] = {"width": 80}
        elif bound == 2:
            inner["max_size"] = {"width": 40}
        elif bound == 3:
            inner["min_size"] = {"height": 50}
        elif bound == 4:
            inner["max_size"] = {"height": 20}
        elif bound == 5:
            inner["min_size"] = {"width": 80}
            inner["max_size"] = {"height": 20}
        if child_display == 2:
            inner.update({"grid_template_columns": "auto",
                          "grid_template_rows": "auto"})
        yield {
            "name": f"ratio-matrix-{scenario}-{parent_display}-{child_display}-{known_axis}-{box_sizing}-{bound}-{direction}",
            "available": ["max-content" if scenario == 2 else 160,
                          "max-content" if scenario == 1 else 120],
            "nodes": [
                {"id": 1, "parent": None, "style": outer},
                {"id": 2, "parent": 0, "style": inner},
                {"id": 3, "parent": 1, "style": {},
                 "measure": {"minimum": [40, 30], "preferred": [40, 30]}},
            ],
        }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--axisly", required=True)
    parser.add_argument("--taffy", required=True)
    parser.add_argument("--details", action="store_true")
    args = parser.parse_args()
    cases = list(make_cases())
    protocol = "AXISLY_TREE_GRID_DETAILS_V1" if args.details else "AXISLY_TREE_V1"
    expected_cases = []
    for offset in range(0, len(cases), 512):
        oracle = json.loads(subprocess.run(
            [args.taffy], input=json.dumps({"protocol": protocol,
                                            "cases": cases[offset:offset + 512]}),
            text=True, capture_output=True, check=True,
        ).stdout)
        assert oracle["taffy"] == "0.14.0"
        expected_cases.extend(oracle["cases"])
    native = json.loads(subprocess.run(
        [args.axisly] + (["--details"] if args.details else []),
        text=True, capture_output=True, check=True,
    ).stdout)
    assert native["protocol"] == "AXISLY_RATIO_MATRIX_V1"
    assert len(native["cases"]) == len(expected_cases) == len(cases) == 1296
    failures = []
    for actual, expected in zip(native["cases"], expected_cases, strict=True):
        try:
            compare(actual, expected)
            if args.details:
                compare_grid_details(actual, expected)
        except AssertionError as error:
            failures.append(error.args[0])
    assert not failures, (len(failures), failures[:12])


if __name__ == "__main__":
    main()
