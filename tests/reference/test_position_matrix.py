"""Absolute positioning across Block, Flex and Grid containing blocks."""

import argparse
import collections
import itertools
import json
import subprocess

from grid_detail_compare import compare_grid_details
from test_flex_matrix import compare


DISPLAY_MODES = (
    "Block", "FlowRoot", "Row", "RowReverse", "Column", "Grid",
    "ColumnReverse", "RowWrapReverse", "ColumnWrapReverse",
)
ALIGNMENTS = ("Start", "Center", "End")
POSITION_CASES = range(15)


def positioned_style(scenario, child_rtl):
    style = {
        "display": "Block", "position": "Absolute",
        "direction": "Rtl" if child_rtl else "Ltr",
        "size": {"width": 20, "height": 10},
    }
    if scenario in (1, 5):
        style["inset"] = {"left": "25%" if scenario == 5 else 10,
                          "top": "50%" if scenario == 5 else 5}
        style["margin"] = {"left": 3, "top": 4}
    elif scenario in (2, 6):
        style["inset"] = {"right": "10%" if scenario == 6 else 10,
                          "bottom": "25%" if scenario == 6 else 5}
        style["margin"] = {"right": 5, "bottom": 6}
    elif scenario in (3, 4, 7, 8, 9):
        style["inset"] = {"left": 10, "right": 20, "top": 5, "bottom": 15}
        if scenario == 4:
            del style["size"]
        elif scenario == 7:
            style["margin"] = {edge: "auto" for edge in
                               ("left", "top", "right", "bottom")}
        elif scenario == 8:
            style["margin"] = {"left": "auto", "top": "auto"}
        elif scenario == 9:
            style["margin"] = {"left": -3, "top": -4,
                               "right": -5, "bottom": -6}
    elif scenario == 10:
        del style["size"]
        style["margin"] = {"left": 3, "top": 4}
    elif scenario in (11, 12):
        style["grid_column"] = {"start": "2", "end": "3"}
        style["grid_row"] = {"start": "2", "end": "3"}
        if scenario == 12:
            del style["size"]
            style["inset"] = {"left": "10%", "top": "25%"}
    elif scenario in (13, 14):
        style["margin"] = {edge: "auto" for edge in
                           ("left", "top", "right", "bottom")}
        if scenario == 14:
            style["inset"] = {"left": 10, "top": 5}
    return style


def make_cases():
    for mode, alignment, parent_rtl, child_rtl, scenario in itertools.product(
        range(len(DISPLAY_MODES)), range(len(ALIGNMENTS)), range(2), range(2),
        POSITION_CASES,
    ):
        if mode != 5 and scenario in (11, 12):
            continue
        display = DISPLAY_MODES[mode]
        parent = {
            "display": display if mode < 2 or mode == 5 else "Flex",
            "position": "Relative",
            "direction": "Rtl" if parent_rtl else "Ltr",
            "size": {"width": 120, "height": 80},
            "padding": {"left": 5, "top": 7, "right": 6, "bottom": 8},
            "border": {edge: 2 for edge in ("left", "top", "right", "bottom")},
            "gap": {"width": 4, "height": 3},
            "align_items": ALIGNMENTS[alignment],
            "justify_items": ALIGNMENTS[alignment],
            "align_content": ALIGNMENTS[alignment],
            "justify_content": ALIGNMENTS[alignment],
        }
        if mode >= 2 and mode != 5:
            parent["flex_direction"] = (
                "Row" if mode == 7 else "Column" if mode == 8 else display)
            if mode >= 7:
                parent["flex_wrap"] = "WrapReverse"
        if mode == 5:
            parent["grid_template_columns"] = "30px 40px"
            parent["grid_template_rows"] = "20px 20px"
        nodes = [
            {"id": 1, "parent": None, "style": parent},
            {"id": 2, "parent": 0, "style": {
                "display": "Block", "size": {"width": 25, "height": 12},
                "flex_shrink": 0}},
            {"id": 3, "parent": 0,
             "style": positioned_style(scenario, child_rtl)},
            {"id": 4, "parent": 2, "style": {
                "display": "Block", "size": {"width": 12, "height": 6}}},
            {"id": 5, "parent": 0, "style": {
                "display": "Block", "size": {"width": 20, "height": 9},
                "flex_shrink": 0}},
        ]
        yield {
            "name": f"position-matrix-{mode}-{alignment}-{parent_rtl}-"
                    f"{child_rtl}-{scenario}",
            "available": [120, 80], "nodes": nodes,
        }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--axisly", required=True)
    parser.add_argument("--taffy", required=True)
    args = parser.parse_args()
    cases = list(make_cases())
    expected = []
    for offset in range(0, len(cases), 512):
        result = subprocess.run([args.taffy], input=json.dumps({
            "protocol": "AXISLY_TREE_GRID_DETAILS_V1",
            "cases": cases[offset:offset + 512],
        }), text=True, capture_output=True, check=True)
        oracle = json.loads(result.stdout)
        assert oracle["taffy"] == "0.14.0"
        expected.extend(oracle["cases"])
    native = json.loads(subprocess.run(
        [args.axisly], text=True, capture_output=True, check=True,
    ).stdout)
    assert native["protocol"] == "AXISLY_POSITION_MATRIX_V1"
    assert len(cases) == len(expected) == len(native["cases"]) == 1428
    failures = []
    for actual, oracle in zip(native["cases"], expected, strict=True):
        try:
            compare(actual, oracle)
            compare_grid_details(actual, oracle)
        except AssertionError as error:
            failures.append(error.args[0])
    groups = collections.Counter((failure[0].split("-")[2],
                                  failure[0].split("-")[-1], failure[1], failure[2])
                                 for failure in failures)
    assert not failures, (len(failures), groups.most_common(25), failures[:16])


if __name__ == "__main__":
    main()
