"""Shared inputs, fail-closed geometry validation and in-process layout timings.

Python startup, parsing, tree creation and geometry dumps are outside timers.
Old output retirement is included in reused-instance layout calls. Final engine
destruction is excluded. This runner never compares subprocess wall-clock times.
"""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import platform
import statistics
import subprocess
import sys


def cases():
    result = []

    def make(name, kind, columns, rows, measured=False, spans=False):
        items = []
        for row in range(len(rows)):
            for col in range(len(columns)):
                index = len(items)
                items.append([col, row, min(2 + index % 3, len(columns) - col) if spans else 1,
                              min(2, len(rows) - row) if spans else 1,
                              20 + (index % 5) * 8 if measured else 0, 24 if measured else 0])
        f = dict(name=name, kind=kind, columns=columns, rows=rows, items=items,
                 measured=measured, padding=8, gap=4)

        def available(tracks, horizontal, frame):
            total = 0
            weights = 0
            for i, (t, v) in enumerate(tracks):
                if t == "F":
                    total += v
                elif t == "W":
                    weights += v
                else:
                    total += max(item[4 if horizontal else 5] for item in items if item[0 if horizontal else 1] == i)
            return 16 + 4 * (len(tracks) - 1) + total + (weights * (16 + frame) if weights else 512 + frame * 16)

        f["sizes"] = [[available(columns, True, i), available(rows, False, i)] for i in range(17)]
        result.append(f)

    make("row_fixed_100", "row", [("F", 20)] * 100, [("F", 24)])
    make("column_fixed_100", "column", [("F", 120)], [("F", 20)] * 100)
    for count in (100, 1000):
        make(f"row_weighted_{count}", "row", [("W", 1 + i % 3) for i in range(count)], [("F", 24)])
    make("column_weighted_1000", "column", [("F", 120)], [("W", 1 + i % 3) for i in range(1000)])
    make("row_content_100", "row", [("C", 0)] * 100, [("F", 24)], measured=True)
    make("fixed_grid_20x20", "grid", [("F", 16)] * 20, [("F", 24)] * 20)
    for side in (20, 100):
        make(f"weighted_grid_{side}x{side}", "grid", [("W", 1)] * side, [("W", 1)] * side)
    for name, track in (("fixed", "F"), ("weighted", "W")):
        make(f"{name}_spans_50x50", "grid", [(track, 16 if track == "F" else 1)] * 50,
             [(track, 24 if track == "F" else 1)] * 50, spans=True)
    return result


def serialize(f):
    lines = [f"GLCMP1 {f['name']} {f['kind']} {len(f['columns'])} {len(f['rows'])} {len(f['items'])} {len(f['sizes'])} {int(f['measured'])} {f['padding']} {f['gap']}"]
    lines.extend(f"{t} {v}" for t, v in f["columns"] + f["rows"])
    lines.extend(" ".join(map(str, item)) for item in f["items"])
    lines.extend(" ".join(map(str, size)) for size in f["sizes"])
    return "\n".join(lines) + "\n"


def expected(f, size):
    def axis(tracks, horizontal):
        content = [max((item[4 if horizontal else 5] for item in f["items"]
                        if item[0 if horizontal else 1] == i), default=0) if t == "C" else 0
                   for i, (t, _) in enumerate(tracks)]
        weight = sum(v for t, v in tracks if t == "W")
        free = size[0 if horizontal else 1] - 2 * f["padding"] - f["gap"] * (len(tracks) - 1)
        free -= sum(v for t, v in tracks if t == "F") + sum(content)
        widths = [v if t == "F" else content[i] if t == "C" else free * v / weight
                  for i, (t, v) in enumerate(tracks)]
        assert all(v >= 0 for v in widths)
        offsets = [f["padding"]]
        for value in widths:
            offsets.append(offsets[-1] + value + f["gap"])
        return widths, offsets
    cw, cx = axis(f["columns"], True)
    rh, ry = axis(f["rows"], False)
    return [[cx[c], ry[r], sum(cw[c:c+cs]) + f["gap"] * (cs-1), sum(rh[r:r+rs]) + f["gap"] * (rs-1)]
            for c, r, cs, rs, _, _ in f["items"]]


def close(a, b):
    return math.isfinite(a) and abs(a-b) <= 1e-4 + 1e-6 * max(abs(a), abs(b))


def invoke(exe, engine, path, mode, iterations=1, samples=1):
    command = [str(exe), engine.removesuffix("_before"), str(path), mode, str(iterations), str(samples)]
    process = subprocess.run(command, capture_output=True, text=True, timeout=180)
    if process.returncode:
        raise RuntimeError(f"{engine} {path.stem} {mode}: {process.stderr.strip()}")
    return json.loads(process.stdout, parse_constant=lambda value: (_ for _ in ()).throw(ValueError(value)))


def validate(f, output):
    frames = output["frames"]
    if len(frames) != 2 * len(f["sizes"]):
        raise ValueError("Wrong frame count")
    maximum_error = 0.0
    for frame, actual in enumerate(frames):
        want = expected(f, f["sizes"][frame % len(f["sizes"])])
        if len(actual) != len(want):
            raise ValueError("Wrong item count")
        for i, (a, b) in enumerate(zip(actual, want)):
            if len(a) != 4 or any(not close(x, y) for x, y in zip(a, b)):
                raise ValueError(f"Geometry mismatch frame={frame}, item={i}: {a} != {b}")
            maximum_error = max(maximum_error, *(abs(x-y) for x, y in zip(a, b)))
    return maximum_error


def checksum(f, mode, iterations):
    value = 0.0
    for i in range(iterations):
        frame = f["sizes"][-1] if mode == "same" else f["sizes"][i % len(f["sizes"])]
        rects = expected(f, frame)
        for index in (0, len(rects)//2, len(rects)-1):
            x, y, w, h = rects[index]
            value += x+y*.5+w*.25+h*.125
    return value


def write_summary(raw, output):
    """Pool every batch mean from both execution orders; never select a best run."""
    groups = {}
    for entry in raw:
        key = (entry["case"], entry["engine"], entry["mode"])
        group = groups.setdefault(key, dict(times=[], counts=[], rounds=[]))
        group["times"].extend(entry["samples_us"])
        group["counts"].extend(entry["measurements"])
        group["rounds"].append(statistics.median(entry["samples_us"]))
    with output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream)
        writer.writerow(["case", "engine", "mode", "batch_count", "median_us", "p95_batch_us",
                         "min_round_median_us", "max_round_median_us", "measurements_per_layout"])
        for key, group in groups.items():
            values = sorted(group["times"])
            if len(set(group["counts"])) != 1:
                raise ValueError("Measurement count changed between rounds")
            writer.writerow([*key, len(values), statistics.median(values), values[math.ceil(.95*len(values))-1],
                             min(group["rounds"]), max(group["rounds"]), group["counts"][0]])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", type=Path, required=True)
    parser.add_argument("--baseline-native", type=Path, help="Preserved executable built before optimization with the same timing harness")
    parser.add_argument("--taffy", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--smoke", action="store_true")
    parser.add_argument("--rounds", type=int, default=2)
    parser.add_argument("--samples", type=int, default=9)
    parser.add_argument("--case", action="append", help="Restrict to named fixtures; repeat to select several")
    parser.add_argument("--engines", nargs="+", choices=("gridly", "gridly_cached", "taffy", "yoga", "gridly_before", "gridly_cached_before"))
    parser.add_argument("--mode", action="append", choices=("cold", "resize", "same"),
                        help="Restrict timing modes; repeat to select several")
    args = parser.parse_args()
    if not 1 <= args.rounds <= 10 or not 1 <= args.samples <= 100:
        parser.error("Invalid rounds/samples")
    fixtures = cases()
    if args.case:
        unknown = set(args.case) - {f["name"] for f in fixtures}
        if unknown:
            parser.error(f"Unknown fixtures: {sorted(unknown)}")
        fixtures = [f for f in fixtures if f["name"] in args.case]
    modes = list(dict.fromkeys(args.mode or ("cold", "resize", "same")))
    available_tools = dict(gridly=args.native.resolve(), gridly_cached=args.native.resolve(),
                           taffy=args.taffy.resolve(), yoga=args.native.resolve())
    if args.baseline_native:
        available_tools = dict(gridly_before=args.baseline_native.resolve(), gridly_cached_before=args.baseline_native.resolve(),
                               **available_tools)
    engines = list(dict.fromkeys(args.engines or available_tools))
    if any(name not in available_tools for name in engines):
        parser.error("Before engines require --baseline-native")
    tools = {name: available_tools[name] for name in engines}

    def engines_for(fixture):
        return [engine for engine in tools if fixture["kind"] != "grid" or engine != "yoga"]

    if any(not engines_for(f) for f in fixtures):
        parser.error("Selected engines do not support all requested fixtures (Yoga has no Grid support)")
    args.output.mkdir(parents=True, exist_ok=True)
    fixture_dir = args.output / "fixtures"
    fixture_dir.mkdir(exist_ok=True)
    verification = []
    for f in fixtures:
        path = fixture_dir / (f["name"] + ".txt")
        path.write_text(serialize(f), encoding="utf-8")
        engines = engines_for(f)
        for engine in engines:
            maximum = validate(f, invoke(tools[engine], engine, path, "verify"))
            verification.append(dict(case=f["name"], engine=engine, frames=34, items=len(f["items"]), max_abs_error=maximum))
        print(f"geometry PASS {f['name']} ({'/'.join(engines)})", flush=True)
    (args.output / "geometry.json").write_text(json.dumps(verification, indent=2), encoding="utf-8")
    print("All geometry checks passed; starting sequential in-process timing.", flush=True)
    raw, rows = [], []
    for round_index in range(1 if args.smoke else args.rounds):
        for f in fixtures:
            path = fixture_dir / (f["name"] + ".txt")
            engines = engines_for(f)
            if round_index % 2:
                engines.reverse()
            iterations = 2 if args.smoke else 340 if len(f["items"]) <= 1000 else 34
            samples = 2 if args.smoke else args.samples
            for mode in modes:
                wanted_checksum = checksum(f, mode, iterations)
                for engine in engines:
                    data = invoke(tools[engine], engine, path, mode, iterations, samples)
                    if data["build"] != "Release" and not args.smoke:
                        raise ValueError("Performance comparison requires Release executables")
                    times, counts, sums = (data[key] for key in ("samples_us", "measurements", "checksums"))
                    if any(len(values) != samples for values in (times, counts, sums)):
                        raise ValueError("Wrong sample count")
                    # Two smoke calls can both land in one clock tick for a cache hit.
                    # Smoke proves functionality, not sub-microsecond performance.
                    if any(not math.isfinite(t) or t < 0 or (t == 0 and not args.smoke) for t in times) or any(not close(x, wanted_checksum) for x in sums):
                        raise ValueError(f"Invalid timing/checksum: {f['name']}/{engine}/{mode}")
                    if any(x != counts[0] or not math.isfinite(x) or x < 0 for x in counts):
                        raise ValueError("Unstable measurement count")
                    if engine in ("gridly_cached", "gridly_cached_before") and mode == "same" and counts[0] != 0:
                        raise ValueError("GridCache hit performed measurement")
                    raw.append(dict(case=f["name"], engine=engine, mode=mode, round=round_index+1, **data))
                    rows.append(dict(case=f["name"], engine=engine, mode=mode, round=round_index+1,
                                     items=len(f["items"]), columns=len(f["columns"]), rows=len(f["rows"]),
                                     iterations=iterations, samples=samples, median_us=statistics.median(times),
                                     p95_batch_us=sorted(times)[math.ceil(.95*len(times))-1],
                                     measurements_per_layout=counts[0]))
            print(f"timing PASS round={round_index+1} {f['name']}", flush=True)
    with (args.output / "results.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    metadata = dict(platform=platform.platform(), python=sys.version, taffy="0.14.0", yoga="3.2.1",
                    rounds=1 if args.smoke else args.rounds, smoke=args.smoke,
                    engines=list(tools), cases=[f["name"] for f in fixtures], modes=modes,
                    executable_sha256={name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in tools.items()},
                    geometry_tolerance="abs 1e-4 + rel 1e-6", timers="in-process steady_clock/Instant; setup and engine destruction excluded; old output retirement included for ALL reused-instance paths",
                    cache="cold=fresh tree, resize=reused tree changing viewport, same=reused tree unchanged viewport")
    (args.output / "environment.json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")
    (args.output / "samples.json").write_text(json.dumps(raw, indent=2), encoding="utf-8")
    write_summary(raw, args.output / "summary.csv")
    print(f"PASS {len(verification)} engine/scenario pairs, {len(rows)} timing records: {args.output}")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"FAIL {error}", file=sys.stderr)
        sys.exit(1)
