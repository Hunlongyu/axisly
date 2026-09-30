"""Audit every pinned upstream fixture without excluding failed inputs."""

import argparse
import collections
import json
from pathlib import Path
import subprocess

from grid_detail_compare import compare_grid_details
from test_flex_matrix import compare
from upstream_expectations import xml_differences


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


def audit(document, native, taffy, group=None, progress=True):
    selected = [(index, case) for index, case in enumerate(document["cases"])
                if group is None or case["source"]["path"].split("/")[0] == group]
    if not selected:
        raise ValueError("No upstream fixtures selected")
    groups = collections.defaultdict(collections.Counter)
    failures = []
    xml_failures = []
    xml_metrics = []
    assertion_counts = collections.Counter()
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
            native_geometry, native_metrics = xml_differences(case, observed, absolute=True)
            taffy_geometry, taffy_metrics = xml_differences(case, reference, absolute=False)
            native_xml = native_geometry[0] if native_geometry else None
            taffy_xml = taffy_geometry[0] if taffy_geometry else None
            for node_expected in case["xml_expected"]:
                assertion_counts.update(node_expected.keys())
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
            if native_metrics or taffy_metrics:
                xml_metrics.append({"name": case["name"], "source": case["source"],
                                    "native": native_metrics, "taffy": taffy_metrics})
        if progress:
            print(f"Audited {start + len(batch)}/{len(selected)}, Taffy differences/errors {len(failures)}",
                  flush=True)
    return {"protocol": "AXISLY_UPSTREAM_AUDIT_V1", "taffy": document["taffy"],
            "commit": document["commit"], "corpus_sha256": document["corpus_sha256"],
            "total": len(selected), "passed": len(selected) - len(failures),
            "groups": {key: dict(value) for key, value in sorted(groups.items())},
            "failures": failures,
            "xml_assertions": dict(sorted(assertion_counts.items())),
            "xml_geometry_differences": xml_failures,
            "xml_metric_differences": xml_metrics}


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
