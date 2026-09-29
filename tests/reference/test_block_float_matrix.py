"""Block and FlowRoot float placement against pinned Taffy 0.14."""

import argparse
import itertools
import json
import subprocess

from test_flex_matrix import compare


DISPLAYS = ("Block", "FlowRoot", "Flex", "Grid")
CLEARS = ("None", "Left", "Right", "Both")


def make_cases():
    for root_kind, float_side, clear, follower_display, follower_width, rtl in itertools.product(
        range(2), range(2), range(4), range(4), range(2), range(2)
    ):
        root = {
            "display": "Block" if root_kind == 0 else "FlowRoot",
            "size": {"width": 100},
            "padding": {"left": 2, "top": 2, "right": 2, "bottom": 2},
            "direction": "Rtl" if rtl else "Ltr",
        }
        follower = {
            "display": DISPLAYS[follower_display],
            "clear": CLEARS[clear],
            "size": {"height": 12},
            "margin": {"left": 3, "top": 5, "right": 2, "bottom": 0},
        }
        if follower_width:
            follower["size"]["width"] = 80
        nodes = [
            {"id": 1, "parent": None, "style": root},
            {"id": 2, "parent": 0, "style": {
                "display": "Block", "float": "Left" if float_side == 0 else "Right",
                "size": {"width": 30, "height": 25}}},
            {"id": 3, "parent": 0, "style": {
                "display": "Block", "float": "Right" if float_side == 0 else "Left",
                "size": {"width": 20, "height": 30}}},
            {"id": 4, "parent": 0, "style": follower},
            {"id": 5, "parent": 3, "style": {
                "size": {"width": 10, "height": 8}}},
            {"id": 6, "parent": 0, "style": {
                "display": "Block", "size": {"height": 5}, "margin": {"top": 4}}},
        ]
        name = (f"block-float-matrix-{root_kind}-{float_side}-{clear}-"
                f"{follower_display}-{follower_width}-{rtl}")
        yield {"name": name, "available": [100, "max-content"], "nodes": nodes}

    for root_float, padding, top_margin, inner_float, display, clear in itertools.product(
        range(2), range(2), range(2), range(2), range(2), range(2)
    ):
        nodes = [
            {"id": 1, "parent": None, "style": {"display": "Block",
                "size": {"width": 100}, "padding": {"left": 2, "top": 2,
                    "right": 2, "bottom": 2}}},
            {"id": 2, "parent": 0, "style": {"display": "Block",
                "float": "Left" if root_float == 0 else "Right",
                "size": {"width": 20, "height": 20}}},
            {"id": 3, "parent": 0, "style": {"display": "Block",
                "margin": {"left": 3, "top": 4 if top_margin else 0},
                "padding": {"left": 3 if padding else 0,
                    "top": 3 if padding else 0}}},
            {"id": 4, "parent": 2, "style": {"display": "Block",
                "float": "Left" if inner_float == 0 else "Right",
                "size": {"width": 10, "height": 12}}},
            {"id": 5, "parent": 2, "style": {"display": "FlowRoot" if display == 0 else "Flex",
                "clear": "Both" if clear else "None",
                "size": {"width": 20, "height": 8}}},
            {"id": 6, "parent": 0, "style": {"display": "FlowRoot",
                "size": {"width": 15, "height": 10}}},
        ]
        name = (f"block-float-nested-{root_float}-{padding}-{top_margin}-"
                f"{inner_float}-{display}-{clear}")
        yield {"name": name, "available": [100, "max-content"], "nodes": nodes}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--axisly", required=True)
    parser.add_argument("--taffy", required=True)
    args = parser.parse_args()
    cases = list(make_cases())
    oracle = json.loads(subprocess.run(
        [args.taffy], input=json.dumps({"protocol": "AXISLY_TREE_V1", "cases": cases}),
        text=True, capture_output=True, check=True,
    ).stdout)
    native = json.loads(subprocess.run(
        [args.axisly], text=True, capture_output=True, check=True,
    ).stdout)
    assert oracle["taffy"] == "0.14.0"
    assert native["protocol"] == "AXISLY_BLOCK_FLOAT_MATRIX_V1"
    assert len(oracle["cases"]) == len(native["cases"]) == len(cases) == 320
    failures = []
    for actual, expected in zip(native["cases"], oracle["cases"], strict=True):
        try:
            compare(actual, expected)
        except AssertionError as error:
            failures.append(error.args[0])
    assert not failures, (len(failures), failures[:8])


if __name__ == "__main__":
    main()
