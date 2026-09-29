"""Flex grow, shrink and basis combinations against Taffy 0.14."""

import argparse
import itertools
import json
import subprocess

from test_flex_matrix import compare


DIRECTIONS = ("Row", "RowReverse", "Column", "ColumnReverse")
WRAPS = ("NoWrap", "Wrap", "WrapReverse")
GROW_BASES = (12, 14, 10, 8)
SHRINK_BASES = (32, 28, 20, 15)
CROSS_SIZES = (18, 24, 16, 20)


def make_cases():
    for direction, wrap, variant, rtl in itertools.product(
        range(4), range(3), range(6), range(2)
    ):
        row = direction < 2
        width, height = (72, 68) if row else (68, 72)
        root = {
            "display": "Flex", "flex_direction": DIRECTIONS[direction],
            "flex_wrap": WRAPS[wrap], "align_items": "Start",
            "direction": "Rtl" if rtl else "Ltr",
            "size": {"width": width, "height": height},
            "padding": {"left": 3, "top": 3, "right": 3, "bottom": 3},
            "gap": {"width": 4, "height": 6},
        }
        nodes = [{"id": 1, "parent": None, "style": root}]
        for index in range(4):
            main = SHRINK_BASES[index] if variant in (1, 3) else GROW_BASES[index]
            cross = CROSS_SIZES[index]
            style = {"size": {"width": main if row else cross,
                              "height": cross if row else main}}
            if variant in (0, 4, 5):
                style["flex_grow"] = 2 if index == 1 else 0 if index == 2 else 1
            elif variant == 2:
                style["flex_grow"] = 0.2 if index == 0 else 0.3 if index == 1 else 0
            if variant == 1:
                style["flex_shrink"] = 2 if index == 1 else 0 if index == 2 else 1
            elif variant == 3:
                style["flex_shrink"] = 0.2 if index == 0 else 0.3 if index == 1 else 0
            else:
                style["flex_shrink"] = 0
            if variant == 4:
                style["flex_basis"] = (10, 20, 15, 5)[index]
            elif variant == 5 and index in (0, 1, 3):
                style["flex_basis"] = ("25%", "40%", None, "10%")[index]
            nodes.append({"id": index + 2, "parent": 0, "style": style})
        yield {"name": f"flex-factors-{direction}-{wrap}-{variant}-{rtl}",
               "available": [width, height], "nodes": nodes}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--axisly", required=True)
    parser.add_argument("--taffy", required=True)
    args = parser.parse_args()
    cases = list(make_cases())
    oracle = json.loads(subprocess.run(
        [args.taffy], input=json.dumps({"protocol": "AXISLY_TREE_V1", "cases": cases}),
        text=True, capture_output=True, check=True
    ).stdout)
    native = json.loads(subprocess.run(
        [args.axisly], text=True, capture_output=True, check=True
    ).stdout)
    assert oracle["taffy"] == "0.14.0"
    assert native["protocol"] == "AXISLY_FLEX_FACTORS_MATRIX_V1"
    assert len(oracle["cases"]) == len(native["cases"]) == len(cases) == 144
    failures = []
    for actual, expected in zip(native["cases"], oracle["cases"], strict=True):
        try:
            compare(actual, expected)
        except AssertionError as error:
            failures.append(error.args[0])
    assert not failures, (len(failures), failures[:8])


if __name__ == "__main__":
    main()
