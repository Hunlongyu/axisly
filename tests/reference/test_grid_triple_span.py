"""Three-track Grid sizing with overlapping spans against Taffy 0.14."""

import argparse
import itertools
import json
import subprocess

from test_flex_matrix import compare
from grid_detail_compare import compare_grid_details


TRACKS = (
    "1fr 2fr 1fr", "auto 1fr auto",
    "minmax(10px, 1fr) fit-content(45px) 2fr",
    "30px min-content 1fr", "0.2fr 0fr 0.5fr",
)
MAIN_SIZES = (80, 75, 30, 22)
CROSS_SIZES = (20, 25, 15, 15)
MAIN_PLACES = ((1, 3), (2, 4), (1, 2), (3, 4))
CROSS_PLACES = ((1, 2), (2, 3), (2, 3), (1, 2))


def make_cases():
    for scenario, track, axis, rtl in itertools.product(range(3), range(5), range(2), range(2)):
        columns = axis == 0
        width, height = (160, 100) if columns else (100, 140)
        root = {
            "display": "Grid", "direction": "Rtl" if rtl else "Ltr",
            "padding": {"left": 5, "top": 5, "right": 5, "bottom": 5},
            "gap": {"width": 4, "height": 6},
            "justify_items": "Start", "align_items": "Start",
            "grid_template_columns": TRACKS[track] if columns else "40px 40px",
            "grid_template_rows": "40px 40px" if columns else TRACKS[track],
        }
        if scenario == 0:
            root["size"] = {"width": width, "height": height}
        nodes = [{"id": 1, "parent": None, "style": root}]
        for index, (main, cross) in enumerate(zip(MAIN_SIZES, CROSS_SIZES, strict=True)):
            main_place, cross_place = MAIN_PLACES[index], CROSS_PLACES[index]
            col = main_place if columns else cross_place
            row = cross_place if columns else main_place
            nodes.append({"id": index + 2, "parent": 0, "style": {
                "size": {"width": main if columns else cross,
                          "height": cross if columns else main},
                "grid_column": {"start": str(col[0]), "end": str(col[1])},
                "grid_row": {"start": str(row[0]), "end": str(row[1])},
            }})
        available = [width, height] if scenario == 0 else (
            ["max-content", "max-content"] if scenario == 1 else
            ["min-content", "min-content"]
        )
        yield {"name": f"grid-triple-span-{scenario}-{track}-{axis}-{rtl}",
               "available": available, "nodes": nodes}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--axisly", required=True)
    parser.add_argument("--taffy", required=True)
    parser.add_argument("--details", action="store_true")
    args = parser.parse_args()
    cases = list(make_cases())
    protocol = "AXISLY_TREE_GRID_DETAILS_V1" if args.details else "AXISLY_TREE_V1"
    oracle = json.loads(subprocess.run(
        [args.taffy], input=json.dumps({"protocol": protocol, "cases": cases}),
        text=True, capture_output=True, check=True
    ).stdout)
    native = json.loads(subprocess.run(
        [args.axisly] + (["--details"] if args.details else []),
        text=True, capture_output=True, check=True
    ).stdout)
    assert oracle["taffy"] == "0.14.0"
    assert native["protocol"] == "AXISLY_GRID_TRIPLE_SPAN_V1"
    assert len(oracle["cases"]) == len(native["cases"]) == len(cases) == 60
    failures = []
    for actual, expected in zip(native["cases"], oracle["cases"], strict=True):
        try:
            compare(actual, expected)
            if args.details:
                compare_grid_details(actual, expected)
        except AssertionError as error:
            failures.append(error.args[0])
    assert not failures, (len(failures), failures[:8])


if __name__ == "__main__":
    main()
