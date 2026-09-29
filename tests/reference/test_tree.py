"""Pinned Taffy Style inventory and mixed-layout tree oracle contract."""
import argparse
import copy
import json
import math
from pathlib import Path
import re
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[2]
FIXTURE = Path(__file__).with_name("fixtures") / "tree.json"
MATRIX = ROOT / "docs" / "30-taffy-014-capability-matrix.md"
TAFFY = None


def invoke(value):
    completed = subprocess.run(
        [str(TAFFY)], input=json.dumps(value), text=True, encoding="utf-8",
        capture_output=True, timeout=15,
    )
    return completed


class TreeReferenceTests(unittest.TestCase):
    def test_compiled_style_fields_match_matrix(self):
        completed = invoke({"protocol": "AXISLY_TREE_STYLE_FIELDS_V1"})
        self.assertEqual(completed.returncode, 0, completed.stderr)
        inventory = json.loads(completed.stdout)
        self.assertEqual(inventory["taffy"], "0.14.0")
        self.assertEqual(len(inventory["fields"]), 44)
        fields = []
        for line in MATRIX.read_text(encoding="utf-8").splitlines():
            cells = [cell.strip() for cell in line.split("|")]
            if len(cells) == 7 and re.fullmatch(r"`[a-z_]+`", cells[1]):
                self.assertTrue(cells[3].startswith(("部分", "未支持", "内部字段")), line)
                self.assertTrue(cells[4], line)
                self.assertTrue(cells[5], line)
                fields.append(cells[1][1:-1])
        self.assertEqual(len(fields), 44)
        self.assertEqual(sorted(fields), inventory["fields"])

    def test_mixed_tree_golden_and_replay(self):
        fixture = json.loads(FIXTURE.read_text(encoding="utf-8"))
        first = invoke(fixture)
        self.assertEqual(first.returncode, 0, first.stderr)
        second = invoke(fixture)
        self.assertEqual(first.stdout, second.stdout)
        output = json.loads(first.stdout)
        self.assertEqual(output["protocol"], "AXISLY_TREE_V1")
        self.assertEqual(output["taffy"], "0.14.0")
        self.assertEqual([case["name"] for case in output["cases"]],
                         [case["name"] for case in fixture["cases"]])
        golden = {
            "flex-row-grow": {1: [0, 0, 300, 100], 2: [10, 0, 50, 20], 3: [70, 0, 220, 20]},
            "mixed-block-flex-grid": {10: [0, 0, 400, 130], 11: [0, 0, 200, 100],
                                       12: [0, 0, 100, 50], 13: [0, 0, 100, 50], 14: [0, 100, 80, 30]},
            "block-float-clear": {20: [0, 0, 240, 120], 21: [0, 0, 40, 30], 22: [0, 30, 60, 20]},
            "hidden-and-positioned": {30: [0, 0, 200, 50], 31: [0, 0, 0, 0],
                                      32: [10, 5, 20, 10], 33: [0, 0, 30, 10]},
            "content-box-and-negative-margin": {40: [0, 0, 180, 80], 41: [-3, 0, 52, 24],
                                                42: [49, 0, 20, 10]},
            "flex-balance-lines": {50: [0, 0, 100, 80], 51: [0, 0, 40, 10],
                                   52: [45, 0, 40, 10], 53: [0, 42.5, 40, 10]},
            "css-grid-tracks-placement": {60: [0, 0, 200, 100], 61: [55, 0, 20, 10],
                                          62: [0, 30, 10, 15]},
            "named-area-placement": {70: [0, 0, 200, 100], 71: [50, 0, 20, 10],
                                     72: [50, 30, 20, 10]},
            "grid-auto-fill": {80: [0, 0, 100, 30], 81: [0, 0, 10, 10]},
            "grid-auto-fit": {90: [0, 0, 100, 30], 91: [0, 0, 10, 10]},
            "block-adjacent-margins": {100: [0, 0, 200, 100], 101: [0, 0, 200, 10],
                                       102: [0, 30, 200, 10]},
            "grid-content-alignment": {110: [0, 0, 100, 80],
                                       111: [80, 42.5, 10, 10]},
            "flex-intrinsic-basis": {120: [0, 0, 200, 40], 121: [0, 0, 30, 10],
                                     122: [30, 0, 20, 10]},
            "flex-baseline": {130: [0, 0, 200, 50], 131: [0, 10, 30, 20],
                              132: [30, 0, 40, 20]},
            "grid-baseline": {140: [0, 0, 100, 40], 141: [0, 10, 20, 20],
                              142: [50, 0, 20, 20]},
            "grid-auto-margins": {150: [0, 0, 100, 50], 151: [40, 40, 20, 10]},
            "rtl-block-child": {160: [0, 0, 100, 40], 161: [80, 0, 20, 10]},
            "flex-scrollbar-reservation": {170: [0, 0, 100, 50], 171: [0, 0, 90, 20]},
            "block-legacy-text-align": {180: [0, 0, 100, 40], 181: [40, 0, 20, 10]},
            "block-contain-float": {190: [0, 0, 100, 30], 191: [0, 0, 20, 30]},
            "grid-absolute-area": {200: [0, 0, 200, 100], 201: [60, 35, 20, 10]},
            "grid-implicit-negative-lines": {210: [0, 0, 100, 30], 211: [30, 0, 10, 10],
                                             212: [75, 0, 10, 10]},
            "grid-row-dense-holes": {220: [0, 0, 90, 40], 221: [0, 0, 10, 10],
                                     222: [0, 20, 10, 10], 223: [60, 0, 10, 10]},
            "grid-named-line-occurrences": {230: [0, 0, 90, 30],
                                            231: [20, 0, 10, 10], 232: [0, 20, 50, 10]},
            "block-parent-child-margins": {240: [0, 0, 100, 45],
                                           241: [0, 20, 100, 10], 242: [0, 0, 100, 10]},
            "block-empty-margin-chain": {250: [0, 0, 100, 30],
                                         251: [0, 20, 100, 0], 252: [0, 0, 100, 0],
                                         253: [0, 20, 100, 10]},
            "block-negative-margin-chain": {260: [0, 0, 100, 0],
                                            261: [0, 20, 100, 0], 262: [0, -10, 100, 10]},
            "block-padding-stops-collapse": {270: [0, 0, 100, 52],
                                             271: [0, 0, 100, 52], 272: [0, 23, 100, 10]},
            "block-fixed-height-collapse": {280: [0, 0, 100, 70],
                                            281: [0, 20, 100, 50], 282: [0, 0, 100, 10]},
            "flex-small-grow-factors": {300: [0, 0, 100, 40],
                                        301: [0, 0, 32, 10], 302: [32, 0, 32, 10]},
            "flex-small-shrink-factor": {310: [0, 0, 40, 40], 311: [0, 0, 72, 10]},
            "flex-grow-max-freeze": {320: [0, 0, 150, 40],
                                    321: [0, 0, 50, 10], 322: [50, 0, 100, 10]},
            "flex-shrink-min-freeze": {330: [0, 0, 100, 40],
                                      331: [0, 0, 60, 10], 332: [60, 0, 40, 10]},
            "absolute-auto-margins": {400: [0, 0, 100, 100],
                                      401: [40, 40, 20, 20]},
            "absolute-one-auto-margin": {410: [0, 0, 100, 100],
                                          411: [70, 75, 20, 20]},
            "absolute-negative-auto-margins": {420: [0, 0, 100, 100],
                                               421: [10, 5, 100, 100]},
            "absolute-rtl-overconstraint": {430: [0, 0, 100, 100],
                                            431: [70, 5, 20, 20]},
            "absolute-flex-static-center": {440: [0, 0, 100, 100],
                                            441: [40, 45, 20, 10]},
            "absolute-flex-static-end": {450: [0, 0, 100, 100],
                                         451: [80, 90, 20, 10]},
            "absolute-grid-static-center": {460: [0, 0, 100, 100],
                                            461: [40, 45, 20, 10]},
            "absolute-grid-static-end": {470: [0, 0, 100, 100],
                                         471: [80, 90, 20, 10]},
            "absolute-block-static-padding": {480: [0, 0, 100, 100],
                                              481: [10, 5, 20, 10]},
            "absolute-flex-static-padding-rtl": {490: [0, 0, 100, 100],
                                                 491: [60, 5, 20, 10]},
            "absolute-grid-static-padding": {500: [0, 0, 100, 100],
                                             501: [0, 0, 20, 10]},
            "absolute-block-static-flow": {510: [0, 0, 100, 100],
                                           511: [0, 0, 100, 20], 512: [0, 35, 20, 10],
                                           513: [0, 35, 100, 20]},
            "block-min-width-before-children": {600: [0, 0, 200, 40],
                                                601: [0, 0, 200, 10]},
            "flex-min-width-before-children": {610: [0, 0, 200, 40],
                                               611: [0, 0, 200, 10]},
            "grid-min-width-before-tracks": {620: [0, 0, 200, 40],
                                             621: [0, 0, 200, 10]},
            "flex-auto-min-height": {700: [0, 0, 100, 100],
                                     701: [0, 0, 20, 100]},
            "grid-auto-min-height": {710: [0, 0, 100, 100],
                                     711: [0, 0, 20, 100]},
            "grid-negative-implicit-sequence": {800: [0, 0, 100, 30],
                                                801: [0, 0, 15, 10],
                                                802: [15, 0, 20, 10]},
            "block-table-intrinsic": {900: [0, 0, 100, 10], 901: [0, 0, 30, 10]},
            "block-replaced-intrinsic": {910: [0, 0, 100, 10],
                                         911: [0, 0, 30, 10]},
            "block-table-margin-boundary": {920: [0, 0, 100, 45],
                                            921: [0, 0, 0, 45], 922: [0, 20, 0, 10]},
            "overflow-scroll-padding": {1000: [0, 0, 100, 50],
                                        1001: [10, 3, 20, 10]},
            "overflow-rtl-origin": {1010: [0, 0, 100, 40],
                                    1011: [80, 0, 20, 10]},
            "overflow-hidden-boundary": {1020: [0, 0, 100, 100],
                                         1021: [0, 0, 20, 20],
                                         1022: [0, 0, 100, 100]},
            "overflow-unreachable": {1030: [0, 0, 100, 50],
                                     1031: [-30, 0, 20, 10]},
            "overflow-absolute-padding": {1040: [0, 0, 100, 50],
                                          1041: [0, 0, 10, 10],
                                          1042: [40, 10, 20, 20]},
            "overflow-border-origin": {1050: [0, 0, 100, 50],
                                       1051: [3, 2, 20, 10]},
            "grid-span-order-short-first": {1060: [0, 0, 150, 20],
                                            1061: [0, 0, 123.33332824707031, 20],
                                            1062: [0, 0, 150, 20]},
            "grid-span-order-long-first": {1070: [0, 0, 150, 20],
                                           1071: [0, 0, 150, 20],
                                           1072: [0, 0, 123.33332824707031, 20]},
            "grid-equal-intrinsic-growth": {1080: [0, 0, 100, 20],
                                            1081: [0, 0, 50, 20],
                                            1082: [50, 0, 50, 20]},
            "grid-capped-intrinsic-growth": {1090: [0, 0, 80, 20],
                                             1091: [0, 0, 50, 20],
                                             1092: [50, 0, 30, 20]},
            "grid-visible-automatic-minimum": {1100: [0, 0, 40, 20],
                                               1101: [0, 0, 80, 20]},
            "grid-scroll-automatic-minimum": {1110: [0, 0, 40, 20],
                                              1111: [0, 0, 40, 20]},
            "grid-scroll-explicit-minimum": {1120: [0, 0, 10, 20],
                                             1121: [0, 0, 20, 20]},
            "grid-spanning-flex-minimum": {1130: [0, 0, 40, 20],
                                           1131: [0, 0, 40, 20]},
            "grid-spanning-auto-minimum": {1140: [0, 0, 40, 20],
                                           1141: [0, 0, 80, 20]},
            "grid-spanning-scroll-minimum": {1150: [0, 0, 40, 20],
                                             1151: [0, 0, 40, 20]},
            "grid-fr-small-min-query": {1160: [0, 0, 32, 20],
                                        1161: [0, 0, 32, 20]},
            "grid-fr-small-max-query": {1170: [0, 0, 40, 20],
                                        1171: [0, 0, 40, 20]},
            "grid-fr-weighted-min-query": {1180: [0, 0, 80, 20],
                                           1181: [0, 0, 80, 20]},
            "grid-fr-weighted-max-query": {1190: [0, 0, 100, 20],
                                           1191: [0, 0, 100, 20]},
            "grid-auto-min-query": {1200: [0, 0, 40, 20],
                                    1201: [0, 0, 20, 20],
                                    1202: [20, 0, 20, 20]},
            "grid-auto-max-query": {1210: [0, 0, 140, 20],
                                    1211: [0, 0, 70, 20],
                                    1212: [70, 0, 70, 20]},
            "grid-max-track-min-query": {1220: [0, 0, 70, 20],
                                         1221: [0, 0, 70, 20]},
            "block-float-ordinary-overlap": {1230: [0, 0, 100, 30],
                                             1231: [0, 0, 30, 30],
                                             1232: [0, 0, 100, 10]},
            "block-float-flow-root": {1240: [0, 0, 100, 30],
                                      1241: [0, 0, 30, 30],
                                      1242: [30, 0, 70, 10]},
            "block-float-fixed-wrap": {1250: [0, 0, 100, 40],
                                       1251: [0, 0, 30, 30],
                                       1252: [0, 30, 80, 10]},
            "block-float-both-sides": {1260: [0, 0, 100, 30],
                                       1261: [0, 0, 30, 30],
                                       1262: [70, 0, 30, 30],
                                       1263: [30, 0, 40, 10]},
            "block-float-margin-context": {1270: [0, 0, 100, 30],
                                           1271: [0, 0, 30, 30],
                                           1272: [30, 5, 65, 10]},
            "block-float-overflow-context": {1280: [0, 0, 100, 30],
                                             1281: [0, 0, 30, 30],
                                             1282: [30, 0, 70, 10]},
            "block-float-flex-context": {1290: [0, 0, 100, 30],
                                         1291: [0, 0, 30, 30],
                                         1292: [30, 0, 70, 10]},
            "block-float-grid-context": {1300: [0, 0, 100, 30],
                                         1301: [0, 0, 30, 30],
                                         1302: [30, 0, 70, 10]},
            "block-float-self-clear": {1310: [0, 0, 100, 40],
                                       1311: [0, 0, 30, 30],
                                       1312: [70, 30, 30, 10]},
            "block-float-intrinsic-child": {1320: [0, 0, 100, 10],
                                            1321: [0, 0, 40, 10],
                                            1322: [0, 0, 40, 10]},
            "block-float-intrinsic-overflow": {1330: [0, 0, 100, 10],
                                               1331: [0, 0, 150, 10],
                                               1332: [0, 0, 150, 10]},
            "block-float-intrinsic-pair": {1340: [0, 0, 100, 10],
                                           1341: [0, 0, 60, 10],
                                           1342: [0, 0, 30, 10],
                                           1343: [30, 0, 30, 10]},
            "block-max-content-width": {1350: [0, 0, 70, 20],
                                        1351: [0, 0, 70, 10],
                                        1352: [0, 10, 70, 10]},
            "block-min-content-width": {1360: [0, 0, 20, 10],
                                        1361: [0, 0, 20, 10]},
            "block-float-intrinsic-flex": {1370: [0, 0, 100, 10],
                                           1371: [0, 0, 40, 10],
                                           1372: [0, 0, 40, 10],
                                           1373: [0, 0, 40, 10]},
            "block-float-intrinsic-grid": {1380: [0, 0, 100, 10],
                                           1381: [0, 0, 40, 10],
                                           1382: [0, 0, 40, 10],
                                           1383: [0, 0, 40, 10]},
            "block-ratio-width": {1390: [0, 0, 100, 50],
                                  1391: [0, 0, 100, 10]},
            "block-ratio-height": {1400: [0, 0, 80, 40],
                                   1401: [0, 0, 80, 10]},
            "block-ratio-auto-width": {1410: [0, 0, 100, 50],
                                       1411: [0, 0, 100, 10]},
            "flex-ratio-width": {1420: [0, 0, 100, 50],
                                 1421: [0, 0, 20, 50]},
            "flex-ratio-height": {1430: [0, 0, 80, 40],
                                  1431: [0, 0, 20, 40]},
            "flex-ratio-auto-width": {1440: [0, 0, 20, 10],
                                      1441: [0, 0, 20, 10]},
            "grid-ratio-width": {1450: [0, 0, 100, 50],
                                 1451: [0, 0, 100, 50]},
            "grid-ratio-height": {1460: [0, 0, 80, 40],
                                  1461: [0, 0, 80, 40]},
            "grid-ratio-auto-width": {1470: [0, 0, 20, 10],
                                      1471: [0, 0, 20, 10]},
            "block-ratio-content-box": {1480: [0, 0, 120, 60],
                                        1481: [10, 5, 100, 10]},
            "flex-ratio-content-box": {1490: [0, 0, 120, 60],
                                       1491: [10, 5, 20, 50]},
            "grid-ratio-content-box": {1500: [0, 0, 120, 60],
                                       1501: [10, 5, 100, 50]},
            "flex-auto-content-width": {1510: [0, 0, 20, 10],
                                        1511: [0, 0, 20, 10]},
            "grid-auto-content-width": {1520: [0, 0, 20, 10],
                                        1521: [0, 0, 20, 10]},
            "flex-auto-max-width-wrap": {1530: [0, 0, 60, 20],
                                         1531: [0, 0, 30, 10],
                                         1532: [30, 0, 30, 10],
                                         1533: [0, 10, 30, 10]},
            "flex-auto-max-height-wrap": {1540: [0, 0, 90, 40],
                                          1541: [0, 0, 30, 30],
                                          1542: [30, 0, 30, 30],
                                          1543: [60, 0, 30, 30]},
            "flex-auto-max-width-wrap-reverse": {1550: [0, 0, 60, 20],
                                                 1551: [0, 10, 30, 10],
                                                 1552: [30, 10, 30, 10],
                                                 1553: [0, 0, 30, 10]},
            "flex-auto-max-width-balance": {1560: [0, 0, 60, 20],
                                            1561: [0, 0, 30, 10],
                                            1562: [30, 0, 30, 10],
                                            1563: [0, 10, 30, 10]},
            "flex-auto-max-width-balance-reverse": {1570: [0, 0, 60, 20],
                                                    1571: [0, 10, 30, 10],
                                                    1572: [30, 10, 30, 10],
                                                    1573: [0, 0, 30, 10]},
            "absolute-percent-basic": {1580: [0, 0, 100, 80],
                                       1581: [25, 40, 20, 10]},
            "absolute-percent-border": {1590: [0, 0, 100, 80],
                                        1591: [66, 47.5, 20, 10]},
            "absolute-percent-stretch": {1600: [0, 0, 100, 80],
                                         1601: [10, 5, 70, 10]},
            "relative-percent-basic": {1610: [0, 0, 100, 80],
                                       1611: [25, 40, 20, 10]},
            "absolute-percent-flex": {1620: [0, 0, 100, 80],
                                      1621: [25, 40, 20, 10]},
            "absolute-percent-grid": {1630: [0, 0, 100, 80],
                                      1631: [25, 40, 20, 10]},
            "absolute-percent-grid-area": {1640: [0, 0, 100, 80],
                                           1641: [55, 50, 20, 10]},
            "absolute-block-auto-static": {1650: [0, 0, 100, 80],
                                           1651: [0, 0, 20, 10],
                                           1652: [0, 0, 20, 10]},
            "absolute-block-auto-percent": {1660: [0, 0, 100, 80],
                                            1661: [10, 20, 20, 10],
                                            1662: [0, 0, 20, 10]},
            "absolute-block-auto-trailing": {1670: [0, 0, 100, 80],
                                             1671: [70, 50, 20, 10],
                                             1672: [0, 0, 20, 10]},
            "absolute-block-auto-floats": {1680: [0, 0, 100, 80],
                                           1681: [5, 0, 50, 10],
                                           1682: [0, 0, 30, 10],
                                           1683: [30, 0, 20, 10]},
            "grid-fit-fr-tracks": {1690: [0, 0, 100, 10],
                                   1691: [0, 0, 30, 10],
                                   1692: [30, 0, 70, 10]},
            "grid-minmax-fr-tracks": {1700: [0, 0, 100, 10],
                                      1701: [0, 0, 50, 10],
                                      1702: [50, 0, 50, 10]},
            "grid-minmax-auto-tracks": {1710: [0, 0, 100, 10],
                                        1711: [0, 0, 70, 10],
                                        1712: [70, 0, 30, 10]},
            "grid-percent-indefinite-tracks": {1720: [0, 0, 90, 10],
                                               1721: [0, 0, 45, 10],
                                               1722: [45, 0, 45, 10]},
            "grid-fit-pair-tracks": {1730: [0, 0, 100, 10],
                                     1731: [0, 0, 30, 10],
                                     1732: [30, 0, 20, 10]},
            "grid-percent-indefinite-rows": {1740: [0, 0, 10, 90],
                                             1741: [0, 0, 10, 45],
                                             1742: [0, 45, 10, 45]},
        }
        for source, result in zip(fixture["cases"], output["cases"]):
            self.assertEqual(len(source["nodes"]), len(result["nodes"]))
            for expected, actual in zip(source["nodes"], result["nodes"]):
                self.assertEqual(actual["id"], expected["id"])
                self.assertEqual(actual["parent"], expected["parent"])
                self.assertEqual(actual["rect"], golden[source["name"]][expected["id"]])
                for field, length in (("rect", 4), ("border", 4), ("padding", 4),
                                      ("margin", 4), ("scrollbar", 2), ("overflow", 4)):
                    self.assertEqual(len(actual[field]), length)
                    self.assertTrue(all(math.isfinite(v) for v in actual[field]))

    def test_rejects_invalid_tree_and_style(self):
        base = json.loads(FIXTURE.read_text(encoding="utf-8"))
        variants = []
        bad = copy.deepcopy(base)
        bad["cases"][0]["nodes"][1]["id"] = 1
        variants.append(bad)
        bad = copy.deepcopy(base)
        bad["cases"][0]["nodes"][1]["parent"] = 2
        variants.append(bad)
        bad = copy.deepcopy(base)
        bad["cases"][0]["nodes"][0]["style"]["not_a_style_field"] = 1
        variants.append(bad)
        bad = copy.deepcopy(base)
        bad["cases"][0]["nodes"][0]["style"]["size"]["width"] = {"calc": 1}
        variants.append(bad)
        bad = copy.deepcopy(base)
        bad["cases"][0]["nodes"][0]["measure"] = {"minimum": [1, 1], "preferred": [2, 2]}
        variants.append(bad)
        bad = copy.deepcopy(base)
        bad["cases"][0]["available"][0] = -1
        variants.append(bad)
        bad = copy.deepcopy(base)
        bad["cases"][0]["name"] = bad["cases"][1]["name"]
        variants.append(bad)
        variants.append({"protocol": "AXISLY_TREE_V1", "cases": []})
        for index, value in enumerate(variants):
            with self.subTest(index=index):
                completed = invoke(value)
                self.assertNotEqual(completed.returncode, 0)
                self.assertTrue(completed.stderr.strip())
                self.assertFalse(completed.stdout.strip())

    def test_rejects_duplicate_json_keys(self):
        completed = subprocess.run(
            [str(TAFFY)], input='{"protocol":"AXISLY_TREE_STYLE_FIELDS_V1",'
                                '"protocol":"AXISLY_TREE_STYLE_FIELDS_V1"}',
            text=True, encoding="utf-8", capture_output=True, timeout=15,
        )
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("Duplicate JSON key", completed.stderr)
        self.assertFalse(completed.stdout.strip())


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--taffy", type=Path, required=True)
    args = parser.parse_args()
    TAFFY = args.taffy
    unittest.main(argv=[__file__], verbosity=2)
