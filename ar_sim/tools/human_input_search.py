#!/usr/bin/env python3
"""Measured CoT frame sizes against human-limited input: which participant counts survive.

Usage: python tools/human_input_search.py [cot_sizes.csv]   (writes human_input.csv and human_input.md)
       python tools/human_input_search.py tak               (Rev 7: TAK Protocol v1 lean and as specified, every frame a
                                                             real message from the codec, alignment on; writes
                                                             human_input_tak.csv and human_input_tak.md)

Frame sizes come from tools/cot_fixtures.py (charged bytes: payload + UDP/IPv4 + authentication). Repair data is an
urgent event sent again, so it takes the urgent size; a repair request takes the measured request size.

Input models (mean rate stays at the profile's one input per participant per second):
  renewal     no two inputs closer than 154 ms (index-finger maximum, about 6.5 taps/s), otherwise random
  string4     engagements of 4 trigger pulls 154 ms apart
  string7     engagements of 7 pulls 154 ms apart: a full second at maximum tapping rate
  string10    engagements of 10 pulls 100 ms apart: a trained shooter's 0.10 s splits
Each run holds a standing urgent reserve for the inputs the design promises in one rolling second:
3 for renewal (it rarely puts more in a second), and the string length otherwise. When that reserve, the
control fixture and the 8 kbit repair share do not fit under the ceiling, the reserve is cut to what fits and the
row says "capped". Case Q2 (two erased copies of every input), three seeds, 60 s, unicast or common delivery,
copies 3, state 10 Hz, control fixture 1 Hz, repair on request, sharedEncode=1.
"""
import csv
import itertools
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

WIRE = len(sys.argv) > 1 and sys.argv[1] == "tak"
SIZES = sys.argv[1] if len(sys.argv) > 1 and not WIRE else "cot_sizes.csv"
STEM = "human_input_tak" if WIRE else "human_input"
exe = "./ar_sim" if sys.platform != "win32" else "ar_sim.exe"
LIMIT, REPAIR_SHARE, COPIES = 200000, 8000, 3
ENCODINGS = (("CoT XML, as specified", "as_specified", "xml"),
             ("CoT XML, lean schema", "lean", "xml"),
             ("TAK v1, lean schema", "lean", "tak_v1"),
             ("XML + session dictionary, as specified", "as_specified", "xml_dict"),
             ("XML + session dictionary, lean", "lean", "xml_dict"),
             ("binary AR detail, lean (new TAK version)", "lean", "ar_binary"))
MODELS = (("renewal", 3, "inputMinGapUs=154000"),
          ("string4", 4, "inputTrain=4 inputTrainGapUs=154000 inputMinGapUs=154000"),
          ("string7", 7, "inputTrain=7 inputTrainGapUs=154000 inputMinGapUs=154000"),
          ("string10", 10, "inputTrain=10 inputTrainGapUs=100000 inputMinGapUs=100000"))
if WIRE:
    # (name, style, encoding) keeps its shape; the encoding names the codec's wire form.
    ENCODINGS = (("TAK v1, lean schema", "lean", "tak_lean"), ("TAK v1, as specified", "as_specified", "tak_specified"))
PLAYERS = (8, 12, 16)
FANOUTS = ("unicast", "common")

table = {}
if WIRE:
    # Charged sizes from a probe run of the codec in the simulator: the largest of each kind, plus 8 B of margin for
    # sequence and value digits over a longer run, plus UDP/IPv4 and authentication.
    PROBE = re.compile(r"wire: event \d+/\d+/(\d+) state \d+/\d+/(\d+) control \d+/\d+/(\d+) repairReq (?:none|\d+/\d+/(\d+))")
    for _, style, wire in ENCODINGS:
        text = subprocess.run([exe, "Q2", "1", "nodes=16", "fanout=common", "durationS=30", f"wire={wire}", "align=1",
                               "sharedEncode=1", "lendUrgentShare=0", "urgentShareBits=180000", "controlHz=1", "repairHz=0"],
                              capture_output=True, text=True).stdout
        m = PROBE.search(text)
        charged = [8 * (int(v) + 8 + 56) if v else 0 for v in m.groups()]
        for kind, bits in zip(("urgent", "state", "control", "repair_request"), charged):
            table[(style, wire, kind)] = bits
        table[(style, wire, "repair_request")] = table[(style, wire, "repair_request")] or table[(style, wire, "control")]
else:
    for r in csv.DictReader(open(SIZES)):
        table[(r["style"], r["encoding"], r["kind"])] = int(r["charged_bits"])

SEED = re.compile(r"obligations (\d+)\s+onTime (\d+)\s+late (\d+)\s+missing (\d+)\s+rejected (\d+).*L\(P50/P95/P99/max\) (-?[\d.]+)/(-?[\d.]+)/(-?[\d.]+)/(-?[\d.]+) ms")
LOAD = re.compile(r"maxRolling (\d+).*meanCharged\(node mean/max\) ([\d.]+)/([\d.]+) kbps\s+byClass\(control/urgent/state/repair\) ([\d.]+)/([\d.]+)/([\d.]+)/([\d.]+).*airtime ([\d.]+)%")
NET = re.compile(r"stateAge\(P50/P95/max\) (-?[\d.]+)/(-?[\d.]+)/(-?[\d.]+) ms")
VERDICT = re.compile(r"service (\w+)\s+profile (\w+)")


def candidate(enc, players, fanout, model):
    name, style, encoding = enc
    mname, promised, knobs = model
    u = table[(style, encoding, "urgent")]
    state, control, request = (table[(style, encoding, k)] for k in ("state", "control", "repair_request"))
    recipients = players - 1 if fanout == "unicast" else 1
    control_load = recipients * control * 1
    want = promised * COPIES * recipients * u
    room = LIMIT - REPAIR_SHARE - control_load
    reserve = min(want, max(room, 0))
    reserve = (reserve // 8) * 8
    args = ["Q2", "3", f"nodes={players}", f"fanout={fanout}", "durationS=60", "sharedEncode=1", "lendUrgentShare=0",
            f"repairShareBits={REPAIR_SHARE}", f"urgentShareBits={reserve}", f"urgentBits={u}", f"stateBits={state}",
            f"controlBits={control}", f"repairRequestBits={request}", "controlHz=1", "repairHz=0", "stateHz=10",
            f"copies={COPIES}"] + knobs.split() + ([f"wire={encoding}", "align=1"] if WIRE else [])
    carried = room // (COPIES * recipients * u) if room > 0 else 0   # inputs per rolling second the ceiling can promise
    return dict(encoding=name, players=players, fanout=fanout, input_model=mname, urgent_bytes=u // 8, state_bytes=state // 8,
                control_bytes=control // 8, promised_per_s=promised, carried_per_s=carried, reserve_bits=reserve,
                capped=want > room, args=args)


def run(c):
    text = subprocess.run([exe] + c["args"], capture_output=True, text=True).stdout
    seeds = list(SEED.finditer(text))
    loads = list(LOAD.finditer(text))
    ages = list(NET.finditer(text))
    verdicts = list(VERDICT.finditer(text))
    ob = sum(int(m[1]) for m in seeds)
    c.update(obligations=ob, on_time=sum(int(m[2]) for m in seeds), late_pct=round(100 * (ob - sum(int(m[2]) for m in seeds)) / ob, 2),
             L_max=max(float(m[9]) for m in seeds), state_age_p95=max(float(m[2]) for m in ages),
             charged_kbps=round(sum(float(m[2]) for m in loads) / len(loads), 1), airtime=max(float(m[8]) for m in loads),
             service="PASS" if all(m[1] == "PASS" for m in verdicts) else "FAIL",
             profile="PASS" if all(m[2] == "PASS" for m in verdicts) else "FAIL")
    return c


grid = [candidate(*x) for x in itertools.product(ENCODINGS, PLAYERS, FANOUTS, MODELS)]
with ThreadPoolExecutor(max_workers=2) as pool:
    results = list(pool.map(run, grid))
fields = ["encoding", "players", "fanout", "input_model", "urgent_bytes", "state_bytes", "control_bytes", "promised_per_s",
          "carried_per_s", "reserve_bits", "capped", "service", "profile", "obligations", "on_time", "late_pct", "L_max",
          "state_age_p95", "charged_kbps", "airtime"]
with open(STEM + ".csv", "w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=fields, extrasaction="ignore")
    w.writeheader()
    w.writerows(results)

out = ["# " + ("TAK Protocol v1 on the wire" if WIRE else "Measured CoT sizes") + " against human-limited input (case Q2, 3 seeds, 60 s)", "",
       "Cell: share of inputs late, then state age P95. 'cap' marks a reserve cut to fit the ceiling.", ""]
for fanout in FANOUTS:
    out += [f"## {fanout}", "", "| encoding (urgent B charged) | players | inputs/s the ceiling can promise | "
            + " | ".join(m[0] for m in MODELS) + " |", "|---|---|---|" + "---|" * len(MODELS)]
    for enc in ENCODINGS:
        for players in PLAYERS:
            rows = [r for r in results if r["encoding"] == enc[0] and r["players"] == players and r["fanout"] == fanout]
            cells = []
            for m in MODELS:
                r = next(x for x in rows if x["input_model"] == m[0])
                late = "on time" if r["late_pct"] == 0 else f"{r['late_pct']} % late"
                age = "no state" if r["state_age_p95"] < 0 else f"{r['state_age_p95']:.0f} ms"
                cells.append(f"{late}; {age}{'; cap' if r['capped'] else ''}")
            out.append(f"| {enc[0]} ({rows[0]['urgent_bytes']}) | {players} | {rows[0]['carried_per_s']} | " + " | ".join(cells) + " |")
    out.append("")
open(STEM + ".md", "w").write("\n".join(out) + "\n")
print("\n".join(out))
