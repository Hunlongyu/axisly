"""Optional, standard-library-only differential driver. See docs/testing.md."""
from __future__ import annotations

import argparse
from collections import Counter
import json
import math
import os
from pathlib import Path
import random
import subprocess
import sys

ABS_EPSILON = 1e-4
REL_EPSILON = 1e-6
ALIGNMENTS = ("start", "center", "end", "stretch")
CASE_KEYS = {"name", "available", "padding", "gap", "columns", "rows", "items"}
ITEM_KEYS = {"id", "at", "margin", "align", "minimum", "preferred"}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def vector(value, length, label):
    require(isinstance(value, list) and len(value) == length, f"{label}: expected {length} numbers")
    require(all(type(x) in (float, int) and 0 <= x <= 1_000_000 and math.isfinite(x) for x in value),
            f"{label}: numbers must be finite, nonnegative and <= 1000000")


def validate_case(case):
    require(isinstance(case, dict) and set(case) == CASE_KEYS, "Unknown or missing case field")
    require(isinstance(case["name"], str) and 0 < len(case["name"]) <= 200, "Invalid case name")
    vector(case["available"], 2, "available")
    vector(case["padding"], 4, "padding")
    vector(case["gap"], 2, "gap")
    for axis in ("columns", "rows"):
        tracks = case[axis]
        require(isinstance(tracks, list) and 1 <= len(tracks) <= 16, "Common subset requires 1..16 explicit tracks on each axis")
        for track in tracks:
            require(isinstance(track, list) and bool(track), "Invalid track")
            kind = track[0]
            require(kind in ("fixed", "weighted", "range", "content"), "Unsupported track kind")
            expected = 1 if kind == "content" else 2 if kind == "fixed" else 3
            require(len(track) == expected, "Unsupported track bounds or extra track fields")
            vector(track[1:], expected - 1, "track parameters")
            if kind in ("weighted", "range"):
                require(track[1] >= 1, "Common subset requires individual weights >= 1; CSS fractions below one differ")
    require(isinstance(case["items"], list) and len(case["items"]) <= 128, "Expected at most 128 items")
    ids = set()
    for item in case["items"]:
        require(isinstance(item, dict) and set(item) == ITEM_KEYS, "Unknown or missing item field")
        ident = item["id"]
        require(type(ident) is int and 0 <= ident < 2**64 and ident not in ids, "Invalid or duplicate item ID")
        ids.add(ident)
        at = item["at"]
        require(isinstance(at, list) and len(at) == 4 and all(type(x) is int and x >= 0 for x in at), "Invalid placement")
        require(at[2] > 0 and at[3] > 0 and at[0] + at[2] <= len(case["columns"])
                and at[1] + at[3] <= len(case["rows"]), "Invalid explicit span")
        vector(item["margin"], 4, "margin")
        vector(item["minimum"], 2, "minimum")
        vector(item["preferred"], 2, "preferred")
        require(all(m <= p for m, p in zip(item["minimum"], item["preferred"])), "Invalid measurement ordering")
        require(isinstance(item["align"], list) and len(item["align"]) == 2
                and all(x in ALIGNMENTS for x in item["align"]), "Unsupported alignment")

    # Eligibility follows input-only rules, never either engine's output. No skipped cases.
    for axis, key in enumerate(("columns", "rows")):
        tracks = case[key]
        content = any(t[0] == "content" for t in tracks)
        flexible = any(t[0] in ("weighted", "range") for t in tracks)
        require(not (content and flexible), "Content and flexible tracks on the same axis are outside this common subset")
        if flexible:
            for item in case["items"]:
                require(item["minimum"][axis] == item["preferred"][axis] == 0
                        and item["margin"][axis] == item["margin"][axis + 2] == 0
                        and item["align"][axis] == "stretch",
                        "Flexible-axis comparison requires zero item metrics/margins and Stretch (explicit track floors remain supported)")
            continue
        sizes = [t[1] if t[0] == "fixed" else 0 for t in tracks]
        if content:
            for item in case["items"]:
                require(item["at"][axis + 2] == 1, "Spans on an axis with Content are outside this common subset")
                start = item["at"][axis]
                if tracks[start][0] == "content":
                    sizes[start] = max(sizes[start], item["preferred"][axis] + item["margin"][axis] + item["margin"][axis + 2])
            extent = sum(sizes) + case["gap"][axis] * (len(sizes) - 1) + case["padding"][axis] + case["padding"][axis + 2]
            require(case["available"][axis] >= extent, "Content shrink/overflow is outside this common subset")
        for item in case["items"]:
            start, span = item["at"][axis], item["at"][axis + 2]
            cell = sum(sizes[start:start + span]) + case["gap"][axis] * (span - 1)
            margin = item["margin"][axis] + item["margin"][axis + 2]
            require(margin <= cell, "Oversized margins differ between the engines")
            if item["align"][axis] != "stretch":
                require(item["preferred"][axis] <= cell - margin, "Clipped non-Stretch preferences are outside this common subset")


def validate_cases(cases):
    require(isinstance(cases, list) and 1 <= len(cases) <= 10000, "Expected 1..10000 cases")
    names = set()
    for case in cases:
        validate_case(case)
        require(case["name"] not in names, "Duplicate case name")
        names.add(case["name"])


def protocol(cases):
    validate_cases(cases)
    lines = [f"AXISLY_REFERENCE_V1 {len(cases)}"]
    for case in cases:
        values = case["available"] + case["padding"] + case["gap"]
        values += [len(case["columns"]), len(case["rows"]), len(case["items"])]
        lines.append(" ".join(map(str, values)))
        for track in case["columns"] + case["rows"]:
            lines.append(" ".join(map(str, [{"fixed": "F", "weighted": "W", "range": "R", "content": "C"}[track[0]]] + track[1:])))
        for item in case["items"]:
            values = [item["id"]] + item["at"] + item["margin"]
            values += [ALIGNMENTS.index(a) for a in item["align"]] + item["minimum"] + item["preferred"]
            lines.append(" ".join(map(str, values)))
    return "\n".join(lines) + "\n"


def strict_json(text):
    def invalid(value):
        raise ValueError(f"Non-finite JSON token: {value}")
    def unique(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, f"Duplicate JSON key: {key}")
            result[key] = value
        return result
    return json.loads(text, parse_constant=invalid, object_pairs_hook=unique)


def load_cases(path):
    document = strict_json(Path(path).read_text(encoding="utf-8"))
    require(isinstance(document, dict) and type(document.get("schema")) is int and document["schema"] == 1, "Unsupported fixture schema")
    cases = document.get("cases")
    validate_cases(cases)
    return cases


def parse_output(text, cases):
    lines = text.splitlines()
    require(len(lines) == len(cases), "Runner returned the wrong number of cases")
    output = []
    for index, (line, case) in enumerate(zip(lines, cases)):
        result = strict_json(line)
        require(isinstance(result, dict) and set(result) == {"case", "size", "items"}, "Malformed runner result")
        require(type(result["case"]) is int and result["case"] == index, "Runner case order/identity mismatch")
        require(isinstance(result["items"], list) and len(result["items"]) == len(case["items"]), "Runner item count mismatch")
        arrays = [(result["size"], 2)]
        for item, source in zip(result["items"], case["items"]):
            require(isinstance(item, dict) and set(item) == {"id", "rect", "cell"}
                    and type(item["id"]) is int and item["id"] == source["id"], "Runner item identity/order mismatch")
            arrays.extend(((item["rect"], 4), (item["cell"], 4)))
        for values, length in arrays:
            require(isinstance(values, list) and len(values) == length and all(
                type(x) in (int, float) and 0 <= x <= 3.4028234663852886e38 and math.isfinite(x)
                for x in values), "Runner returned invalid geometry")
        output.append(result)
    return output


def run_tool(path, cases):
    completed = subprocess.run([str(path)], input=protocol(cases), text=True, encoding="utf-8",
                               capture_output=True, timeout=60,
                               creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
    require(completed.returncode == 0, f"{path.name} exited {completed.returncode}: {completed.stderr.strip()}")
    return parse_output(completed.stdout, cases)


def difference(actual, reference):
    pairs = [("size", actual["size"], reference["size"])]
    for a, b in zip(actual["items"], reference["items"]):
        for key in ("rect", "cell"):
            pairs.append((f"item {a['id']}.{key}", a[key], b[key]))
    for label, left, right in pairs:
        for axis, (a, b) in enumerate(zip(left, right)):
            tolerance = ABS_EPSILON + REL_EPSILON * max(abs(a), abs(b))
            if abs(a - b) > tolerance:
                return f"{label}[{axis}]: axisly={a}, taffy={b}, tolerance={tolerance}"
    return None


def save_failure(directory, cases, diagnostic):
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / "failure.json"
    path.write_text(json.dumps({"schema": 1, "cases": cases, "diagnostic": diagnostic}, indent=2,
                               ensure_ascii=False, allow_nan=False) + "\n", encoding="utf-8")
    return path


def item(ident=0, at=None, margin=None, align=None, minimum=None, preferred=None):
    return {"id": ident, "at": at or [0, 0, 1, 1], "margin": margin or [0, 0, 0, 0],
            "align": align or ["stretch", "stretch"], "minimum": minimum or [0, 0], "preferred": preferred or [0, 0]}


def fixture(name, columns, rows, items, available=None, padding=None, gap=None):
    return {"name": name, "columns": columns, "rows": rows, "items": items, "available": available or [500, 500],
            "padding": padding or [0, 0, 0, 0], "gap": gap or [0, 0]}


def random_cases(seed, count):
    rng = random.Random(seed)
    cases = []
    for family in ("fixed", "flexible", "content"):
        for sample in range(count):
            nc, nr = rng.randint(1, 5), rng.randint(1, 4)
            def track():
                if family == "fixed":
                    return ["fixed", rng.randint(80, 600) / 4]
                if family == "content":
                    return ["content"] if rng.randrange(3) else ["fixed", rng.randint(80, 600) / 4]
                choice = rng.choice(("fixed", "weighted", "range"))
                return ["fixed", rng.randint(0, 300) / 4] if choice == "fixed" else [choice, rng.randint(1, 12), rng.randint(0, 400) / 4]
            columns, rows = [track() for _ in range(nc)], [track() for _ in range(nr)]
            if family == "flexible":
                columns[0] = ["weighted", rng.randint(1, 5), rng.randint(0, 100)]
                rows[0] = ["range", rng.randint(1, 5), rng.randint(0, 50)]
            elif family == "content":
                columns[0], rows[0] = ["content"], ["content"]
            children = []
            for ident in range(rng.randint(1, 14)):
                column, row = rng.randrange(nc), rng.randrange(nr)
                cs = 1 if family == "content" else rng.randint(1, nc - column)
                rs = 1 if family == "content" else rng.randint(1, nr - row)
                if family == "flexible":
                    children.append(item(ident, [column, row, cs, rs]))
                else:
                    pref = [rng.randint(0, 40) / 4, rng.randint(0, 40) / 4]
                    minimum = [rng.randint(0, int(p * 4)) / 4 for p in pref]
                    children.append(item(ident, [column, row, cs, rs], [rng.randint(0, 16) / 4 for _ in range(4)],
                                         [rng.choice(ALIGNMENTS), rng.choice(ALIGNMENTS)], minimum, pref))
            rng.shuffle(children)  # IDs deliberately differ from row, column and input position.
            cases.append(fixture(f"random-{family}-{sample}", columns, rows, children,
                [1200, 1200] if family == "content" else [rng.randint(0, 3200) / 4, rng.randint(0, 2400) / 4],
                [rng.randint(0, 80) / 4 for _ in range(4)], [rng.randint(0, 40) / 4, rng.randint(0, 40) / 4]))
    return cases


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--axisly", type=Path, required=True)
    parser.add_argument("--taffy", type=Path, required=True)
    parser.add_argument("--fixtures", type=Path, default=Path(__file__).with_name("fixtures") / "common.json")
    parser.add_argument("--replay", type=Path)
    parser.add_argument("--random-cases", type=int, default=250, help="cases per family (three families)")
    parser.add_argument("--seed", type=int, default=20260925)
    parser.add_argument("--failure-dir", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        require(0 <= args.random_cases <= 3000, "--random-cases must be between zero and 3000")
        cases = load_cases(args.replay or args.fixtures)
        if not args.replay:
            cases += random_cases(args.seed, args.random_cases)
        validate_cases(cases)
        try:
            native, reference = run_tool(args.axisly, cases), run_tool(args.taffy, cases)
        except (ValueError, OSError, subprocess.TimeoutExpired) as error:
            path = save_failure(args.failure_dir, cases, {"error": str(error), "seed": args.seed})
            print(f"FAIL runner: {error}\nReplay fixture: {path}", file=sys.stderr)
            return 1
        for case, a, b in zip(cases, native, reference):
            mismatch = difference(a, b)
            if mismatch:
                path = save_failure(args.failure_dir, [case], {"error": mismatch, "seed": args.seed, "axisly": a, "taffy": b})
                print(f"FAIL {case['name']}: {mismatch}\nReplay fixture: {path}", file=sys.stderr)
                return 1
        families = Counter(case["name"].split("-")[1] if case["name"].startswith("random-") else "fixture" for case in cases)
        print(f"PASS {len(cases)} cases ({dict(families)}), seed={args.seed}; Taffy 0.14.0 unrounded; "
              f"abs={ABS_EPSILON}, rel={REL_EPSILON}")
        return 0
    except (ValueError, OSError) as error:
        print(f"Invalid reference input: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
