"""Admission, protocol, independent goldens, mismatch persistence and replay."""
import argparse
import copy
from contextlib import redirect_stdout, redirect_stderr
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import advanced as a

TOOLS = []
FIXTURES = a.load_cases(Path(__file__).with_name("fixtures") / "advanced.json")


class AdvancedTests(unittest.TestCase):
    def test_generation_is_deterministic(self):
        a.validate_cases(a.random_cases(17, 50))
        self.assertEqual(a.random_cases(17, 2), a.random_cases(17, 2))
        self.assertNotEqual(a.random_cases(17, 2), a.random_cases(18, 2))

    def test_admission_rejects_semantic_mismatches(self):
        for mutate in (
            lambda c: c.update(flow="invalid"),
            lambda c: c.update(implicit=False),
            lambda c: c.update(rounding=True),
            lambda c: c.update(available=[0, 0]),
            lambda c: c["items"][0].update(preferred=[50, 20]),
            lambda c: c["items"][0].update(baseline=8),
            lambda c: c["items"].append(copy.deepcopy(c["items"][0])),
            lambda c: c["columns"].__setitem__(0, ["content"]),
        ):
            c = copy.deepcopy(FIXTURES[0])
            mutate(c)
            with self.assertRaises(ValueError):
                a.validate_case(c)

    def test_baseline_clipping_is_rejected(self):
        c = copy.deepcopy(next(c for c in FIXTURES if c["name"] == "fixture-baseline-offsets"))
        c["rows"] = [["fixed", 20]]
        with self.assertRaisesRegex(ValueError, "Clipped"):
            a.validate_case(c)

    def test_bottom_baseline_intrinsic_difference_rejected(self):
        c = copy.deepcopy(next(c for c in FIXTURES if c["name"] == "fixture-baseline-fallback"))
        c["items"][0]["minimum"][1] = 0
        with self.assertRaisesRegex(ValueError, "MinContent"):
            a.validate_case(c)

    def test_nonfinite_bool_and_oversized_numbers_rejected(self):
        for invalid in (True, float("nan"), float("inf"), -1, 10**400):
            c = copy.deepcopy(FIXTURES[0])
            c["implicit_tracks"][0] = invalid
            with self.assertRaises(ValueError):
                a.validate_case(c)

    def test_golden_percentage_padding(self):
        c = next(c for c in FIXTURES if c["name"] == "fixture-percentage-padding")
        expected = {"case": 0, "size": [174, 72], "items": [
            {"id": 0, "rect": [10, 5, 90, 60], "cell": [10, 5, 90, 60], "at": [0, 0, 1, 1]},
            {"id": 1, "rect": [104, 5, 40, 60], "cell": [104, 5, 40, 60], "at": [1, 0, 1, 1]}]}
        for tool in TOOLS:
            self.assertEqual(a.run_tool(tool, [c]), [expected])

    def test_golden_custom_baseline(self):
        c = next(c for c in FIXTURES if c["name"] == "fixture-baseline-offsets")
        for tool in TOOLS:
            result = a.run_tool(tool, [c])[0]
            self.assertEqual(result["size"], [200, 60])
            self.assertEqual([i["rect"] for i in result["items"]], [[0, 8, 20, 30], [100, 0, 25, 24]])

    def test_golden_dense_fills_hole(self):
        c = a.fixture("dense", [["fixed", 40]] * 3, [["fixed", 30]], [
            a.item(0, [None, None, 2, 1]), a.item(1, [None, None, 2, 1]), a.item(2, [None, None, 1, 1])])
        c.update(flow="row_dense", implicit=True)
        for tool in TOOLS:
            result = a.run_tool(tool, [c])[0]
            self.assertEqual([i["at"] for i in result["items"]], [[0, 0, 2, 1], [0, 1, 2, 1], [2, 0, 1, 1]])

    def test_malformed_placements_and_geometry_rejected(self):
        c = FIXTURES[5]
        valid = a.run_tool(TOOLS[0], [c])[0]
        for placement in ([False, 0, 1, 1], [0, 0, 0, 1], [9000, 0, 1, 1], None):
            bad = copy.deepcopy(valid)
            bad["items"][0]["at"] = placement
            with self.assertRaises(ValueError):
                a.parse_output(json.dumps(bad), [c])
        bad = copy.deepcopy(valid)
        bad["items"][0]["rect"][0] = -1
        with self.assertRaises(ValueError):
            a.parse_output(json.dumps(bad), [c])

    def test_placement_mismatch_saved_and_replayable(self):
        c = FIXTURES[5]
        valid = a.run_tool(TOOLS[0], [c])[0]
        bad = copy.deepcopy(valid)
        bad["items"][0]["at"][0] += 1
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            source = a.compare.save_failure(folder / "source", [c], {})
            args = ["--gridly", str(TOOLS[0]), "--taffy", str(TOOLS[1]), "--replay", str(source), "--failure-dir", str(folder / "fail")]
            with patch("advanced.run_tool", side_effect=[[valid], [bad]]), redirect_stderr(io.StringIO()):
                self.assertEqual(a.main(args), 1)
            failure = folder / "fail" / "failure.json"
            self.assertEqual(a.load_cases(failure), [c])
            args[args.index(str(source))] = str(failure)
            with redirect_stdout(io.StringIO()):
                self.assertEqual(a.main(args), 0)

    def test_runner_failure_is_not_success(self):
        with tempfile.TemporaryDirectory() as temporary:
            source = a.compare.save_failure(Path(temporary) / "source", [FIXTURES[5]], {})
            with patch("advanced.run_tool", side_effect=ValueError("runner failure")), redirect_stderr(io.StringIO()):
                self.assertEqual(a.main(["--gridly", str(TOOLS[0]), "--taffy", str(TOOLS[1]), "--replay", str(source),
                                         "--failure-dir", str(Path(temporary) / "fail")]), 1)

    def test_both_runners_reject_bad_v2(self):
        valid = a.protocol([FIXTURES[5]])
        for tool in TOOLS:
            for text in ("GRIDLY_REFERENCE_V2 0", "GRIDLY_REFERENCE_V2 1", valid + "extra", valid.replace("P 0.25", "P nan")):
                result = subprocess.run([str(tool)], input=text, text=True, encoding="utf-8", capture_output=True,
                                        timeout=10, creationflags=subprocess.CREATE_NO_WINDOW if a.compare.os.name == "nt" else 0)
                self.assertNotEqual(result.returncode, 0)
                self.assertTrue(result.stderr.strip())


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--gridly", type=Path, required=True)
    parser.add_argument("--taffy", type=Path, required=True)
    args = parser.parse_args()
    TOOLS = [args.gridly, args.taffy]
    unittest.main(argv=[__file__], verbosity=2)
