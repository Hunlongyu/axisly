"""Compare the current CSS LayoutTree, pinned Taffy and Yoga on shared trees.

All timed input frames are first replayed with full geometry validation.
Parsing, API style conversion, destruction and geometry dumps are outside timers.
Each timer includes layout and three rectangle reads, plus dirty propagation
for leaf_edit or tree creation and style installation for build.
"""
import argparse
import csv
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import random
import statistics
import subprocess
import sys


class LayoutLimit(RuntimeError):
    """A layout API explicitly rejected the input because of its work budget."""


def pin_cpu(cpu):
    """Children inherit the same affinity; no machine-wide settings change."""
    if cpu is None:
        return
    if cpu < 0:
        raise ValueError("CPU index must be nonnegative")
    if sys.platform == "win32":
        import ctypes
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.GetCurrentProcess.restype = ctypes.c_void_p
        kernel.GetProcessAffinityMask.argtypes = [ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_size_t)]
        kernel.SetProcessAffinityMask.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
        process = kernel.GetCurrentProcess()
        available, system = ctypes.c_size_t(), ctypes.c_size_t()
        if not kernel.GetProcessAffinityMask(process, ctypes.byref(available), ctypes.byref(system)):
            raise ctypes.WinError(ctypes.get_last_error())
        if not available.value & (1 << cpu):
            raise ValueError("CPU is outside this process's allowed affinity")
        if not kernel.SetProcessAffinityMask(process, 1 << cpu):
            raise ctypes.WinError(ctypes.get_last_error())
    elif hasattr(os, "sched_setaffinity"):
        os.sched_setaffinity(0, {cpu})
    else:
        raise ValueError("CPU affinity is unsupported on this platform")


def node(parent=-1, **overrides):
    return dict(parent=parent, **(dict(display="F", direction="R", wrap=0,
        width=-1, height=-1, basis=-1, grow=0, shrink=1, padding=0,
        margin=0, gap=0, column=0, column_span=1, row=0, row_span=1,
        measure=0, preferred=[0, 0], columns=[], rows=[]) | overrides))


def cases():
    result = []

    def add(name, nodes, sizes):
        assert len(sizes) == 17
        result.append(dict(name=name, nodes=nodes, sizes=sizes,
            engines=["axisly_css", "taffy"] + (["yoga"] if all(n["display"] == "F" for n in nodes) else []),
            modes=["build", "cold", "resize", "same"] + (["leaf_edit"] if any(n["measure"] for n in nodes) else [])))

    for count in (100, 1000):
        nodes = [node(width=-2, height=-2, padding=8, gap=4)]
        nodes += [node(0, width=20, height=24, shrink=0) for _ in range(count)]
        add(f"flex_fixed_{count}", nodes, [[16 + 4 * (count - 1) + 20 * count + 512 + i * 16, 160 + i * 4] for i in range(17)])
        nodes = [node(width=-2, height=-2, padding=8, gap=4)]
        nodes += [node(0, height=24, basis=0, grow=1 + i % 3, shrink=0) for i in range(count)]
        weights = sum(1 + i % 3 for i in range(count))
        add(f"flex_grow_{count}", nodes, [[16 + 4 * (count - 1) + weights * (16 + i), 160 + i * 4] for i in range(17)])
    nodes = [node(width=-2, height=-2, direction="C", padding=8, gap=4)]
    nodes += [node(0, width=120, basis=0, grow=1 + i % 3, shrink=0) for i in range(1000)]
    add("flex_column_grow_1000", nodes, [[160 + i * 4, 16 + 4 * 999 + 1999 * (16 + i)] for i in range(17)])
    nodes = [node(width=-2, height=-2, padding=8, gap=4)]
    nodes += [node(0, width=92 + i % 5 * 8, height=24) for i in range(100)]
    add("flex_shrink_100", nodes, [[520 + i * 13, 160] for i in range(17)])
    nodes = [dict(n) for n in nodes]
    nodes[0]["wrap"] = 1
    add("flex_wrap_100", nodes, [[420 + i * 13, 800] for i in range(17)])
    nodes = [node(width=-2, height=-2, padding=8, gap=4)]
    nodes += [node(0, height=24, shrink=0, measure=1, preferred=[20 + i % 5 * 8, 24]) for i in range(100)]
    add("flex_content_100", nodes, [[4012 + i * 16, 160] for i in range(17)])

    for count in (96, 384):
        nodes = [node(width=-2, wrap=1, padding=8, gap=8)]
        for panel in range(count // 8):
            parent = len(nodes)
            nodes.append(node(0, direction="C", width=220, padding=5, margin=3, gap=2))
            nodes += [node(parent, shrink=0, measure=2,
                preferred=[180 + ((panel * 8 + i) % 9) * 40, 20]) for i in range(8)]
        add(f"flex_nested_text_{count}", nodes, [[880 + i * 13, -1] for i in range(17)])
    nodes = [node(width=-2, direction="C", padding=1, gap=2)]
    for _ in range(12):
        nodes.append(node(len(nodes) - 1, direction="C", padding=1, gap=2))
    nodes += [node(12, shrink=0, measure=2, preferred=[180 + i % 5 * 40, 20]) for i in range(16)]
    add("flex_deep_12", nodes, [[120 + i * 13, -1] for i in range(17)])
    nodes = [node(display="B", width=-2, padding=8)]
    nodes += [node(0, measure=2, preferred=[180 + i % 9 * 40, 20]) for i in range(96)]
    add("block_text_96", nodes, [[240 + i * 13, -1] for i in range(17)])

    for side, weighted, spans in ((20, False, False), (20, True, False), (100, True, False), (50, True, True)):
        tracks = [("W", 1)] * side if weighted else [("F", 16)] * side
        rows = [("W", 1)] * side if weighted else [("F", 24)] * side
        nodes = [node(display="G", width=-2, height=-2, padding=8, gap=4, columns=tracks, rows=rows)]
        for r in range(side):
            for c in range(side):
                index = r * side + c
                nodes.append(node(0, column=c + 1, row=r + 1,
                    column_span=min(2 + index % 3, side - c) if spans else 1,
                    row_span=min(2, side - r) if spans else 1))
        add(f"grid_{'grow' if weighted else 'fixed'}_{side}x{side}{'_spans' if spans else ''}", nodes,
            [[16 + 4 * (side - 1) + side * (32 + i), 16 + 4 * (side - 1) + side * (40 + i)] for i in range(17)])
    for count in (96, 384):
        nodes = [node(display="G", width=-2, padding=8, gap=8,
            columns=[("W", 1)] * 4, rows=[("A", 0)] * (count // 32))]
        for panel in range(count // 8):
            parent = len(nodes)
            display = ("B", "F", "G")[panel % 3]
            nodes.append(node(0, display=display, direction="C", padding=5, margin=3, gap=2,
                column=panel % 4 + 1, row=panel // 4 + 1,
                columns=[("W", 1)] if display == "G" else [],
                rows=[("A", 0)] * 8 if display == "G" else []))
            nodes += [node(parent, shrink=0, measure=2,
                row=i + 1 if display == "G" else 0,
                preferred=[180 + ((panel * 8 + i) % 9) * 40, 20]) for i in range(8)]
        add(f"mixed_text_{count}", nodes, [[880 + i * 13, -1] for i in range(17)])
    # Different dimensions and decorations exercise the ordinary resolver
    # when cross-node style sharing has little or no reuse.
    for count in (100, 1000):
        children = [node(0, width=20 + i * .125, height=24 + (i % 7) * .5,
                         padding=(i % 5) * .25, margin=(i % 3) * .25, shrink=0)
                    for i in range(count)]
        extent = sum(n["width"] + 2 * n["margin"] for n in children) + 4 * (count - 1) + 16
        add(f"flex_distinct_styles_{count}", [node(width=-2, height=-2, padding=8, gap=4)] + children,
            [[extent + 512 + i * 16, 180 + i * 4] for i in range(17)])
    return result


def serialize(f):
    lines = [f"CSSCMP1 {f['name']} {len(f['nodes'])} {len(f['sizes'])}"]
    keys = ("parent", "display", "direction", "wrap", "width", "height", "basis", "grow", "shrink",
        "padding", "margin", "gap", "column", "column_span", "row", "row_span", "measure")
    for n in f["nodes"]:
        lines.append(" ".join(map(str, [*(n[k] for k in keys), *n["preferred"], len(n["columns"]), len(n["rows"])])))
        lines.extend(f"{t} {v}" for t, v in n["columns"] + n["rows"])
    lines.extend(f"{w} {h}" for w, h in f["sizes"])
    return "\n".join(lines) + "\n"


def invoke(exe, engine, path, mode, iterations, samples=1):
    implementation = "axisly_css" if engine == "axisly_before" else engine
    p = subprocess.run([str(exe), implementation, str(path), mode, str(iterations), str(samples)],
        capture_output=True, text=True, timeout=300)
    if p.returncode:
        if p.stderr.startswith("LAYOUT_LIMIT:"):
            raise LayoutLimit(f"{engine}/{path.stem}/{mode}: {p.stderr.strip()}")
        raise RuntimeError(f"{engine}/{path.stem}/{mode}: {p.stderr.strip()}")
    return json.loads(p.stdout, parse_constant=lambda x: (_ for _ in ()).throw(ValueError(x)))


def close(a, b):
    return math.isfinite(a) and math.isfinite(b) and abs(a - b) <= 1e-4 + 1e-6 * max(abs(a), abs(b))


def geometry_close(a, b):
    # Same 0.01px absolute floor as the full upstream differential gate.
    # Flex shrink accumulates float rounding (e.g. 398.75 vs 398.7495).
    return math.isfinite(a) and math.isfinite(b) and abs(a - b) <= .01 + 1e-6 * max(abs(a), abs(b))


def validate(f, mode, actual, expected):
    if len(actual) != len(expected):
        raise ValueError("Wrong frame count")
    maximum = 0.0
    for frame, (first, second) in enumerate(zip(actual, expected)):
        if len(first) != len(f["nodes"]) or len(second) != len(f["nodes"]):
            raise ValueError("Wrong node count")
        for i, (a, b) in enumerate(zip(first, second)):
            if len(a) != 4 or len(b) != 4 or any(not geometry_close(x, y) for x, y in zip(a, b)):
                raise ValueError(f"Geometry mismatch {f['name']}/{mode}/frame={frame}/node={i}: {a} vs {b}")
            maximum = max(maximum, *(abs(x - y) for x, y in zip(a, b)))
    return maximum


def checksum(frames):
    def rect(r):
        x, y, w, h = r
        return x + y * .5 + w * .25 + h * .125
    return sum(sum(rect(frame[i]) for i in (0, len(frame) // 2, len(frame) - 1)) for frame in frames)


def bootstrap_intervals(group, reference=None, repetitions=2000):
    """Exploratory 95% hierarchical bootstrap of batch-mean medians.

    Resample rounds, then batches within each selected round. For a ratio,
    pair the round selection, but independently resample each engine's batches.
    Frames within a batch are not independent observations.
    """
    if len(group) < 3 or any(len(row["samples_us"]) < 2 for row in group):
        return None
    peers = {row["round"]: row for row in reference} if reference is not None else None
    if peers is not None and (len(peers) != len(group)
            or any(row["round"] not in peers for row in group)
            or any(len(row["samples_us"]) < 2 for row in peers.values())):
        return None
    rng = random.Random(0)
    estimates = []
    for _ in range(repetitions):
        times, denominators = [], []
        for row in rng.choices(group, k=len(group)):
            values = row["samples_us"]
            times.extend(rng.choices(values, k=len(values)))
            if peers is not None:
                values = peers[row["round"]]["samples_us"]
                denominators.extend(rng.choices(values, k=len(values)))
        estimate = statistics.median(times)
        if peers is not None:
            denominator = statistics.median(denominators)
            if denominator <= 0:
                return None
            estimate /= denominator
        estimates.append(estimate)
    estimates.sort()
    return estimates[math.floor(.025 * (repetitions - 1))], estimates[math.ceil(.975 * (repetitions - 1))]


def summary(raw, output, smoke=False):
    groups = {}
    for row in raw:
        group = groups.setdefault((row["case"], row["engine"], row["mode"]), [])
        group.append(row)
    with output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream)
        writer.writerow(["case", "engine", "mode", "nodes", "batch_count", "iterations",
            "median_us", "p95_batch_us", "min_round_median_us", "max_round_median_us", "measurements_per_layout",
            "median_ci95_low_us", "median_ci95_high_us", "ratio_to_taffy",
            "ratio_ci95_low", "ratio_ci95_high", "comparison"])
        for key, group in groups.items():
            times = sorted(t for g in group for t in g["samples_us"])
            counts = [c for g in group for c in g["measurements"]]
            if len(set(counts)) != 1:
                raise ValueError("Measurement count changed between samples")
            medians = [statistics.median(g["samples_us"]) for g in group]
            interval = None if smoke else bootstrap_intervals(group)
            reference = groups.get((key[0], "taffy", key[2]))
            ratio, ratio_interval, comparison = "", None, "insufficient_samples"
            if smoke:
                comparison = "smoke_only"
            elif key[1] == "taffy":
                ratio, comparison = 1, "reference"
            elif reference:
                denominator = statistics.median(t for g in reference for t in g["samples_us"])
                if denominator > 0:
                    ratio = statistics.median(times) / denominator
                    ratio_interval = bootstrap_intervals(group, reference)
                    if ratio_interval:
                        low, high = ratio_interval
                        comparison = "faster" if high < 1 else "slower" if low > 1 else "inconclusive"
            writer.writerow([*key, group[0]["nodes"], len(times), group[0]["iterations"],
                statistics.median(times), times[math.ceil(.95 * len(times)) - 1],
                min(medians), max(medians), counts[0], *(interval or ("", "")), ratio,
                *(ratio_interval or ("", "")), comparison])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", type=Path, required=True)
    parser.add_argument("--include-reuse", action="store_true",
        help="Also measure Axisly with an explicit 64 MiB resource retained across temporary trees")
    parser.add_argument("--include-retained", action="store_true",
        help="Also measure Axisly with per-node reads and no flat output or Grid track diagnostics")
    parser.add_argument("--baseline-native", type=Path,
        help="Interleave a saved CSS baseline executable with the current engine in each timing round")
    parser.add_argument("--taffy", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--case", action="append")
    parser.add_argument("--mode", action="append", choices=("cold", "resize", "same", "leaf_edit", "build"),
        help="Select scenarios; build additionally times node creation and style installation")
    parser.add_argument("--rounds", type=int, default=3)
    parser.add_argument("--samples", type=int, default=9)
    parser.add_argument("--frames-scale", type=int, default=1,
        help="Multiply frames per batch while retaining all-frame geometry verification")
    parser.add_argument("--cpu", type=int,
        help="Pin this process and all engine subprocesses to one logical CPU")
    parser.add_argument("--smoke", action="store_true")
    parser.add_argument("--record-limits", action="store_true",
        help="Record explicit resource-limit failures; retain their inputs and exclude only those failed engine/mode timings")
    args = parser.parse_args()
    if not 1 <= args.rounds <= 10 or not 1 <= args.samples <= 100 or not 1 <= args.frames_scale <= 20:
        parser.error("Invalid rounds/samples")
    pin_cpu(args.cpu)
    fixtures = cases()
    if args.case:
        unknown = set(args.case) - {f["name"] for f in fixtures}
        if unknown:
            parser.error(f"Unknown fixtures {sorted(unknown)}")
        fixtures = [f for f in fixtures if f["name"] in args.case]
    if args.mode:
        for f in fixtures:
            f["modes"] = [mode for mode in dict.fromkeys(args.mode) if mode == "build" or mode in f["modes"]]
        fixtures = [f for f in fixtures if f["modes"]]
        if not fixtures:
            parser.error("Selected cases do not support the requested modes")
    args.output.mkdir(parents=True, exist_ok=True)
    inputs = args.output / "fixtures"
    inputs.mkdir(exist_ok=True)
    tools = dict(axisly_css=args.native.resolve(), taffy=args.taffy.resolve(), yoga=args.native.resolve())
    if args.include_reuse:
        tools["axisly_css_reuse"] = args.native.resolve()
        for f in fixtures:
            f["engines"].insert(1, "axisly_css_reuse")
    if args.include_retained:
        tools["axisly_css_retained"] = args.native.resolve()
        for f in fixtures:
            f["engines"].insert(1, "axisly_css_retained")
    if args.baseline_native:
        tools["axisly_before"] = args.baseline_native.resolve()
        for f in fixtures:
            f["engines"].insert(0, "axisly_before")
    hashes = {name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in tools.items()}
    expected_sums, geometry, failures = {}, [], []
    failed_groups = set()
    for f in fixtures:
        path = inputs / (f["name"] + ".txt")
        path.write_text(serialize(f), encoding="utf-8")
        f["iterations"] = 2 if args.smoke else (170 if len(f["nodes"]) <= 129 else 34) * args.frames_scale
        frames = 17 if args.smoke else f["iterations"]
        for mode in f["modes"]:
            reference = invoke(tools["taffy"], "taffy", path, "verify_" + mode, frames)["frames"]
            validate(f, mode, reference, reference)  # Includes shape and finite checks.
            expected_sums[f["name"], mode] = checksum(reference[:f["iterations"]])
            for engine in f["engines"]:
                try:
                    actual = reference if engine == "taffy" else invoke(tools[engine], engine, path, "verify_" + mode, frames)["frames"]
                except LayoutLimit as e:
                    if not args.record_limits:
                        raise
                    failed_groups.add((f["name"], engine, mode))
                    failures.append(dict(case=f["name"], engine=engine, mode=mode, error=str(e)))
                    print(f"layout LIMIT {e}", flush=True)
                    continue
                maximum = validate(f, mode, actual, reference)
                geometry.append(dict(case=f["name"], engine=engine, mode=mode, frames=frames,
                    nodes=len(f["nodes"]), max_abs_error=maximum))
        status = "PARTIAL" if any(key[0] == f["name"] for key in failed_groups) else "PASS"
        print(f"geometry {status} {f['name']} ({'/'.join(f['engines'])})", flush=True)
    (args.output / "geometry.json").write_text(json.dumps(geometry, indent=2), encoding="utf-8")
    (args.output / "failures.json").write_text(json.dumps(failures, indent=2), encoding="utf-8")
    print(f"Geometry replay: {len(geometry)} passed, {len(failures)} resource-limit groups; starting sequential timing.", flush=True)
    raw = []
    rounds = 1 if args.smoke else args.rounds
    samples = 2 if args.smoke else args.samples
    for round_index in range(rounds):
        for f in fixtures:
            engines = f["engines"] if round_index % 2 == 0 else list(reversed(f["engines"]))
            for mode in f["modes"]:
                for engine in engines:
                    if (f["name"], engine, mode) in failed_groups:
                        continue
                    data = invoke(tools[engine], engine, inputs / (f["name"] + ".txt"), mode, f["iterations"], samples)
                    if data["build"] != "Release" and not args.smoke:
                        raise ValueError("Timing requires Release")
                    times, sums, counts = (data[k] for k in ("samples_us", "checksums", "measurements"))
                    if any(len(v) != samples for v in (times, sums, counts)):
                        raise ValueError("Wrong sample count")
                    if any(not math.isfinite(t) or t < 0 or (t == 0 and not args.smoke) for t in times):
                        raise ValueError("Invalid timing")
                    if any(not close(v, expected_sums[f["name"], mode]) for v in sums):
                        raise ValueError(f"Checksum mismatch {f['name']}/{engine}/{mode}")
                    if any(not math.isfinite(v) or v < 0 or v != counts[0] for v in counts):
                        raise ValueError("Unstable measurement count")
                    if mode == "same" and counts[0] != 0:
                        raise ValueError("Unchanged input measured leaves")
                    raw.append(dict(case=f["name"], engine=engine, mode=mode, round=round_index + 1,
                        nodes=len(f["nodes"]), iterations=f["iterations"], **data))
            print(f"timing PASS round={round_index + 1} {f['name']}", flush=True)
    if hashes != {name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in tools.items()}:
        raise ValueError("Executable changed during run")
    script = Path(__file__).resolve()
    repo = script.parents[2]
    source_paths = [script, script.with_name("css_native.cpp"), script.with_name("css_axisly_adapter.hpp"),
        script.with_name("css_fixture.hpp"), script.parent / "taffy/src/bin/css.rs",
        script.parent / "taffy/Cargo.toml", script.parent / "taffy/Cargo.lock"]
    # HEAD alone does not identify an uncommitted optimization. Include the
    # actual library sources and configuration alongside benchmark adapters.
    source_paths += [repo / name for name in subprocess.check_output(
        ["git", "ls-files", "src", "include", "cmake", "CMakeLists.txt", "CMakePresets.json"],
        cwd=repo, text=True).splitlines()]
    meta = dict(platform=platform.platform(), python=sys.version,
        timestamp_utc=datetime.now(timezone.utc).isoformat(), taffy="0.14.0", yoga="3.2.1",
        axisly_api="css::LayoutTree", rounds=rounds, samples=samples, smoke=args.smoke,
        baseline_interleaved=args.baseline_native is not None,
        explicit_memory_reuse=args.include_reuse,
        retained_output=args.include_retained,
        cpu_affinity=args.cpu, frames_scale=args.frames_scale,
        record_limits=args.record_limits, resource_limit_groups=len(failures),
        cases=[f["name"] for f in fixtures], source_revision=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repo, text=True).strip(),
        source_worktree_dirty=bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=repo, text=True).strip()),
        source_sha256={str(p.relative_to(repo)): hashlib.sha256(p.read_bytes()).hexdigest() for p in source_paths if p.is_file()},
        executable_sha256=hashes, fixture_sha256={p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs.glob("*.txt")},
        geometry_tolerance="abs 0.01px + rel 1e-6; every node, every timed frame; Taffy geometry reference",
        timers="in-process steady_clock/Instant; edit/dirty + compute + three root-relative rectangle reads; build mode additionally includes node creation and style installation; destruction excluded",
        measurement="synthetic fixed and wrapping metrics; identical arithmetic; no real font/rendering",
        statistics="2000 deterministic hierarchical bootstrap resamples of rounds then batch means; paired rounds and independent engine batches for median ratios; exploratory 95% percentile intervals only with >=3 rounds; smoke has no performance inference",
        results="axisly_css retains flat root-relative geometry and Grid diagnostics; optional axisly_css_retained uses per-node reads without flat output or track diagnostics; Taffy/Yoga retain node-local geometry; no full-array export added to either")
    (args.output / "environment.json").write_text(json.dumps(meta, indent=2), encoding="utf-8")
    (args.output / "samples.json").write_text(json.dumps(raw, indent=2), encoding="utf-8")
    summary(raw, args.output / "summary.csv", smoke=args.smoke)
    print(f"DONE {len(fixtures)} cases, {len(geometry)} valid engine/mode groups, {len(failures)} limit groups, {len(raw)} timing records: {args.output}")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, RuntimeError, subprocess.TimeoutExpired) as e:
        print(f"FAIL {e}", file=sys.stderr)
        sys.exit(1)
