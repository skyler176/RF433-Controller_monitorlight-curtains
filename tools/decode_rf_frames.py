#!/usr/bin/env python3

import argparse
import collections
import glob
import os

from analyze_rfraw import action_name, read_capture


def classify_bit(high_us, low_us):
    high_short = 100 <= high_us <= 350
    high_long = 350 < high_us <= 750
    low_short = 100 <= low_us <= 350
    low_long = 350 < low_us <= 900
    if high_short and low_long:
        return "0"
    if high_long and low_short:
        return "1"
    return None


def split_frames(edges):
    frames = []
    current = []
    for duration, level in edges:
        if level == 0 and duration >= 1800:
            if current:
                frames.append(current)
            current = []
        else:
            current.append((duration, level))
    if current:
        frames.append(current)
    return frames


def decode_frame(edges):
    bits = []
    index = 0
    while index + 1 < len(edges):
        first = edges[index]
        second = edges[index + 1]
        if first[1] == 1 and second[1] == 0:
            bit = classify_bit(first[0], second[0])
            if bit is None:
                return None
            bits.append(bit)
            index += 2
        else:
            index += 1
    return "".join(bits) if len(bits) >= 8 else None


def main():
    parser = argparse.ArgumentParser(description="Decode CX9-B-A PWM-like RF frames")
    parser.add_argument("path")
    args = parser.parse_args()

    grouped = collections.defaultdict(list)
    for path in sorted(glob.glob(os.path.join(args.path, "*.rfraw"))):
        grouped[action_name(path)].append(read_capture(path))

    action_counts = {}
    for action, captures in grouped.items():
        counts = collections.Counter()
        sample_support = collections.Counter()
        total_segments = 0
        for capture in captures:
            seen = set()
            for segment in split_frames(capture["edges"]):
                total_segments += 1
                decoded = decode_frame(segment)
                if decoded:
                    counts[decoded] += 1
                    seen.add(decoded)
            sample_support.update(seen)
        action_counts[action] = counts
        print(f"\n{action}: samples={len(captures)} segments={total_segments} decoded={sum(counts.values())}")
        for bits, occurrences in counts.most_common(10):
            print(
                f"  samples={sample_support[bits]:2d} occurrences={occurrences:3d} "
                f"bits={len(bits):2d} data={bits}"
            )

    print("\nAction-specific leaders:")
    for action, counts in sorted(action_counts.items()):
        if not counts:
            print(f"  {action}: none")
            continue
        ranked = []
        for bits, own_count in counts.items():
            other_max = max(
                (other_counts.get(bits, 0) for other, other_counts in action_counts.items() if other != action),
                default=0,
            )
            ranked.append((own_count - other_max, own_count, other_max, bits))
        score, own, other, bits = max(ranked)
        print(f"  {action}: score={score} own={own} other_max={other} data={bits}")


if __name__ == "__main__":
    main()
