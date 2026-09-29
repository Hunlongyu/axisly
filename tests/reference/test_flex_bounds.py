"""Flex min/max sizing, wrapping, axes, and direction against Taffy 0.14."""

import argparse
import itertools
import json
import subprocess

from test_flex_matrix import compare


DIRECTIONS = ("Row", "RowReverse", "Column", "ColumnReverse")
WRAPS = ("NoWrap", "Wrap", "WrapReverse", "Balance", "BalanceReverse")
MAIN_SIZES = (42, 30, 25, 18)
CROSS_SIZES = (18, 24, 16, 20)


def make_cases(query="definite"):
    for scenario, direction, wrap, variant, rtl in itertools.product(
        range(5), range(4), range(len(WRAPS)), range(5), range(2)
    ):
        row = direction < 2
        width, height = (72, 68) if row else (68, 72)
        root = {
            "display": "Flex", "flex_direction": DIRECTIONS[direction],
            "flex_wrap": WRAPS[wrap], "align_items": "Start",
            "direction": "Rtl" if rtl else "Ltr",
            "padding": {"left": 3, "top": 3, "right": 3, "bottom": 3},
            "gap": {"width": 4, "height": 6},
        }
        if wrap >= 3:
            root["flex_line_count"] = 2
        axis = "width" if row else "height"
        if scenario == 0:
            root["size"] = {"width": width, "height": height}
        elif scenario == 2:
            root["max_size"] = {axis: 40}
        elif scenario == 3:
            root["min_size"] = {axis: 80}
        elif scenario == 4:
            root["min_size"] = {axis: 80}
            root["max_size"] = {axis: 40}
        nodes = [{"id": 1, "parent": None, "style": root}]
        for index, (main, cross) in enumerate(zip(MAIN_SIZES, CROSS_SIZES, strict=True)):
            style = {"size": {"width": main if row else cross,
                              "height": cross if row else main}}
            if (variant == 0 or variant == 2) and index == 0:
                style["max_size"] = {axis: 20 if variant == 0 else 28}
            if variant == 1 and index == 0:
                style["min_size"] = {axis: 58}
            if variant == 2 and index == 1:
                style["min_size"] = {axis: 45}
            if variant == 3 and index == 2:
                style["max_size"] = {axis: 12}
            if variant == 3 and index == 3:
                style["min_size"] = {axis: 26}
            if variant == 4 and index == 3:
                leading, trailing = ("left", "right") if row else ("top", "bottom")
                style["margin"] = {leading: -9, trailing: -9}
                style["padding"] = {leading: 5, trailing: 5}
                style["border"] = {leading: 1, trailing: 1}
            nodes.append({"id": index + 2, "parent": 0, "style": style})
        prefix = {"definite": "flex-bounds", "max-content": "flex-bounds-max",
                  "min-content": "flex-bounds-min"}[query]
        available = [width, height] if query == "definite" else [query, query]
        yield {"name": f"{prefix}-{scenario}-{direction}-{wrap}-{variant}-{rtl}",
               "available": available, "nodes": nodes}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--axisly", required=True)
    parser.add_argument("--taffy", required=True)
    parser.add_argument("--query", choices=("definite", "max-content", "min-content"),
                        default="definite")
    args = parser.parse_args()
    cases = list(make_cases(args.query))
    oracle = json.loads(subprocess.run(
        [args.taffy], input=json.dumps({"protocol": "AXISLY_TREE_V1", "cases": cases}),
        text=True, capture_output=True, check=True
    ).stdout)
    native = json.loads(subprocess.run(
        [args.axisly] + ([] if args.query == "definite" else ["--" + args.query]),
        text=True, capture_output=True, check=True
    ).stdout)
    assert oracle["taffy"] == "0.14.0"
    assert native["protocol"] == "AXISLY_FLEX_BOUNDS_MATRIX_V1"
    assert len(oracle["cases"]) == len(native["cases"]) == len(cases) == 1000
    failures = []
    for actual, expected in zip(native["cases"], oracle["cases"], strict=True):
        try:
            compare(actual, expected)
        except AssertionError as error:
            failures.append(error.args[0])
    assert not failures, (len(failures), failures[:8])


if __name__ == "__main__":
    main()
