"""Item alignment across Flex and Grid axes, directions, safety and overflow."""

import argparse
import collections
import itertools
import json
import subprocess

from grid_detail_compare import compare_grid_details
from test_flex_matrix import compare


POSITIONS = (
    "Start", "End", "FlexStart", "FlexEnd", "SelfStart", "SelfEnd",
    "Center", "Baseline", "Stretch",
)
ALIGNMENTS = POSITIONS + tuple("Safe" + value for value in POSITIONS[:7])
FLEX_DIRECTIONS = ("Row", "RowReverse", "Column", "ColumnReverse")


def make_cases():
    for scenario, mode, alignment, parent_rtl, child_rtl in itertools.product(
        range(3), range(10), range(len(ALIGNMENTS)), range(2), range(2)
    ):
        flex = mode < 8
        row = flex and mode % 4 < 2
        parent = {
            "display": "Flex" if flex else "Grid",
            "size": {"width": 120, "height": 100},
            "direction": "Rtl" if parent_rtl else "Ltr",
            "align_items": "End", "justify_items": "End",
        }
        if flex:
            parent.update({
                "flex_direction": FLEX_DIRECTIONS[mode % 4],
                "flex_wrap": "WrapReverse" if mode >= 4 else "NoWrap",
            })
        else:
            parent.update({
                "grid_template_columns": "80px",
                "grid_template_rows": "60px",
                "align_content": "Start", "justify_content": "Start",
            })
        child = {
            "direction": "Rtl" if child_rtl else "Ltr",
            "flex_shrink": 0,
        }
        if flex:
            child["align_self"] = ALIGNMENTS[alignment]
        else:
            child["justify_self" if mode == 8 else "align_self"] = ALIGNMENTS[alignment]
            child["align_self" if mode == 8 else "justify_self"] = "Start"
        if scenario < 2:
            child["size"] = {
                "width": 40 if scenario == 0 else 140,
                "height": 20 if scenario == 0 else 110,
            }
        elif flex:
            child["size"] = {"width": 40} if row else {"height": 20}
        yield {
            "name": f"item-alignment-{scenario}-{mode}-{alignment}-{parent_rtl}-{child_rtl}",
            "available": [120, 100],
            "nodes": [
                {"id": 1, "parent": None, "style": parent},
                {"id": 2, "parent": 0, "style": child,
                 "measure": {"minimum": [40, 20], "preferred": [40, 20]}},
            ],
        }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--axisly", required=True)
    parser.add_argument("--taffy", required=True)
    args = parser.parse_args()
    cases = list(make_cases())
    expected_cases = []
    for offset in range(0, len(cases), 512):
        oracle = json.loads(subprocess.run(
            [args.taffy], input=json.dumps({
                "protocol": "AXISLY_TREE_GRID_DETAILS_V1",
                "cases": cases[offset:offset + 512],
            }), text=True, capture_output=True, check=True,
        ).stdout)
        assert oracle["taffy"] == "0.14.0"
        expected_cases.extend(oracle["cases"])
    native = json.loads(subprocess.run(
        [args.axisly], text=True, capture_output=True, check=True,
    ).stdout)
    assert native["protocol"] == "AXISLY_ITEM_ALIGNMENT_MATRIX_V1"
    assert len(native["cases"]) == len(expected_cases) == len(cases) == 1920
    failures = []
    for actual, expected in zip(native["cases"], expected_cases, strict=True):
        try:
            compare(actual, expected)
            compare_grid_details(actual, expected)
        except AssertionError as error:
            failures.append(error.args[0])
    assert not failures, (len(failures),
                          collections.Counter(str(failure[2]) for failure in failures),
                          failures[:20])


if __name__ == "__main__":
    main()
