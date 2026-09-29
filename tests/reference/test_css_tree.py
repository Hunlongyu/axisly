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
    "block-parent-child-margins",
    "block-empty-margin-chain",
    "block-negative-margin-chain",
    "block-padding-stops-collapse",
    "block-fixed-height-collapse",
    "flex-small-grow-factors",
    "flex-small-shrink-factor",
    "flex-grow-max-freeze",
    "flex-shrink-min-freeze",
    "absolute-auto-margins",
    "absolute-one-auto-margin",
    "absolute-negative-auto-margins",
    "absolute-rtl-overconstraint",
    "absolute-flex-static-center",
    "absolute-flex-static-end",
    "absolute-grid-static-center",
    "absolute-grid-static-end",
    "absolute-block-static-padding",
    "absolute-flex-static-padding-rtl",
    "absolute-grid-static-padding",
    "absolute-block-static-flow",
    "block-min-width-before-children",
    "flex-min-width-before-children",
    "grid-min-width-before-tracks",
    "flex-auto-min-height",
    "grid-auto-min-height",
    "grid-negative-implicit-sequence",
    "block-table-intrinsic",
    "block-replaced-intrinsic",
    "block-table-margin-boundary",
    "overflow-scroll-padding",
    "overflow-rtl-origin",
    "overflow-hidden-boundary",
    "overflow-unreachable",
    "overflow-absolute-padding",
    "overflow-border-origin",
    "grid-span-order-short-first",
    "grid-span-order-long-first",
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
        reference_absolute = []
        for node in reference["nodes"]:
            x, y, width, height = node["rect"]
            if node["parent"] is not None:
                parent_x, parent_y, _, _ = reference_absolute[node["parent"]]
                x += parent_x
                y += parent_y
            reference_absolute.append([x, y, width, height])
        expected = {node["id"]: rect for node, rect in
                    zip(reference["nodes"], reference_absolute, strict=True)}
        actual = {node["id"]: node["rect"] for node in native["nodes"]}
        assert actual.keys() == expected.keys(), (name, actual.keys(), expected.keys())
        for node_id, rect in actual.items():
            assert all(
                math.isfinite(a) and math.isclose(a, b, rel_tol=0, abs_tol=0.01)
                for a, b in zip(rect, expected[node_id], strict=True)
            ), (name, node_id, rect, expected[node_id])
        for node in native["nodes"]:
            target = next(item for item in reference["nodes"] if item["id"] == node["id"])
            for field in ("margin", "padding", "border", "scrollbar", "overflow"):
                assert all(math.isclose(a, b, rel_tol=0, abs_tol=0.01)
                           for a, b in zip(node[field], target[field], strict=True)), (
                    name, node["id"], field, node[field], target[field])


if __name__ == "__main__":
    main()
