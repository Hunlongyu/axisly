"""Validate fixture admission and code generation independently of layout."""

from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET

from css_case_codegen import case_source, generate, grid_line, number, string, track_list
from upstream_fixture import read_case, style


class FixtureAdmission(unittest.TestCase):
    def test_css_attributes_preserve_axes_and_edges(self):
        actual = style(ET.fromstring('''<div display="grid" direction="rtl"
            box-sizing="content-box" min-height="25%" border-left="2px"
            row-gap="7px" overflow-y="scroll" justify-self="safe end"
            grid-column-start="span 2 name"/>'''))
        self.assertEqual(actual, {
            "display": "Grid", "direction": "Rtl", "box_sizing": "ContentBox",
            "min_size": {"height": "25%"}, "border": {"left": "2px"},
            "gap": {"height": "7px"}, "overflow": {"x": "Visible", "y": "Scroll"},
            "justify_self": "SafeEnd", "grid_column": {"start": "span 2 name"}})

    def test_unknown_attributes_are_errors(self):
        with self.assertRaisesRegex(ValueError, "Unknown upstream attribute"):
            style(ET.fromstring('<div unsupported-layout-field="10px"/>'))
        with self.assertRaisesRegex(ValueError, "Unknown upstream attribute"):
            style(ET.fromstring('<div writing-mode="vertical-lr"/>'))

    def test_containment_uses_the_pinned_bitset_representation(self):
        self.assertEqual(style(ET.fromstring('<div contain="content"/>')), {"contain": 3})


    def test_measured_text_and_source_identity(self):
        xml = '''<test name="example" use-rounding="false">
            <viewport width="min-content"/><input><div display="block">
            <text writing-mode="vertical-lr">  XX​XXX  </text></div></input>
            <expectations><node x="0" y="0" width="10" height="50">
            <node x="0" y="0" width="10" height="50"/>
            </node></expectations></test>'''
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            path = root / "leaf" / "example.xml"
            path.parent.mkdir()
            path.write_text(xml, encoding="utf-8")
            actual = read_case(path, root, 7)
            self.assertEqual(actual["name"], "upstream-0007-leaf")
            self.assertEqual(actual["available"], ["min-content", "max-content"])
            self.assertEqual(actual["nodes"][1]["measure"], {
                "ahem_text": "XX​XXX", "vertical": True})
            self.assertEqual(actual["nodes"][1]["parent"], 0)
            self.assertFalse(actual["use_rounding"])
            self.assertEqual(actual["source"]["path"], "leaf/example.xml")
            self.assertEqual(len(actual["source"]["sha256"]), 64)
            path.write_text(xml.replace('name="example"', 'name="other"'), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Invalid fixture identity"):
                read_case(path, root, 7)

    def test_input_and_expected_topology_must_match(self):
        xml = '''<test name="case"><viewport/><input><div><div/></div></input>
            <expectations><node/></expectations></test>'''
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            path = root / "case.xml"
            path.write_text(xml, encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "topology differs"):
                read_case(path, root, 0)


class TypedCodeGeneration(unittest.TestCase):
    def case(self):
        return {"name": "example", "available": [100, "min-content"],
                "nodes": [{"id": 1, "parent": None, "style": {"display": "Block"}},
                          {"id": 2, "parent": 0, "style": {},
                           "measure": {"ahem_text": "X​XX", "vertical": False}}]}

    def test_finite_f32_extremes_are_admitted(self):
        self.assertEqual(number(36893500000000000000), "3.68935013e+19F")
        for invalid in (float("inf"), float("nan"), 1e39):
            with self.assertRaises(ValueError):
                number(invalid)

    def test_utf8_and_source_delimiters_remain_string_data(self):
        self.assertEqual(string('X​"\\\n'), '"X\\342\\200\\213\\"\\\\\\012"')

    def test_nested_track_functions_and_line_names(self):
        tracks, names = track_list('[start] repeat(2, [inner] minmax(20px, 1fr) [end]) [last]')
        self.assertIn('GridRepeat{RepeatKind::Count, 2,', tracks)
        self.assertIn('TrackSizing::minmax(TrackBreadth::length(20.0F), TrackBreadth::fraction(1.0F))', tracks)
        self.assertIn('{{"inner"}, {"end"}}', tracks)
        self.assertEqual(names, '{{"start"}, {"last"}}')

    def test_large_explicit_track_lists_preserve_their_count(self):
        tracks, names = track_list("repeat(auto-fill, " + "1px " * 21000 + ")")
        self.assertEqual(tracks, '{GridRepeat{RepeatKind::AutoFill, 1, '
                         'fixture_track_runs<TrackSizing>({{21000, TrackSizing::fixed(1.0F)}}), {}}}')
        self.assertEqual(names, '{}')

    def test_grid_line_named_occurrences_and_extreme_indices(self):
        self.assertEqual(grid_line('name -2'), 'GridLine::named("name", -2)')
        self.assertEqual(grid_line('span 2 name'), 'GridLine::named_span("name", 2)')
        self.assertEqual(grid_line('-19005'), 'GridLine::index(-19005)')
        for invalid in ('0', '32768', 'span -1', 'span 65536', 'name other', '1 2'):
            with self.assertRaises(ValueError):
                grid_line(invalid)

    def test_unknown_fields_and_forward_parents_are_errors(self):
        case = self.case()
        case['nodes'][0]['style']['unknown'] = 1
        with self.assertRaisesRegex(ValueError, "Unmapped fixture Style field"):
            case_source(case, 0)
        case = self.case()
        case['nodes'][1]['parent'] = 2
        with self.assertRaisesRegex(ValueError, "Parent must precede child"):
            case_source(case, 0)

    def test_regeneration_preserves_unchanged_source_timestamps(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            generate({'cases': [self.case()]}, output)
            before = {path.name: path.stat().st_mtime_ns for path in output.iterdir()}
            generate({'cases': [self.case()]}, output)
            after = {path.name: path.stat().st_mtime_ns for path in output.iterdir()}
            self.assertEqual(before, after)


if __name__ == '__main__':
    unittest.main()
