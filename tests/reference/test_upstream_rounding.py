"""Exercise production pixel rounding against the pinned Taffy implementation."""
import argparse
import json
from pathlib import Path
import subprocess

from test_flex_matrix import compare
from grid_detail_compare import compare_grid_details


def run(command, document=None):
    result = subprocess.run(command, input=None if document is None else json.dumps(document),
                            text=True, encoding="utf-8", capture_output=True, timeout=120, check=True)
    return json.loads(result.stdout)


def main():
    parser = argparse.ArgumentParser()
    for key in ("cases", "axisly", "taffy", "output"):
        parser.add_argument("--" + key, required=True)
    args = parser.parse_args()
    document = json.loads(Path(args.cases).read_text(encoding="utf-8"))
    cases = document["cases"]
    failures = []
    nodes = 0
    for first in range(0, len(cases), 100):
        batch = cases[first:first + 100]
        native = run([args.axisly, "--first", str(first), "--count", str(len(batch)), "--rounding"])
        inputs = [dict({key: case[key] for key in ("name", "available", "nodes")}, rounding=True) for case in batch]
        oracle = run([args.taffy], {"protocol": "AXISLY_TREE_GRID_DETAILS_V1", "cases": inputs})
        assert oracle["taffy"] == "0.14.0"
        assert len(native["cases"]) == len(oracle["cases"]) == len(batch)
        for case, actual, expected in zip(batch, native["cases"], oracle["cases"], strict=True):
            assert actual["name"] == expected["name"] == case["name"]
            nodes += len(case["nodes"])
            try:
                assert "error" not in actual and "error" not in expected, (actual, expected)
                compare(actual, expected)
                compare_grid_details(actual, expected)
            except AssertionError as error:
                failures.append({"name": case["name"], "difference": error.args[0]})
    report = {"total": len(cases), "passed": len(cases) - len(failures), "nodes": nodes,
              "taffy": "0.14.0", "commit": document["commit"], "failures": failures}
    Path(args.output).write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: value for key, value in report.items() if key != "failures"}))
    if failures:
        print(json.dumps(failures[:12], indent=2))
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
