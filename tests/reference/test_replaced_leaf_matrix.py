"""Measured replaced-leaf differential matrix across container algorithms."""

import argparse
import collections
import itertools
import json
import subprocess

from grid_detail_compare import compare_grid_details
from test_flex_matrix import compare


DISPLAYS = ("Block", "Flex", "Grid")


def make_cases():
    for parent, replaced, ratio, box, sized_axis, bound, alignment, direction in itertools.product(
        range(3), range(2), range(2), range(2), range(3), range(5), range(2), range(2)
    ):
        root_style = {
            "display": DISPLAYS[parent],
            "direction": "Rtl" if direction else "Ltr",
            "size": {"width": 120, "height": 100},
        }
        if alignment:
            root_style.update({"align_items": "Start", "justify_items": "Start"})
        if parent == 2:
            root_style.update({"grid_template_columns": "auto",
                               "grid_template_rows": "auto"})
        leaf_style = {
            "display": "Block",
            "item_is_replaced": bool(replaced),
            "direction": root_style["direction"],
            "box_sizing": "ContentBox" if box else "BorderBox",
            "size": {"width": 36} if sized_axis == 1 else
                    {"height": 24} if sized_axis == 2 else {},
            "padding": {"left": 4, "right": 4, "top": 3, "bottom": 3},
            "border": {edge: 1 for edge in ("left", "top", "right", "bottom")},
        }
        if ratio:
            leaf_style["aspect_ratio"] = 2
        if bound == 1:
            leaf_style["min_size"] = {"width": 50}
        elif bound == 2:
            leaf_style["max_size"] = {"width": 30}
        elif bound == 3:
            leaf_style["min_size"] = {"height": 26}
        elif bound == 4:
            leaf_style["max_size"] = {"height": 16}
        yield {
            "name": f"replaced-leaf-matrix-{parent}-{replaced}-{ratio}-{box}-"
                    f"{sized_axis}-{bound}-{alignment}-{direction}",
            "available": [120, 100],
            "nodes": [
                {"id": 1, "parent": None, "style": root_style},
                {"id": 2, "parent": 0, "style": leaf_style,
                 "measure": {"minimum": [20, 10], "preferred": [40, 10]}},
            ],
        }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--axisly", required=True)
    parser.add_argument("--taffy", required=True)
    args = parser.parse_args()
    cases = list(make_cases())
    assert len(cases) == 1440
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
    assert native["protocol"] == "AXISLY_REPLACED_LEAF_MATRIX_V1"
    assert len(native["cases"]) == len(oracle_cases) == len(cases)
    by_name = {case["name"]: case for case in native["cases"]}
    for suffix, size in (
        ("0-0-1-0-0-0-0", [120, 60]),
        ("0-1-1-0-0-0-0", [50, 25]),
        ("1-0-1-0-0-2-0", [30, 100]),
        ("1-0-1-1-0-2-0", [40, 100]),
        ("2-0-1-0-0-0-0", [200, 100]),
        ("2-0-1-0-0-2-1", [30, 15]),
        ("2-0-1-0-0-4-1", [32, 16]),
        ("2-0-1-0-1-1-1", [50, 18]),
    ):
        for direction in range(2):
            actual = by_name[f"replaced-leaf-matrix-{suffix}-{direction}"]
            assert actual["nodes"][1]["rect"][2:] == size, (actual["name"], size)
    grid = by_name["replaced-leaf-matrix-2-0-1-0-0-0-0-0"]["nodes"][0]["grid"]
    assert grid["columns"]["positions"] == [[0, 200]]
    assert grid["rows"]["positions"] == [[0, 100]]
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
