"""Grid spanning contributions across fixed and intrinsic container queries."""

import argparse
import itertools
import json
import subprocess

from test_flex_matrix import compare
from test_grid_matrix import TRACK_PAIRS
from grid_detail_compare import compare_grid_details


def make_cases():
    for scenario, columns, rows, mode, rtl in itertools.product(
        range(4), range(len(TRACK_PAIRS)), range(len(TRACK_PAIRS)), range(2), range(2)
    ):
        name = f"grid-span-matrix-{scenario}-{columns}-{rows}-{mode}-{rtl}"
        style = {
            "display": "Grid", "direction": "Rtl" if rtl else "Ltr",
            "padding": {"left": 5, "top": 5, "right": 5, "bottom": 5},
            "gap": {"width": 4, "height": 6},
            "grid_template_columns": TRACK_PAIRS[columns],
            "grid_template_rows": TRACK_PAIRS[rows],
            "justify_items": "Start", "align_items": "Start",
        }
        if scenario == 0:
            style["size"] = {"width": 120, "height": 90}
        if mode == 0:
            sizes = ((70, 20), (25, 30), (15, 15))
            placements = (((1, 3), (1, 2)), ((1, 2), (2, 3)),
                          ((2, 3), (2, 3)))
        else:
            sizes = ((20, 70), (30, 25), (15, 15))
            placements = (((1, 2), (1, 3)), ((2, 3), (1, 2)),
                          ((2, 3), (2, 3)))
        nodes = [{"id": 1, "parent": None, "style": style}]
        for index, ((width, height), (column, row)) in enumerate(
            zip(sizes, placements, strict=True)
        ):
            nodes.append({"id": index + 2, "parent": 0, "style": {
                "size": {"width": width, "height": height},
                "grid_column": {"start": str(column[0]), "end": str(column[1])},
                "grid_row": {"start": str(row[0]), "end": str(row[1])},
            }})
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
    protocol = "AXISLY_TREE_GRID_DETAILS_V1" if args.details else "AXISLY_TREE_V1"
    oracle_cases = []
    for start in range(0, len(cases), 512):
        oracle = json.loads(subprocess.run(
            [args.taffy], input=json.dumps({"protocol": protocol,
                                           "cases": cases[start:start + 512]}),
            text=True, capture_output=True, check=True
        ).stdout)
        assert oracle["taffy"] == "0.14.0"
        oracle_cases.extend(oracle["cases"])
    native = json.loads(subprocess.run(
        [args.axisly, "--span"] + (["--details"] if args.details else []),
        text=True, capture_output=True, check=True
    ).stdout)
    assert native["protocol"] == "AXISLY_GRID_SPAN_MATRIX_V1"
    assert len(oracle_cases) == len(native["cases"]) == len(cases) == 1296
    failures = []
    for actual, expected in zip(native["cases"], oracle_cases, strict=True):
        try:
            compare(actual, expected)
            if args.details:
                compare_grid_details(actual, expected)
        except AssertionError as error:
            failures.append(error.args[0])
    groups = sorted({tuple(failure[0].split("-")[3:7]) for failure in failures})
    assert not failures, (len(failures), groups, failures[:4])


if __name__ == "__main__":
    main()
