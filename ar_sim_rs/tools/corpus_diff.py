"""Lists where two golden corpora differ, record by record.

usage: python corpus_diff.py <reference_dir> <other_dir> [examples_per_file]

Used by the Windows gate to show where the MSVC build of the C++ reference writes
different records from the glibc corpus of record (ruling R6). Prints, per file, the
count of differing lines and the first few differences, cut to a readable width.
"""

import json
import sys
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
        for index in diffs[:examples]:
            print(f"  line {index + 1}: keys differing: {', '.join(differing_keys(left[index], right[index]))}")
            print(f"    glibc: {shown(left[index])}")
            print(f"    other: {shown(right[index])}")
    print(f"total differing records: {total}")


if __name__ == "__main__":
    main()
