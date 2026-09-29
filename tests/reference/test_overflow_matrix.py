"""Pinned Taffy differential for overflow propagation in Block/Flex/Grid."""

import argparse
import itertools
import json
import subprocess

from test_flex_matrix import compare


DISPLAYS = ("Block", "Flex", "Grid")
OVERFLOWS = ("Visible", "Clip", "Hidden", "Scroll")


def make_cases():
    for display, overflow_x, overflow_y in itertools.product(
        DISPLAYS, OVERFLOWS, OVERFLOWS
    ):
        name = f"overflow-matrix-{display}-{overflow_x}-{overflow_y}"
        yield {
            "name": name,
            "available": [120, 60],
            "nodes": [
                {"id": 1, "parent": None, "style": {
                    "display": display, "size": {"width": 120, "height": 60},
                }},
                {"id": 2, "parent": 0, "style": {
                    "display": "Block", "position": "Relative",
                    "size": {"width": 40, "height": 20},
                    "overflow": {"x": overflow_x, "y": overflow_y},
                }},
                {"id": 3, "parent": 1, "style": {
                    "display": "Block", "position": "Absolute",
                    "size": {"width": 100, "height": 80},
                }},
            ],
        }
    for display, overflow_x in itertools.product(
        ("Flex", "Grid"), OVERFLOWS
    ):
        root_style = {
            "display": display, "size": {"width": 40, "height": 20},
        }
        if display == "Grid":
            root_style["grid_template_columns"] = "1fr"
            root_style["grid_template_rows"] = "20px"
        yield {
            "name": f"overflow-auto-min-{display}-{overflow_x}",
            "available": [40, 20],
            "nodes": [
                {"id": 1, "parent": None, "style": root_style},
                {"id": 2, "parent": 0, "style": {
                    "display": "Block", "overflow": {
                        "x": overflow_x, "y": "Visible",
                    },
                }},
                {"id": 3, "parent": 1, "style": {}, "measure": {
                    "minimum": [80, 10], "preferred": [80, 10],
                }},
            ],
        }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--axisly", required=True)
    parser.add_argument("--taffy", required=True)
    args = parser.parse_args()
    cases = list(make_cases())
    oracle = json.loads(subprocess.run(
        [args.taffy], input=json.dumps({
            "protocol": "AXISLY_TREE_V1", "cases": cases,
        }), text=True, capture_output=True, check=True,
    ).stdout)
    assert oracle["taffy"] == "0.14.0"
    native = json.loads(subprocess.run(
        [args.axisly], text=True, capture_output=True, check=True,
    ).stdout)
    assert native["protocol"] == "AXISLY_OVERFLOW_MATRIX_V1"
    assert len(oracle["cases"]) == len(native["cases"]) == 56
    by_name = {case["name"]: case for case in native["cases"]}
    # Taffy 0.14 Overflow documents Clip as axis-local propagation clipping,
    # while Hidden/Scroll create scroll containers and clip both axes.
    propagation = {
        "Visible-Visible": [0, 0, 100, 80],
        "Clip-Visible": [0, 0, 40, 80],
        "Visible-Clip": [0, 0, 100, 20],
        "Clip-Scroll": [0, 0, 40, 20],
        "Hidden-Visible": [0, 0, 40, 20],
    }
    for display, suffix in itertools.product(DISPLAYS, propagation):
        assert by_name[f"overflow-matrix-{display}-{suffix}"]["nodes"][0][
            "overflow"
        ] == propagation[suffix]
    # Clip retains the content-based automatic minimum; Hidden/Scroll use zero.
    for display, overflow in itertools.product(("Flex", "Grid"), OVERFLOWS):
        expected_width = 80 if overflow in ("Visible", "Clip") else 40
        assert by_name[f"overflow-auto-min-{display}-{overflow}"]["nodes"][1][
            "rect"
        ][2] == expected_width
    failures = []
    for observed, expected in zip(native["cases"], oracle["cases"], strict=True):
        try:
            compare(observed, expected)
        except AssertionError as error:
            failures.append(error.args[0])
    assert not failures, (len(failures), failures[:20])


if __name__ == "__main__":
    main()
