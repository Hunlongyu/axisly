"""Audit every pinned upstream fixture without excluding failed inputs."""

import argparse
import collections
import json
import math
from pathlib import Path
import struct
import subprocess

from grid_detail_compare import compare_grid_details
from test_flex_matrix import compare


def replay_cases(executable, cases, first=None):
    if first is None:
        inputs = [{key: case[key] for key in ("name", "available", "nodes")} for case in cases]
        command, fixture = [executable], json.dumps({
            "protocol": "AXISLY_TREE_GRID_DETAILS_V1", "cases": inputs})
    else:
        command, fixture = [executable, "--first", str(first), "--count", str(len(cases))], None
    try:
        result = subprocess.run(command, input=fixture, text=True, encoding="utf-8",
                                capture_output=True, timeout=120)
        error = result.stderr.strip() if result.returncode else None
        if result.returncode and not error:
            error = f"Runner exited with code {result.returncode}"
    except subprocess.TimeoutExpired:
        error = "Runner timed out after 120 seconds"
    if error is not None:
        if len(cases) == 1:
            return [{"name": cases[0]["name"], "error": error}]
        middle = len(cases) // 2
        return (replay_cases(executable, cases[:middle], first)
                + replay_cases(executable, cases[middle:], None if first is None else first + middle))
    output = json.loads(result.stdout)
    if first is None:
        valid = output.get("taffy") == "0.14.0"
    else:
        valid = output.get("protocol") == "AXISLY_UPSTREAM_NATIVE_V1"
    if not valid or len(output["cases"]) != len(cases):
        raise ValueError("Corpus runner returned an invalid version, protocol or case count")
    return output["cases"]


def round_pixel(value):
    return math.floor(value + 0.5) if value >= 0 else math.ceil(value - 0.5)


def f32(value):
    return struct.unpack("f", struct.pack("f", value))[0]


def xml_geometry_difference(case, result, absolute):
    """Compare the stored upstream geometry; do not infer browser conformance."""
    if "error" in result:
        return None
    cumulative = []
    for index, (node, expected) in enumerate(zip(result["nodes"], case["xml_expected"], strict=True)):
        observed = [f32(value) for value in node["rect"]]
        parent = case["nodes"][index]["parent"]
        if absolute:
            cumulative.append(observed[:2])
            if parent is not None:
                observed[0] = f32(observed[0] - f32(result["nodes"][parent]["rect"][0]))
                observed[1] = f32(observed[1] - f32(result["nodes"][parent]["rect"][1]))
        else:
            cumulative.append([f32(observed[0] + (cumulative[parent][0] if parent is not None else 0)),
                               f32(observed[1] + (cumulative[parent][1] if parent is not None else 0))])
        if case["use_rounding"]:
            x, y = cumulative[-1]
            observed = [round_pixel(observed[0]), round_pixel(observed[1]),
                        round_pixel(f32(x + observed[2])) - round_pixel(x),
                        round_pixel(f32(y + observed[3])) - round_pixel(y)]
        target = [float(expected[key]) for key in ("x", "y", "width", "height")]
        if any(abs(a - b) >= 0.1 for a, b in zip(observed, target, strict=True)):
            return {"node": index + 1, "field": "rect", "actual": observed, "expected": target}
    return None


def audit(document, native, taffy, group=None, progress=True):
    selected = [(index, case) for index, case in enumerate(document["cases"])
                if group is None or case["source"]["path"].split("/")[0] == group]
    if not selected:
        raise ValueError("No upstream fixtures selected")
    groups = collections.defaultdict(collections.Counter)
    failures = []
    xml_failures = []
    # Group selection preserves a contiguous range in the sorted corpus.
    for start in range(0, len(selected), 100):
        batch = selected[start:start + 100]
        first = batch[0][0]
        if batch[-1][0] != first + len(batch) - 1:
            raise ValueError("Non-contiguous upstream fixture range")
        batch_cases = [case for _, case in batch]
        actual = replay_cases(native, batch_cases, first)
        expected = replay_cases(taffy, batch_cases)
        for (_, case), observed, reference in zip(batch, actual, expected, strict=True):
            if observed["name"] != case["name"] or reference["name"] != case["name"]:
                raise ValueError("Upstream case identity mismatch")
            category = case["source"]["path"].split("/")[0]
            groups[category]["total"] += 1
            failure = None
            if "error" in observed:
                failure = {"kind": "native_error", "message": observed["error"]}
            if "error" in reference:
                failure = {"kind": "reference_error", "message": reference["error"],
                           "native_error": observed.get("error")}
            if failure is None:
                try:
                    compare(observed, reference)
                    compare_grid_details(observed, reference)
                except AssertionError as error:
                    failure = {"kind": "layout_difference", "difference": error.args[0]}
            if failure is None:
                groups[category]["passed"] += 1
            else:
                groups[category][failure["kind"]] += 1
                failures.append({"name": case["name"], "source": case["source"], **failure})
            native_xml = xml_geometry_difference(case, observed, absolute=True)
            taffy_xml = xml_geometry_difference(case, reference, absolute=False)
            if "error" not in observed:
                groups[category]["native_xml_geometry_comparisons"] += 1
            if "error" not in reference:
                groups[category]["taffy_xml_geometry_comparisons"] += 1
            if native_xml is not None:
                groups[category]["native_xml_geometry_difference"] += 1
            if taffy_xml is not None:
                groups[category]["taffy_xml_geometry_difference"] += 1
            if native_xml is not None or taffy_xml is not None:
                xml_failures.append({"name": case["name"], "source": case["source"],
                                     "native": native_xml, "taffy": taffy_xml})
        if progress:
            print(f"Audited {start + len(batch)}/{len(selected)}, Taffy differences/errors {len(failures)}",
                  flush=True)
    return {"protocol": "AXISLY_UPSTREAM_AUDIT_V1", "taffy": document["taffy"],
            "commit": document["commit"], "corpus_sha256": document["corpus_sha256"],
            "total": len(selected), "passed": len(selected) - len(failures),
            "groups": {key: dict(value) for key, value in sorted(groups.items())},
            "failures": failures,
            "xml_geometry_differences": xml_failures}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cases", required=True)
    parser.add_argument("--axisly", required=True)
    parser.add_argument("--taffy", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--group")
    args = parser.parse_args()
    document = json.loads(Path(args.cases).read_text(encoding="utf-8"))
    result = audit(document, args.axisly, args.taffy, args.group)
    Path(args.output).write_text(json.dumps(result, ensure_ascii=True, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: result[key] for key in ("total", "passed", "groups")}, indent=2))
    return 1 if result["failures"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
