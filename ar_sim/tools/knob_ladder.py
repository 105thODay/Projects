#!/usr/bin/env python3
"""Read knob_search_<fanout>.csv and print, per player count and frame size, the least-changed passing settings.

Usage: python tools/knob_ladder.py [unicast|common] [dither]
Two state-freshness targets are reported: P95 age at or under 110 ms and at or under 210 ms.
"""
import csv
import sys

fanout = sys.argv[1] if len(sys.argv) > 1 else "unicast"
suffix = "_dither" if len(sys.argv) > 2 and sys.argv[2] == "dither" else ""
rows = [r for r in csv.DictReader(open(f"knob_search_{fanout}{suffix}.csv")) if r["service"] == "PASS" and r["profile"] == "PASS"]
for r in rows:
    r["age"] = float(r["state_age_p95"])
    r["changed"] = int(r["knobs_changed"])
print("| players | frame B | target age ms | knobs changed | state Hz | control | repair fixture | copies | inputs/s | state age P95 ms | charged kbps | airtime % |")
print("|---|---|---|---|---|---|---|---|---|---|---|---|")
for players in ("8", "12", "16"):
    for frame in ("800", "400", "200", "100"):
        for target in (110.0, 210.0):
            fit = [r for r in rows if r["players"] == players and r["frame_bytes"] == frame and 0 <= r["age"] <= target]
            if not fit:
                print(f"| {players} | {frame} | {target:.0f} | none passes | | | | | | | | |")
                continue
            # Fewest knobs changed; among those keep three copies and one input per second when possible, then freshest state.
            r = min(fit, key=lambda r: (r["changed"], r["copies"] != "3", r["input_hz"] != "1", r["age"]))
            print(f"| {players} | {frame} | {target:.0f} | {r['changed']} | {r['state_hz']} | {r['control']} | {r['repair_fixture']} | {r['copies']} | "
                  f"{r['input_hz']} | {r['age']} | {r['charged_kbps']} | {r['airtime']} |")
