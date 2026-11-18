"""Offline analysis of an L4 live traffic run: clocks, urgent events, state, catch-up, ledger, L6 codec cost."""

import json
import math
import statistics
from pathlib import Path

from .adbio import monitor_summary
from .reporting import add_monitor_lines, fmt

CATCHUP_BASE = 1_000_000
NETWORK_BUDGET_MS = 59.0  # 100 ms less 10 input, 30 display, 1 uncertainty (requirements, section 02)
KIND_NAMES = {0: "event", 1: "state", 2: "summary", 3: "repair_request", 4: "repair_status", 5: "calibration"}


def reachability(out, name, count):
    logs = load_logs(out, name, count)
    matrix = {}
    for s in range(count):
        sent = {h for (_, cls, kind, _, _, _, _, h) in logs[s]["sends"] if kind == 1}
        for r in range(count):
            heard = {h for (_, snd, _, kind, _, _, _, ok, h) in logs[r]["recvs"] if snd == s and kind == 1}
            matrix[(s, r)] = len(sent & heard) / len(sent) if sent and s != r else (1.0 if s == r else 0.0)
    return matrix


def print_matrix(phones, matrix):
    count = len(phones)
    print("share of each sender's state frames heard (rows send, columns receive):")
    print("        " + " ".join(f"{r:>6}" for r in range(count)))
    for s in range(count):
        print(f"{s:>3} {phones[s].model[:4]:>4}" + " ".join(
            f"{'-':>6}" if s == r else f"{matrix[(s, r)] * 100:5.1f}%" for r in range(count)))


def load_logs(out, name, count):
    logs = []
    for index in range(count):
        folder = Path(out) / f"{name}_{index}"
        sends, onsets, deferred = [], {}, []
        recvs, blackout = [], 0
        tx = folder / f"tx_{index}.csv"
        rx = folder / f"rx_{index}.csv"
        if tx.is_file():
            for line in tx.read_text().splitlines():
                p = line.split(",")
                if p[0] == "S":
                    # t, class, kind, seq, copy, bytes, encode_ns, hash
                    sends.append((int(p[1]), p[2], int(p[3]), int(p[4]), int(p[5]), int(p[6]), int(p[7]), p[8]))
                elif p[0] == "E":
                    onsets[int(p[2])] = (int(p[1]), int(p[3]) if len(p) > 3 else 0)
                elif p[0] == "D":
                    deferred.append((int(p[1]), p[2], int(p[3]), int(p[4])))
        if rx.is_file():
            for line in rx.read_text().splitlines():
                p = line.split(",")
                if p[0] == "R":
                    # t, sender, owner, kind, seq, bytes, decode_ns, ok, hash
                    recvs.append((int(p[1]), int(p[2]), int(p[3]), int(p[4]), int(p[5]), int(p[6]), int(p[7]),
                                  int(p[8]), p[9]))
                elif p[0] == "B":
                    blackout += 1
        logs.append({"sends": sends, "onsets": onsets, "deferred": deferred, "recvs": recvs,
                     "blackout": blackout})
    return logs


def percentile(values, q):
    if not values:
        return float("nan")
    ordered = sorted(values)
    position = (len(ordered) - 1) * q
    low = math.floor(position)
    high = math.ceil(position)
    return ordered[low] + (ordered[high] - ordered[low]) * (position - low)


class PairClock:
    """Receiver clock minus sender clock, as a line in the sender's time, from minimum delays.

    Each phone's log counts from its own process start. For a pair, the smallest observed
    (receive - send) in each direction bounds the offset; assuming the fastest path is
    equally fast both ways, offset = (min_ab - min_ba) / 2 per time bin, fitted to a line
    so clock drift over the run is taken out too.
    """

    def __init__(self, ab, ba, bins=6):
        # ab: (send_t in a's clock, recv_t - send_t) for frames a -> b; ba likewise b -> a.
        self.samples = len(ab) + len(ba)
        if not ab or not ba:
            self.a, self.b, self.min_delay_ns = 0.0, 0.0, float("nan")
            self.ok = False
            return
        start = min(ab[0][0], ba[0][0])
        end = max(ab[-1][0], ba[-1][0])
        width = max((end - start) / bins, 1)
        points = []
        delays = []
        for k in range(bins):
            lo, hi = start + k * width, start + (k + 1) * width
            m1 = [d for t, d in ab if lo <= t < hi]
            m2 = [d for t, d in ba if lo <= t < hi]
            if m1 and m2:
                points.append(((lo + hi) / 2, (min(m1) - min(m2)) / 2))
                delays.append((min(m1) + min(m2)) / 2)
        if not points:
            points = [((start + end) / 2, (min(d for _, d in ab) - min(d for _, d in ba)) / 2)]
            delays = [(min(d for _, d in ab) + min(d for _, d in ba)) / 2]
        if len(points) >= 2:
            mt = statistics.fmean(t for t, _ in points)
            mo = statistics.fmean(o for _, o in points)
            var = sum((t - mt) ** 2 for t, _ in points)
            self.b = sum((t - mt) * (o - mo) for t, o in points) / var if var else 0.0
            self.a = mo - self.b * mt
        else:
            self.a, self.b = points[0][1], 0.0
        self.min_delay_ns = statistics.fmean(delays)
        self.ok = True

    def offset(self, t):
        return self.a + self.b * t


def analyze(out, name):
    out = Path(out)
    meta = json.loads((out / f"{name}_meta.json").read_text())
    phones = meta["phones"]
    count = len(phones)
    logs = load_logs(out, name, count)
    warmup_ns = int(meta["warmup_s"] * 1e9)
    end_ns = int(meta["duration_s"] * 1e9)
    window = (warmup_ns + 1_000_000_000, end_ns - 2_000_000_000)

    # Frame times by sender and hash; unique frames (state, summaries) set the clocks.
    send_index = []
    for s in range(count):
        by_hash = {}
        for row in logs[s]["sends"]:
            by_hash.setdefault(row[7], []).append(row)
        send_index.append(by_hash)

    def matched(s, r, kinds):
        """(send_t, recv_t, send_row) for frames s sent and r heard, first arrival of each send."""
        pairs = []
        seen = set()
        for (t, snd, owner, kind, seq, size, dec, ok, h) in logs[r]["recvs"]:
            if snd != s or kind not in kinds:
                continue
            rows = send_index[s].get(h)
            if not rows or len(rows) != 1 or h in seen:
                continue
            seen.add(h)
            pairs.append((rows[0][0], t, rows[0]))
        return pairs

    clocks = {}
    for s in range(count):
        for r in range(s + 1, count):
            ab = sorted((st, rt - st) for st, rt, _ in matched(s, r, {1, 2}))
            ba = sorted((st, rt - st) for st, rt, _ in matched(r, s, {1, 2}))
            clock = PairClock(ab, ba)
            clocks[(s, r)] = clock
            clocks[(r, s)] = ("inverse", clock)

    def offset(s, r, t_s):
        """Receiver clock minus sender clock at sender time t_s."""
        clock = clocks[(s, r)]
        if isinstance(clock, tuple):
            # clock was fitted for r -> s in r's time; invert: offset_sr(t) = -offset_rs(t + offset_sr)
            inner = clock[1]
            guess = -inner.offset(t_s)
            return -inner.offset(t_s + guess)
        return clock.offset(t_s)

    report = {"phones": phones, "meta": {k: v for k, v in meta.items() if k not in ("phones", "stdout")},
              "pairs": [], "events": [], "state": [], "catchup": [], "ledger": [], "codec": []}

    # Clocks.
    for s in range(count):
        for r in range(s + 1, count):
            clock = clocks[(s, r)]
            report["pairs"].append({"a": s, "b": r, "ok": clock.ok, "samples": clock.samples,
                                    "min_one_way_ms": clock.min_delay_ns / 1e6,
                                    "drift_ppm": clock.b * 1e6})

    # State: loss, one-way delay and age at each receiver.
    for s in range(count):
        state_sends = [row for row in logs[s]["sends"] if row[2] == 1 and window[0] <= row[0] <= window[1]]
        sent_hashes = {row[7] for row in state_sends}
        for r in range(count):
            if r == s:
                continue
            pairs = matched(s, r, {1})
            heard = [(st, rt) for st, rt, row in pairs if row[7] in sent_hashes]
            delays = [(rt - st - offset(s, r, st)) / 1e6 for st, rt in heard]
            # Age: at each 10 ms step of r's clock, now minus the send time of the newest state heard.
            arrivals = sorted((rt, st + offset(s, r, st)) for st, rt, _ in pairs)
            ages = []
            newest = None
            index = 0
            lo = window[0] + offset(s, r, window[0])
            hi = window[1] + offset(s, r, window[1])
            step = 10_000_000
            t = lo
            while t <= hi:
                while index < len(arrivals) and arrivals[index][0] <= t:
                    newest = arrivals[index][1] if newest is None else max(newest, arrivals[index][1])
                    index += 1
                if newest is not None:
                    ages.append((t - newest) / 1e6)
                t += step
            report["state"].append({
                "sender": s, "receiver": r, "sent": len(state_sends), "heard": len(heard),
                "loss_pct": 100 * (1 - len(heard) / len(state_sends)) if state_sends else float("nan"),
                "delay_ms_p50": percentile(delays, 0.5), "delay_ms_p95": percentile(delays, 0.95),
                "delay_ms_max": max(delays) if delays else float("nan"),
                "age_ms_p50": percentile(ages, 0.5), "age_ms_p95": percentile(ages, 0.95),
                "age_ms_max": max(ages) if ages else float("nan"),
            })

    # Urgent events: first arrival against onset, copies heard, repair.
    for s in range(count):
        onsets = {seq: on for seq, on in logs[s]["onsets"].items() if window[0] <= on[0] <= window[1]}
        copy_sends = {}
        repair_sends = {}
        for row in logs[s]["sends"]:
            if row[2] == 0 and row[3] < CATCHUP_BASE:
                (repair_sends if row[4] == 99 else copy_sends).setdefault(row[3], []).append(row[0])
        for r in range(count):
            if r == s:
                continue
            arrivals = {}
            for (t, snd, owner, kind, seq, size, dec, ok, h) in logs[r]["recvs"]:
                if owner == s and kind == 0 and seq in onsets:
                    arrivals.setdefault(seq, []).append(t)
            on_time = 0
            latencies = []
            by_copies = 0
            repaired = []
            unrecovered = 0
            copies_heard = 0
            copies_possible = 0
            for seq, (onset, _) in onsets.items():
                times = sorted(arrivals.get(seq, []))
                first_repair = min(repair_sends.get(seq, [float("inf")]))
                copies_possible += len(copy_sends.get(seq, []))
                if not times:
                    unrecovered += 1
                    continue
                corrected = [t - offset(s, r, onset) for t in times]
                # Corrected to the sender's clock; anything heard before the first resend left came by a copy.
                via_copies = [c for c in corrected if c < first_repair]
                copies_heard += min(len(via_copies), len(copy_sends.get(seq, [])))
                latency_ms = (corrected[0] - onset) / 1e6
                if via_copies:
                    by_copies += 1
                    latencies.append(latency_ms)
                    if latency_ms <= NETWORK_BUDGET_MS:
                        on_time += 1
                else:
                    repaired.append(latency_ms)
            report["events"].append({
                "sender": s, "receiver": r, "events": len(onsets), "by_copies": by_copies,
                "on_time": on_time, "on_time_pct": 100 * on_time / len(onsets) if onsets else float("nan"),
                "latency_ms_p50": percentile(latencies, 0.5), "latency_ms_p95": percentile(latencies, 0.95),
                "latency_ms_max": max(latencies) if latencies else float("nan"),
                "copy_delivery_pct": 100 * copies_heard / copies_possible if copies_possible else float("nan"),
                "repaired": len(repaired), "repair_ms_max": max(repaired) if repaired else float("nan"),
                "unrecovered": unrecovered,
            })

    # Catch-up from the leader (id 0).
    leader_items = [row for row in logs[0]["sends"] if row[2] == 0 and row[3] >= CATCHUP_BASE]
    if leader_items:
        first = min(row[0] for row in leader_items)
        for r in range(1, count):
            got = {}
            for (t, snd, owner, kind, seq, size, dec, ok, h) in logs[r]["recvs"]:
                if owner == 0 and kind == 0 and seq >= CATCHUP_BASE and seq not in got:
                    got[seq] = t - offset(0, r, first)
            done = (max(got.values()) - first) / 1e9 if got else float("nan")
            report["catchup"].append({"receiver": r, "items_sent": len(leader_items), "items_heard": len(got),
                                      "complete_s": done})

    # Ledger and errors, from each phone's closing summary.
    for index, rows in enumerate(meta["stdout"]):
        done = next((row for row in rows if row.get("what") == "net_done"), None)
        if done:
            report["ledger"].append({"id": index, **{k: done[k] for k in (
                "ledger_max_rolling_bits", "ledger_violations", "deferred", "sent", "send_failures",
                "decode_failures", "events_created", "inputs_delayed", "blackout_drops")}})

    # L6 codec cost inside the live run.
    for index in range(count):
        encode = {}
        for row in logs[index]["sends"]:
            if row[6] > 0:
                encode.setdefault(row[2], []).append(row[6] / 1000)
        for seq, (t, enc) in logs[index]["onsets"].items():
            if enc > 0:
                encode.setdefault(0, []).append(enc / 1000)
        decode = {}
        for row in logs[index]["recvs"]:
            decode.setdefault(row[3], []).append(row[6] / 1000)
        for kind in sorted(set(encode) | set(decode)):
            if kind < 0:
                continue
            report["codec"].append({"id": index, "kind": KIND_NAMES.get(kind, str(kind)),
                                    "encode_us_p50": percentile(encode.get(kind, []), 0.5),
                                    "encode_us_p99": percentile(encode.get(kind, []), 0.99),
                                    "decode_us_p50": percentile(decode.get(kind, []), 0.5),
                                    "decode_us_p99": percentile(decode.get(kind, []), 0.99)})

    lines = net_report_lines(report)
    add_monitor_lines(lines, phones, monitor_summary(out / "monitor.csv"))
    (out / f"{name}_report.json").write_text(json.dumps(report, indent=1, default=str))
    (out / f"{name}_report.md").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))
    print(f"\nsaved to {out}")
    return report


def net_report_lines(report):
    phones = report["phones"]
    meta = report["meta"]
    name = lambda i: f"{i} {phones[i]['model']}"
    lines = ["# L4 live traffic on the phones", "",
             "Layer 4 behavior (multicast, three urgent copies, the rolling ledger, summary-driven repair) with real "
             "L6 encoding inside; L5 appears only as the leader's catch-up burst.", "",
             f"{len(phones)} phones, {meta['duration_s']} s, avatar state {meta['state_hz']} Hz, summaries "
             f"{meta['summary_hz']} Hz, inputs {meta['input_hz']} per s per phone, group {meta['group']}. "
             f"Measured from {meta['warmup_s'] + 1} s to 2 s before the end. Network budget per urgent event: "
             f"{NETWORK_BUDGET_MS:.0f} ms from input onset to first arrival.", "",
             "## Phones", "", "| Id | Model | Android | ABI | Wi-Fi address |", "|---|---|---|---|---|"]
    for i, p in enumerate(phones):
        lines.append(f"| {i} | {p['model']} | {p['android']} | {p['abi']} | {p['wlan_ip']} |")
    lines += ["", "## Clocks between phones", "",
              "| Pair | Fastest one-way path | Drift | Samples |", "|---|---|---|---|"]
    for pair in report["pairs"]:
        lines.append(f"| {pair['a']}–{pair['b']} | {fmt(pair['min_one_way_ms'], 2)} ms | "
                     f"{fmt(pair['drift_ppm'], 1)} ppm | {pair['samples']} |")
    lines += ["", "## Urgent events (three copies each)", "",
              "| Sender → receiver | Events | On time | Latency P50 / P95 / max | Copies heard | Repaired (worst) | Lost |",
              "|---|---|---|---|---|---|---|"]
    for e in report["events"]:
        lines.append(f"| {name(e['sender'])} → {e['receiver']} | {e['events']} | {fmt(e['on_time_pct'])} % | "
                     f"{fmt(e['latency_ms_p50'])} / {fmt(e['latency_ms_p95'])} / {fmt(e['latency_ms_max'])} ms | "
                     f"{fmt(e['copy_delivery_pct'])} % | {e['repaired']} ({fmt(e['repair_ms_max'], 0)} ms) | "
                     f"{e['unrecovered']} |")
    lines += ["", "## Avatar state", "",
              "| Sender → receiver | Loss | Delay P50 / P95 | Age P50 / P95 / max |", "|---|---|---|---|"]
    for st in report["state"]:
        lines.append(f"| {name(st['sender'])} → {st['receiver']} | {fmt(st['loss_pct'], 2)} % | "
                     f"{fmt(st['delay_ms_p50'])} / {fmt(st['delay_ms_p95'])} ms | "
                     f"{fmt(st['age_ms_p50'])} / {fmt(st['age_ms_p95'])} / {fmt(st['age_ms_max'])} ms |")
    if report["catchup"]:
        lines += ["", "## Catch-up from the leader (phone 0)", "",
                  "| Receiver | Items heard | Complete after |", "|---|---|---|"]
        for c in report["catchup"]:
            lines.append(f"| {name(c['receiver'])} | {c['items_heard']} of {c['items_sent']} | "
                         f"{fmt(c['complete_s'], 2)} s |")
    lines += ["", "## Ledger", "",
              "| Phone | Worst rolling second | Violations | Deferred or dropped | Send failures | Decode failures |",
              "|---|---|---|---|---|---|"]
    for l in report["ledger"]:
        deferred = ", ".join(f"{k} {v}" for k, v in l["deferred"].items()) or "none"
        lines.append(f"| {name(l['id'])} | {l['ledger_max_rolling_bits']:,} bit | {l['ledger_violations']} | "
                     f"{deferred} | {l['send_failures']} | {l['decode_failures']} |")
    lines += ["", "## L6 codec cost inside the run (microseconds)", "",
              "| Phone | Kind | Encode P50 / P99 | Decode P50 / P99 |", "|---|---|---|---|"]
    for c in report["codec"]:
        lines.append(f"| {name(c['id'])} | {c['kind']} | {fmt(c['encode_us_p50'])} / {fmt(c['encode_us_p99'])} | "
                     f"{fmt(c['decode_us_p50'])} / {fmt(c['decode_us_p99'])} |")
    return lines


