"""Pinned Taffy differential for containment across layout algorithms."""

import argparse
import itertools
import json
import subprocess

from test_flex_matrix import compare


DISPLAYS = ("Block", "Flex", "Grid")
CONTAINS = ("None", "Layout", "Paint", "Content")


def make_case(scenario, display, contain):
    name = f"contain-{scenario}-{display}-{contain}"
    value = CONTAINS.index(contain)
    if scenario == "overflow":
        return {
            "name": name, "available": [120, 60], "nodes": [
                {"id": 1, "parent": None, "style": {
                    "display": display, "size": {"width": 120, "height": 60},
                }},
                {"id": 2, "parent": 0, "style": {
                    "display": "Block", "position": "Relative",
                    "size": {"width": 40, "height": 20}, "contain": value,
                }},
                {"id": 3, "parent": 1, "style": {
                    "display": "Block", "position": "Absolute",
                    "size": {"width": 100, "height": 80},
                }},
            ],
        }
    if scenario == "baseline":
        return {
            "name": name, "available": [100, 50], "nodes": [
                {"id": 1, "parent": None, "style": {
                    "display": "Flex", "size": {"width": 100, "height": 50},
                    "align_items": "Baseline",
                }},
                {"id": 2, "parent": 0, "style": {
                    "display": display, "size": {"width": 30, "height": 20},
                    "contain": value,
                }},
                {"id": 3, "parent": 1, "style": {
                    "size": {"width": 10, "height": 10},
                }, "measure": {
                    "minimum": [10, 10], "preferred": [10, 10],
                    "baseline": 3,
                }},
                {"id": 4, "parent": 0, "style": {
                    "size": {"width": 20, "height": 20},
                }, "measure": {
                    "minimum": [20, 20], "preferred": [20, 20],
                    "baseline": 15,
                }},
            ],
        }
    return {
        "name": name, "available": [100, "max-content"], "nodes": [
            {"id": 1, "parent": None, "style": {
                "display": "Block", "size": {"width": 100},
            }},
            {"id": 2, "parent": 0, "style": {
                "display": "Block", "contain": value,
            }},
            {"id": 3, "parent": 1, "style": {
                "display": "Block", "float": "Left",
                "size": {"width": 20, "height": 30},
            }},
        ],
    }


def make_cases():
    for display, contain in itertools.product(DISPLAYS, CONTAINS):
        yield make_case("overflow", display, contain)
        yield make_case("baseline", display, contain)
    for contain in CONTAINS:
        yield make_case("float", "Block", contain)


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
    assert native["protocol"] == "AXISLY_CONTAIN_MATRIX_V1"
    assert len(oracle["cases"]) == len(native["cases"]) == 28
    by_name = {case["name"]: case for case in native["cases"]}
    for display, contain in itertools.product(DISPLAYS, CONTAINS):
        overflow = by_name[f"contain-overflow-{display}-{contain}"]["nodes"][0][
            "overflow"
        ]
        assert overflow == ([0, 0, 100, 80] if contain == "None"
                            else [0, 0, 40, 20])
        baseline = by_name[f"contain-baseline-{display}-{contain}"]["nodes"]
        expected_y = 0 if contain in ("Layout", "Content") else (
            5 if display == "Grid" else 12
        )
        assert baseline[1]["rect"][1] == expected_y
    for contain in CONTAINS:
        middle = by_name[f"contain-float-Block-{contain}"]["nodes"][1]
        assert middle["rect"][3] == (0 if contain == "None" else 30)
    failures = []
    for observed, expected in zip(native["cases"], oracle["cases"], strict=True):
        try:
            compare(observed, expected)
        except AssertionError as error:
            failures.append(error.args[0])
    assert not failures, (len(failures), failures[:20])


if __name__ == "__main__":
    main()
