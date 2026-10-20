#!/usr/bin/env python3

import argparse
import collections
import glob
import os
import statistics
import struct


HEADER = struct.Struct("<IHBBII" + "I" * 12 + "IHBB")
EDGE = struct.Struct("<HB")
MAGIC = 0x43583952


def percentile(values, fraction):
    if not values:
        return 0
    index = round((len(values) - 1) * fraction)
    return sorted(values)[index]


def read_capture(path):
    with open(path, "rb") as handle:
        raw_header = handle.read(HEADER.size)
        fields = HEADER.unpack(raw_header)
        if fields[0] != MAGIC:
            raise ValueError(f"{path}: invalid magic")
        edge_count = fields[-3]
        edges = [EDGE.unpack(handle.read(EDGE.size)) for _ in range(edge_count)]

    baseline = fields[6:12]
    signal = fields[12:18]
    return {
        "path": path,
        "action_index": fields[2],
        "sample_number": fields[3],
        "baseline_ms": fields[4],
        "signal_ms": fields[5],
        "baseline": baseline,
        "signal": signal,
        "total_edges": fields[-4],
        "stored_edges": edge_count,
        "overflow": bool(fields[-2]),
        "edges": edges,
    }


def action_name(path):
    name = os.path.basename(path).removesuffix(".rfraw")
    return name.rsplit("_", 1)[0]


def summarize(captures):
    grouped = collections.defaultdict(list)
    for capture in captures:
        grouped[action_name(capture["path"])].append(capture)

    for action in sorted(grouped):
        action_captures = grouped[action]
        durations = [duration for capture in action_captures for duration, _ in capture["edges"]]
        high = [duration for capture in action_captures for duration, level in capture["edges"] if level]
        low = [duration for capture in action_captures for duration, level in capture["edges"] if not level]
        rounded = collections.Counter(round(value / 25) * 25 for value in durations)
        common = ", ".join(f"{value}us:{count}" for value, count in rounded.most_common(8))
        baseline_rates = [capture["baseline"][0] * 1000 / capture["baseline_ms"] for capture in action_captures]
        signal_rates = [capture["signal"][0] * 1000 / capture["signal_ms"] for capture in action_captures]

        print(f"\n{action}: samples={len(action_captures)} edges={len(durations)}")
        print(
            "  edge_rate/s baseline={:.0f} signal={:.0f} change={:+.1f}%".format(
                statistics.mean(baseline_rates),
                statistics.mean(signal_rates),
                (statistics.mean(signal_rates) / statistics.mean(baseline_rates) - 1) * 100,
            )
        )
        print(
            "  duration_us p10={} p25={} p50={} p75={} p90={}".format(
                percentile(durations, 0.10),
                percentile(durations, 0.25),
                percentile(durations, 0.50),
                percentile(durations, 0.75),
                percentile(durations, 0.90),
            )
        )
        print(
            "  median_us high={} low={} overflow_samples={}".format(
                round(statistics.median(high)) if high else 0,
                round(statistics.median(low)) if low else 0,
                sum(capture["overflow"] for capture in action_captures),
            )
        )
        print(f"  common_25us_bins {common}")


def main():
    parser = argparse.ArgumentParser(description="Analyze CX9-B-A .rfraw captures")
    parser.add_argument("path", help="Extracted SPIFFS directory or glob")
    args = parser.parse_args()
    pattern = os.path.join(args.path, "*.rfraw") if os.path.isdir(args.path) else args.path
    paths = sorted(glob.glob(pattern))
    if not paths:
        raise SystemExit("no .rfraw files found")
    summarize([read_capture(path) for path in paths])


if __name__ == "__main__":
    main()
