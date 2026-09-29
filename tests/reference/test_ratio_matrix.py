"""Mixed-container aspect-ratio constraints against Taffy 0.14."""

import argparse
import itertools
import json
import subprocess

from grid_detail_compare import compare_grid_details
from test_flex_matrix import compare


DISPLAYS = ("Block", "Flex", "Grid")


def make_cases(scrollbar=False):
    for scenario, parent_display, child_display, known_axis, box_sizing, bound, direction in itertools.product(
        range(1 if scrollbar else 3), range(3), range(3), range(2), range(2),
        (0, 1, 3) if scrollbar else range(6), range(2)
    ):
        outer = {
            "display": DISPLAYS[parent_display],
            "direction": "Rtl" if direction else "Ltr",
            "size": {**({"width": 160} if scenario != 2 else {}),
                     **({"height": 120} if scenario != 1 else {})},
            "align_items": "Start", "justify_items": "Start",
        }
        if parent_display == 2:
            outer.update({"grid_template_columns": "120px",
                          "grid_template_rows": "auto"})
        inner = {
            "display": DISPLAYS[child_display],
            "direction": outer["direction"],
            "box_sizing": "BorderBox" if box_sizing == 0 else "ContentBox",
            "aspect_ratio": 2,
            "size": {"width": 60} if known_axis == 0 else {"height": 40},
            "padding": {"left": 10, "right": 10, "top": 5, "bottom": 5},
            "flex_shrink": 0,
            "align_items": "Start", "justify_items": "Start",
        }
        if bound == 1:
            inner["min_size"] = {"width": 80}
        elif bound == 2:
            inner["max_size"] = {"width": 40}
        elif bound == 3:
            inner["min_size"] = {"height": 50}
        elif bound == 4:
            inner["max_size"] = {"height": 20}
        elif bound == 5:
            inner["min_size"] = {"width": 80}
            inner["max_size"] = {"height": 20}
        if child_display == 2:
            inner.update({"grid_template_columns": "auto",
                          "grid_template_rows": "auto"})
        for mode in range(3 if scrollbar else 1):
            case_inner = dict(inner)
            if scrollbar:
                case_inner["scrollbar_width"] = 8
                case_inner["overflow"] = {
                    "x": "Visible" if mode == 1 else "Scroll",
                    "y": "Visible" if mode == 0 else "Scroll",
                }
            prefix = "ratio-scrollbar-matrix" if scrollbar else "ratio-matrix"
            name = (f"{prefix}-{scenario}-{parent_display}-{child_display}-"
                    f"{known_axis}-{box_sizing}-{bound}-{direction}")
            if scrollbar:
                name += f"-{mode}"
            yield {
                "name": name,
                "available": ["max-content" if scenario == 2 else 160,
                              "max-content" if scenario == 1 else 120],
                "nodes": [
                    {"id": 1, "parent": None, "style": outer},
                    {"id": 2, "parent": 0, "style": case_inner},
                    {"id": 3, "parent": 1, "style": {},
                     "measure": {"minimum": [40, 30], "preferred": [40, 30]}},
                ],
            }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--axisly", required=True)
    parser.add_argument("--taffy", required=True)
    parser.add_argument("--details", action="store_true")
    parser.add_argument("--scrollbar", action="store_true")
    args = parser.parse_args()
    assert not (args.details and args.scrollbar)
    cases = list(make_cases(args.scrollbar))
    protocol = "AXISLY_TREE_GRID_DETAILS_V1" if args.details else "AXISLY_TREE_V1"
    expected_cases = []
    for offset in range(0, len(cases), 512):
        oracle = json.loads(subprocess.run(
            [args.taffy], input=json.dumps({"protocol": protocol,
                                            "cases": cases[offset:offset + 512]}),
            text=True, capture_output=True, check=True,
        ).stdout)
        assert oracle["taffy"] == "0.14.0"
        expected_cases.extend(oracle["cases"])
    native = json.loads(subprocess.run(
        [args.axisly] + (["--details"] if args.details else
                         ["--scrollbar"] if args.scrollbar else []),
        text=True, capture_output=True, check=True,
    ).stdout)
    assert native["protocol"] == ("AXISLY_RATIO_SCROLLBAR_MATRIX_V1"
                                  if args.scrollbar else "AXISLY_RATIO_MATRIX_V1")
    assert len(native["cases"]) == len(expected_cases) == len(cases) == (
        648 if args.scrollbar else 1296
    )
    if args.scrollbar:
        by_name = {case["name"]: case for case in native["cases"]}
        for known_axis, box_sizing, size in (
            (0, 0, [60, 30]), (0, 1, [80, 40]),
            (1, 0, [80, 40]), (1, 1, [100, 50]),
        ):
            for mode in range(3):
                name = (f"ratio-scrollbar-matrix-0-0-0-{known_axis}-"
                        f"{box_sizing}-0-0-{mode}")
                assert by_name[name]["nodes"][1]["rect"][2:] == size
    failures = []
    for actual, expected in zip(native["cases"], expected_cases, strict=True):
        try:
            compare(actual, expected)
            if args.details:
                compare_grid_details(actual, expected)
        except AssertionError as error:
            failures.append(error.args[0])
    assert not failures, (len(failures), failures[:12])


if __name__ == "__main__":
    main()
