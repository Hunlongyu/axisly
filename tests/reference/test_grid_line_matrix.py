"""Grid line endpoints, repeated names, and automatic placement against Taffy 0.14."""

import argparse
import itertools
import json
import subprocess

from grid_detail_compare import compare_grid_details
from test_flex_matrix import compare


LINES = ("auto", "1", "2", "-1", "x", "2 x", "span 1", "span 2 x",
         "missing", "-1 x", "span 3 x", "span 2 missing")
TEMPLATE = "[x] 20px [x] 20px [x] 20px [x]"


def make_cases(start_line=None):
    starts = range(len(LINES)) if start_line is None else (start_line,)
    for start, end, axis, rtl, flow, second, sizing, auto_tracks in itertools.product(
        starts, range(len(LINES)), range(2), range(2), range(4), range(4),
        range(2), range(2)
    ):
        root = {
            "display": "Grid", "size": {"width": 100, "height": 100},
            "padding": {"left": 5, "top": 5, "right": 5, "bottom": 5},
            "gap": {"width": 3, "height": 3},
            "direction": "Rtl" if rtl else "Ltr",
            "grid_auto_flow": ("Row", "Column", "RowDense", "ColumnDense")[flow],
            "grid_template_columns": TEMPLATE,
            "grid_template_rows": TEMPLATE,
            "justify_items": "Stretch" if sizing else "Start",
            "align_items": "Stretch" if sizing else "Start",
        }
        if auto_tracks:
            root["grid_auto_columns"] = "11px 17px"
            root["grid_auto_rows"] = "13px 29px"
        placement = {"start": LINES[start], "end": LINES[end]}
        item = {"grid_column" if axis == 0 else "grid_row": placement}
        if not sizing:
            item["size"] = {"width": 10, "height": 10}
        second_style = {"size": {"width": 10, "height": 10}}
        primary = "grid_column" if flow in (0, 2) else "grid_row"
        secondary = "grid_row" if flow in (0, 2) else "grid_column"
        if second in (1, 3):
            second_style[secondary] = {"start": "2", "end": "auto"}
        if second in (2, 3):
            second_style[primary] = {"start": "2", "end": "auto"}
        nodes = [
            {"id": 1, "parent": None, "style": root},
            {"id": 2, "parent": 0, "style": item},
            {"id": 3, "parent": 0, "style": second_style},
            {"id": 4, "parent": 0, "style": {"size": {"width": 10, "height": 10}}},
        ]
        yield {"name": f"grid-line-matrix-{start}-{end}-{axis}-{rtl}-{flow}-{second}-{sizing}-{auto_tracks}",
               "available": [100, 100], "nodes": nodes}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--axisly", required=True)
    parser.add_argument("--taffy", required=True)
    parser.add_argument("--start-line", type=int)
    parser.add_argument("--details", action="store_true")
    args = parser.parse_args()
    if args.start_line is not None and not 0 <= args.start_line < len(LINES):
        parser.error("--start-line must name an available line endpoint")
    cases = list(make_cases(args.start_line))
    expected_cases = []
    protocol = "AXISLY_TREE_GRID_DETAILS_V1" if args.details else "AXISLY_TREE_V1"
    for offset in range(0, len(cases), 1024):
        oracle = json.loads(subprocess.run(
            [args.taffy], input=json.dumps({
                "protocol": protocol, "cases": cases[offset:offset + 1024]}),
            text=True, capture_output=True, check=True,
        ).stdout)
        assert oracle["taffy"] == "0.14.0"
        assert oracle["protocol"] == protocol
        expected_cases.extend(oracle["cases"])
    native = json.loads(subprocess.run(
        [args.axisly] + ([] if args.start_line is None
                         else ["--start-line", str(args.start_line)])
        + (["--details"] if args.details else []),
        text=True, capture_output=True, check=True,
    ).stdout)
    assert native["protocol"] == "AXISLY_GRID_LINE_MATRIX_V1"
    expected_count = 36864 if args.start_line is None else 3072
    assert len(expected_cases) == len(native["cases"]) == len(cases) == expected_count
    failures = []
    for actual, expected in zip(native["cases"], expected_cases, strict=True):
        try:
            compare(actual, expected)
            if args.details:
                compare_grid_details(actual, expected)
        except AssertionError as error:
            failures.append(error.args[0])
    assert not failures, (len(failures), failures[:8])


if __name__ == "__main__":
    main()
