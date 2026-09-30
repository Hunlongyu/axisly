"""Typed calc track functions, repetition, implicit axes and mixed children."""
import argparse
import copy
import itertools
import json
import subprocess
from pathlib import Path
from test_calc_matrix import calc
from test_flex_matrix import compare
from grid_detail_compare import compare_grid_details


def length(value):
    return {"op": "length", "value": value}


def track(a, b=None):
    return {"minimum": a, "maximum": b if b is not None else a}


def cases():
    for variant, space, definite, rows, repeat, rtl, implicit in itertools.product(
            range(5), range(3), range(2), range(2), range(4), range(2), range(2)):
        if variant == 0:
            tracks = [track(calc(20, .1)), track(calc(35, .05))]
        elif variant == 1:
            tracks = [track(calc(10, .05), calc(80, .1)), track(calc(15, .02), calc(60, .08))]
        elif variant == 2:
            tracks = [track({"op": "clamp", "lower": length(25),
                             "value": {"op": "percent", "value": .1}, "upper": length(10)}),
                      track(calc(15, .1))]
        elif variant == 3:
            tracks = [track({"op": "max", "left": length(0), "right": calc(0, -.1)}),
                      track(calc(20, .1))]
        else:
            tracks = [{"minimum_length": 10, "maximum": calc(80, .1)},
                      {"minimum_length": 15, "maximum": calc(60, .08)}]
        template = {"kind": ("single", "count", "auto-fill", "auto-fit")[repeat], "tracks": tracks}
        if repeat == 1:
            template["count"] = 2
        auto = [track(calc(15, .05)), track({"op": "max", "left": length(0), "right": calc(50, -.05)})]
        direction = "Rtl" if rtl else "Ltr"
        root = {"display": "Grid", "direction": direction,
                "gap_width": calc(3, .02), "gap_height": calc(2, .01),
                "calc_rows" if rows else "calc_columns": [template],
                "grid_template_columns" if rows else "grid_template_rows": "60px",
                "calc_auto_columns": auto, "calc_auto_rows": auto}
        if definite:
            root.update({"width": calc(400, 0), "height": calc(180, 0)})
        nodes = [{"id": 1, "parent": None, "style": root}]
        for index, display in enumerate(("Block", "Flex", "Grid")):
            parent = len(nodes)
            style = {"display": display, "direction": direction,
                     "min_width": length(0), "min_height": length(0)}
            if implicit and index != 1:
                style["grid_row_start" if rows else "grid_column_start"] = -12 if index == 0 else 12
            nodes.append({"id": 2 + index * 3, "parent": 0, "style": style})
            for leaf in range(2):
                nodes.append({"id": 3 + index * 3 + leaf, "parent": parent,
                              "content": [30, 15] if leaf else [50, 10],
                              "style": {"display": "Block", "direction": direction,
                                        "height": calc(10, .1), "min_width": length(0)}})
        yield {"name": f"calc-tracks-{variant}-{space}-{definite}-{rows}-{repeat}-{rtl}-{implicit}",
               "available": [[500, 250], ["max-content"] * 2, ["min-content"] * 2][space],
               "nodes": nodes}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--axisly", required=True)
    parser.add_argument("--taffy", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    inputs = list(cases())
    # The oracle must reject malformed fixtures instead of comparing a silently
    # substituted template. These cases do not enter the geometry denominator.
    bad_inputs = []
    for mutation in ("zero-count", "ambiguous-minimum", "unknown-kind", "conflict", "empty"):
        case = copy.deepcopy(inputs[0])
        template = case["nodes"][0]["style"]["calc_columns"][0]
        if mutation == "zero-count":
            template.update(kind="count", count=0)
        elif mutation == "ambiguous-minimum":
            template["tracks"][0]["minimum_length"] = 10
        elif mutation == "unknown-kind":
            template["kind"] = "automatic"
        elif mutation == "conflict":
            case["nodes"][0]["style"]["grid_template_columns"] = "20px"
        else:
            template["tracks"] = []
        bad_inputs.append(case)
    for case in bad_inputs:
        rejected = subprocess.run([args.taffy], input=json.dumps({
            "protocol": "AXISLY_CALC_V1", "cases": [case]}),
            text=True, capture_output=True, timeout=15)
        assert rejected.returncode != 0 and not rejected.stdout.strip(), rejected
    native = json.loads(subprocess.run([args.axisly], capture_output=True, text=True,
                                      check=True, timeout=120).stdout)
    assert native["protocol"] == "AXISLY_CALC_TRACKS_V1"
    assert len(native["cases"]) == len(inputs) == 960
    failures = []
    for start in range(0, len(inputs), 64):
        reference = json.loads(subprocess.run([args.taffy], input=json.dumps({
            "protocol": "AXISLY_CALC_V1", "cases": inputs[start:start + 64]}),
            text=True, capture_output=True, check=True, timeout=120).stdout)
        assert reference["taffy"] == "0.14.0"
        for case, got, want in zip(inputs[start:start + 64],
                native["cases"][start:start + 64], reference["cases"], strict=True):
            assert got["name"] == want["name"] == case["name"]
            try:
                compare(got, want)
                compare_grid_details(got, want)
            except AssertionError as error:
                failures.append(error.args[0])
    Path(args.output).write_text(json.dumps({"total": len(inputs), "passed": len(inputs) - len(failures),
                                           "rejected_invalid_inputs": len(bad_inputs),
                                           "failures": failures}, indent=2) + "\n", encoding="utf-8")
    assert not failures, (len(failures), failures[:12])


if __name__ == "__main__":
    main()
