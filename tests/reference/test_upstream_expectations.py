"""Exercise assertion admission and catch independently corrupted outputs."""

import copy
import unittest

from upstream_expectations import parse_resolved_tracks, validate_expectation, xml_differences


class ExpectedMetrics(unittest.TestCase):
    def setUp(self):
        self.case = {"use_rounding": False, "nodes": [{"parent": None}],
            "xml_expected": [{"x": "0", "y": "0", "width": "100", "height": "20",
                "scroll_width": "26", "scroll_height": "0",
                "resolved-rows": "none", "resolved-columns": "[a] 40px [b] 60px [c]"}]}
        self.result = {"nodes": [{"rect": [0, 0, 100, 20], "border": [2, 0, 4, 0],
            "scrollbar": [10, 0], "overflow": [0, 0, 110, 20], "grid": {
                "rows": {"positions": [], "line_names": []},
                "columns": {"positions": [[2, 42], [45, 105]],
                            "line_names": [["a"], ["b"], ["c"]]}}}]}

    def test_all_fields_and_used_sizes_exclude_gaps(self):
        self.assertEqual(xml_differences(self.case, self.result, True), ([], []))

    def test_corrupted_scroll_track_size_and_names_are_detected(self):
        for mutate, field in ((lambda node: node["overflow"].__setitem__(2, 111), "scroll_width"),
                (lambda node: node["grid"]["columns"]["positions"][0].__setitem__(1, 43), "resolved-columns"),
                (lambda node: node["grid"]["columns"]["line_names"][1].append("extra"), "resolved-columns")):
            with self.subTest(field=field):
                changed = copy.deepcopy(self.result)
                mutate(changed["nodes"][0])
                geometry, metrics = xml_differences(self.case, changed, True)
                self.assertEqual(geometry, [])
                self.assertEqual([item["field"] for item in metrics], [field])

    def test_rounding_uses_cumulative_edges_for_scroll_extent(self):
        case = {"use_rounding": True, "nodes": [{"parent": None}, {"parent": 0}],
            "xml_expected": [dict(x="0", y="0", width="100", height="20"),
                dict(x="1", y="0", width="10", height="10", scroll_width="2")]}
        nodes = [{"rect": [0.25, 0, 100, 20]},
                 {"rect": [0.85, 0, 10.4, 10], "border": [0.4, 0, 0.4, 0],
                  "scrollbar": [1, 0], "overflow": [0, 0, 11.4, 10]}]
        geometry, metrics = xml_differences(case, {"nodes": nodes}, True)
        self.assertEqual((geometry, metrics), ([], []))

    def test_unknown_nonfinite_and_malformed_assertions_are_rejected(self):
        base = dict(x="0", y="0", width="0", height="0")
        for extra in ({"unknown": "0"}, {"scroll_width": "nan"},
                      {"resolved-rows": "[name] bad"}, {"resolved-columns": "[] 1px"}):
            with self.subTest(extra=extra), self.assertRaises(ValueError):
                validate_expectation(base | extra)
        with self.assertRaisesRegex(ValueError, "Incomplete"):
            validate_expectation({"width": "0"})
        self.assertEqual(parse_resolved_tracks("[first last] 1.25px [end]"),
                         [["first", "last"], 1.25, ["end"]])


if __name__ == "__main__":
    unittest.main()
