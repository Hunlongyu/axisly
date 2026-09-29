"""Compare supported native CSS tree fixtures with pinned Taffy 0.14.0."""

import argparse
import json
import math
import subprocess
from pathlib import Path


CASES = (
    "flex-row-grow",
    "content-box-and-negative-margin",
    "flex-balance-lines",
    "css-grid-tracks-placement",
    "named-area-placement",
    "grid-auto-fill",
    "grid-auto-fit",
    "hidden-and-positioned",
    "block-float-clear",
    "block-adjacent-margins",
    "grid-content-alignment",
    "flex-intrinsic-basis",
    "flex-baseline",
    "grid-baseline",
    "grid-auto-margins",
    "rtl-block-child",
    "flex-scrollbar-reservation",
    "block-legacy-text-align",
    "block-contain-float",
    "grid-absolute-area",
    "grid-implicit-negative-lines",
    "grid-row-dense-holes",
    "grid-named-line-occurrences",
    "mixed-block-flex-grid",
)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--axisly", required=True)
    parser.add_argument("--taffy", required=True)
    parser.add_argument("--fixture", required=True)
    args = parser.parse_args()
    fixture = Path(args.fixture).read_text(encoding="utf-8")
    result = subprocess.run(
        [args.taffy], input=fixture, text=True, capture_output=True, check=True
    )
    oracle = json.loads(result.stdout)
    assert oracle["protocol"] == "AXISLY_TREE_V1" and oracle["taffy"] == "0.14.0"
    by_name = {case["name"]: case for case in oracle["cases"]}
    for name in CASES:
        native = json.loads(
            subprocess.run(
                [args.axisly, name], text=True, capture_output=True, check=True
            ).stdout
        )
        reference = by_name[name]
        expected = {node["id"]: node["rect"] for node in reference["nodes"]}
        actual = {node["id"]: node["rect"] for node in native["nodes"]}
        assert actual.keys() == expected.keys(), (name, actual.keys(), expected.keys())
        for node_id, rect in actual.items():
            assert all(
                math.isfinite(a) and math.isclose(a, b, rel_tol=0, abs_tol=0.01)
                for a, b in zip(rect, expected[node_id], strict=True)
            ), (name, node_id, rect, expected[node_id])
        for node in native["nodes"]:
            target = next(item for item in reference["nodes"] if item["id"] == node["id"])
            for field in ("margin", "padding", "border", "scrollbar"):
                assert all(math.isclose(a, b, rel_tol=0, abs_tol=0.01)
                           for a, b in zip(node[field], target[field], strict=True)), (
                    name, node["id"], field, node[field], target[field])


if __name__ == "__main__":
    main()
