"""Lists where two golden corpora differ, record by record.

usage: python corpus_diff.py <reference_dir> <other_dir> [examples_per_file]

Used by the Windows gate to show where the MSVC build of the C++ reference writes
different records from the glibc corpus of record (ruling R6). Prints, per file, the
count of differing lines and the first few differences, cut to a readable width.
"""

import json
import sys
from collections import defaultdict
from pathlib import Path

FILES = ["messages.jsonl", "sim_messages.jsonl", "flips.jsonl", "rng.jsonl"]
WIDTH = 400


def shown(line):
    return line if len(line) <= WIDTH else line[:WIDTH] + " ..."


def differing_keys(left, right):
    """The top-level JSON keys whose values differ, for a short summary."""
    try:
        a, b = json.loads(left), json.loads(right)
    except ValueError:
        return ["(not JSON)"]
    return sorted(key for key in set(a) | set(b) if a.get(key) != b.get(key))


def ulps(left, right):
    """Distance in units in the last place between two doubles given as 16-hex-digit bits."""
    def ordered(bits):
        value = int(bits, 16)
        return value if value < 1 << 63 else (1 << 63) - value
    return abs(ordered(left) - ordered(right))


def field_differences(left, right, prefix, tally):
    """Adds every differing leaf of two JSON values to tally: path -> [count, worst ulps]."""
    if isinstance(left, dict) and isinstance(right, dict):
        for key in set(left) | set(right):
            field_differences(left.get(key), right.get(key), f"{prefix}.{key}" if prefix else key, tally)
    elif isinstance(left, list) and isinstance(right, list) and len(left) == len(right):
        for index, (a, b) in enumerate(zip(left, right)):
            field_differences(a, b, f"{prefix}[]", tally)
    elif left != right:
        entry = tally[prefix]
        entry[0] += 1
        hexes = all(isinstance(v, str) and len(v) == 16 and all(c in "0123456789abcdef" for c in v) for v in (left, right))
        if hexes:
            entry[1] = max(entry[1], ulps(left, right))
        else:
            entry[1] = None


def main():
    reference, other = Path(sys.argv[1]), Path(sys.argv[2])
    examples = int(sys.argv[3]) if len(sys.argv) > 3 else 5
    total = 0
    for name in FILES:
        left = (reference / name).read_text().splitlines()
        right = (other / name).read_text().splitlines()
        if len(left) != len(right):
            print(f"{name}: {len(left)} lines in the reference, {len(right)} in the other")
        diffs = [i for i, (a, b) in enumerate(zip(left, right)) if a != b]
        total += len(diffs) + abs(len(left) - len(right))
        print(f"{name}: {len(diffs)} of {min(len(left), len(right))} records differ")
        tally = defaultdict(lambda: [0, 0])
        for index in diffs:
            try:
                a, b = json.loads(left[index]), json.loads(right[index])
            except ValueError:
                continue
            for key in ("msg", "decoded"):
                if key in a and key in b:
                    field_differences(a[key], b[key], key, tally)
            for key in ("wire", "error", "flips", "draws"):
                if a.get(key) != b.get(key):
                    tally[key][0] += 1
                    tally[key][1] = None
        for path, (count, worst) in sorted(tally.items(), key=lambda item: -item[1][0]):
            gap = f", at most {worst} ulps apart" if worst is not None else ""
            print(f"  {path}: differs in {count} records{gap}")
        for index in diffs[:examples]:
            print(f"  line {index + 1}: keys differing: {', '.join(differing_keys(left[index], right[index]))}")
            print(f"    glibc: {shown(left[index])}")
            print(f"    other: {shown(right[index])}")
    print(f"total differing records: {total}")


if __name__ == "__main__":
    main()
