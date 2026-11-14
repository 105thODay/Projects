#!/usr/bin/env python3
"""Independent re-scoring of a run from its trace ledgers.

Usage: python tools/verify_trace.py <prefix>
Reads <prefix>_input_obligations.csv, _transmissions.csv and _responses.csv and
recomputes, without any simulator code:
  1. every sender's rolling one-second charged-bit sum at each transmit start
     (NET-002), in exact ticks, and the budget balance the simulator logged;
  2. every obligation's outcome and latency from the timestamps alone (NET-001);
  3. the D stage of each successful first-hop copy (NET-009).
Exits non-zero when anything disagrees with what the simulator wrote.
"""
import csv
import sys
from collections import defaultdict, deque

prefix = sys.argv[1]
LIMIT, WINDOW_TICKS = 200000, 9_000_000
DEADLINE_US, DISPLAY_US, STAGE_D_US = 100000.0, 30000.0, 20000.0
problems = []

# 1. Rolling ledger
per_sender = defaultdict(list)
tx_by_id = {}
for row in csv.DictReader(open(prefix + "_transmissions.csv")):
    per_sender[row["sender_id"]].append((int(row["t_tx_start_ticks"]), int(row["charged_bits"]), int(row["budget_balance_bits"]), row["tx_id"]))
    tx_by_id[row["tx_id"]] = row
worst = 0
emissions = 0
for sender, rows in per_sender.items():
    rows.sort()
    window, total = deque(), 0
    for ticks, bits, balance, tx_id in rows:
        emissions += 1
        while window and window[0][0] <= ticks - WINDOW_TICKS:      # (t - 1 s, t]: a charge exactly 1 s old has left
            total -= window.popleft()[1]
        window.append((ticks, bits))
        total += bits
        worst = max(worst, total)
        if total > LIMIT:
            problems.append(f"sender {sender} tx {tx_id}: rolling sum {total} over the ceiling")
        if LIMIT - total != balance:
            problems.append(f"sender {sender} tx {tx_id}: balance {balance} logged, {LIMIT - total} recomputed")

# 2. Obligations and outcomes
inputs = {}
for row in csv.DictReader(open(prefix + "_input_obligations.csv")):
    inputs[(row["input_id"], row["recipient_id"])] = float(row["t_input_us"])
counts, recount = defaultdict(int), defaultdict(int)
late_d = 0
seen = set()
for row in csv.DictReader(open(prefix + "_responses.csv")):
    key = (row["input_id"], row["recipient_id"])
    if key in seen:
        problems.append(f"obligation {key} classified twice")
    seen.add(key)
    counts[row["outcome"]] += 1
    if row["outcome"] == "rejected":
        mine = "rejected"
    elif row["t_publish_us"] == "null":
        mine = "missing"
    else:
        uncertainty = float(row["uncertainty_us"])
        if row["t_photon_us"] != "null":
            latency = float(row["t_photon_us"]) - inputs[key] + uncertainty
        else:
            latency = float(row["t_publish_us"]) - inputs[key] + DISPLAY_US + uncertainty
        if row["effect_correct"] != "1":
            mine = "incorrect"
        elif latency <= DEADLINE_US + 1e-6 and row["apply_count"] == "1":
            mine = "on_time_correct"
        else:
            mine = "late_correct"
        tx = tx_by_id.get(row["successful_tx_id"])
        if tx is not None and tx["traffic_class"] == "urgent" and tx["hop_index"] == "0":
            if float(row["t_publish_us"]) - float(tx["t_attempt_start_us"]) > STAGE_D_US + 1e-6:
                late_d += 1
    recount[mine] += 1
    if mine != row["outcome"]:
        problems.append(f"obligation {key}: simulator says {row['outcome']}, timestamps say {mine}")
if seen != set(inputs):
    problems.append(f"{len(set(inputs) - seen)} obligations have no response row; {len(seen - set(inputs))} responses have no obligation")

print(f"{prefix}: {emissions} emissions, worst rolling sum {worst} bits (ceiling {LIMIT}); "
      f"{len(inputs)} obligations: " + ", ".join(f"{k} {v}" for k, v in sorted(recount.items())) + f"; first-hop D over 20 ms: {late_d}")
for problem in problems[:10]:
    print("  DISAGREE:", problem)
print("trace check:", "AGREES with the simulator" if not problems else f"{len(problems)} disagreement(s)")
sys.exit(1 if problems else 0)
