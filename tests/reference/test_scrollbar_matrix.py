"""Pinned Taffy differential for scrollbar gutters and percent children."""

import argparse
import itertools
import json
import subprocess

from test_flex_matrix import compare


DISPLAYS = ("Block", "Flex", "Grid")
DIRECTIONS = ("Ltr", "Rtl")
MODES = (
    ("Visible", "Visible"), ("Scroll", "Visible"),
    ("Visible", "Scroll"), ("Scroll", "Scroll"),
    ("Hidden", "Scroll"), ("Clip", "Scroll"),
)
SIZINGS = ("BorderBox", "ContentBox")


def make_cases():
    for display, direction, (overflow_x, overflow_y), sizing in itertools.product(
        DISPLAYS, DIRECTIONS, MODES, SIZINGS
    ):
        for absolute in (False, True):
            yield {
                "name": f"scrollbar-matrix-{display}-{direction}-"
                        f"{overflow_x}-{overflow_y}-{sizing}"
                        f"{'-absolute' if absolute else ''}",
                "available": [100, 50],
                "nodes": [
                    {"id": 1, "parent": None, "style": {
                        "display": display, "position": "Relative",
                        "direction": direction, "box_sizing": sizing,
                        "size": {"width": 100, "height": 50},
                        "padding": {
                            "left": 5, "top": 5, "right": 5, "bottom": 5,
                        },
                        "overflow": {"x": overflow_x, "y": overflow_y},
                        "scrollbar_width": 10,
                    }},
                    {"id": 2, "parent": 0, "style": {
                        "display": "Block",
                        "position": "Absolute" if absolute else "Relative",
                        "size": ({"width": 20, "height": 10} if absolute
                                 else {"width": "100%", "height": "100%"}),
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
    assert native["protocol"] == "AXISLY_SCROLLBAR_MATRIX_V1"
    assert len(oracle["cases"]) == len(native["cases"]) == 144
    for case, source in zip(native["cases"], cases, strict=True):
        root, child = case["nodes"]
        style = source["nodes"][0]["style"]
        overflow = style["overflow"]
        content_box = style["box_sizing"] == "ContentBox"
        rtl = style["direction"] == "Rtl"
        vertical = overflow["y"] == "Scroll"
        horizontal = overflow["x"] == "Scroll"
        assert root["rect"][2:] == ([110, 60] if content_box else [100, 50])
        assert root["scrollbar"] == [10 if vertical else 0,
                                      10 if horizontal else 0]
        if source["nodes"][1]["style"]["position"] == "Absolute":
            grid = style["display"] == "Grid"
            expected_x = ((root["rect"][2] - (20 if grid else 25))
                          if rtl else (0 if grid else 5))
            expected_y = 0 if grid else 5
            assert child["rect"][0] - root["rect"][0] == expected_x
            assert child["rect"][1] - root["rect"][1] == expected_y
            assert child["rect"][2:] == [20, 10]
            assert root["overflow"] == ([0, 0, 20, 10] if grid
                                        else [0, 0, 25, 15])
            continue
        assert child["rect"][0] - root["rect"][0] == (
            15 if rtl and vertical else 5
        )
        assert child["rect"][1] - root["rect"][1] == 5
        assert child["rect"][2:] == [
            (100 if content_box else 90) - (10 if vertical else 0),
            (50 if content_box else 40) - (10 if horizontal else 0),
        ]
    failures = []
    for observed, expected in zip(native["cases"], oracle["cases"], strict=True):
        try:
            compare(observed, expected)
        except AssertionError as error:
            failures.append(error.args[0])
    assert not failures, (len(failures), failures[:20])


if __name__ == "__main__":
    main()
