"""Deterministic cross product of Flex axes, wrapping, alignment and direction."""

import argparse
import itertools
import json
import math
import subprocess


DIRECTIONS = ("Row", "RowReverse", "Column", "ColumnReverse")
WRAPS = ("NoWrap", "Wrap", "WrapReverse", "Balance", "BalanceReverse")
ITEM_POSITIONS = ("Start", "End", "Center")
CONTENT_POSITIONS = ("FlexStart", "FlexEnd", "Center")
LINE_POSITIONS = ("Start", "End", "Center")
ITEM_SIZES = ((22, 17), (29, 23), (18, 31), (27, 14))


def make_cases(scenario="definite"):
    for direction, wrap, item, content, lines, rtl, line_count in itertools.product(
        range(len(DIRECTIONS)), range(len(WRAPS)), range(len(ITEM_POSITIONS)),
        range(len(CONTENT_POSITIONS)), range(len(LINE_POSITIONS)), range(2),
        range(1, 4)
    ):
        if wrap < 3 and line_count != 1:
            continue
        prefix = {"definite": "flex-matrix", "max-content": "flex-intrinsic-max",
                  "min-content": "flex-intrinsic-min"}[scenario]
        name = f"{prefix}-{direction}-{wrap}-{item}-{content}-{lines}-{rtl}-{line_count}"
        style = {
            "display": "Flex", "flex_direction": DIRECTIONS[direction],
            "flex_wrap": WRAPS[wrap], "align_items": ITEM_POSITIONS[item],
            "justify_content": CONTENT_POSITIONS[content],
            "align_content": LINE_POSITIONS[lines],
            "direction": "Rtl" if rtl else "Ltr",
            "padding": {"left": 3, "top": 3, "right": 3, "bottom": 3},
            "gap": {"width": 4, "height": 6},
        }
        if wrap >= 3:
            style["flex_line_count"] = line_count
        if scenario == "definite":
            style["size"] = {"width": 72, "height": 68}
        nodes = [{"id": 1, "parent": None, "style": style}]
        nodes.extend({
            "id": index + 2, "parent": 0,
            "style": {"size": {"width": width, "height": height},
                      "flex_shrink": 0},
        } for index, (width, height) in enumerate(ITEM_SIZES))
        available = [72, 68] if scenario == "definite" else [scenario, scenario]
        yield {"name": name, "available": available, "nodes": nodes}


def absolute_nodes(nodes):
    positions = []
    for node in nodes:
        x, y, width, height = node["rect"]
        if node["parent"] is not None:
            parent = positions[node["parent"]]
            x += parent[0]
            y += parent[1]
        positions.append([x, y, width, height])
    return positions


def compare(native, oracle):
    assert native["name"] == oracle["name"]
    expected_rects = absolute_nodes(oracle["nodes"])
    for actual, expected, rect in zip(
        native["nodes"], oracle["nodes"], expected_rects, strict=True
    ):
        assert actual["id"] == expected["id"], native["name"]
        for field, target in (
            ("rect", rect),
            *((field, expected[field]) for field in
              ("margin", "padding", "border", "scrollbar", "overflow")),
        ):
            observed = actual[field]
            assert all(math.isfinite(a) and math.isclose(
                a, b, rel_tol=0, abs_tol=0.01
            ) for a, b in zip(observed, target, strict=True)), (
                native["name"], actual["id"], field, observed, target
            )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--axisly", required=True)
    parser.add_argument("--taffy", required=True)
    parser.add_argument("--scenario", choices=("definite", "max-content", "min-content"),
                        default="definite")
    args = parser.parse_args()
    cases = list(make_cases(args.scenario))
    oracle_cases = []
    for start in range(0, len(cases), 512):
        fixture = json.dumps({"protocol": "AXISLY_TREE_V1",
                              "cases": cases[start:start + 512]})
        oracle = json.loads(subprocess.run(
            [args.taffy], input=fixture, text=True, capture_output=True, check=True
        ).stdout)
        assert oracle["taffy"] == "0.14.0"
        oracle_cases.extend(oracle["cases"])
    native = json.loads(subprocess.run(
        [args.axisly] + ([] if args.scenario == "definite" else ["--" + args.scenario]),
        text=True, capture_output=True, check=True
    ).stdout)
    assert native["protocol"] == ("AXISLY_FLEX_MATRIX_V1" if args.scenario == "definite"
                                  else "AXISLY_FLEX_INTRINSIC_MATRIX_V1")
    assert len(oracle_cases) == len(native["cases"]) == len(cases) == 1944
    failures = []
    for actual, expected in zip(native["cases"], oracle_cases, strict=True):
        try:
            compare(actual, expected)
        except AssertionError as error:
            failures.append(error.args[0])
    assert not failures, (len(failures), failures[:8])


if __name__ == "__main__":
    main()
