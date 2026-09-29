"""Multi-item Flex baseline groups across wrapping, directions and sizing."""

import argparse
import itertools
import json
import subprocess

from test_flex_matrix import compare


DIRECTIONS = ("Row", "RowReverse", "Column", "ColumnReverse")
WRAPS = ("NoWrap", "Wrap", "WrapReverse")
ALIGNMENTS = (("Baseline", "Baseline"), ("Baseline", "Start"),
              ("Start", "Baseline"), ("Baseline", "Stretch"))


def make_cases():
    for direction, wrap, parent_rtl, child_rtl, dimensions, alignment in itertools.product(
        range(4), range(3), range(2), range(2), range(3), range(4)
    ):
        row = direction < 2
        width, height = ((120, 100), (70, 100) if row else (120, 38),
                         (120, 25) if row else (50, 100))[dimensions]
        parent = {
            "display": "Flex", "size": {"width": width, "height": height},
            "flex_direction": DIRECTIONS[direction], "flex_wrap": WRAPS[wrap],
            "align_items": "End", "gap": {"width": 3, "height": 3},
            "direction": "Rtl" if parent_rtl else "Ltr",
        }
        children = []
        for index, (item_width, item_height, baseline) in enumerate(
            ((40, 20, 12), (45, 30, 7))
        ):
            children.append({
                "id": index + 2, "parent": 0,
                "style": {
                    "direction": "Rtl" if child_rtl and index == 1 else "Ltr",
                    "align_self": ALIGNMENTS[alignment][index],
                    "flex_shrink": 0,
                },
                "measure": {"minimum": [item_width, item_height],
                            "preferred": [item_width, item_height],
                            "baseline": baseline},
            })
        yield {
            "name": f"baseline-matrix-{direction}-{wrap}-{parent_rtl}-{child_rtl}-{dimensions}-{alignment}",
            "available": [width, height],
            "nodes": [{"id": 1, "parent": None, "style": parent}, *children],
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
                "protocol": "AXISLY_TREE_V1", "cases": cases[offset:offset + 512],
            }), text=True, capture_output=True, check=True,
        ).stdout)
        assert oracle["taffy"] == "0.14.0"
        expected_cases.extend(oracle["cases"])
    native = json.loads(subprocess.run(
        [args.axisly], text=True, capture_output=True, check=True,
    ).stdout)
    assert native["protocol"] == "AXISLY_BASELINE_MATRIX_V1"
    assert len(native["cases"]) == len(expected_cases) == len(cases) == 576
    failures = []
    for actual, expected in zip(native["cases"], expected_cases, strict=True):
        try:
            compare(actual, expected)
        except AssertionError as error:
            failures.append(error.args[0])
    assert not failures, (len(failures), failures[:12])


if __name__ == "__main__":
    main()
