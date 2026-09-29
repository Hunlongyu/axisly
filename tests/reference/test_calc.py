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
    assert set(names) == set(GOLDEN) and len(names) == len(GOLDEN)
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
        assert native["name"] == name and len(native["nodes"]) == 1
        node = native["nodes"][0]
        for field, expected in zip(("rect", "margin", "padding", "border"),
                                   GOLDEN[name], strict=True):
            assert near(case[field], expected), (name, field, case[field], expected)
            assert near(node[field], case[field]), (
                name, field, node[field], case[field])

    bad = dict(fixture)
    bad["cases"] = fixture["cases"] + [fixture["cases"][0]]
    rejected = subprocess.run([args.taffy], input=json.dumps(bad), text=True,
                              capture_output=True, timeout=15)
    assert rejected.returncode != 0 and "Duplicate calc case name" in rejected.stderr


if __name__ == "__main__":
    main()
