"""Measured leaves inside nested Flex containers against pinned Taffy 0.14."""

import argparse
import itertools
import json
import subprocess

from test_flex_matrix import compare


DIRECTIONS = ("Row", "RowReverse", "Column", "ColumnReverse")
WRAPS = ("NoWrap", "Wrap", "WrapReverse")


def make_cases(scenario):
    for direction, wrap, nested_direction, variant, rtl in itertools.product(
        range(4), range(3), range(2), range(2), range(2)
    ):
        row = direction < 2
        width, height = (90, 80) if row else (80, 90)
        root = {
            "display": "Flex", "flex_direction": DIRECTIONS[direction],
            "flex_wrap": WRAPS[wrap], "align_items": "Start",
            "direction": "Rtl" if rtl else "Ltr",
            "padding": {"left": 3, "top": 3, "right": 3, "bottom": 3},
            "gap": {"width": 4, "height": 6},
        }
        if scenario == "definite":
            root["size"] = {"width": width, "height": height}
        nested = {
            "display": "Flex",
            "flex_direction": "Row" if nested_direction == 0 else "Column",
            "align_items": "Start", "flex_shrink": 0,
            "padding": {"left": 2, "top": 2, "right": 2, "bottom": 2},
            "gap": {"width": 3, "height": 3},
        }
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
        prefix = {"definite": "flex-nested", "max-content": "flex-nested-max",
                  "min-content": "flex-nested-min"}[scenario]
        available = [width, height] if scenario == "definite" else [scenario, scenario]
        yield {"name": f"{prefix}-{direction}-{wrap}-{nested_direction}-{variant}-{rtl}",
               "available": available, "nodes": nodes}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--axisly", required=True)
    parser.add_argument("--taffy", required=True)
    parser.add_argument("--scenario", choices=("definite", "max-content", "min-content"),
                        default="definite")
    args = parser.parse_args()
    cases = list(make_cases(args.scenario))
    oracle = json.loads(subprocess.run(
        [args.taffy], input=json.dumps({"protocol": "AXISLY_TREE_V1", "cases": cases}),
        text=True, capture_output=True, check=True,
    ).stdout)
    native = json.loads(subprocess.run(
        [args.axisly] + ([] if args.scenario == "definite" else ["--" + args.scenario]),
        text=True, capture_output=True, check=True,
    ).stdout)
    assert oracle["taffy"] == "0.14.0"
    assert native["protocol"] == "AXISLY_FLEX_NESTED_V1"
    assert len(oracle["cases"]) == len(native["cases"]) == len(cases) == 96
    failures = []
    for actual, expected in zip(native["cases"], oracle["cases"], strict=True):
        try:
            compare(actual, expected)
        except AssertionError as error:
            failures.append(error.args[0])
    assert not failures, (len(failures), failures[:8])


if __name__ == "__main__":
    main()
