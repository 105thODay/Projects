#!/usr/bin/env python3
"""Put the common-delivery points that carry human-rate input through the rest of the tests.

Usage: python tools/common_point_tests.py [cot_sizes.csv]   (writes common_points.csv and common_points.md)
Every run: 16 participants, common delivery, measured CoT sizes, standing urgent reserve for 7 inputs per rolling
second (an index finger at its maximum rate), 8 kbit repair share, state 10 Hz, control fixture 1 Hz, repair on
request, inputs as a renewal process with a 154 ms dead time unless the row says otherwise. Three seeds, 60 s.
"""
import csv
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

SIZES = sys.argv[1] if len(sys.argv) > 1 else "cot_sizes.csv"
exe = "./ar_sim" if sys.platform != "win32" else "ar_sim.exe"
table = {(r["style"], r["encoding"], r["kind"]): int(r["charged_bits"]) for r in csv.DictReader(open(SIZES))}
POINTS = (("CoT XML, lean schema", "lean", "xml"), ("TAK v1, lean schema", "lean", "tak_v1"),
          ("XML + session dictionary, as specified", "as_specified", "xml_dict"))
HUMAN = "inputMinGapUs=154000"
TESTS = (("two erasures, human renewal input", f"Q2 3 {HUMAN}"),
         ("phase sweep, two erasures, periodic input", "phase Q2"),
         ("all 16 fire 7-pull strings in lockstep, 154 ms splits, two erasures", "Q2 3 syncPhases=1 inputTrain=7 inputTrainGapUs=154000"),
         ("all 16 fire 10-pull strings in lockstep, 100 ms splits, two erasures", "Q2 3 syncPhases=1 inputTrain=10 inputTrainGapUs=100000"),
         ("jitter 10 ms, two erasures", f"J0 3 {HUMAN} jitterMaxUs=10000 eraseCopies=2"),
         ("independent loss 1 %", f"R0 3 {HUMAN} lossProb=0.01"),
         ("independent loss 3 %", f"R0 3 {HUMAN} lossProb=0.03"),
         ("two erasures, 1 % loss, 5 ms jitter", f"Q2 3 {HUMAN} lossProb=0.01 jitterMaxUs=5000"),
         ("blockage, 1 copy period", f"C0 3 {HUMAN} blockPeriods=1"),
         ("blockage, 2 copy periods", f"C0 3 {HUMAN} blockPeriods=2"),
         ("blockage, 3 copy periods", f"C0 3 {HUMAN} blockPeriods=3"),
         ("blackout 50 ms every 10 s", f"S0 3 {HUMAN} lossProb=0 blackoutUs=50000"),
         ("duplicates and reordering", f"P0 3 {HUMAN}"),
         ("partition 1 s, replay, takeover", f"R1 3 {HUMAN}"),
         ("PHY 5 Mbit/s, two erasures", f"Q2 3 {HUMAN} phyBps=5000000"),
         ("PHY 2 Mbit/s, two erasures", f"Q2 3 {HUMAN} phyBps=2000000"))

SEED = re.compile(r"obligations (\d+)\s+onTime (\d+).*L\(P50/P95/P99/max\) (-?[\d.]+)/(-?[\d.]+)/(-?[\d.]+)/(-?[\d.]+) ms")
LOAD = re.compile(r"meanCharged\(node mean/max\) ([\d.]+)/([\d.]+) kbps.*airtime ([\d.]+)%")
NET = re.compile(r"stateAge\(P50/P95/max\) (-?[\d.]+)/(-?[\d.]+)/(-?[\d.]+) ms")
VERDICT = re.compile(r"service (\w+)\s+profile (\w+)")
PHASE = re.compile(r"phase: runs (\d+)\s+service PASS (\d+)\s+profile PASS (\d+)\s+obligations (\d+)\s+onTime (\d+)\s+worst L ([\d.]+) ms")


def job(point, test):
    name, style, enc = point
    u, s, c, r = (table[(style, enc, k)] for k in ("urgent", "state", "control", "repair_request"))
    words = test[1].split()
    head = words[:2]                               # "Q2 3" or "phase Q2"
    sizes = ["nodes=16", "fanout=common", "sharedEncode=1", "lendUrgentShare=0", "repairShareBits=8000",
             f"urgentShareBits={7 * 3 * u}", f"urgentBits={u}", f"stateBits={s}", f"controlBits={c}", f"repairRequestBits={r}",
             "controlHz=1", "repairHz=0", "stateHz=10", "durationS=60"]
    if words[0] == "phase":
        sizes.remove("durationS=60")
    return dict(point=name, test=test[0], args=head + sizes + words[len(head):])


def run(j):
    text = subprocess.run([exe] + j["args"], capture_output=True, text=True).stdout
    p = PHASE.search(text)
    if p:
        j.update(result=f"{p[2]} of {p[1]} phase points pass; worst L {float(p[6]):.0f} ms" if p[2] != p[1]
                 else f"{p[1]} of {p[1]}; worst L {float(p[6]):.0f} ms")
        return j
    seeds = list(SEED.finditer(text))
    ob = sum(int(m[1]) for m in seeds)
    late = ob - sum(int(m[2]) for m in seeds)
    age = max(float(m[2]) for m in NET.finditer(text))
    loads = list(LOAD.finditer(text))
    verdict = [m[1] for m in VERDICT.finditer(text)]
    share = "on time" if late == 0 else (f"{late} of {ob} late" if 100 * late / ob < 0.1 else f"{100 * late / ob:.1f} % late")
    j.update(result=f"{share}; worst L {max(float(m[6]) for m in seeds):.0f} ms; state {age:.0f} ms; "
                    f"{sum(float(m[1]) for m in loads) / len(loads):.0f} kbps; airtime {max(float(m[3]) for m in loads):.0f} %",
             service=f"{verdict.count('PASS')}/{len(verdict)}")
    return j


jobs = [job(p, t) for t in TESTS for p in POINTS]
with ThreadPoolExecutor(max_workers=4) as pool:
    results = list(pool.map(run, jobs))
with open("common_points.csv", "w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=["point", "test", "result", "service"], extrasaction="ignore")
    w.writeheader()
    w.writerows(results)
out = ["# 16 participants, common delivery, measured CoT sizes, human-limited input", "",
       "| test | " + " | ".join(f"{p[0]} ({table[(p[1], p[2], 'urgent')] // 8} B)" for p in POINTS) + " |",
       "|---|" + "---|" * len(POINTS)]
for t in TESTS:
    out.append(f"| {t[0]} | " + " | ".join(next(r["result"] for r in results if r["test"] == t[0] and r["point"] == p[0]) for p in POINTS) + " |")
open("common_points.md", "w").write("\n".join(out) + "\n")
print("\n".join(out))
