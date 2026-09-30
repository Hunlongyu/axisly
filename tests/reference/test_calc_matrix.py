"""Calc callback expressions in four-level mixed and replaced-content trees."""

import argparse
import itertools
import json
from pathlib import Path
import subprocess

from test_flex_matrix import compare


def calc(length, percent):
    return {"op": "add", "left": {"op": "length", "value": length},
            "right": {"op": "percent", "value": percent}}


def common(display, content_box, rtl):
    return {"display": display, "content_box": bool(content_box),
            "direction": "Rtl" if rtl else "Ltr"}


def cases():
    displays = ("Block", "Flex", "Grid")
    for outer, middle, inner, space, variant, box, replaced, rtl in itertools.product(
            range(3), range(3), range(3), range(3), range(4), range(2), range(2), range(2)):
        root = common(displays[outer], box, rtl) | {"gap_width": calc(3, .1), "gap_height": calc(2, .05)}
        if variant == 0:
            root.update(width=calc(30, .7), height=calc(20, .3))
        elif variant == 3:
            root["width"] = {"op": "clamp", "lower": {"op": "length", "value": 150},
                             "value": {"op": "percent", "value": .5},
                             "upper": {"op": "length", "value": 100}}
        parent = common(displays[middle], box, rtl) | {
            "padding_left": calc(2, .02), "padding_top": calc(1, .01),
            "border_left": {"op": "length", "value": 1},
            "min_width": calc(10, .05), "max_width": calc(140, .1), "gap_width": calc(2, .05)}
        if variant == 0:
            parent["width"] = calc(20, .5)
        child = common(displays[inner], box, rtl) | {
            "height": calc(15, .25), "margin_left": calc(-2 if variant == 3 else 2, .01), "flex_grow": .5}
        child.update({"intrinsic_width": "min-content"} if variant == 2 else {"width": calc(10, .5)})
        leaf = common("Block", box, rtl) | {"item_is_replaced": bool(replaced),
            "aspect_ratio": 2, "max_width": calc(120, -.1)}
        if variant == 1:
            leaf["width"] = calc(30, .75)
        elif variant == 0:
            leaf.update(width=calc(10, .6), height=calc(10, 0))
        else:
            leaf["height"] = {"op": "max", "left": {"op": "length", "value": 0},
                "right": {"op": "subtract", "left": {"op": "length", "value": 15},
                          "right": {"op": "percent", "value": .05}}}
        tail = common("Block", box, rtl) | {"width": calc(10, .05), "height": calc(8, 0)}
        yield {"name": "calc-matrix-" + "-".join(map(str, (outer, middle, inner, space, variant, box, replaced, rtl))),
            "available": ([250, 100], ["max-content"] * 2, ["min-content"] * 2)[space],
            "nodes": [{"id": 1, "parent": None, "style": root},
                      {"id": 2, "parent": 0, "style": parent},
                      {"id": 3, "parent": 1, "style": child},
                      {"id": 4, "parent": 2, "content": [70, 20], "style": leaf},
                      {"id": 5, "parent": 0, "content": [10, 8], "style": tail}]}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--axisly", required=True)
    parser.add_argument("--taffy", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    inputs = list(cases())
    native = json.loads(subprocess.run([args.axisly], text=True, capture_output=True,
                                      check=True, timeout=120).stdout)
    assert native["protocol"] == "AXISLY_CALC_MATRIX_V1"
    assert len(native["cases"]) == len(inputs) == 2592
    failures = []
    for start in range(0, len(inputs), 128):
        reference = json.loads(subprocess.run([args.taffy], input=json.dumps({
            "protocol": "AXISLY_CALC_V1", "cases": inputs[start:start + 128]}),
            text=True, capture_output=True, check=True, timeout=120).stdout)
        assert reference["taffy"] == "0.14.0"
        for case, observed, expected in zip(inputs[start:start + 128],
                native["cases"][start:start + 128], reference["cases"], strict=True):
            assert observed["name"] == expected["name"] == case["name"]
            try:
                compare(observed, expected)
            except AssertionError as error:
                failures.append(error.args[0])
    Path(args.output).write_text(json.dumps({"total": len(inputs),
        "passed": len(inputs) - len(failures), "failures": failures}, indent=2) + "\n", encoding="utf-8")
    assert not failures, (len(failures), failures[:12])


if __name__ == "__main__":
    main()
