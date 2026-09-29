"""Named areas, generated lines, and repeated templates against Taffy 0.14."""

import argparse
import itertools
import json
import subprocess
from collections import Counter

from grid_detail_compare import compare_grid_details
from test_flex_matrix import compare


AREAS = ((('a', 'a'), ('b', 'c')),
         (('a', 'b'), ('a', 'c')),
         (('a', 'a', '.'), ('a', 'a', 'b'), ('c', 'c', 'b')))
TEMPLATES = ("[q] 20px [q] 25px [q]", "20px",
             "[a-start] repeat(2, [q] 20px) [a-end]",
             "[a-start] repeat(auto-fill, [q] 20px) [a-end]",
             "[a-start] repeat(auto-fit, [q] 20px) [a-end]")
PLACEMENTS = (
    ("a", "a"), ("a-start", "a-end"), ("a", "auto"),
    ("auto", "a"), ("1", "a"), ("a", "-1"),
    ("span 2 a", "a-end"), ("b", "c"),
)
FLOWS = ("Row", "Column", "RowDense", "ColumnDense")


def area_definition(cells):
    bounds = {}
    for row, values in enumerate(cells, start=1):
        for column, name in enumerate(values, start=1):
            if name == '.':
                continue
            extent = bounds.setdefault(name, [row, row + 1, column, column + 1])
            extent[0] = min(extent[0], row)
            extent[1] = max(extent[1], row + 1)
            extent[2] = min(extent[2], column)
            extent[3] = max(extent[3], column + 1)
    return {"areas": [dict(name=name, row_start=box[0], row_end=box[1],
                           column_start=box[2], column_end=box[3])
                      for name, box in bounds.items()],
            "row_count": len(cells), "column_count": len(cells[0])}


def make_cases():
    for areas, tracks, endpoints, axis, rtl, flow, sizing, query in itertools.product(
        range(len(AREAS)), range(len(TEMPLATES)), range(len(PLACEMENTS)), range(2), range(2),
        range(len(FLOWS)), range(2), range(3)
    ):
        root = {
            "display": "Grid",
            "padding": {edge: 4 for edge in ("left", "top", "right", "bottom")},
            "border": {edge: 1 for edge in ("left", "top", "right", "bottom")},
            "direction": "Rtl" if rtl else "Ltr",
            "gap": {"width": 3, "height": 3},
            "grid_auto_flow": FLOWS[flow],
            "grid_template_areas": area_definition(AREAS[areas]),
            "grid_template_columns": TEMPLATES[tracks],
            "grid_template_rows": TEMPLATES[tracks],
            "justify_items": "Stretch" if sizing else "Start",
            "align_items": "Stretch" if sizing else "Start",
        }
        if query == 0:
            root["size"] = {"width": 120, "height": 100}
        selected = {"start": PLACEMENTS[endpoints][0],
                    "end": PLACEMENTS[endpoints][1]}
        area_a = {"start": "a", "end": "a"}
        first = {
            "grid_column": selected if axis == 0 else area_a,
            "grid_row": selected if axis == 1 else area_a,
        }
        if not sizing:
            first["size"] = {"width": 10, "height": 10}
        nodes = [
            {"id": 1, "parent": None, "style": root},
            {"id": 2, "parent": 0, "style": first},
            {"id": 3, "parent": 0, "style": {"size": {"width": 10, "height": 10}}},
            {"id": 4, "parent": 0, "style": {
                "size": {"width": 10, "height": 10},
                "grid_column": {"start": "b", "end": "b"},
                "grid_row": {"start": "b", "end": "b"},
            }},
        ]
        available = [120, 100] if query == 0 else [
            "max-content" if query == 1 else "min-content"
        ] * 2
        yield {"name": f"grid-area-matrix-{areas}-{tracks}-{endpoints}-{axis}-"
                       f"{rtl}-{flow}-{sizing}-{query}", "available": available,
               "nodes": nodes}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--axisly", required=True)
    parser.add_argument("--taffy", required=True)
    parser.add_argument("--details", action="store_true")
    args = parser.parse_args()
    cases = list(make_cases())
    protocol = "AXISLY_TREE_GRID_DETAILS_V1" if args.details else "AXISLY_TREE_V1"
    oracle_cases = []
    for offset in range(0, len(cases), 512):
        oracle = json.loads(subprocess.run(
            [args.taffy], input=json.dumps({"protocol": protocol,
                                           "cases": cases[offset:offset + 512]}),
            text=True, capture_output=True, check=True,
        ).stdout)
        assert oracle["protocol"] == protocol and oracle["taffy"] == "0.14.0"
        oracle_cases.extend(oracle["cases"])
    native = json.loads(subprocess.run(
        [args.axisly] + (["--details"] if args.details else []),
        text=True, capture_output=True, check=True,
    ).stdout)
    assert native["protocol"] == "AXISLY_GRID_AREA_MATRIX_V1"
    assert len(cases) == len(oracle_cases) == len(native["cases"]) == 11520
    failures = []
    for actual, expected in zip(native["cases"], oracle_cases, strict=True):
        try:
            compare(actual, expected)
            if args.details:
                compare_grid_details(actual, expected)
        except AssertionError as error:
            failures.append(error.args[0])
    groups = Counter(tuple(failure[0].split("-")[3:6]) for failure in failures)
    assert not failures, (len(failures), groups.most_common(20), failures[:8])


if __name__ == "__main__":
    main()
