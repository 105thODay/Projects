#!/usr/bin/env python3
"""Summarize ar_matrix.txt into ar_summary.md and ar_summary.csv.

Usage: python tools/summarize_matrix.py [ar_matrix.txt [ar_summary]]   (second argument: output name, no extension)
One row per run group. Counts are summed over the group's seeds; maxima are the
worst seed; percentiles are the mean of the per-seed values.
"""
import csv
import re
import statistics
import sys

path = sys.argv[1] if len(sys.argv) > 1 else "ar_matrix.txt"
stem = sys.argv[2] if len(sys.argv) > 2 else "ar_summary"
SEED = re.compile(r"obligations (\d+)\s+onTime (\d+)\s+late (\d+)\s+missing (\d+)\s+rejected (\d+)\s+incorrect (\d+)\s+dupEffects (\d+).*"
                  r"L\(P50/P95/P99/max\) (-?[\d.]+)/(-?[\d.]+)/(-?[\d.]+)/(-?[\d.]+) ms.*publication\(max\) (-?[\d.]+) ms")
STAGE = re.compile(r"W\(max by copy\) ([\d. ]+) ms\s+copyGap\(max\) ([\d.]+) ms\s+D\(max\) ([\d.]+) ms.*violations W (\d+) gap (\d+) D (\d+) publication (\d+)\s+urgentDeferred (\d+)")
LOAD = re.compile(r"maxRolling (\d+) bits.*meanCharged\(node mean/max\) ([\d.]+)/([\d.]+) kbps\s+byClass\(control/urgent/state/repair\) ([\d.]+)/([\d.]+)/([\d.]+)/([\d.]+).*airtime ([\d.]+)%\s+stateSuperseded (\d+)")
NET = re.compile(r"lost (\d+) \(([\d.]+)%\).*stateAge\(P50/P95/max\) (-?[\d.]+)/(-?[\d.]+)/(-?[\d.]+) ms")
INTEG = re.compile(r"dedupSuppressed (\d+)\s+revisionRejected (\d+)\s+staleStateRejected (\d+)\s+replayRejected (\d+)\s+unauthorizedRejected (\d+)\s+"
                   r"resurrections (\d+)\s+unauthorizedApplied (\d+)\s+repair\(requests/frames\) (\d+)/(\d+)\s+recovery\(max\) (\S+) ms\s+unrecovered (\d+)")
VERDICT = re.compile(r"service (\w+)\s+profile (\w+)\s+conditional-network (\w+)")
PHASE = re.compile(r"runs (\d+)\s+service PASS (\d+)\s+profile PASS (\d+)\s+obligations (\d+)\s+onTime (\d+)\s+worst L ([\d.]+) ms.*worst publication ([\d.]+) ms\s+worst W ([\d.]+) ms")

groups, order, key, gates = {}, [], None, []
for line in open(path):
    line = line.rstrip("\n")
    if line.startswith("## "):
        key = tuple(part.strip() for part in line[3:].split("|"))
        groups[key] = []
        order.append(key)
    elif line.startswith("seed "):
        m = SEED.search(line)
        groups[key].append(dict(ob=int(m[1]), on=int(m[2]), late=int(m[3]), miss=int(m[4]), rej=int(m[5]), inc=int(m[6]), dup=int(m[7]),
                                p50=float(m[8]), p95=float(m[9]), p99=float(m[10]), mx=float(m[11]), pub=float(m[12])))
    elif line.startswith("stage:"):
        m = STAGE.search(line)
        groups[key][-1].update(w=max(float(x) for x in m[1].split()), d=float(m[3]), vw=int(m[4]), vgap=int(m[5]), vd=int(m[6]), deferred=int(m[8]))
    elif line.startswith("load:"):
        m = LOAD.search(line)
        groups[key][-1].update(rolling=int(m[1]), charged=float(m[2]), state=float(m[6]), airtime=float(m[8]), superseded=int(m[9]))
    elif line.startswith("net:"):
        m = NET.search(line)
        groups[key][-1].update(losspct=float(m[2]), age50=float(m[3]), age95=float(m[4]))
    elif line.startswith("integrity:"):
        m = INTEG.search(line)
        groups[key][-1].update(dedup=int(m[1]), stale=int(m[3]), replay=int(m[4]), unauth=int(m[5]), resurrect=int(m[6]), unauthapplied=int(m[7]),
                               repairs=int(m[9]), recovery=(None if m[10] == "null" else float(m[10])), unrecovered=int(m[11]))
    elif line.startswith("verdict: service"):
        m = VERDICT.search(line)
        groups[key][-1].update(service=m[1] == "PASS", profile=m[2] == "PASS", conditional=m[3] == "PASS")
    elif line.startswith("phase:"):
        m = PHASE.search(line)
        groups[key] = dict(phase=True, runs=int(m[1]), service=int(m[2]), profile=int(m[3]), ob=int(m[4]), on=int(m[5]),
                           worst=float(m[6]), pub=float(m[7]), w=float(m[8]))
    elif line.startswith("analytic checks:") or line.startswith("verdict: F0"):
        gates.append(line)

rows = []
for key in order:
    runs = groups[key]
    if not runs or isinstance(runs, dict):
        continue
    n = len(runs)
    ob, on = sum(r["ob"] for r in runs), sum(r["on"] for r in runs)
    recoveries = [r["recovery"] for r in runs if r["recovery"] is not None]
    rows.append(dict(group=key[0], label=key[1], command=key[2], seeds=n, obligations=ob, on_time=on,
                     late=sum(r["late"] for r in runs), missing=sum(r["miss"] for r in runs), rejected=sum(r["rej"] for r in runs),
                     duplicate_effects=sum(r["dup"] for r in runs), miss_fraction=round(1 - on / ob, 6) if ob else 0,
                     L_p50=round(statistics.mean(r["p50"] for r in runs), 1), L_p95=round(statistics.mean(r["p95"] for r in runs), 1),
                     L_p99=round(statistics.mean(r["p99"] for r in runs), 1), L_max=round(max(r["mx"] for r in runs), 1),
                     W_max=round(max(r["w"] for r in runs), 1), D_max=round(max(r["d"] for r in runs), 1),
                     stage_violations=sum(r["vw"] + r["vgap"] + r["vd"] for r in runs),
                     max_rolling_bits=max(r["rolling"] for r in runs), charged_kbps=round(statistics.mean(r["charged"] for r in runs), 1),
                     state_kbps=round(statistics.mean(r["state"] for r in runs), 1), airtime_pct=round(max(r["airtime"] for r in runs), 1),
                     loss_pct=round(statistics.mean(r["losspct"] for r in runs), 2), state_age_p95=round(statistics.mean(r["age95"] for r in runs), 1),
                     repairs=sum(r["repairs"] for r in runs), recovery_max_ms=round(max(recoveries), 1) if recoveries else "",
                     service_pass=f"{sum(r['service'] for r in runs)}/{n}", profile_pass=f"{sum(r['profile'] for r in runs)}/{n}",
                     conditional_pass=f"{sum(r['conditional'] for r in runs)}/{n}"))

with open(stem + ".csv", "w", newline="") as f:
    writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
    writer.writeheader()
    writer.writerows(rows)

out = ["# Networked AR profile: simulation summary", "",
       "Transport-queue mode, conditional networking results. `L` is input to modeled correct effect in ms (publication plus the",
       "10 ms input, 30 ms display and 1 ms uncertainty allowances). Counts are summed over seeds; maxima are the worst seed.", ""]
for line in gates:
    out.append("- " + line)
out.append("")
phases = []
current = None
for key in order:
    runs = groups[key]
    if isinstance(runs, dict):
        phases.append(f"- **{key[1]}**: {runs['runs']} phase points, service pass {runs['service']}, profile pass {runs['profile']}, "
                    f"{runs['on']} of {runs['ob']} on time, worst L {runs['worst']:.3f} ms, worst publication {runs['pub']:.3f} ms, worst W {runs['w']:.3f} ms")
        continue
    row = next((r for r in rows if (r["group"], r["label"], r["command"]) == key), None)
    if row is None:
        continue
    if key[0] != current:
        current = key[0]
        out += ["", f"## {current}", "",
                "| case | seeds | obligations | on time | late | missing | L P50 / P95 / max | W max | D max | max rolling bits | charged kbps | state kbps | state age P95 | service | profile |",
                "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    out.append(f"| {row['label']} | {row['seeds']} | {row['obligations']} | {row['on_time']} | {row['late']} | {row['missing'] + row['rejected']} | "
               f"{row['L_p50']} / {row['L_p95']} / {row['L_max']} | {row['W_max']} | {row['D_max']} | {row['max_rolling_bits']} | {row['charged_kbps']} | "
               f"{row['state_kbps']} | {row['state_age_p95']} | {row['service_pass']} | {row['profile_pass']} |")
out += ["", "## phase sweeps (12 input phases x 12 display phases x 3 boundary offsets, modeled photon)", ""] + phases
open(stem + ".md", "w").write("\n".join(out) + "\n")
print("\n".join(out))
