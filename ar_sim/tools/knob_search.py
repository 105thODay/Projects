#!/usr/bin/env python3
"""Search the profile's assumed workload knobs for settings that carry 8 to 16 participants.

Usage: python tools/knob_search.py [unicast|common] [dither]
  writes knob_search_<fanout>.csv, or knob_search_<fanout>_dither.csv with "dither", and prints a summary.
  "dither" (Rev 4): inputs vary +/-100 ms about their mean interval, the profile's 8 kbit repair share is held
  back from control and state, and the standing urgent reserve covers two whole bursts at one input per second,
  because two inputs from one participant can then fall inside one rolling second.
Every candidate keeps the retained limits: 100 ms response, 200 kbit per rolling second per client.
A candidate passes when every input is on time with the bounded erasures its copy count allows
(two erased for three copies, one erased for two), over three seeds. State age is reported, not gated.
"""
import csv
import itertools
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

fanout = sys.argv[1] if len(sys.argv) > 1 else "unicast"
dither = len(sys.argv) > 2 and sys.argv[2] == "dither"
REPAIR_SHARE = 8000 if dither else 0
exe = "./ar_sim" if sys.platform != "win32" else "ar_sim.exe"
PLAYERS = (8, 12, 16)
FRAME_BYTES = (800, 400, 200, 100)          # urgent copy and state record, complete frame
STATE_HZ = (15, 10, 5)
CONTROL = {"7.5 Hz": 7.5, "2 Hz": 2, "1 Hz": 1}
REPAIR_FIXTURE = {"5 Hz": 5, "off": 0}      # off: repair frames go only when a recipient asks
COPIES = (3, 2)
INPUT_HZ = (1, 0.5)
LIMIT = 200000

SEED = re.compile(r"obligations (\d+)\s+onTime (\d+).*L\(P50/P95/P99/max\) (-?[\d.]+)/(-?[\d.]+)/(-?[\d.]+)/(-?[\d.]+) ms")
LOAD = re.compile(r"maxRolling (\d+).*meanCharged\(node mean/max\) ([\d.]+)/([\d.]+) kbps\s+byClass\(control/urgent/state/repair\) ([\d.]+)/([\d.]+)/([\d.]+)/([\d.]+).*airtime ([\d.]+)%")
NET = re.compile(r"stateAge\(P50/P95/max\) (-?[\d.]+)/(-?[\d.]+)/(-?[\d.]+) ms")
VERDICT = re.compile(r"service (\w+)\s+profile (\w+)")


def candidate(players, frame, state_hz, control, repair, copies, input_hz):
    recipients = players - 1 if fanout == "unicast" else 1
    bits = frame * 8
    control_bits = min(3200, bits)
    burst = copies * recipients * bits
    # Standing urgent reserve: one input's copies, plus one more copy set when inputs recur within the window
    # (periodic inputs), or a second whole burst (dithered inputs, whose intervals can be shorter than the window).
    if dither:
        reserve = burst * (2 if input_hz >= 1 else 1)
    else:
        reserve = burst + (recipients * bits if input_hz >= 1 else 0)
    reserve = -(-reserve // 800) * 800
    control_load = recipients * control_bits * CONTROL[control]
    feasible = reserve + control_load + REPAIR_SHARE <= LIMIT
    changed = sum([frame != 800, state_hz != 15, control != "7.5 Hz", repair != "5 Hz", copies != 3, input_hz != 1])
    args = [f"Q{copies - 1}", "3", f"nodes={players}", f"fanout={fanout}", "durationS=60", "sharedEncode=1", "lendUrgentShare=0",
            f"urgentShareBits={reserve}", f"urgentBits={bits}", f"stateBits={bits}", f"stateHz={state_hz}", f"controlBits={control_bits}",
            f"controlHz={CONTROL[control]}", f"repairHz={REPAIR_FIXTURE[repair]}", f"copies={copies}", f"inputHz={input_hz}"]
    if dither:
        args += ["inputDitherUs=200000", f"repairShareBits={REPAIR_SHARE}"]
    return dict(players=players, frame_bytes=frame, state_hz=state_hz, control=control, repair_fixture=repair, copies=copies, input_hz=input_hz,
                reserve_bits=reserve, knobs_changed=changed, feasible=feasible, args=args)


def run(c):
    if not c["feasible"]:
        c.update(service="skipped: reserve plus control exceed the ceiling", on_time="", obligations="", L_max="", state_age_p95="",
                 state_kbps="", charged_kbps="", max_rolling="", airtime="", profile="")
        return c
    text = subprocess.run([exe] + c["args"], capture_output=True, text=True).stdout
    seeds = [m for m in SEED.finditer(text)]
    loads = [m for m in LOAD.finditer(text)]
    ages = [m for m in NET.finditer(text)]
    verdicts = [m for m in VERDICT.finditer(text)]
    c.update(obligations=sum(int(m[1]) for m in seeds), on_time=sum(int(m[2]) for m in seeds), L_max=max(float(m[6]) for m in seeds),
             state_age_p95=round(max(float(m[2]) for m in ages), 1), state_kbps=round(sum(float(m[6]) for m in loads) / len(loads), 1),
             charged_kbps=round(sum(float(m[2]) for m in loads) / len(loads), 1), max_rolling=max(int(m[1]) for m in loads),
             airtime=max(float(m[8]) for m in loads), service="PASS" if all(m[1] == "PASS" for m in verdicts) else "FAIL",
             profile="PASS" if all(m[2] == "PASS" for m in verdicts) else "FAIL")
    return c


grid = [candidate(*combo) for combo in itertools.product(PLAYERS, FRAME_BYTES, STATE_HZ, CONTROL, REPAIR_FIXTURE, COPIES, INPUT_HZ)]
with ThreadPoolExecutor(max_workers=4) as pool:
    results = list(pool.map(run, grid))
fields = ["players", "frame_bytes", "state_hz", "control", "repair_fixture", "copies", "input_hz", "reserve_bits", "knobs_changed", "service", "profile",
          "obligations", "on_time", "L_max", "state_age_p95", "state_kbps", "charged_kbps", "max_rolling", "airtime"]
with open(f"knob_search_{fanout}{'_dither' if dither else ''}.csv", "w", newline="") as f:
    writer = csv.DictWriter(f, fieldnames=fields, extrasaction="ignore")
    writer.writeheader()
    writer.writerows(results)

print(f"knob search, fanout={fanout}{', dithered inputs' if dither else ''}: {len(results)} candidates")
for players in PLAYERS:
    mine = [r for r in results if r["players"] == players]
    passed = [r for r in mine if r["service"] == "PASS"]
    print(f"\n## {players} participants: {len(passed)} of {len(mine)} candidates keep every input on time "
          f"({sum(1 for r in mine if r['service'].startswith('skipped'))} skipped as infeasible)")
    print("| knobs changed | frame B | state Hz | control | repair fixture | copies | inputs/s | reserve kbit | state age P95 ms | state kbps | charged kbps | airtime % | profile |")
    print("|---|---|---|---|---|---|---|---|---|---|---|---|---|")
    # For each count of changed knobs, the passing candidate with the freshest state.
    shown = 0
    for changed in range(0, 7):
        tier = sorted((r for r in passed if r["knobs_changed"] == changed), key=lambda r: (r["state_age_p95"] < 0, r["state_age_p95"]))
        for r in tier[:2]:
            print(f"| {changed} | {r['frame_bytes']} | {r['state_hz']} | {r['control']} | {r['repair_fixture']} | {r['copies']} | {r['input_hz']} | "
                  f"{r['reserve_bits'] / 1000:.1f} | {r['state_age_p95']} | {r['state_kbps']} | {r['charged_kbps']} | {r['airtime']} | {r['profile']} |")
            shown += 1
    if shown == 0:
        print("| none | | | | | | | | | | | | |")
