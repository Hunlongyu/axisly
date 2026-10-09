"""Sequential, same-fixture before/after timing; allocation probes run separately."""
import argparse
import csv
import ctypes
import hashlib
import io
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).parent / "comparison"))
from css_compare import bootstrap_intervals


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--before", required=True, type=Path)
    parser.add_argument("--after", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--cpu", type=int, default=4)
    parser.add_argument("--rounds", type=int, default=3)
    parser.add_argument("--samples", type=int, default=9)
    parser.add_argument("--iterations", type=int, default=12)
    parser.add_argument("--case", action="append")
    parser.add_argument("--mode", action="append", choices=["cold", "same", "edit", "resize"])
    parser.add_argument("--queries", type=int, action="append")
    parser.add_argument("--counts", type=int, nargs="+", default=[100, 1000, 10000])
    args = parser.parse_args()
    if os.name == "nt":
        api = ctypes.WinDLL("kernel32", use_last_error=True)
        api.GetCurrentProcess.restype = ctypes.c_void_p
        api.SetProcessAffinityMask.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
        if not api.SetProcessAffinityMask(api.GetCurrentProcess(), 1 << args.cpu):
            raise ctypes.WinError(ctypes.get_last_error())
    else:
        os.sched_setaffinity(0, {args.cpu})
    args.output.mkdir(parents=True, exist_ok=True)
    cases = args.case or ["lookup_source", "lookup_owned", "block", "rounded", "rounded_sparse", "baseline"]
    raw, summary = [], []
    for case in cases:
        for count in args.counts:
            for mode in (args.mode or ("cold", "same", "edit", "resize")):
                for queries in (args.queries if args.queries is not None else ([3, 0] if case.startswith("lookup") else [3])):
                    groups = {"before": [], "after": []}
                    for round_index in range(args.rounds):
                        values = {}
                        for engine in (["before", "after"] if round_index % 2 == 0 else ["after", "before"]):
                            binary = getattr(args, engine).resolve()
                            command = [str(binary), "--case", case, "--mode", mode, "--count", str(count),
                                       "--queries", str(queries), "--samples", str(args.samples),
                                       "--iterations", str(args.iterations)]
                            rows = list(csv.DictReader(io.StringIO(subprocess.check_output(command, text=True))))
                            values[engine] = [row["checksum"] for row in rows]
                            groups[engine].append({"round": round_index, "samples_us": [float(row["us"]) for row in rows]})
                            raw.extend(dict(row, engine=engine, round=round_index) for row in rows)
                        if values["before"] != values["after"]:
                            raise RuntimeError(f"Checksum mismatch: {case} {count} {mode}")
                    before, after = [statistics.median(v for row in groups[key] for v in row["samples_us"])
                                     for key in ("before", "after")]
                    interval = bootstrap_intervals(groups["after"], groups["before"])
                    classification = "smoke_only" if interval is None else (
                        "faster" if interval[1] < 1 else "slower" if interval[0] > 1 else "inconclusive")
                    summary.append(dict(case=case, mode=mode, count=count, queries=queries,
                                        before_us=before, after_us=after, speedup=before / after,
                                        ratio_low=interval[0] if interval else "", ratio_high=interval[1] if interval else "",
                                        comparison=classification))
                    print(case, count, mode, queries, round(before, 2), round(after, 2), classification, flush=True)
    for name, rows in (("raw.csv", raw), ("summary.csv", summary)):
        with (args.output / name).open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, rows[0].keys())
            writer.writeheader()
            writer.writerows(rows)
    repo = Path(__file__).resolve().parents[1]
    source_paths = subprocess.check_output(["git", "ls-files", "src", "include", "benchmarks", "CMakeLists.txt"], cwd=repo, text=True).splitlines()
    source_paths += ["benchmarks/css_hotpaths.cpp", "benchmarks/hotpaths_compare.py"]
    metadata = dict(platform=platform.platform(), cpu=args.cpu, rounds=args.rounds, samples=args.samples,
                    iterations=args.iterations, before=str(args.before.resolve()), after=str(args.after.resolve()),
                    before_sha256=hashlib.sha256(args.before.read_bytes()).hexdigest(),
                    after_sha256=hashlib.sha256(args.after.read_bytes()).hexdigest(),
                    revision=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repo, text=True).strip(),
                    worktree=subprocess.check_output(["git", "status", "--porcelain"], cwd=repo, text=True),
                    source_sha256={p: hashlib.sha256((repo / p).read_bytes()).hexdigest() for p in source_paths})
    (args.output / "environment.json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
