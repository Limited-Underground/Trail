"""Host checks for the Repeater battery logger export and planning worksheet."""

import io
from pathlib import Path
import sys
import unittest


HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / "tools"))
from repeater_power_session import analyze, battery_model


HEADER = "elapsed_s,battery_v,current_ma,state\n"


class LoggerTests(unittest.TestCase):
    def analyze_rows(self, rows, max_gap=10):
        return analyze(io.StringIO(HEADER + rows), max_gap)

    def test_integrates_signed_current_and_instantaneous_power(self):
        result = self.analyze_rows("0,4,100,rx\n2,3,300,rx\n5,2,-100,rx\n")
        total = result["total"]
        self.assertEqual(total["duration_s"], 5)
        self.assertAlmostEqual(total["net_discharge_mah"], 700 / 3600)
        self.assertAlmostEqual(total["net_discharge_wh"], 2350 / 3_600_000)
        self.assertAlmostEqual(total["mean_current_ma"], 140)
        self.assertEqual(result["negative_current_samples"], 1)
        self.assertEqual(result["peak_sampled_discharge_current_ma"], 300)
        self.assertEqual(result["min_sampled_battery_v"], 2)

    def test_sample_trace_keeps_transition_out_of_stable_states(self):
        fixture = HERE / "fixtures/repeater_power_sample.csv"
        with fixture.open(encoding="utf-8", newline="") as stream:
            result = analyze(stream, 1)
        states = result["by_state"]
        self.assertEqual(result["sample_count"], 7)
        self.assertEqual(result["max_sample_gap_s"], 1)
        self.assertEqual(states["transition"]["duration_s"], 2)
        self.assertAlmostEqual(states["rx"]["mean_current_ma"], 20)
        self.assertAlmostEqual(states["rx_gnss_track"]["mean_current_ma"], 60)
        for field in ("net_discharge_mah", "net_discharge_wh", "duration_s"):
            self.assertAlmostEqual(sum(values[field] for values in states.values()), result["total"][field])

    def test_gap_fails_without_interpolation(self):
        with self.assertRaisesRegex(ValueError, "gap 3 s exceeds 2 s"):
            self.analyze_rows("0,4,10,rx\n3,4,10,rx\n", max_gap=2)

    def test_exact_decimal_gap_tolerates_float_representation(self):
        result = self.analyze_rows("259199.8,4,10,rx\n259199.9,4,10,rx\n259200.0,4,10,rx\n", max_gap=0.1)
        self.assertAlmostEqual(result["total"]["duration_s"], 0.2)

    def test_large_timestamp_cannot_hide_a_gap(self):
        with self.assertRaisesRegex(ValueError, "gap 2 s exceeds 0.1 s"):
            self.analyze_rows("10000000000000000,4,10,rx\n10000000000000002,4,10,rx\n", max_gap=0.1)

    def test_duplicate_or_reversed_timestamps_fail(self):
        for second in ("1", "0"):
            with self.subTest(second=second):
                with self.assertRaisesRegex(ValueError, "increase strictly"):
                    self.analyze_rows(f"1,4,10,rx\n{second},4,20,rx\n")

    def test_invalid_measurements_and_state_fail(self):
        for bad in ("1,4,NaN,rx", "1,inf,10,rx", "1,0,10,rx", "1,4,10,",
                    "1,4,10,transition", "1,4,10", "1,4,10,rx,extra", '1,4,10,"rx'):
            with self.subTest(bad=bad):
                with self.assertRaises(ValueError):
                    self.analyze_rows(f"0,4,10,rx\n{bad}\n")

    def test_incomplete_trace_and_invalid_gap_fail(self):
        with self.assertRaisesRegex(ValueError, "at least two"):
            self.analyze_rows("0,4,10,rx\n")
        for gap in (0, -1, float("inf"), float("nan")):
            with self.subTest(gap=gap):
                with self.assertRaisesRegex(ValueError, "finite and positive"):
                    self.analyze_rows("0,4,10,rx\n1,4,10,rx\n", max_gap=gap)

    def test_unexpected_schema_fails(self):
        with self.assertRaisesRegex(ValueError, "expected header"):
            analyze(io.StringIO("elapsed_s,current_ma,battery_v,state\n"), 1)


class PlanningModelTests(unittest.TestCase):
    def test_archived_workloads_are_reproduced(self):
        result = battery_model()
        rows = {(row["gnss_ma"], row["forwards_per_hour"]): row for row in result["cases"]}
        self.assertEqual(set(rows), {(41.0, 30), (41.0, 120), (0.0, 30), (0.0, 120)})
        self.assertAlmostEqual(rows[41.0, 30]["drawn_wh_72h"], 15.4979976)
        self.assertAlmostEqual(rows[41.0, 120]["drawn_wh_72h"], 15.7982304)
        self.assertEqual([row["nominal_mah_rounded_up_100"] for row in rows.values()], [9000, 9200, 2700, 2900])
        self.assertAlmostEqual(result["assumed_pack_screening_mean_ma"], 86.85185185)


if __name__ == "__main__":
    unittest.main()
