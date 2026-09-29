"""Compare typed Axisly calculations against a custom Taffy 0.14 calc tree."""

import argparse
import json
import math
import subprocess
from pathlib import Path


GOLDEN = {
    "calc-leaf-size": ([0, 0, 70, 60], [0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 0, 0]),
    "calc-leaf-content-box": ([0, 0, 115, 20], [0, 0, 0, 0], [15, 0, 20, 0], [2, 0, 3, 0]),
    "calc-leaf-min-max": ([0, 0, 70, 10], [0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 0, 0]),
    "calc-leaf-margins": ([0, 0, 50, 20], [40, 25, 6, 20], [0, 0, 0, 0], [0, 0, 0, 0]),
    "calc-leaf-clamp": ([0, 0, 100, 30], [0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 0, 0]),
    "calc-leaf-indefinite": ([0, 0, 10, 10], [0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 0, 0]),
    "calc-leaf-partial-basis": ([0, 0, 70, 25], [0, 12, 0, 0],
                                [0, 12, 0, 0], [0, 3, 0, 0]),
    "calc-leaf-inverted-clamp": ([0, 0, 100, 10], [0, 0, 0, 0],
                                 [0, 0, 0, 0], [0, 0, 0, 0]),
}

TREE_GOLDEN = {
    "calc-block-child": {
        7100: [0, 0, 240, 60], 7101: [41.3, 0, 131.5, 20],
        7102: [41.3, 0, 75.75, 10]},
    "calc-flex-gap-basis": {
        7110: [0, 0, 200, 40], 7111: [0, 0, 40, 20],
        7112: [55, 0, 145, 20]},
    "calc-grid-gap-item": {
        7120: [0, 0, 200, 50], 7121: [0, 0, 35, 10],
        7122: [70, 0, 85, 10]},
    "calc-flex-auto-gap": {
        7130: [0, 0, 65, 40], 7131: [0, 0, 40, 40],
        7132: [51.5, 0, 20, 40]},
    "calc-grid-auto-gap": {
        7140: [0, 0, 40, 40], 7141: [0, 0, 10, 10],
        7142: [29, 0, 10, 10]},
}


def near(actual, expected):
    return len(actual) == len(expected) and all(
        math.isfinite(a) and math.isclose(a, b, rel_tol=0, abs_tol=0.01)
        for a, b in zip(actual, expected, strict=True)
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--axisly", required=True)
    parser.add_argument("--taffy", required=True)
    parser.add_argument("--fixture", required=True)
    args = parser.parse_args()
    fixture = json.loads(Path(args.fixture).read_text(encoding="utf-8"))
    assert fixture["protocol"] == "AXISLY_CALC_V1"
    names = [case["name"] for case in fixture["cases"]]
    assert set(names) == set(GOLDEN) | set(TREE_GOLDEN)
    assert len(names) == len(GOLDEN) + len(TREE_GOLDEN)
    run = subprocess.run([args.taffy], input=json.dumps(fixture), text=True,
                         capture_output=True, check=True, timeout=15)
    reference = json.loads(run.stdout)
    assert reference["protocol"] == "AXISLY_CALC_V1"
    assert reference["taffy"] == "0.14.0"
    assert [case["name"] for case in reference["cases"]] == names
    for case in reference["cases"]:
        name = case["name"]
        native = json.loads(subprocess.run([args.axisly, name], text=True,
                                          capture_output=True, check=True, timeout=15).stdout)
        assert native["name"] == name
        if name in GOLDEN:
            assert len(native["nodes"]) == 1
            node = native["nodes"][0]
            for field, expected in zip(("rect", "margin", "padding", "border"),
                                       GOLDEN[name], strict=True):
                assert near(case[field], expected), (name, field, case[field], expected)
                assert near(node[field], case[field]), (
                    name, field, node[field], case[field])
            continue
        absolute = []
        for node in case["nodes"]:
            x, y, width, height = node["rect"]
            if node["parent"] is not None:
                parent_x, parent_y, _, _ = absolute[node["parent"]]
                x += parent_x
                y += parent_y
            absolute.append([x, y, width, height])
        expected = {node["id"]: (rect, node) for node, rect in
                    zip(case["nodes"], absolute, strict=True)}
        actual = {node["id"]: node for node in native["nodes"]}
        assert actual.keys() == expected.keys(), (name, actual.keys(), expected.keys())
        for node_id, node in actual.items():
            rect, reference_node = expected[node_id]
            assert near(rect, TREE_GOLDEN[name][node_id]), (name, node_id, rect)
            assert near(node["rect"], rect), (name, node_id, "rect", node["rect"], rect)
            for field in ("margin", "padding", "border"):
                assert near(node[field], reference_node[field]), (
                    name, node_id, field, node[field], reference_node[field])

    bad = dict(fixture)
    bad["cases"] = fixture["cases"] + [fixture["cases"][0]]
    rejected = subprocess.run([args.taffy], input=json.dumps(bad), text=True,
                              capture_output=True, timeout=15)
    assert rejected.returncode != 0 and "Duplicate calc case name" in rejected.stderr
    invalid_tree = {"protocol": "AXISLY_CALC_V1", "cases": [{
        "name": "invalid-parent", "available": [100, 100], "nodes": [
            {"id": 1, "parent": None, "style": {}},
            {"id": 2, "parent": 1, "style": {}},
        ]}]}
    rejected = subprocess.run([args.taffy], input=json.dumps(invalid_tree),
                              text=True, capture_output=True, timeout=15)
    assert rejected.returncode != 0 and "Invalid calc tree identity" in rejected.stderr


if __name__ == "__main__":
    main()
