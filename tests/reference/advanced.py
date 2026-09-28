"""Fail-closed V2 comparison: automatic placement, percentages and first baselines."""
from __future__ import annotations

import argparse
from collections import Counter
import copy
import json
from pathlib import Path
import random
import subprocess
import sys

import compare

FLOWS = ("explicit", "row", "column", "row_dense", "column_dense")
ALIGNMENTS = compare.ALIGNMENTS + ("baseline",)
CASE_KEYS = compare.CASE_KEYS | {"flow", "implicit", "implicit_tracks"}
ITEM_KEYS = compare.ITEM_KEYS | {"baseline"}
AUTO = 2**32 - 1
require = compare.require


def validate_case(case):
    require(isinstance(case, dict) and set(case) == CASE_KEYS, "Unknown or missing advanced case field")
    require(isinstance(case["name"], str) and 0 < len(case["name"]) <= 200, "Invalid case name")
    for key, length in (("available", 2), ("padding", 4), ("gap", 2), ("implicit_tracks", 2)):
        compare.vector(case[key], length, key)
    require(case["flow"] in FLOWS and type(case["implicit"]) is bool, "Invalid flow or implicit flag")
    for axis, key in enumerate(("columns", "rows")):
        require(case["padding"][axis] + case["padding"][axis + 2] <= case["available"][axis],
                "Padding overflow differs between engines")
        tracks = case[key]
        require(isinstance(tracks, list) and 1 <= len(tracks) <= 16, "Expected 1..16 explicit tracks")
        for track in tracks:
            require(isinstance(track, list) and len(track) == 2 and track[0] in ("fixed", "percentage"),
                    "V2 supports only fixed and percentage tracks")
            compare.vector(track[1:], 1, "track")
    require(isinstance(case["items"], list) and len(case["items"]) <= 128, "Expected at most 128 items")
    unique = set()
    automatic = False
    for item in case["items"]:
        require(isinstance(item, dict) and set(item) == ITEM_KEYS, "Unknown or missing advanced item field")
        ident = item["id"]
        require(type(ident) is int and 0 <= ident < 2**64 and ident not in unique, "Invalid or duplicate item ID")
        unique.add(ident)
        at = item["at"]
        require(isinstance(at, list) and len(at) == 4, "Invalid placement")
        require(all(v is None or type(v) is int and 0 <= v < 16 for v in at[:2]), "Invalid position")
        require(all(type(v) is int and 1 <= v <= 16 for v in at[2:]), "Invalid span")
        if None in at[:2]:
            automatic = True
            require(case["flow"] != "explicit" and case["implicit"], "Automatic cases require a flow and implicit tracks")
        for axis, key in enumerate(("columns", "rows")):
            if at[axis] is not None and not case["implicit"]:
                require(at[axis] + at[axis + 2] <= len(case[key]), "Implicit tracks disabled")
        for key, length in (("margin", 4), ("minimum", 2), ("preferred", 2)):
            compare.vector(item[key], length, key)
        require(all(a <= b for a, b in zip(item["minimum"], item["preferred"])), "Invalid measurement ordering")
        align = item["align"]
        require(isinstance(align, list) and len(align) == 2 and align[0] in compare.ALIGNMENTS and align[1] in ALIGNMENTS,
                "Unsupported alignment")
        if item["baseline"] is not None:
            compare.vector([item["baseline"]], 1, "baseline")
            require(item["baseline"] <= item["preferred"][1], "Baseline exceeds height")
        if align[1] == "baseline":
            require(None not in at[:2] and at[3] == 1, "Baseline subset requires explicit placement and one row")
            require(item["baseline"] is not None or item["minimum"][1] == item["preferred"][1],
                    "Bottom fallback requires equal minimum/preferred height: Taffy probes MinContent baselines")

    if automatic:
        # Placement is tested independently of intrinsic contributions and clipping.
        require(all(i["minimum"] == i["preferred"] == [0, 0] and i["margin"] == [0, 0, 0, 0]
                    and i["align"] == ["stretch", "stretch"] and i["baseline"] is None for i in case["items"]),
                "Automatic subset requires zero metrics/margins and Stretch")
    else:
        # This admission check uses input arithmetic, never an engine's output.
        groups = {}
        for i in case["items"]:
            if i["align"][1] == "baseline":
                b = i["preferred"][1] if i["baseline"] is None else i["baseline"]
                groups[i["at"][1]] = max(groups.get(i["at"][1], 0), i["margin"][1] + b)
        for i in case["items"]:
            for axis, key in enumerate(("columns", "rows")):
                start, span = i["at"][axis], i["at"][axis + 2]
                basis = case["available"][axis] - case["padding"][axis] - case["padding"][axis + 2]
                sizes = [t[1] if t[0] == "fixed" else t[1] * basis for t in case[key]]
                sizes += [case["implicit_tracks"][axis]] * max(0, start + span - len(sizes))
                cell = sum(sizes[start:start + span]) + case["gap"][axis] * (span - 1)
                margin = i["margin"][axis] + i["margin"][axis + 2]
                require(margin <= cell, "Oversized margins differ between engines")
                if i["align"][axis] != "stretch":
                    height = i["preferred"][axis]
                    if axis == 1 and i["align"][1] == "baseline":
                        baseline = height if i["baseline"] is None else i["baseline"]
                        height += groups[start] - i["margin"][1] - baseline
                    require(height + margin <= cell, "Clipped alignment or baseline is outside V2")


def validate_cases(cases):
    require(isinstance(cases, list) and 1 <= len(cases) <= 10000, "Expected 1..10000 cases")
    names = set()
    for case in cases:
        validate_case(case)
        require(case["name"] not in names, "Duplicate case name")
        names.add(case["name"])


def protocol(cases):
    validate_cases(cases)
    lines = [f"GRIDLY_REFERENCE_V2 {len(cases)}"]
    for c in cases:
        lines.append(" ".join(map(str, c["available"] + c["padding"] + c["gap"] +
                                  [len(c["columns"]), len(c["rows"]), len(c["items"])])))
        lines.extend(f"{'F' if t[0] == 'fixed' else 'P'} {t[1]}" for t in c["columns"] + c["rows"])
        lines.append(" ".join(map(str, [FLOWS.index(c["flow"]), int(c["implicit"])] + c["implicit_tracks"])))
        for i in c["items"]:
            values = [i["id"]] + [AUTO if v is None else v for v in i["at"]] + i["margin"]
            values += [compare.ALIGNMENTS.index(i["align"][0]), ALIGNMENTS.index(i["align"][1])]
            values += i["minimum"] + i["preferred"] + [int(i["baseline"] is not None)]
            if i["baseline"] is not None:
                values.append(i["baseline"])
            lines.append(" ".join(map(str, values)))
    return "\n".join(lines) + "\n"


def parse_output(text, cases):
    lines = text.splitlines()
    require(len(lines) == len(cases), "Runner returned the wrong number of cases")
    results = [compare.strict_json(line) for line in lines]
    normalized = copy.deepcopy(results)
    for result in normalized:
        require(isinstance(result, dict) and isinstance(result.get("items"), list), "Malformed advanced result")
        for i in result["items"]:
            require(isinstance(i, dict) and set(i) == {"id", "rect", "cell", "at"}, "Malformed advanced item")
            at = i.pop("at")
            require(isinstance(at, list) and len(at) == 4 and all(type(v) is int and 0 <= v <= 4096 for v in at)
                    and at[2] > 0 and at[3] > 0, "Invalid resolved placement")
    compare.parse_output("\n".join(json.dumps(r) for r in normalized), cases)
    return results


def run_tool(path, cases):
    result = subprocess.run([str(path)], input=protocol(cases), text=True, encoding="utf-8", capture_output=True,
                            timeout=60, creationflags=subprocess.CREATE_NO_WINDOW if compare.os.name == "nt" else 0)
    require(result.returncode == 0, f"{path.name} exited {result.returncode}: {result.stderr.strip()}")
    return parse_output(result.stdout, cases)


def difference(a, b):
    for x, y in zip(a["items"], b["items"]):
        if x["at"] != y["at"]:
            return f"item {x['id']} placement: gridly={x['at']}, taffy={y['at']}"
    return compare.difference(a, b)


def item(ident=0, at=None, **kwargs):
    return {**compare.item(ident, at, **kwargs), "baseline": None}


def fixture(name, columns, rows, items, **kwargs):
    return {**compare.fixture(name, columns, rows, items, **kwargs), "flow": "explicit", "implicit": False,
            "implicit_tracks": [37, 29]}


def random_cases(seed, count):
    rng = random.Random(seed)
    cases = []
    for flow in FLOWS[1:]:
        for n in range(count):
            nc, nr = rng.randint(2, 6), rng.randint(2, 5)
            children = []
            for ident in range(rng.randint(4, 30)):
                col = rng.randrange(nc) if rng.randrange(3) == 0 else None
                row = rng.randrange(nr) if rng.randrange(3) == 0 else None
                children.append(item(ident, [col, row, rng.randint(1, 2), rng.randint(1, 2)]))
            rng.shuffle(children)
            c = fixture(f"random-{flow}-{n}", [["fixed", rng.randint(20, 80)] for _ in range(nc)],
                        [["fixed", rng.randint(20, 80)] for _ in range(nr)], children,
                        padding=[3, 7, 11, 5], gap=[rng.randrange(8), rng.randrange(8)])
            c.update(flow=flow, implicit=True)
            cases.append(c)
    for n in range(count):
        nc, nr = rng.randint(1, 6), rng.randint(1, 4)
        track = lambda: ["percentage", rng.randint(1, 8) / 8] if rng.randrange(3) else ["fixed", rng.randint(20, 100)]
        cases.append(fixture(f"random-percentage-{n}", [track() for _ in range(nc)], [track() for _ in range(nr)],
                            [item(i, [rng.randrange(nc), rng.randrange(nr), 1, 1]) for i in range(12)],
                            available=[rng.randint(200, 1500), rng.randint(200, 1200)], padding=[7, 13, 17, 5], gap=[3, 9]))
        children = []
        for i in range(12):
            child = item(i, [i % 4, i // 4, 1, 1], preferred=[rng.randint(5, 40), rng.randint(10, 50)],
                         margin=[2, rng.randint(0, 6), 3, rng.randint(0, 6)], align=["start", "baseline"])
            if rng.randrange(3):
                child["baseline"] = rng.randint(0, int(child["preferred"][1]))
            else:
                child["minimum"][1] = child["preferred"][1]
            children.append(child)
        rng.shuffle(children)
        cases.append(fixture(f"random-baseline-{n}", [["fixed", 80]] * 4, [["fixed", 130]] * 3,
                            children, padding=[9, 3, 8, 11], gap=[3, 7]))
    return cases


def load_cases(path):
    doc = compare.strict_json(Path(path).read_text(encoding="utf-8"))
    require(isinstance(doc, dict) and type(doc.get("schema")) is int and doc["schema"] == 1, "Unsupported fixture schema")
    validate_cases(doc.get("cases"))
    return doc["cases"]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gridly", type=Path, required=True)
    parser.add_argument("--taffy", type=Path, required=True)
    parser.add_argument("--fixtures", type=Path, default=Path(__file__).with_name("fixtures") / "advanced.json")
    parser.add_argument("--replay", type=Path)
    parser.add_argument("--random-cases", type=int, default=150, help="cases per family (six families)")
    parser.add_argument("--seed", type=int, default=20260928)
    parser.add_argument("--failure-dir", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        require(0 <= args.random_cases <= 1500, "Random count outside limits")
        cases = load_cases(args.replay or args.fixtures)
        if not args.replay:
            cases += random_cases(args.seed, args.random_cases)
        validate_cases(cases)
        try:
            native, reference = run_tool(args.gridly, cases), run_tool(args.taffy, cases)
        except (ValueError, OSError, subprocess.TimeoutExpired) as error:
            path = compare.save_failure(args.failure_dir, cases, {"error": str(error), "seed": args.seed})
            print(f"FAIL runner: {error}\nReplay fixture: {path}", file=sys.stderr)
            return 1
        for c, a, b in zip(cases, native, reference):
            mismatch = difference(a, b)
            if mismatch:
                path = compare.save_failure(args.failure_dir, [c], {"error": mismatch, "gridly": a, "taffy": b})
                print(f"FAIL {c['name']}: {mismatch}\nReplay fixture: {path}", file=sys.stderr)
                return 1
        families = Counter(c["name"].split("-")[1] if c["name"].startswith("random-") else "fixture" for c in cases)
        print(f"PASS {len(cases)} advanced cases {dict(families)}, seed={args.seed}; Taffy 0.14.0 unrounded; "
              f"abs={compare.ABS_EPSILON}, rel={compare.REL_EPSILON}; placement, cells, rectangles, extents")
        return 0
    except (ValueError, OSError) as error:
        print(f"Invalid advanced reference input: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
