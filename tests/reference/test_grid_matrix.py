"""Deterministic Grid track, item alignment and direction differential."""

import argparse
import itertools
import json
import subprocess

from test_flex_matrix import compare
from grid_detail_compare import compare_grid_details


TRACK_PAIRS = (
    "30px 1fr", "1fr 2fr", "auto auto",
    "minmax(10px, 1fr) fit-content(50px)",
    "0fr 1fr", "0.2fr 0.2fr", "0.5fr 1fr",
    "20% 30%", "minmax(10%, 40%) minmax(5px, max-content)",
)
POSITIONS = ("Start", "End", "Center")
ITEM_SIZES = ((15, 10), (30, 20), (25, 25), (10, 15))


def make_cases():
    for scenario, columns, rows, justify, align, rtl in itertools.product(
        range(4),
        range(len(TRACK_PAIRS)), range(len(TRACK_PAIRS)),
        range(len(POSITIONS)), range(len(POSITIONS)), range(2)
    ):
        name = f"grid-matrix-{scenario}-{columns}-{rows}-{justify}-{align}-{rtl}"
        style = {
            "display": "Grid", "direction": "Rtl" if rtl else "Ltr",
            "padding": {"left": 5, "top": 5, "right": 5, "bottom": 5},
            "gap": {"width": 4, "height": 6},
            "grid_template_columns": TRACK_PAIRS[columns],
            "grid_template_rows": TRACK_PAIRS[rows],
            "justify_items": POSITIONS[justify],
            "align_items": POSITIONS[align],
        }
        if scenario == 0:
            style["size"] = {"width": 120, "height": 90}
        nodes = [{"id": 1, "parent": None, "style": style}]
        nodes.extend({
            "id": index + 2, "parent": 0,
            "style": {"size": {"width": width, "height": height}},
        } for index, (width, height) in enumerate(ITEM_SIZES))
        available = [120, 90] if scenario in (0, 3) else (
            ["max-content", "max-content"] if scenario == 1
            else ["min-content", "min-content"]
        )
        yield {"name": name, "available": available, "nodes": nodes}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--axisly", required=True)
    parser.add_argument("--taffy", required=True)
    parser.add_argument("--details", action="store_true")
    args = parser.parse_args()
    cases = list(make_cases())
    expected = []
    protocol = "AXISLY_TREE_GRID_DETAILS_V1" if args.details else "AXISLY_TREE_V1"
    for start in range(0, len(cases), 1024):
        fixture = json.dumps({"protocol": protocol,
                              "cases": cases[start:start + 1024]})
        oracle = json.loads(subprocess.run(
            [args.taffy], input=fixture, text=True, capture_output=True, check=True
        ).stdout)
        assert oracle["taffy"] == "0.14.0"
        expected.extend(oracle["cases"])
    native = json.loads(subprocess.run(
        [args.axisly] + (["--details"] if args.details else []),
        text=True, capture_output=True, check=True
    ).stdout)
    assert native["protocol"] == "AXISLY_GRID_MATRIX_V1"
    assert len(expected) == len(native["cases"]) == len(cases) == 5832
    failures = []
    for actual, expected_case in zip(native["cases"], expected, strict=True):
        try:
            compare(actual, expected_case)
            if args.details:
                compare_grid_details(actual, expected_case)
        except AssertionError as error:
            failures.append(error.args[0])
    groups = sorted({tuple(failure[0].split("-")[2:5]) for failure in failures})
    assert not failures, (len(failures), groups, failures[:4])


if __name__ == "__main__":
    main()
