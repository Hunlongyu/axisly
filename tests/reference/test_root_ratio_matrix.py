"""Root available-space and aspect-ratio differential matrix."""

import argparse
import collections
import itertools
import json
import subprocess

from grid_detail_compare import compare_grid_details
from test_flex_matrix import compare


DISPLAYS = ("Block", "Flex", "Grid")
AVAILABLE = (
    [200, 160],
    ["max-content", 160],
    [200, "max-content"],
    ["max-content", "max-content"],
    ["min-content", "min-content"],
)


def make_cases():
    for display, available, sized_axis, box, bounds, direction in itertools.product(
        range(3), range(5), range(3), range(2), range(4), range(2)
    ):
        style = {
            "display": DISPLAYS[display],
            "direction": "Rtl" if direction else "Ltr",
            "box_sizing": "ContentBox" if box else "BorderBox",
            "aspect_ratio": 1.5,
            "size": {"width": 80} if sized_axis == 0 else
                    {"height": 60} if sized_axis == 1 else {},
            "padding": {"left": 7, "right": 7, "top": 5, "bottom": 5},
            "border": {"left": 2, "right": 2, "top": 3, "bottom": 3},
            "align_items": "Start", "justify_items": "Start",
        }
        if bounds in (1, 3):
            style["min_size"] = {"width": 100}
        if bounds in (2, 3):
            style["max_size"] = {"height": 40}
        if display == 2:
            style.update({"grid_template_columns": "auto",
                          "grid_template_rows": "auto"})
        yield {
            "name": f"root-ratio-matrix-{display}-{available}-{sized_axis}-"
                    f"{box}-{bounds}-{direction}",
            "available": AVAILABLE[available],
            "nodes": [
                {"id": 1, "parent": None, "style": style},
                {"id": 2, "parent": 0, "style": {},
                 "measure": {"minimum": [42, 28], "preferred": [42, 28]}},
            ],
        }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--axisly", required=True)
    parser.add_argument("--taffy", required=True)
    args = parser.parse_args()
    cases = list(make_cases())
    assert len(cases) == 720
    oracle_cases = []
    for start in range(0, len(cases), 360):
        oracle = json.loads(subprocess.run(
            [args.taffy], input=json.dumps({
                "protocol": "AXISLY_TREE_GRID_DETAILS_V1",
                "cases": cases[start:start + 360],
            }), text=True, capture_output=True, check=True,
        ).stdout)
        assert oracle["taffy"] == "0.14.0"
        oracle_cases.extend(oracle["cases"])
    native = json.loads(subprocess.run(
        [args.axisly], text=True, capture_output=True, check=True,
    ).stdout)
    assert native["protocol"] == "AXISLY_ROOT_RATIO_MATRIX_V1"
    assert len(native["cases"]) == len(oracle_cases) == len(cases)
    by_name = {case["name"]: case for case in native["cases"]}
    for direction in range(2):
        assert by_name[f"root-ratio-matrix-0-0-2-0-2-{direction}"]["nodes"][0]["rect"] == [
            0, 0, 200, 40
        ]
        assert by_name[f"root-ratio-matrix-0-0-2-1-2-{direction}"]["nodes"][0]["rect"] == [
            0, 0, 200, 56
        ]
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
