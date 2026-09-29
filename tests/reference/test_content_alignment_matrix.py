"""Container alignment keywords over Flex lines and Grid tracks."""

import argparse
import collections
import itertools
import json
import subprocess

from grid_detail_compare import compare_grid_details
from test_flex_matrix import compare


POSITIONS = (
    "Start", "End", "FlexStart", "FlexEnd", "Center", "Stretch",
    "SpaceBetween", "SpaceEvenly", "SpaceAround",
)
ALIGNMENTS = POSITIONS + tuple("Safe" + value for value in POSITIONS[:5])
DIRECTIONS = ("Row", "RowReverse", "Column", "ColumnReverse")
WRAPS = ("NoWrap", "Wrap", "WrapReverse")


def make_cases():
    for mode, alignment, scenario, rtl in itertools.product(
        range(26), range(len(ALIGNMENTS)), range(3), range(2)
    ):
        flex = mode < 24
        axis = mode % 2 if flex else mode - 24
        style = {
            "display": "Flex" if flex else "Grid",
            "direction": "Rtl" if rtl else "Ltr",
            "align_items": "Start", "justify_items": "Start",
            "gap": {"width": 4, "height": 4},
            "align_content": ALIGNMENTS[alignment] if axis == 0 else "Stretch",
            "justify_content": ALIGNMENTS[alignment] if axis == 1 else "Start",
        }
        if flex:
            direction = (mode // 2) % 4
            row = direction < 2
            wrap = mode // 8
            style.update({
                "flex_direction": DIRECTIONS[direction],
                "flex_wrap": WRAPS[wrap],
                "size": {"width": 90 if row
                         else (55 if scenario == 1 else 120),
                         "height": (35 if scenario == 1 else 120) if row else 50},
            })
            children = []
            for index in range(4):
                width, height = 40, 20
                if scenario == 2 and index == 0:
                    width, height = (110, 20) if row else (40, 65)
                children.append({
                    "id": index + 2, "parent": 0,
                    "style": {"size": {"width": width, "height": height},
                              "flex_shrink": 0},
                })
        else:
            style.update({
                "size": {"width": 80 if scenario == 1 else 120,
                         "height": 50 if scenario == 1 else 100},
                "grid_template_columns": "40px 30px 20px" if scenario != 2 else "40px",
                "grid_template_rows": "20px 20px 20px" if scenario != 2 else "20px",
            })
            count = 3 if scenario != 2 else 1
            children = [{
                "id": index + 2, "parent": 0,
                "style": {"size": {"width": 10, "height": 10}},
            } for index in range(count)]
        size = style["size"]
        yield {
            "name": f"content-alignment-{mode}-{alignment}-{scenario}-{rtl}",
            "available": [size["width"], size["height"]],
            "nodes": [{"id": 1, "parent": None, "style": style}, *children],
        }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--axisly", required=True)
    parser.add_argument("--taffy", required=True)
    args = parser.parse_args()
    cases = list(make_cases())
    expected_cases = []
    for offset in range(0, len(cases), 512):
        oracle = json.loads(subprocess.run(
            [args.taffy], input=json.dumps({
                "protocol": "AXISLY_TREE_GRID_DETAILS_V1",
                "cases": cases[offset:offset + 512],
            }), text=True, capture_output=True, check=True,
        ).stdout)
        assert oracle["taffy"] == "0.14.0"
        expected_cases.extend(oracle["cases"])
    native = json.loads(subprocess.run(
        [args.axisly], text=True, capture_output=True, check=True,
    ).stdout)
    assert native["protocol"] == "AXISLY_CONTENT_ALIGNMENT_MATRIX_V1"
    assert len(native["cases"]) == len(expected_cases) == len(cases) == 2184
    failures = []
    for actual, expected in zip(native["cases"], expected_cases, strict=True):
        try:
            compare(actual, expected)
            compare_grid_details(actual, expected)
        except AssertionError as error:
            failures.append(error.args[0])
    assert not failures, (len(failures),
                          collections.Counter(str(failure[2]) for failure in failures),
                          failures[:12])


if __name__ == "__main__":
    main()
