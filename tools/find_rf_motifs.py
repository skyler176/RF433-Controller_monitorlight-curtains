#!/usr/bin/env python3

import argparse
import collections
import glob
import os

from analyze_rfraw import action_name, read_capture


def quantize(capture, quantum):
    return tuple((round(duration / quantum) * quantum, level) for duration, level in capture["edges"])


def motifs(sequence, length):
    return {sequence[index:index + length] for index in range(len(sequence) - length + 1)}


def main():
    parser = argparse.ArgumentParser(description="Find action-specific repeated RF motifs")
    parser.add_argument("path")
    parser.add_argument("--quantum", type=int, default=50)
    parser.add_argument("--length", type=int, default=12)
    parser.add_argument("--minimum-support", type=float, default=0.8)
    args = parser.parse_args()

    paths = sorted(glob.glob(os.path.join(args.path, "*.rfraw")))
    grouped = collections.defaultdict(list)
    for path in paths:
        grouped[action_name(path)].append(read_capture(path))

    support = {}
    for action, captures in grouped.items():
        counts = collections.Counter()
        for capture in captures:
            counts.update(motifs(quantize(capture, args.quantum), args.length))
        support[action] = counts

    for action in sorted(grouped):
        required = max(2, round(len(grouped[action]) * args.minimum_support))
        candidates = []
        for motif, own_support in support[action].items():
            if own_support < required:
                continue
            other_support = max(
                (counts.get(motif, 0) for other, counts in support.items() if other != action),
                default=0,
            )
            candidates.append((own_support - other_support, own_support, other_support, motif))
        candidates.sort(reverse=True)
        print(f"\n{action}: samples={len(grouped[action])} required={required} candidates={len(candidates)}")
        for score, own, other, motif in candidates[:5]:
            rendered = " ".join(f"{level}:{duration}" for duration, level in motif)
            print(f"  score={score} own={own} other_max={other}  {rendered}")


if __name__ == "__main__":
    main()
