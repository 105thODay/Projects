#!/usr/bin/env python3
"""Write the run list: the profile's cases and sweeps, at the starter frame size or a smaller one.

Usage: python tools/make_matrix.py       -> run_matrix.sh/.bat, output ar_matrix.txt (800 B starter frames)
       python tools/make_matrix.py 200   -> run_matrix_200.sh/.bat, output ar_matrix_200.txt (200 B frames)
       python tools/make_matrix.py tak   -> run_matrix_tak.sh/.bat, output ar_matrix_tak.txt (Rev 7: every frame a real
                                            TAK Protocol v1 lean message, alignment on; reserves sized from a 513 B event)
With a frame size, every run carries that size for urgent copies, state records and control frames, and the list
gains the unicast operating points for 8, 12 and 16 participants found by tools/knob_search.py.
Feed the output to tools/summarize_matrix.py. A section header is '## <group> | <label> | <command>'.
"""
import sys

TAK = len(sys.argv) > 1 and sys.argv[1] == "tak"
TAK_EVENT_BYTES = 513                                       # largest lean TAK v1 event: 457 B payload + 28 UDP/IP + 28 auth
FRAME = TAK_EVENT_BYTES if TAK else (int(sys.argv[1]) if len(sys.argv) > 1 else 800)   # complete frame, bytes
U = FRAME * 8                                               # one urgent copy, bits
SUFFIX = "_tak" if TAK else ("" if FRAME == 800 else f"_{FRAME}")
# The size overrides go ahead of a run's own overrides, so a sweep's value for the same knob wins. In TAK mode the
# codec sets every frame's size, so the size knobs are not passed.
size = " wire=tak_lean align=1" if TAK else ("" if FRAME == 800 else f" urgentBits={U} stateBits={U} controlBits={min(3200, U)}")
runs = []   # (group, label, command)

def add(group, label, command):
    words = command.split(" ")
    head = 2 if words[0] == "phase" or (len(words) > 1 and words[1].isdigit()) else 1
    runs.append((group, label, " ".join(words[:head]) + size + (" " + " ".join(words[head:]) if words[head:] else "")))

def kbit(bits):
    return f"{bits / 1000:g}"

# 1. Bounded qualification (README steps 2 and 3)
for case in ("Q0", "Q1", "Q2"):
    add("qualification", case, f"{case} 10")
    add("qualification", f"{case} phase sweep", f"phase {case}")
add("qualification", "Q2 phase sweep, D at its 20 ms limit", "phase Q2 propagationUs=18860")

# 2. Loss, jitter, blockage, outage, correctness (README step 4)
for loss in ("0", "0.01", "0.03"):
    for direction, name in ((0, "both ways"), (1, "A to B"), (2, "B to A")):
        add("R0 independent loss", f"loss {loss} {name}", f"R0 10 lossProb={loss} lossDirection={direction}")
for jitter in (0, 5000, 10000):
    for erase in (0, 1, 2):
        add("J0 jitter", f"jitter {jitter // 1000} ms, {erase} erased", f"J0 3 jitterMaxUs={jitter} eraseCopies={erase}")
for block in (1, 2, 3, 5):
    add("C0 shared blockage", f"{block} copy periods blocked", f"C0 3 blockPeriods={block}")
for loss in ("0.05", "0.10"):
    add("S0 severe loss", f"loss {loss}", f"S0 10 lossProb={loss} blackoutUs=0")
for blackout in (50000, 100000, 250000):
    add("S0 blackout", f"{blackout // 1000} ms blackout every 10 s", f"S0 10 lossProb=0 blackoutUs={blackout}")
for reorder in (5000, 20000):
    add("P0 duplicate and reorder", f"reorder delay {reorder // 1000} ms", f"P0 3 reorderUs={reorder}")
for burst in (1, 2, 4):
    add("O0 simultaneous inputs", f"burst {burst}, lending as written", f"O0 3 burst={burst}")
add("O0 simultaneous inputs", "burst 4, standing urgent reserve 64 kbit", "O0 3 burst=4 lendUrgentShare=0")
big = -(-4 * 4 * U // 8000) * 8000              # four inputs: three copies each, plus one more copy set
add("O0 simultaneous inputs", f"burst 4, standing urgent reserve {kbit(big)} kbit", f"O0 3 burst=4 lendUrgentShare=0 urgentShareBits={big}")
add("R1 partition and rejoin", "1000 ms partition, replay, takeover", "R1 3")

# 3. One-variable sweeps from the two-node starter (README step 4)
for rate in ("0.5", "1", "3"):
    add("sweep input rate", f"{rate} inputs/s", f"Q0 3 inputHz={rate}")
for rate in ("10", "15", "30"):
    if TAK:
        add("sweep routine state", f"{rate} Hz, TAK v1 lean state", f"Q0 3 stateHz={rate}")   # the codec sets the size
        continue
    for size_bytes in (FRAME // 2, FRAME):
        add("sweep routine state", f"{rate} Hz, {size_bytes} B", f"Q0 3 stateHz={rate} stateBits={size_bytes * 8}")
for hz in (30, 45, 60, 90):
    for erase in (0, 1, 2):
        add("sweep opportunity rate", f"{hz} Hz, {erase} erased", f"Q{erase} 3 opportunityHz={hz}")
for phy in (10000000, 2000000, 1000000, 500000, 200000):
    add("sweep PHY rate", f"{phy // 1000} kbit/s, 2 erased", f"Q2 3 phyBps={phy}")

# 4. Participants and fan-out (NET-018)
for nodes in (2, 3, 4, 8, 16):
    add("participants", f"{nodes} nodes, unicast, lending as written", f"Q0 3 nodes={nodes}")
    reserve = max(64000, -(-4 * (nodes - 1) * U // 8000) * 8000)
    if reserve < 200000:
        add("participants", f"{nodes} nodes, unicast, standing urgent reserve {reserve // 1000} kbit", f"Q0 3 nodes={nodes} lendUrgentShare=0 urgentShareBits={reserve}")
    add("participants", f"{nodes} nodes, common delivery", f"Q0 3 nodes={nodes} fanout=common")
add("participants", "32 nodes, common delivery", "Q0 3 nodes=32 fanout=common")
for nodes in (8, 16, 32):
    add("participants", f"{nodes} nodes, common delivery, 2 erased, 1% loss, 5 ms jitter", f"Q2 3 nodes={nodes} fanout=common lossProb=0.01 jitterMaxUs=5000")
for phy in (10000000, 5000000, 2000000, 1000000):
    add("participants", f"16 nodes, common delivery, PHY {phy // 1000} kbit/s", f"Q0 3 nodes=16 fanout=common phyBps={phy}")
add("participants", "16 nodes, common delivery, correlated 3% loss", "R0 10 nodes=16 fanout=common lossProb=0.03 correlatedLoss=1")

# 5. Routes
# A relayed copy is charged again at each relay, so the reserve must hold the copies of every hop.
for hops in (1, 2, 3):
    burst = hops * 3 * 3 * U                     # one input, three copies to three recipients, every hop
    for rate, factor in (("1", 4 / 3), ("0.5", 1.0)):
        reserve = -(-int(burst * factor) // 8000) * 8000
        label = f"4 nodes, unicast, {hops} hop, {rate} input/s, reserve {reserve // 1000} kbit"
        if reserve <= 192000:
            add("routes", label, f"Q0 3 nodes=4 hops={hops} inputHz={rate} lendUrgentShare=0 urgentShareBits={reserve}")
        else:
            add("routes", label + " (over the ceiling: run with 192)", f"Q0 3 nodes=4 hops={hops} inputHz={rate} lendUrgentShare=0 urgentShareBits=192000")

# 6. Operating points from the knob search (smaller frames only): the tests that search did not run.
#    Rev 4: every point holds the profile's 8 kbit repair share back from control and state, and inputs
#    are dithered +/-100 ms about their mean interval, except in the phase sweep and the simultaneous-input
#    case, which need fixed instants.
if FRAME != 800 and not TAK:                   # TAK mode: the 16-participant point is tools/wire_point_tests.py
    size = ""                                    # these commands carry their own sizes
    shared = (f"sharedEncode=1 lendUrgentShare=0 repairShareBits=8000 urgentBits={U} stateBits={U} "
              f"controlBits={min(3200, U)} controlHz=1 repairHz=0")
    # (name, participants, copies, inputs per second, state Hz). Periodic inputs need a standing urgent reserve of
    # c*R*u, plus R*u at one input per second; dithered inputs at one per second can put two whole bursts in one
    # rolling second, so they need 2*c*R*u.
    POINTS = {200: (("8 nodes, 3 copies, state 10 Hz", 8, 3, "1", 10), ("12 nodes, 3 copies, state 5 Hz", 12, 3, "1", 5),
                    ("16 nodes, 2 copies, 0.5 input/s, state 5 Hz", 16, 2, "0.5", 5), ("16 nodes, 3 copies, state 5 Hz", 16, 3, "1", 5)),
              100: (("8 nodes, 3 copies, state 15 Hz", 8, 3, "1", 15), ("12 nodes, 3 copies, state 15 Hz", 12, 3, "1", 15),
                    ("16 nodes, 3 copies, state 10 Hz", 16, 3, "1", 10))}
    for name, nodes, copies, rate, state in POINTS[FRAME]:
        group = f"unicast {FRAME} B: {name}"
        reserve  = -(-(copies + (1 if rate == "1" else 0)) * (nodes - 1) * U // 800) * 800
        reserveD = -(-copies * (2 if rate == "1" else 1) * (nodes - 1) * U // 800) * 800
        erase = copies - 1
        base = f"nodes={nodes} {shared} copies={copies} inputHz={rate} stateHz={state}"
        fixed = f"{base} urgentShareBits={reserve}"                            # periodic inputs
        point = f"{base} urgentShareBits={reserveD} inputDitherUs=200000"      # dithered inputs
        add(group, "clean", f"Q0 3 {point}")
        add(group, f"{erase} erased", f"Q{erase} 3 {point}")
        add(group, f"{name}, {erase} erased, phase sweep", f"phase Q{erase} {fixed}")
        for jitter in (5000, 10000):
            add(group, f"{erase} erased, jitter {jitter // 1000} ms", f"J0 3 {point} jitterMaxUs={jitter} eraseCopies={erase}")
        for loss in ("0.01", "0.03"):
            add(group, f"independent loss {loss}", f"R0 3 {point} lossProb={loss}")
        for loss in ("0.05", "0.10"):
            add(group, f"independent loss {loss}", f"S0 3 {point} lossProb={loss} blackoutUs=0")
        add(group, f"{erase} erased, 1% loss, 5 ms jitter", f"Q{erase} 3 {point} lossProb=0.01 jitterMaxUs=5000")
        for block in (1, 2, 3, 5):
            add(group, f"{block} copy periods blocked", f"C0 3 {point} blockPeriods={block}")
        add(group, "2 copy periods blocked, repair share 16 kbit", f"C0 3 {point} blockPeriods=2 repairShareBits=16000")
        for control in ("2", "7.5"):
            add(group, f"3 copy periods blocked, control fixture {control} Hz", f"C0 3 {point} blockPeriods=3 controlHz={control}")
        for blackout in (50000, 100000, 250000):
            add(group, f"{blackout // 1000} ms blackout every 10 s", f"S0 3 {point} lossProb=0 blackoutUs={blackout}")
        add(group, "duplicates and reordering", f"P0 3 {point}")
        for burst in (2, 4):
            want = reserve * burst
            label = f"{burst} simultaneous inputs, reserve {kbit(want)} kbit"
            add(group, label if want <= 184000 else label + " (over the ceiling: run with 184)",
                f"O0 3 {base} burst={burst} urgentShareBits={min(want, 184000)}")
        add(group, "1000 ms partition, replay, takeover", f"R1 3 {point}")
        want = reserveD * 2
        label = f"2 hop routes, reserve {kbit(want)} kbit"
        add(group, label if want <= 184000 else label + " (over the ceiling: run with 184)",
            f"Q0 3 {base} inputDitherUs=200000 hops=2 urgentShareBits={min(want, 184000)}")
        for phy in (5000000, 2000000, 1000000):
            add(group, f"{erase} erased, PHY {phy // 1000} kbit/s", f"Q{erase} 3 {point} phyBps={phy}")
        for hz in (60, 45):
            add(group, f"{erase} erased, opportunities at {hz} Hz", f"Q{erase} 3 {point} opportunityHz={hz}")

out = f"ar_matrix{SUFFIX}.txt"
sh = ["#!/bin/sh", f"# Networked AR profile: every case and sweep. Output: {out} (then: python3 tools/summarize_matrix.py).",
      f"exec > {out}", "echo '## gate | analytic | analytic'; ./ar_sim analytic", "echo '## gate | F0 | F0'; ./ar_sim F0"]
bat = ["@echo off", f"rem Networked AR profile: every case and sweep. Output: {out} (then: python tools\\summarize_matrix.py).",
       f"echo ## gate ^| analytic ^| analytic> {out}", f"ar_sim.exe analytic >> {out}",
       f"echo ## gate ^| F0 ^| F0>> {out}", f"ar_sim.exe F0 >> {out}"]
for group, label, command in runs:
    sh.append(f"echo '## {group} | {label} | {command}'; ./ar_sim {command}")
    bat.append(f"echo ## {group} ^| {label} ^| {command}>> {out}")
    bat.append(f"ar_sim.exe {command} >> {out}")
open(f"run_matrix{SUFFIX}.sh", "w").write("\n".join(sh) + "\n")
open(f"run_matrix{SUFFIX}.bat", "w").write("\r\n".join(bat) + "\r\n")
print(len(runs), "run groups")
