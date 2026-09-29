"""Auto-repeat counts, collapsed tracks and implicit placement against Taffy 0.14."""

import argparse
import itertools
import json
import subprocess
from collections import Counter
from dataclasses import dataclass

from grid_detail_compare import compare_grid_details
from test_flex_matrix import compare


PATTERNS = (
    "20px",
    "minmax(10px, 1fr)",
    "minmax(min-content, 20px)",
    "20%",
    "20px 10px",
    "minmax(10px, max-content)",
)
GAPS = (0, 5, "10%")
ITEM_COUNTS = (0, 1, 2, 4)


@dataclass(frozen=True)
class SizeScenario:
    available: int | str
    preferred: int | None = None
    minimum: int | None = None
    maximum: int | None = None


SIZE_SCENARIOS = (
    SizeScenario(95, preferred=95), SizeScenario(35, preferred=35),
    SizeScenario("max-content", maximum=95),
    SizeScenario("max-content", minimum=95),
    SizeScenario("max-content"),
    SizeScenario(95), SizeScenario(35),
    SizeScenario(35, maximum=95), SizeScenario(35, minimum=95),
    SizeScenario(95, maximum=35),
)


def make_cases():
    for axis, repeat_kind, track, constraint, gap, children, rtl in itertools.product(
        range(2), range(2), range(len(PATTERNS)), range(len(SIZE_SCENARIOS)), range(len(GAPS)),
        range(len(ITEM_COUNTS)), range(2)
    ):
        scenario = SIZE_SCENARIOS[constraint]
        columns = axis == 0
        dimension = "width" if columns else "height"
        other = "height" if columns else "width"
        root = {
            "display": "Grid", "direction": "Rtl" if rtl else "Ltr",
            "grid_auto_flow": "Row" if columns else "Column",
            "padding": {edge: 3 for edge in ("left", "top", "right", "bottom")},
            "border": {edge: 1 for edge in ("left", "top", "right", "bottom")},
            "justify_items": "Start", "align_items": "Start",
            "gap": {dimension: GAPS[gap]},
            "grid_template_columns": (
                f"repeat({'auto-fill' if repeat_kind == 0 else 'auto-fit'}, "
                f"{PATTERNS[track]})" if columns else "20px"
            ),
            "grid_template_rows": (
                "20px" if columns else
                f"repeat({'auto-fill' if repeat_kind == 0 else 'auto-fit'}, "
                f"{PATTERNS[track]})"
            ),
            "size": {other: 60},
        }
        if scenario.preferred is not None:
            root["size"][dimension] = scenario.preferred
        if scenario.maximum is not None:
            root["max_size"] = {dimension: scenario.maximum}
        if scenario.minimum is not None:
            root["min_size"] = {dimension: scenario.minimum}
        nodes = [{"id": 1, "parent": None, "style": root}]
        nodes.extend({"id": index + 2, "parent": 0,
                      "style": {"size": {"width": 10, "height": 10}}}
                     for index in range(ITEM_COUNTS[children]))
        available = ([scenario.available, 60] if columns else [60, scenario.available])
        yield {
            "name": f"grid-repeat-matrix-{axis}-{repeat_kind}-{track}-{constraint}-"
                    f"{gap}-{children}-{rtl}",
            "available": available, "nodes": nodes,
        }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--axisly", required=True)
    parser.add_argument("--taffy", required=True)
    parser.add_argument("--details", action="store_true")
    args = parser.parse_args()
    cases = list(make_cases())
    protocol = "AXISLY_TREE_GRID_DETAILS_V1" if args.details else "AXISLY_TREE_V1"
    oracle_cases = []
    for start in range(0, len(cases), 512):
        oracle = json.loads(subprocess.run(
            [args.taffy], input=json.dumps({"protocol": protocol,
                                           "cases": cases[start:start + 512]}),
            text=True, capture_output=True, check=True,
        ).stdout)
        assert oracle["protocol"] == protocol and oracle["taffy"] == "0.14.0"
        oracle_cases.extend(oracle["cases"])
    native = json.loads(subprocess.run(
        [args.axisly] + (["--details"] if args.details else []),
        text=True, capture_output=True, check=True,
    ).stdout)
    assert native["protocol"] == "AXISLY_GRID_REPEAT_MATRIX_V1"
    assert len(cases) == len(oracle_cases) == len(native["cases"]) == (
        2 * 2 * len(PATTERNS) * len(SIZE_SCENARIOS) * len(GAPS) * len(ITEM_COUNTS) * 2
    )
    failures = []
    for actual, expected in zip(native["cases"], oracle_cases, strict=True):
        try:
            compare(actual, expected)
            if args.details:
                compare_grid_details(actual, expected)
        except AssertionError as error:
            failures.append(error.args[0])
    groups = Counter(tuple(failure[0].split("-")[3:7]) for failure in failures)
    assert not failures, (len(failures), groups.most_common(30), failures[:8])


if __name__ == "__main__":
    main()
