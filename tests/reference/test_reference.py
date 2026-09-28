"""Checks the comparison boundary, diagnostics and actual runner protocol, not the layout implementation."""
import argparse
from contextlib import redirect_stderr, redirect_stdout
import io
import json
import math
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import compare

TOOLS = []


def simple():
    return compare.fixture("control", [["fixed", 100]], [["fixed", 80]], [compare.item()])


def result():
    return {"case": 0, "size": [100, 80], "items": [{"id": 0, "rect": [0, 0, 100, 80], "cell": [0, 0, 100, 80]}]}


class ReferenceTests(unittest.TestCase):
    def test_fixtures_and_generation(self):
        cases = compare.load_cases(Path(__file__).with_name("fixtures") / "common.json")
        compare.validate_cases(cases + compare.random_cases(20260925, 250))
        self.assertEqual(len(cases), 30)
        self.assertEqual(compare.random_cases(123, 5), compare.random_cases(123, 5))
        self.assertNotEqual(compare.random_cases(123, 5), compare.random_cases(124, 5))

    def test_zero_and_large_id(self):
        case = simple()
        case["available"] = [0, 0]
        case["items"][0]["id"] = 2**64 - 1
        self.assertIn(str(2**64 - 1), compare.protocol([case]))

    def test_nonfinite_and_huge_inputs(self):
        for invalid in (math.nan, math.inf, -1, 10**400, True):
            with self.subTest(value=str(invalid)[:15]):
                case = simple()
                case["available"][0] = invalid
                with self.assertRaises(ValueError):
                    compare.validate_case(case)

    def test_duplicate_ids(self):
        case = simple()
        case["items"].append(compare.item())
        with self.assertRaises(ValueError):
            compare.validate_case(case)

    def test_invalid_span(self):
        for span in (0, 2):
            case = simple()
            case["items"][0]["at"][2] = span
            with self.assertRaises(ValueError):
                compare.validate_case(case)

    def test_unknown_fields(self):
        case = simple()
        case["rounding"] = True
        with self.assertRaises(ValueError):
            compare.validate_case(case)

    def test_fractional_css_weights_rejected(self):
        case = simple()
        case["columns"] = [["weighted", 0.5, 0]]
        with self.assertRaisesRegex(ValueError, "weights"):
            compare.validate_case(case)

    def test_unsupported_weighted_cap(self):
        case = simple()
        case["columns"] = [["weighted", 1, 0, 100]]
        with self.assertRaisesRegex(ValueError, "bounds"):
            compare.validate_case(case)

    def test_content_shrink_rejected(self):
        case = simple()
        case["columns"] = [["content"]]
        case["items"][0]["preferred"] = [100, 0]
        case["available"][0] = 50
        with self.assertRaisesRegex(ValueError, "Content shrink"):
            compare.validate_case(case)

    def test_content_spans_rejected(self):
        case = simple()
        case["columns"] = [["content"], ["content"]]
        case["items"][0]["at"][2] = 2
        with self.assertRaisesRegex(ValueError, "Spans"):
            compare.validate_case(case)

    def test_content_flex_mix_rejected(self):
        case = simple()
        case["columns"] = [["content"], ["weighted", 1, 0]]
        with self.assertRaisesRegex(ValueError, "same axis"):
            compare.validate_case(case)

    def test_oversized_margin_rejected(self):
        case = simple()
        case["items"][0]["margin"][0] = 101
        with self.assertRaisesRegex(ValueError, "margins"):
            compare.validate_case(case)

    def test_clipped_preference_rejected(self):
        case = simple()
        case["items"][0]["align"][0] = "end"
        case["items"][0]["preferred"][0] = 101
        with self.assertRaisesRegex(ValueError, "Clipped"):
            compare.validate_case(case)

    def test_intrinsic_weighted_requirement_rejected(self):
        case = simple()
        case["columns"] = [["weighted", 1, 0]]
        case["items"][0]["preferred"][0] = 10
        with self.assertRaisesRegex(ValueError, "Flexible-axis"):
            compare.validate_case(case)

    def test_tolerance_and_detection(self):
        a, b = result(), result()
        b["items"][0]["rect"][2] += 0.0001
        self.assertIsNone(compare.difference(a, b))
        b["items"][0]["rect"][2] += 0.01
        self.assertIn("rect[2]", compare.difference(a, b))
        b = result()
        b["items"][0]["cell"][0] = 0.01
        self.assertIn("cell[0]", compare.difference(a, b))
        b = result()
        b["size"][0] = 500
        self.assertIn("size[0]", compare.difference(a, b))

    def test_malformed_outputs(self):
        variants = ["", "{}", json.dumps(result()) + "\n" + json.dumps(result()), '{"size":[NaN,0]}']
        for text in variants:
            with self.subTest(text=text[:30]), self.assertRaises(ValueError):
                compare.parse_output(text, [simple()])
        for field, value in (("case", 1), ("case", False), ("size", [math.inf, 80]), ("items", [])):
            data = result()
            data[field] = value
            with self.assertRaises(ValueError):
                compare.parse_output(json.dumps(data), [simple()])

    def test_item_order_and_finiteness(self):
        for ident in (1, False):
            data = result()
            data["items"][0]["id"] = ident
            with self.assertRaises(ValueError):
                compare.parse_output(json.dumps(data), [simple()])
        for value in (-1, 10**400, False):
            data = result()
            data["items"][0]["rect"][0] = value
            with self.assertRaises(ValueError):
                compare.parse_output(json.dumps(data), [simple()])

    def test_duplicate_json_keys(self):
        with self.assertRaises(ValueError):
            compare.strict_json('{"schema":1,"schema":2}')

    def test_mismatch_fixture_and_replay(self):
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder)
            source = compare.save_failure(directory / "input", [simple()], {})
            bad = result()
            bad["size"][0] += 1
            args = ["--axisly", str(TOOLS[0]), "--taffy", str(TOOLS[1]), "--replay", str(source),
                    "--failure-dir", str(directory / "failure")]
            with patch("compare.run_tool", side_effect=[[result()], [bad]]), redirect_stderr(io.StringIO()):
                self.assertEqual(compare.main(args), 1)
            failure = directory / "failure" / "failure.json"
            self.assertEqual(compare.load_cases(failure), [simple()])
            args[args.index(str(source))] = str(failure)
            with redirect_stdout(io.StringIO()):
                self.assertEqual(compare.main(args), 0)  # Replay the saved input through BOTH real binaries.

    def test_runner_failure_is_not_a_pass(self):
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder)
            source = compare.save_failure(directory / "input", [simple()], {})
            with patch("compare.run_tool", side_effect=ValueError("simulated protocol failure")), redirect_stderr(io.StringIO()):
                code = compare.main(["--axisly", str(TOOLS[0]), "--taffy", str(TOOLS[1]), "--replay", str(source),
                                     "--failure-dir", str(directory / "failure")])
            self.assertEqual(code, 1)
            self.assertEqual(compare.load_cases(directory / "failure" / "failure.json"), [simple()])

    def test_both_runners_against_fixed_golden(self):
        for tool in TOOLS:
            with self.subTest(tool=tool.name):
                self.assertEqual(compare.run_tool(tool, [simple()]), [result()])

    def test_both_runners_reject_bad_protocol(self):
        valid = compare.protocol([simple()])
        for tool in TOOLS:
            for text in ("wrong 1", "AXISLY_REFERENCE_V1 0", "AXISLY_REFERENCE_V1 10001", valid + "extra",
                         "AXISLY_REFERENCE_V1 1\n", valid.replace("F 100", "F nan"), valid.replace("F 100", "F -1")):
                with self.subTest(tool=tool.name, text=text[:30]):
                    completed = subprocess.run([str(tool)], input=text, text=True, encoding="utf-8", capture_output=True,
                                               timeout=10, creationflags=subprocess.CREATE_NO_WINDOW if compare.os.name == "nt" else 0)
                    self.assertNotEqual(completed.returncode, 0)
                    self.assertTrue(completed.stderr.strip())


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--axisly", type=Path, required=True)
    parser.add_argument("--taffy", type=Path, required=True)
    args = parser.parse_args()
    TOOLS = [args.axisly, args.taffy]
    unittest.main(argv=[__file__], verbosity=2)
