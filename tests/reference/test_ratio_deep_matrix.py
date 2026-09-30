"""Four-level aspect-ratio, percentage and scrollbar differential matrix."""

import argparse
import collections
import itertools
import json
import subprocess

from grid_detail_compare import compare_grid_details
from test_flex_matrix import compare


DISPLAYS = ("Block", "Flex", "Grid")


def make_cases():
    for root, middle, inner, known, percentage, box, overflow, direction in itertools.product(
        range(3), range(3), range(3), range(3), range(2), range(2), range(4), range(2)
    ):
        root_style = {
            "display": DISPLAYS[root], "size": {"width": 200, "height": 160},
            "padding": {edge: 4 for edge in ("left", "top", "right", "bottom")},
            "align_items": "Start", "justify_items": "Start",
        }
        if root == 2:
            root_style.update({"grid_template_columns": "150px",
                               "grid_template_rows": "auto"})
        middle_style = {
            "display": DISPLAYS[middle],
            "direction": "Rtl" if direction else "Ltr",
            "box_sizing": "ContentBox" if box else "BorderBox",
            "aspect_ratio": 1.5,
            "size": {"width": 80} if known == 0 else {"height": 60} if known == 1 else {},
            "padding": {"left": 7, "right": 7, "top": 5, "bottom": 5},
            "flex_shrink": 0, "align_items": "Start", "justify_items": "Start",
            "scrollbar_width": 8,
        }
        if overflow:
            middle_style["overflow"] = {
                "x": "Scroll" if overflow in (1, 3) else "Visible",
                "y": "Scroll" if overflow in (2, 3) else "Visible",
            }
        if middle == 2:
            middle_style.update({"grid_template_columns": "auto",
                                 "grid_template_rows": "auto"})
        inner_style = {
            "display": DISPLAYS[inner], "aspect_ratio": 2,
            "size": {"width": "50%"} if percentage == 0 else {"height": "50%"},
            "padding": {edge: 3 for edge in ("left", "top", "right", "bottom")},
            "flex_shrink": 0, "align_items": "Start", "justify_items": "Start",
        }
        if inner == 2:
            inner_style.update({"grid_template_columns": "auto",
                                "grid_template_rows": "auto"})
        yield {
            "name": f"ratio-deep-matrix-{root}-{middle}-{inner}-{known}-"
                    f"{percentage}-{box}-{overflow}-{direction}",
            "available": [200, 160],
            "nodes": [
                {"id": 1, "parent": None, "style": root_style},
                {"id": 2, "parent": 0, "style": middle_style},
                {"id": 3, "parent": 1, "style": inner_style},
                {"id": 4, "parent": 2, "style": {},
                 "measure": {"minimum": [42, 28], "preferred": [42, 28]}},
            ],
        }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--axisly", required=True)
    parser.add_argument("--taffy", required=True)
    args = parser.parse_args()
    cases = list(make_cases())
    assert len(cases) == 2592
    oracle_cases = []
    for start in range(0, len(cases), 384):
        oracle = json.loads(subprocess.run(
            [args.taffy], input=json.dumps({
                "protocol": "AXISLY_TREE_GRID_DETAILS_V1",
                "cases": cases[start:start + 384],
            }), text=True, capture_output=True, check=True,
        ).stdout)
        assert oracle["taffy"] == "0.14.0"
        oracle_cases.extend(oracle["cases"])
    native = json.loads(subprocess.run(
        [args.axisly], text=True, capture_output=True, check=True,
    ).stdout)
    assert native["protocol"] == "AXISLY_RATIO_DEEP_MATRIX_V1"
    assert len(native["cases"]) == len(oracle_cases) == len(cases)
    failures = []
    for actual, expected in zip(native["cases"], oracle_cases, strict=True):
        try:
            compare(actual, expected)
            compare_grid_details(actual, expected)
        except AssertionError as error:
            failures.append(error.args[0])
    groups = collections.Counter((failure[0].split("-")[3],
                                  failure[1], failure[2])
                                 for failure in failures)
    assert not failures, (len(failures), groups.most_common(20), failures[:12])


if __name__ == "__main__":
    main()
