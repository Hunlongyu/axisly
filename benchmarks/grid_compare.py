"""Serial before/after Grid benchmark; exact normalized geometry before timing."""
import argparse
import csv
import datetime
import hashlib
import io
import json
import pathlib
import platform
import statistics
import subprocess

from comparison.css_compare import pin_cpu


def invoke(exe, case, mode, count, *extra):
    process = subprocess.run([str(exe), "--case", case, "--mode", mode,
                              "--count", str(count), *map(str, extra)],
                             capture_output=True, text=True, check=True)
    return process.stdout


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=pathlib.Path, required=True)
    parser.add_argument("--candidate", type=pathlib.Path, required=True)
    parser.add_argument("--allocation-baseline", type=pathlib.Path)
    parser.add_argument("--allocation-candidate", type=pathlib.Path)
    parser.add_argument("--metrics-baseline", type=pathlib.Path)
    parser.add_argument("--metrics-candidate", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--case", action="append")
    parser.add_argument("--rounds", type=int, default=3)
    parser.add_argument("--samples", type=int, default=7)
    parser.add_argument("--iterations", type=int, default=3)
    parser.add_argument("--full", action="store_true")
    parser.add_argument("--cpu", type=int, help="Pin this process and its children to one logical CPU")
    parser.add_argument("--environment", type=pathlib.Path,
                        help="JSON with compiler, source revisions and machine conditions")
    args = parser.parse_args()
    if min(args.rounds, args.samples, args.iterations) < 1:
        parser.error("rounds, samples and iterations must be positive")
    pin_cpu(args.cpu)
    jobs = []
    for case in ("auto_row", "auto_column", "auto_spans", "auto_dense"):
        for count in ((100, 1000, 2000) if case == "auto_dense" else (100, 1000, 5000, 10000)):
            jobs.append((case, "cold", count))
    for case in ("style_nested", "style_source"):
        for count in (100, 1000, 5000):
            jobs.append((case, "edit", count))
    for case in ("auto_fit", "span_limits", "repeat_nested", "float"):
        for count in (100, 1000):
            jobs.append((case, "content" if case in ("repeat_nested", "span_limits") else "resize", count))
    jobs += [("auto_row", "same", 1000), ("auto_row", "resize", 1000)]
    if args.case:
        jobs = [job for job in jobs if job[0] in args.case]
    if not jobs:
        parser.error("no matching cases")
    args.output.mkdir(parents=True, exist_ok=True)
    paths = {key: value.resolve() for key, value in vars(args).items()
             if ("baseline" in key or "candidate" in key) and value}
    manifest = {key: {"path": str(path), "sha256": digest(path)} for key, path in paths.items()}
    manifest.update(rounds=args.rounds, samples=args.samples, iterations=args.iterations, full=args.full, cpu=args.cpu,
                    started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                    platform=platform.platform(),
                    geometry="Exact IEEE-754 values, normalized identities; four full-output frames",
                    peak_memory="Incremental allocations within one measured step, excluding retained storage")
    if args.environment:
        manifest["environment"] = json.loads(args.environment.read_text(encoding="utf-8-sig"))
    geometry, raw = [], []
    extra = ["--full"] if args.full else []
    for case, mode, count in jobs:
        iterations = 1 if case == "auto_dense" and count >= 1000 else args.iterations
        samples = min(3, args.samples) if case == "auto_dense" and count >= 1000 else args.samples
        before = invoke(args.baseline, case, mode, count, "--verify")
        after = invoke(args.candidate, case, mode, count, "--verify")
        if before != after:
            (args.output / f"{case}-{mode}-{count}-before.txt").write_text(before)
            (args.output / f"{case}-{mode}-{count}-after.txt").write_text(after)
            raise RuntimeError(f"Geometry changed: {case}/{mode}/{count}")
        geometry.append(dict(case=case, mode=mode, count=count,
                             sha256=hashlib.sha256(after.encode()).hexdigest()))
        expected_checksum = None
        for round_index in range(args.rounds):
            for label in (("baseline", "candidate") if round_index % 2 == 0 else ("candidate", "baseline")):
                rows = list(csv.DictReader(io.StringIO(invoke(paths[label], case, mode, count,
                    "--samples", samples, "--iterations", iterations, *extra))))
                for row in rows:
                    checksum = float(row["checksum"])
                    if expected_checksum is None:
                        expected_checksum = checksum
                    if checksum != expected_checksum:
                        raise RuntimeError(f"Timing checksum changed: {case}/{mode}/{count}")
                    raw.append(dict(engine=label, round=round_index, iterations=iterations, **row))
        for kind in ("allocation", "metrics"):
            for label in ("baseline", "candidate"):
                key = f"{kind}_{label}"
                if key not in paths:
                    continue
                rows = csv.DictReader(io.StringIO(invoke(paths[key], case, mode, count,
                    "--samples", 1, "--iterations", iterations, *extra)))
                for row in rows:
                    if float(row["checksum"]) != expected_checksum:
                        raise RuntimeError(f"Instrumented checksum changed: {case}/{mode}/{count}")
                    raw.append(dict(engine=key, round=0, iterations=iterations, **row))
        print(f"verified {case}/{mode}/{count}", flush=True)
    groups = {}
    for row in raw:
        key = tuple(row[field] for field in ("case", "mode", "count", "engine"))
        groups.setdefault(key, []).append(row)
    summary = []
    for key, rows in groups.items():
        times = sorted(float(row["us"]) for row in rows)
        row = dict(zip(("case", "mode", "count", "engine"), key))
        row.update(median_us=statistics.median(times), p95_batch_us=times[max(0, (95 * len(times) + 99) // 100 - 1)])
        for field in ("allocations", "bytes", "peak_incremental_bytes", "builds", "hits", "probes",
                      "occupied_visits", "fit_visits", "limit_visits"):
            row[field] = statistics.median(float(sample[field]) for sample in rows)
        summary.append(row)
    for filename, rows in (("samples.csv", raw), ("summary.csv", summary), ("geometry.csv", geometry)):
        with (args.output / filename).open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
