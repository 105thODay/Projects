#!/usr/bin/env python3
"""Player-count limits of common delivery, on the ideal medium and on an 802.11 access-point channel (Rev 6).

Usage: python tools/player_limits.py [quick] [wire=<form>] [channels=<name,...>]
       (writes player_limits.csv/.md; with wire= or channels=, player_limits_<wire>[_<channels>].csv/.md)
       Rev 7: python tools/player_limits.py wire=tak_lean channels=ideal
Every run: common delivery, lean CoT XML encoded by the codec and charged at its real size, serverless alignment,
state 10 Hz, control fixture 1 Hz, repair on request, 8 kbit repair share, urgent reserve for 7 inputs a second.
A participant count passes when every obligation in three seeds of 30 s is on time (service PASS in all three).
The search climbs the ladder and stops at the first count that fails; it reports the last pass and that failure.

Workloads
  normal      human renewal input: mean 1 a second, never two closer than 154 ms (ruling A17)
  together    every participant fires at the same instant once a second; every device's 90 Hz opportunity
              clock, state and fixture timers share one phase (syncPhases=1: the adversarial bound)
  strings     every participant fires a 7-pull string, 154 ms splits, all in lockstep, clocks aligned as above
  ..., own clocks  the same inputs, but each device keeps its own clock phases (syncPhases=2)
Results are cached in player_limits.csv by (case, workload, channel); delete a row to recompute it.
Each workload runs clean (Q0: only the channel loses frames) and with two erasures (Q2: the profile's bound).

Channels
  ideal       the Rev 1-5 medium: one 10 Mbit/s collision-free half-duplex channel
  basic/vo    802.11 AP; group frames at the 6 Mbit/s basic rate, urgent and control on AC_VO
  basic/be    the same with every frame on AC_BE (no voice marking)
  unicast/vo  the AP converts each group frame to acknowledged unicast, one per recipient
  basic/doze  basic/vo with one station in power save: group frames wait for the DTIM beacon (DTIM period 1)
  unicast/doze unicast/vo with one station in power save: its frames wait for the next beacon
"""
import csv
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

exe = "./ar_sim" if sys.platform != "win32" else "ar_sim.exe"
QUICK = "quick" in sys.argv[1:]
WIRE = next((a.split("=", 1)[1] for a in sys.argv[1:] if a.startswith("wire=")), "xml_lean")
ONLY = next((a.split("=", 1)[1].split(",") for a in sys.argv[1:] if a.startswith("channels=")), None)
STEM = "player_limits" + ("" if WIRE == "xml_lean" and not ONLY else f"_{WIRE}" + ("_" + "-".join(c.replace("/", "-") for c in ONLY) if ONLY else ""))
LADDER = [2, 4, 6, 8, 10, 12, 14, 16, 20, 24, 28, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 192]
EVENT_PAYLOAD = {"xml_lean": 630, "tak_lean": 470}[WIRE]   # largest event seen at 160 participants, plus margin
RESERVE = 7 * 3 * 8 * (EVENT_PAYLOAD + 28 + 28 + 38)
BASE = ["fanout=common", "sharedEncode=1", "lendUrgentShare=0", "repairShareBits=8000", "controlHz=1", "repairHz=0",
        "stateHz=10", "durationS=30", f"wire={WIRE}", "align=1", f"urgentShareBits={RESERVE}"]
WORKLOADS = (("normal", ["inputMinGapUs=154000"]),
             ("together", ["syncPhases=1"]),
             ("strings", ["syncPhases=1", "inputTrain=7", "inputTrainGapUs=154000"]),
             ("together, own clocks", ["syncPhases=2"]),
             ("strings, own clocks", ["syncPhases=2", "inputTrain=7", "inputTrainGapUs=154000"]))
CHANNELS = (("ideal", []),
            ("basic/vo", ["channel=wifi_ap", "group=basic"]),
            ("basic/be", ["channel=wifi_ap", "group=basic", "wifiVoice=0"]),
            ("unicast/vo", ["channel=wifi_ap", "group=unicast"]),
            ("basic/doze", ["channel=wifi_ap", "group=basic", "wifiDozing=1"]),
            ("unicast/doze", ["channel=wifi_ap", "group=unicast", "wifiDozing=1"]))
if ONLY:
    CHANNELS = tuple(c for c in CHANNELS if c[0] in ONLY)
CASES = ("Q0", "Q2")

SEED = re.compile(r"obligations (\d+)\s+onTime (\d+).*L\(P50/P95/P99/max\) (-?[\d.]+)/(-?[\d.]+)/(-?[\d.]+)/(-?[\d.]+) ms")
VERDICT = re.compile(r"verdict: service (\w+)")
LOAD = re.compile(r"meanCharged\(node mean/max\) ([\d.]+)/([\d.]+) kbps.*airtime ([\d.]+)%")
WIFI = re.compile(r"wifi: busy ([\d.]+)%\s+attempts (\d+)\s+collided (\d+) \(([\d.]+)%\).*dropped (\d+)\s+group (\d+) \(lost (\d+)\)")


def run(n, case, workload, channel):
    args = [exe, case, "3", f"nodes={n}"] + BASE + workload + channel
    text = subprocess.run(args, capture_output=True, text=True).stdout
    seeds = list(SEED.finditer(text))
    verdicts = VERDICT.findall(text)
    ob = sum(int(m[1]) for m in seeds)
    late = ob - sum(int(m[2]) for m in seeds)
    loads = list(LOAD.finditer(text))
    wifi = list(WIFI.finditer(text))
    return dict(n=n, passed=bool(verdicts) and all(v == "PASS" for v in verdicts),
                late_pct=100.0 * late / ob if ob else 100.0,
                worst_l=max((float(m[6]) for m in seeds), default=-1.0),
                busy=max((float(m[3]) for m in loads), default=0.0),
                kbps=sum(float(m[1]) for m in loads) / len(loads) if loads else 0.0,
                collided=max((float(m[4]) for m in wifi), default=0.0),
                dropped=sum(int(m[5]) for m in wifi),
                group_lost=sum(int(m[7]) for m in wifi), group=sum(int(m[6]) for m in wifi))


def search(key):
    case, (wname, wargs), (cname, cargs) = key
    last, fail = None, None
    for n in LADDER:
        r = run(n, case, wargs, cargs)
        if r["passed"]:
            last = r
        else:
            fail = r
            break
    return dict(case=case, workload=wname, channel=cname, last=last, fail=fail)


def cached():
    """Earlier results, so a new workload or channel does not rerun the old ones."""
    try:
        rows = list(csv.DictReader(open(STEM + ".csv")))
    except FileNotFoundError:
        return {}
    out = {}
    for row in rows:
        def side(prefix, n):
            if not n:
                return None
            return dict(n=int(n), late_pct=float(row[prefix + "late_pct"] or 0), worst_l=row[prefix + "worst_L_ms"],
                        busy=float(row[prefix + "busy_pct"] or 0), collided=float(row[prefix + "collided_pct"] or 0),
                        dropped=row[prefix + "dropped"], group_lost=row[prefix + "group_lost"], kbps=float(row[prefix + "kbps"] or 0))
        out[(row["case"], row["workload"], row["channel"])] = dict(
            case=row["case"], workload=row["workload"], channel=row["channel"], last=side("pass_", row["passes"]), fail=side("fail_", row["fails"]))
    return out


keys = [(case, w, c) for case in CASES for w in WORKLOADS for c in CHANNELS]
if QUICK:
    keys = [k for k in keys if k[0] == "Q0" and k[1][0] == "normal"]
done = cached()
todo = [k for k in keys if (k[0], k[1][0], k[2][0]) not in done]
with ThreadPoolExecutor(max_workers=2) as pool:
    fresh = list(pool.map(search, todo))
results = [done.get((k[0], k[1][0], k[2][0])) or next(r for r in fresh if (r["case"], r["workload"], r["channel"]) == (k[0], k[1][0], k[2][0]))
           for k in keys]


def cell(r):
    last, fail = r["last"], r["fail"]
    if last is None:
        head = f"fails at {fail['n']}"
    elif fail is None:
        head = f"≥ {last['n']}"
    else:
        head = f"**{last['n']}**"
    if fail is None:
        return head + " (top of ladder)"
    why = f"{fail['late_pct']:.1f} % late at {fail['n']}"
    if r["channel"] != "ideal":
        why += f", {fail['collided']:.0f} % of attempts collide, busy {fail['busy']:.0f} %"
    else:
        why += f", airtime {fail['busy']:.0f} %"
    return f"{head} ({why})"


with open(STEM + ".csv", "w", newline="") as f:
    w = csv.writer(f)
    sides = ("late_pct", "worst_L_ms", "busy_pct", "collided_pct", "dropped", "group_lost", "kbps")
    w.writerow(["case", "workload", "channel", "passes", "fails"] + [f"fail_{k}" for k in sides] + [f"pass_{k}" for k in sides])

    def values(side):
        side = side or {}
        return [side.get("late_pct", ""), side.get("worst_l", ""), side.get("busy", ""), side.get("collided", ""),
                side.get("dropped", ""), side.get("group_lost", ""), side.get("kbps", "")]
    for r in results:
        w.writerow([r["case"], r["workload"], r["channel"], (r["last"] or {}).get("n", ""), (r["fail"] or {}).get("n", "")]
                   + values(r["fail"]) + values(r["last"]))
out = [f"# Player-count limits, common delivery, {'TAK Protocol v1, lean' if WIRE == 'tak_lean' else 'lean CoT XML'}, serverless alignment"
       + (f", channels {', '.join(ONLY)}" if ONLY else ""), "",
       "Largest count with every obligation on time in three 30 s seeds; in parentheses, what happens at the next rung.", ""]
for case in CASES:
    sub = [r for r in results if r["case"] == case]
    if not sub:
        continue
    out += [f"## {case}: " + ("only the channel loses frames" if case == "Q0" else "two erasures on every urgent event (the profile's bound)"), "",
            "| channel | " + " | ".join(w[0] for w in WORKLOADS) + " |", "|---|" + "---|" * len(WORKLOADS)]
    for cname, _ in CHANNELS:
        row = [next((cell(r) for r in sub if r["channel"] == cname and r["workload"] == w[0]), "") for w in WORKLOADS]
        if any(row):
            out.append(f"| {cname} | " + " | ".join(row) + " |")
    out.append("")
open(STEM + ".md", "w").write("\n".join(out))
print("\n".join(out))
