"""Measured leaves in mixed Flex/Grid trees against pinned Taffy 0.14."""

import argparse
import itertools
import json
import subprocess

from test_flex_matrix import compare
from grid_detail_compare import compare_grid_details


DIRECTIONS = ("Row", "RowReverse", "Column", "ColumnReverse")
WRAPS = ("NoWrap", "Wrap", "WrapReverse")
FLOWS = ("Row", "Column", "RowDense", "ColumnDense")
ITEM_POSITIONS = ("Start", "Center", "End")


def make_cases(scenario):
    for root_kind, root_alignment, direction, wrap, nested_kind, nested_direction, variant, rtl in itertools.product(
        range(2), range(3), range(4), range(3), range(2), range(2), range(2), range(2)
    ):
        row = direction < 2
        width, height = (90, 80) if row else (80, 90)
        root = {
            "display": "Flex" if root_kind == 0 else "Grid",
            "flex_wrap": WRAPS[wrap], "align_items": ITEM_POSITIONS[root_alignment],
            "direction": "Rtl" if rtl else "Ltr",
            "padding": {"left": 3, "top": 3, "right": 3, "bottom": 3},
            "gap": {"width": 4, "height": 6},
        }
        if root_kind == 0:
            root["flex_direction"] = DIRECTIONS[direction]
        else:
            root["grid_auto_flow"] = FLOWS[direction]
            root["justify_items"] = ITEM_POSITIONS[root_alignment]
            root["grid_template_columns"] = (
                "auto auto" if wrap == 0 else "1fr 1fr" if wrap == 1
                else "minmax(min-content, 1fr) max-content"
            )
            root["grid_template_rows"] = "auto auto"
        if scenario == "definite":
            root["size"] = {"width": width, "height": height}
        nested = {
            "display": "Flex" if nested_kind == 0 else "Grid",
            "align_items": "Start", "flex_shrink": 0,
            "padding": {"left": 2, "top": 2, "right": 2, "bottom": 2},
            "gap": {"width": 3, "height": 3},
        }
        if nested_kind == 0:
            nested["flex_direction"] = "Row" if nested_direction == 0 else "Column"
        else:
            nested["grid_template_columns"] = "auto auto" if nested_direction == 0 else "auto"
            nested["grid_template_rows"] = "auto" if nested_direction == 0 else "auto auto"
        if variant == 1:
            nested["flex_grow"] = 1
            nested["flex_basis"] = 20
        nodes = [
            {"id": 1, "parent": None, "style": root},
            {"id": 2, "parent": 0, "style": nested},
            {"id": 3, "parent": 1, "style": {},
             "measure": {"minimum": [10, 8], "preferred": [24, 18]}},
            {"id": 4, "parent": 1, "style": {},
             "measure": {"minimum": [16, 11], "preferred": [29, 21]}},
            {"id": 5, "parent": 0, "style": {"size": {"width": 23, "height": 19},
                                               "flex_shrink": 0}},
            {"id": 6, "parent": 0, "style": {},
             "measure": {"minimum": [12, 9], "preferred": [28, 16]}},
        ]
        prefix = {"definite": "mixed-nested", "max-content": "mixed-nested-max",
                  "min-content": "mixed-nested-min",
                  "auto-definite": "mixed-nested-auto"}[scenario]
        available = [width, height] if scenario in ("definite", "auto-definite") else [scenario, scenario]
        yield {"name": f"{prefix}-{root_kind}-{root_alignment}-{direction}-{wrap}-{nested_kind}-"
                       f"{nested_direction}-{variant}-{rtl}",
               "available": available, "nodes": nodes}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--axisly", required=True)
    parser.add_argument("--taffy", required=True)
    parser.add_argument("--scenario", choices=("definite", "max-content", "min-content", "auto-definite"),
                        default="definite")
    parser.add_argument("--details", action="store_true")
    args = parser.parse_args()
    cases = list(make_cases(args.scenario))
    oracle_cases = []
    protocol = "AXISLY_TREE_GRID_DETAILS_V1" if args.details else "AXISLY_TREE_V1"
    for start in range(0, len(cases), 512):
        oracle = json.loads(subprocess.run(
            [args.taffy], input=json.dumps({"protocol": protocol,
                                      "cases": cases[start:start + 512]}),
            text=True, capture_output=True, check=True,
        ).stdout)
        assert oracle["taffy"] == "0.14.0"
        oracle_cases.extend(oracle["cases"])
    native = json.loads(subprocess.run(
        [args.axisly]
        + ([] if args.scenario == "definite" else ["--" + args.scenario])
        + (["--details"] if args.details else []),
        text=True, capture_output=True, check=True,
    ).stdout)
    assert native["protocol"] == "AXISLY_MIXED_NESTED_V1"
    assert len(oracle_cases) == len(native["cases"]) == len(cases) == 1152
    failures = []
    for actual, expected in zip(native["cases"], oracle_cases, strict=True):
        try:
            compare(actual, expected)
            if args.details:
                compare_grid_details(actual, expected)
        except AssertionError as error:
            failures.append(error.args[0])
    assert not failures, (len(failures), failures[:8])


if __name__ == "__main__":
    main()
