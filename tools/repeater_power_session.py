"""Analyze exported battery samples and reproduce the D-098 planning estimate."""

import argparse
import csv
from dataclasses import dataclass
from decimal import Decimal, DecimalException
import json
import math
from pathlib import Path
import re


COLUMNS = ("elapsed_s", "battery_v", "current_ma", "state")
STATE_NAME = re.compile(r"[a-z][a-z0-9_-]*")
TRANSITION = "transition"


@dataclass
class Totals:
    seconds: float = 0.0
    milliamp_seconds: float = 0.0
    milliwatt_seconds: float = 0.0

    def add(self, seconds, mean_ma, mean_mw):
        self.seconds += seconds
        self.milliamp_seconds += seconds * mean_ma
        self.milliwatt_seconds += seconds * mean_mw
        if not all(map(math.isfinite, (self.seconds, self.milliamp_seconds, self.milliwatt_seconds))):
            raise ValueError("sample integration exceeds the numeric range")

    def summary(self):
        return {
            "duration_s": self.seconds,
            "net_discharge_mah": self.milliamp_seconds / 3600,
            "net_discharge_wh": self.milliwatt_seconds / 3_600_000,
            "mean_current_ma": self.milliamp_seconds / self.seconds,
        }


def sample_values(row, line):
    if len(row) != len(COLUMNS):
        raise ValueError(f"line {line}: expected four columns")
    try:
        elapsed_s = Decimal(row[0])
        battery_v, current_ma = (float(value) for value in row[1:3])
    except (DecimalException, ValueError):
        raise ValueError(f"line {line}: elapsed time, voltage and current must be numbers") from None
    if not elapsed_s.is_finite() or not all(map(math.isfinite, (battery_v, current_ma, battery_v * current_ma))):
        raise ValueError(f"line {line}: non-finite sample or sample power")
    if elapsed_s < 0 or battery_v <= 0:
        raise ValueError(f"line {line}: elapsed time must be nonnegative and voltage positive")
    state = row[3]
    if not STATE_NAME.fullmatch(state) or state == TRANSITION:
        raise ValueError(f"line {line}: invalid or reserved state label")
    return elapsed_s, battery_v, current_ma, state


def analyze(stream, max_gap_s):
    """Integrate signed current and power, separating intervals that straddle state changes."""
    if not math.isfinite(max_gap_s) or max_gap_s <= 0:
        raise ValueError("maximum sample gap must be finite and positive")

    gap_limit = Decimal(str(max_gap_s))
    rows = csv.reader(stream, strict=True)
    total = Totals()
    by_state = {}
    previous = None
    count = 0
    negative_samples = 0
    peak_current_ma = -math.inf
    min_battery_v = math.inf
    max_sample_gap_s = 0.0
    try:
        if tuple(next(rows, ())) != COLUMNS:
            raise ValueError("expected header: " + ",".join(COLUMNS))
        for row in rows:
            current = sample_values(row, rows.line_num)
            time_s, volts, milliamps, state = current
            if previous is not None:
                prev_s, prev_v, prev_ma, prev_state = previous
                interval = time_s - prev_s
                if interval <= 0:
                    raise ValueError(f"line {rows.line_num}: elapsed time must increase strictly")
                if interval > gap_limit:
                    raise ValueError(f"line {rows.line_num}: sample gap {interval:g} s exceeds {max_gap_s:g} s")
                dt = float(interval)
                if dt == 0:
                    raise ValueError(f"line {rows.line_num}: sample interval is too small to integrate")
                max_sample_gap_s = max(max_sample_gap_s, dt)
                mean_ma = 0.5 * prev_ma + 0.5 * milliamps
                mean_mw = 0.5 * (prev_v * prev_ma) + 0.5 * (volts * milliamps)
                total.add(dt, mean_ma, mean_mw)
                bucket = state if prev_state == state else TRANSITION
                by_state.setdefault(bucket, Totals()).add(dt, mean_ma, mean_mw)
            previous = current
            count += 1
            negative_samples += milliamps < 0
            peak_current_ma = max(peak_current_ma, milliamps)
            min_battery_v = min(min_battery_v, volts)
    except DecimalException as error:
        raise ValueError(f"line {rows.line_num}: elapsed time exceeds the numeric range") from error
    except csv.Error as error:
        raise ValueError(f"line {rows.line_num}: invalid CSV ({error})") from error

    if count < 2:
        raise ValueError("at least two samples are required")
    return {
        "sample_count": count,
        "max_sample_gap_s": max_sample_gap_s,
        "min_sampled_battery_v": min_battery_v,
        "peak_sampled_discharge_current_ma": max(0.0, peak_current_ma),
        "negative_current_samples": negative_samples,
        "total": total.summary(),
        "by_state": {name: values.summary() for name, values in sorted(by_state.items())},
    }


def battery_model():
    """Reproduce the T-0055 72-hour planning scenarios, including GNSS sensitivity."""
    hours = 72
    model_voltage_v = 3.7
    receive_ma = 5.3
    transmit_ma = 118.0
    cpu_ma = 6.5
    board_ma = 5.0
    gnss_on_ma = 41.0
    seconds_per_frame = 0.4
    planning_margin = 1.2
    cold_fraction = 0.70
    reserve_fraction = 0.20
    nominal_pack_mah = 13_400
    usable_fraction = cold_fraction * (1 - reserve_fraction)

    cases = []
    for gnss_ma in (gnss_on_ma, 0.0):
        for forwards_per_hour in (30, 120):
            tx_s_per_hour = forwards_per_hour * seconds_per_frame
            tx_fraction = tx_s_per_hour / 3600
            mean_ma = receive_ma + (transmit_ma - receive_ma) * tx_fraction + cpu_ma + board_ma + gnss_ma
            drawn_mah = mean_ma * hours
            drawn_wh = drawn_mah * model_voltage_v / 1000
            needed_nominal_mah = drawn_mah * planning_margin / usable_fraction
            cases.append({
                "gnss_ma": gnss_ma,
                "forwards_per_hour": forwards_per_hour,
                "tx_seconds_per_hour": tx_s_per_hour,
                "mean_current_ma": mean_ma,
                "drawn_mah_72h": drawn_mah,
                "drawn_wh_72h": drawn_wh,
                "usable_wh_required_with_margin": drawn_wh * planning_margin,
                "nominal_mah_required": needed_nominal_mah,
                "nominal_mah_rounded_up_100": math.ceil(needed_nominal_mah / 100) * 100,
            })
    return {
        "kind": "planning_estimate",
        "assumptions": {
            "hours": hours, "battery_voltage_v": model_voltage_v, "radio_receive_ma": receive_ma,
            "radio_transmit_ma": transmit_ma, "radio_transmit_scenario_dbm": 22,
            "mcu_ma": cpu_ma, "board_overhead_ma": board_ma, "gnss_active_ma": gnss_on_ma,
            "seconds_per_forward": seconds_per_frame, "load_margin_factor": planning_margin,
            "cold_capacity_fraction": cold_fraction, "reserve_fraction": reserve_fraction,
            "assumed_pack_nominal_mah": nominal_pack_mah,
        },
        "assumed_pack_screening_mean_ma": nominal_pack_mah * usable_fraction / (hours * planning_margin),
        "cases": cases,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    analyze_command = commands.add_parser("analyze", help="integrate a saved, normalized logger CSV")
    analyze_command.add_argument("csv", type=Path)
    analyze_command.add_argument("--max-gap-s", type=float, required=True)
    commands.add_parser("model", help="print the D-098 30 and 120 forwards/hour planning cases")
    args = parser.parse_args()
    try:
        if args.command == "analyze":
            if args.csv.suffix.lower() != ".csv" or not args.csv.is_file():
                raise ValueError("provide an existing logger export in .csv format")
            with args.csv.open(encoding="utf-8-sig", newline="") as stream:
                result = analyze(stream, args.max_gap_s)
            result.update(kind="logger_export", input_csv=str(args.csv), allowed_gap_s=args.max_gap_s)
        else:
            result = battery_model()
        print(json.dumps(result, indent=2, allow_nan=False))
    except (ValueError, OSError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
