"""Statistical/report contracts; no timing-sensitive performance assertions."""
import csv
from pathlib import Path
import tempfile
import unittest

from css_compare import bootstrap_intervals, cases, summary


def rows(engine, values):
    return [dict(case="fixture", mode="cold", engine=engine, round=i + 1,
                 nodes=2, iterations=17, samples_us=[v] * 9, measurements=[1] * 9)
            for i, v in enumerate(values)]


class StatisticsTests(unittest.TestCase):
    def test_default_covers_initialization(self):
        self.assertTrue(all("build" in case["modes"] for case in cases()))

    def test_constant_and_paired_rounds(self):
        self.assertEqual(bootstrap_intervals(rows("a", [2, 2, 2])), (2, 2))
        # Common machine drift cancels only when round IDs are paired.
        self.assertEqual(bootstrap_intervals(rows("a", [2, 20, 200]),
                                             rows("b", [1, 10, 100])[::-1]), (2, 2))

    def test_insufficient_samples(self):
        self.assertIsNone(bootstrap_intervals(rows("a", [1, 2])))
        self.assertIsNone(bootstrap_intervals(rows("a", [1, 2, 3]), rows("b", [1, 2])))
        self.assertIsNone(bootstrap_intervals(rows("a", [1, 1, 1]), rows("b", [0, 0, 0])))

    def test_report_and_smoke(self):
        raw = rows("taffy", [2, 2, 2]) + rows("fast", [1, 1, 1])
        raw += rows("slow", [4, 4, 4]) + rows("equal", [2, 2, 2])
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "summary.csv"
            summary(raw, path)
            with path.open(encoding="utf-8") as stream:
                report = {r["engine"]: r for r in csv.DictReader(stream)}
            self.assertEqual(report["fast"]["comparison"], "faster")
            self.assertEqual(report["slow"]["comparison"], "slower")
            self.assertEqual(report["equal"]["comparison"], "inconclusive")
            summary(raw, path, smoke=True)
            with path.open(encoding="utf-8") as stream:
                for row in csv.DictReader(stream):
                    self.assertEqual(row["comparison"], "smoke_only")
                    self.assertEqual(row["median_ci95_low_us"], "")
                    self.assertEqual(row["ratio_to_taffy"], "")


if __name__ == "__main__":
    unittest.main()
