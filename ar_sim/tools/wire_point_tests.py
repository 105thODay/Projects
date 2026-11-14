#!/usr/bin/env python3
"""Rerun the Rev 5 common-delivery battery with encoded-wire CoT and serverless alignment (Rev 6, ideal channel).

Usage: python tools/wire_point_tests.py [forms=<wire,...>] [channel args ...]   (writes wire_points.csv and wire_points.md)
  forms=tak_lean runs only that form (Rev 7: TAK v1 on the ideal medium) and adds it to the output name.
Every run is the Rev 5 operating point: 16 participants, common delivery, standing urgent reserve for 7 inputs per
rolling second, 8 kbit repair share, state 10 Hz, control fixture 1 Hz, repair on request, human renewal input with a
154 ms dead time unless the row says otherwise; three seeds, 60 s. What changes from Rev 5:
  - every frame is a real CoT message, encoded by the C++ codec, charged at its own size and decoded by its receivers;
  - align=1: random-waypoint motion, the positioning function, alignment, calibration epochs and their frames.
The urgent reserve is sized from a probe run's largest encoded event, so it covers the real messages.
Extra arguments (for example channel=wifi_ap) are appended to every run.
"""
import csv
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

exe = "./ar_sim" if sys.platform != "win32" else "ar_sim.exe"
FORMS = next((a.split("=", 1)[1].split(",") for a in sys.argv[1:] if a.startswith("forms=")), None)
EXTRA = [a for a in sys.argv[1:] if not a.startswith("forms=")]
STEM = "wire_points" + ("_" + "_".join(FORMS) if FORMS else "") + ("_" + "_".join(a.replace("=", "-") for a in EXTRA) if EXTRA else "")
POINTS = (("CoT XML, lean schema", "xml_lean"), ("TAK v1, lean schema", "tak_lean"))
# The as-specified schemas are checked for admission only: their reserve alone is compared with the ledger.
SPECIFIED = (("CoT XML, as specified", "xml_specified"), ("TAK v1, as specified", "tak_specified"))
if FORMS:
    POINTS = tuple(p for p in POINTS if p[1] in FORMS)
    SPECIFIED = tuple(p for p in SPECIFIED if p[1].split("_")[0] == FORMS[0].split("_")[0])
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
LOAD = re.compile(r"meanCharged\(node mean/max\) ([\d.]+)/([\d.]+) kbps\s+byClass\(control/urgent/state/repair\) ([\d.]+)/([\d.]+)/([\d.]+)/([\d.]+).*airtime ([\d.]+)%")
NET = re.compile(r"stateAge\(P50/P95/max\) (-?[\d.]+)/(-?[\d.]+)/(-?[\d.]+) ms")
VERDICT = re.compile(r"service (\w+)\s+profile (\w+)")
WIRE = re.compile(r"event (\d+)/(\d+)/(\d+) .*decodeFail (\d+)\s+oversize (\d+) \(urgent (\d+)\)")
AVATAR = re.compile(r"avatar near ([\d.]+)/([\d.]+)/([\d.]+) far ([\d.]+)/([\d.]+)/([\d.]+) m\s+marker near ([\d.]+)/([\d.]+)/([\d.]+) far ([\d.]+)/([\d.]+)/([\d.]+)")
PHASE = re.compile(r"phase: runs (\d+)\s+service PASS (\d+)\s+profile PASS (\d+)\s+obligations (\d+)\s+onTime (\d+)\s+worst L ([\d.]+) ms")

BASE = ["nodes=16", "fanout=common", "sharedEncode=1", "lendUrgentShare=0", "repairShareBits=8000",
        "controlHz=1", "repairHz=0", "stateHz=10", "align=1"]


def reserve(wire):
    """Seven inputs a rolling second, three copies each, at the largest event the codec produced in a probe run."""
    probe = subprocess.run([exe, "Q2", "1", *BASE, f"wire={wire}", "urgentShareBits=200000", "durationS=30", HUMAN, *EXTRA],
                           capture_output=True, text=True).stdout
    largest = int(WIRE.search(probe)[3]) + 8          # 8 B margin: sequence and value digits grow over a longer run
    return 7 * 3 * 8 * (largest + 28 + 28), largest


RESERVE = {wire: reserve(wire) for _, wire in POINTS + SPECIFIED}


def job(point, test):
    name, wire = point
    words = test[1].split()
    head = words[:2]
    args = head + BASE + [f"wire={wire}", f"urgentShareBits={RESERVE[wire][0]}", "durationS=60"] + words[len(head):] + EXTRA
    if words[0] == "phase":
        args.remove("durationS=60")
    return dict(point=name, test=test[0], args=args)


def run(j):
    text = subprocess.run([exe] + j["args"], capture_output=True, text=True).stdout
    p = PHASE.search(text)
    if p:
        j.update(result=f"{p[2]} of {p[1]} phase points pass; worst L {float(p[6]):.0f} ms" if p[2] != p[1]
                 else f"{p[1]} of {p[1]}; worst L {float(p[6]):.0f} ms", service=f"{p[2]}/{p[1]}", profile=f"{p[3]}/{p[1]}")
        return j
    seeds = list(SEED.finditer(text))
    if not seeds:
        j.update(result="no output: " + text[-200:].replace("\n", " "))
        return j
    ob = sum(int(m[1]) for m in seeds)
    late = ob - sum(int(m[2]) for m in seeds)
    age = max(float(m[2]) for m in NET.finditer(text))
    loads = list(LOAD.finditer(text))
    verdict = VERDICT.findall(text)
    wire = list(WIRE.finditer(text))
    avatar = list(AVATAR.finditer(text))
    share = "on time" if late == 0 else (f"{late} of {ob} late" if 100 * late / ob < 0.1 else f"{100 * late / ob:.1f} % late")
    kbps = sum(float(m[1]) for m in loads) / len(loads)
    control = sum(float(m[3]) for m in loads) / len(loads)
    oversize = sum(int(m[6]) for m in wire)
    j.update(result=f"{share}; worst L {max(float(m[6]) for m in seeds):.0f} ms; state {age:.0f} ms; {kbps:.0f} kbps; "
                    f"airtime {max(float(m[7]) for m in loads):.0f} %" + (f"; {oversize} urgent frames over the datagram" if oversize else ""),
             service=f"{sum(v[0] == 'PASS' for v in verdict)}/{len(verdict)}",
             profile=f"{sum(v[1] == 'PASS' for v in verdict)}/{len(verdict)}",
             kbps=f"{kbps:.1f}", control_kbps=f"{control:.1f}",
             decode_fail=sum(int(m[4]) for m in wire),
             avatar_near_p95=max(float(m[2]) for m in avatar) if avatar else "",
             avatar_far_p95=max(float(m[5]) for m in avatar) if avatar else "",
             marker_near_p95=max(float(m[8]) for m in avatar) if avatar else "",
             marker_far_p95=max(float(m[11]) for m in avatar) if avatar else "")
    return j


jobs = [job(p, t) for t in TESTS for p in POINTS]
with ThreadPoolExecutor(max_workers=2) as pool:
    results = list(pool.map(run, jobs))
fields = ["point", "test", "result", "service", "profile", "kbps", "control_kbps", "decode_fail",
          "avatar_near_p95", "avatar_far_p95", "marker_near_p95", "marker_far_p95"]
with open(STEM + ".csv", "w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=fields, extrasaction="ignore")
    w.writeheader()
    w.writerows(results)
out = ["# 16 participants, common delivery, encoded-wire CoT, serverless alignment" + (f" ({' '.join(EXTRA)})" if EXTRA else "")
       + (f"; forms {', '.join(FORMS)}" if FORMS else ""), "",
       "Urgent reserve per point: 7 inputs x 3 copies at the largest encoded event in a probe run (+8 B): " +
       "; ".join(f"{wire} {RESERVE[wire][1]} B payload, {RESERVE[wire][0]} bits" for _, wire in POINTS), "",
       "As specified, neither schema is admissible at 16 participants: " +
       "; ".join(f"{name} reserves {RESERVE[wire][0]} of the 200000-bit ledger for urgent copies alone ({RESERVE[wire][1]} B events" +
                 (", over the 1200 B datagram with authentication" if RESERVE[wire][1] + 28 > 1200 else "") + ")" for name, wire in SPECIFIED) +
       ", leaving too little for 10 Hz state.", "",
       "| test | " + " | ".join(p[0] for p in POINTS) + " |", "|---|" + "---|" * len(POINTS)]
for t in TESTS:
    cells = []
    for p in POINTS:
        r = next(r for r in results if r["test"] == t[0] and r["point"] == p[0])
        cells.append(r["result"] + (f" (profile {r['profile']})" if r.get("profile") and r["profile"].split("/")[0] != r["profile"].split("/")[1] else ""))
    out.append(f"| {t[0]} | " + " | ".join(cells) + " |")
out += ["", "Alignment and calibration (two erasures, human renewal input; P95 over three seeds, worst seed):", "",
        "| point | control kbps | avatar near P95 m | avatar far P95 m | marker near P95 m | marker far P95 m |", "|---|---|---|---|---|---|"]
for p in POINTS:
    r = next(r for r in results if r["test"] == TESTS[0][0] and r["point"] == p[0])
    out.append(f"| {p[0]} | {r.get('control_kbps', '')} | {r.get('avatar_near_p95', '')} | {r.get('avatar_far_p95', '')} | "
               f"{r.get('marker_near_p95', '')} | {r.get('marker_far_p95', '')} |")
open(STEM + ".md", "w").write("\n".join(out) + "\n")
print("\n".join(out))
