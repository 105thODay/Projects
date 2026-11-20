"""Offline analysis of an L4 live traffic run: clocks, urgent events, state, catch-up, ledger, L6 codec cost."""

import json
import math
import statistics
from pathlib import Path

from .adbio import monitor_summary
from .reporting import add_monitor_lines, fmt

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


def load_logs(out, name, count, session=False):
    """Each phone's sends, onsets, deferrals, receptions and L5 notes, by log id (the phone's place in the run).

    In session mode the frames name senders by roster index, which the leader assigns at admission;
    receptions are mapped back to log ids from each phone's own 'admitted' note (the leader holds 0).
    """
    logs = []
    for index in range(count):
        folder = Path(out) / f"{name}_{index}"
        sends, onsets, deferred = [], {}, []
        recvs, blackout, notes = [], 0, []
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
                elif p[0] == "L":
                    notes.append((int(p[1]), p[2], int(p[3]), int(p[4]), int(p[5])))
        if rx.is_file():
            for line in rx.read_text().splitlines():
                p = line.split(",")
                if p[0] == "R":
                    # t, sender, owner, kind, seq, bytes, decode_ns, ok, hash (then disposition, ignored here)
                    recvs.append((int(p[1]), int(p[2]), int(p[3]), int(p[4]), int(p[5]), int(p[6]), int(p[7]),
                                  int(p[8]), p[9]))
                elif p[0] == "B":
                    blackout += 1
        logs.append({"sends": sends, "onsets": onsets, "deferred": deferred, "recvs": recvs,
                     "blackout": blackout, "notes": notes})
    if session:
        index_to_log = {0: 0}
        for log_id, log in enumerate(logs):
            admitted = next((a for (t, what, a, b, c) in log["notes"] if what == "admitted"), None)
            if admitted is not None:
                index_to_log[admitted] = log_id
        mapped = lambda i: index_to_log.get(i, -1) if i >= 0 else i
        for log in logs:
            log["recvs"] = [(t, mapped(snd), mapped(own), kind, seq, size, dec, ok, h)
                            for (t, snd, own, kind, seq, size, dec, ok, h) in log["recvs"]]
    return logs


def presence(log, log_id, session, end_ns):
    """Intervals of the phone's own clock in which it was a member that should hear traffic."""
    if not session:
        return [(0, end_ns)]
    intervals = []
    start = 0 if log_id == 0 else None
    for (t, what, a, b, c) in log["notes"]:
        if what == "admitted" and start is None:
            start = t
        elif what in ("left", "closed", "close_sent") and start is not None:
            intervals.append((start, t))
            start = None
    if start is not None:
        intervals.append((start, end_ns))
    return intervals


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
    session = meta.get("mode") == "session"
    logs = load_logs(out, name, count, session)
    warmup_ns = int(meta["warmup_s"] * 1e9)
    end_ns = int(meta["duration_s"] * 1e9)
    window = (warmup_ns + 1_000_000_000, end_ns - 2_000_000_000)
    present = [presence(logs[i], i, session, end_ns) for i in range(count)]
    MARGIN_NS = 1_000_000_000

    def hears(r, t_r):
        """Whether receiver r was a member from t_r (its own clock) through the next second."""
        return any(lo + MARGIN_NS <= t_r and t_r + MARGIN_NS <= hi for lo, hi in present[r])

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
              "pairs": [], "events": [], "state": [], "session": [], "admissions": [], "ledger": [], "codec": []}

    # Clocks.
    for s in range(count):
        for r in range(s + 1, count):
            clock = clocks[(s, r)]
            report["pairs"].append({"a": s, "b": r, "ok": clock.ok, "samples": clock.samples,
                                    "min_one_way_ms": clock.min_delay_ns / 1e6,
                                    "drift_ppm": clock.b * 1e6})

    # State: loss, one-way delay and age at each receiver.
    for s in range(count):
        all_state = [row for row in logs[s]["sends"] if row[2] == 1 and window[0] <= row[0] <= window[1]]
        for r in range(count):
            if r == s:
                continue
            state_sends = [row for row in all_state if hears(r, row[0] + offset(s, r, row[0]))]
            sent_hashes = {row[7] for row in state_sends}
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
            sender_present = present[s]
            step = 10_000_000
            t = lo
            while t <= hi:
                while index < len(arrivals) and arrivals[index][0] <= t:
                    newest = arrivals[index][1] if newest is None else max(newest, arrivals[index][1])
                    index += 1
                t_s = t - offset(s, r, t)
                sending = any(a + MARGIN_NS <= t_s <= b for a, b in sender_present)
                if newest is not None and hears(r, t) and sending:
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
        all_onsets = {seq: on for seq, on in logs[s]["onsets"].items() if window[0] <= on[0] <= window[1]}
        copy_sends = {}
        repair_sends = {}
        for row in logs[s]["sends"]:
            # Own events only: urgent copies, and resends answering repair (copy 99). A leader's
            # catch-up relays (repair class, copy 0) carry other members' markers.
            if row[2] == 0 and row[1] == "urgent":
                copy_sends.setdefault(row[3], []).append(row[0])
            elif row[2] == 0 and row[4] == 99:
                repair_sends.setdefault(row[3], []).append(row[0])
        for r in range(count):
            if r == s:
                continue
            onsets = {seq: on for seq, on in all_onsets.items() if hears(r, on[0] + offset(s, r, on[0]))}
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
            held = []
            air = []
            for seq, (onset, _) in onsets.items():
                times = sorted(arrivals.get(seq, []))
                first_repair = min(repair_sends.get(seq, [float("inf")]))
                copies_possible += len(copy_sends.get(seq, []))
                first_copy = min(copy_sends.get(seq, [onset]))
                # More than one send opportunity (11.1 ms) between onset and the first copy: the
                # sender held the input for room in the urgent reserve (7 events a second).
                if first_copy - onset > 12_000_000:
                    held.append((first_copy - onset) / 1e6)
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
                    air.append((corrected[0] - first_copy) / 1e6)
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
                "unrecovered": unrecovered, "held": len(held), "held_ms_max": max(held) if held else float("nan"),
                "air_ms_p95": percentile(air, 0.95), "air_ms_max": max(air) if air else float("nan"),
            })

    # L5: admissions, catch-up, readiness, departures, the close, and retained-set agreement.
    if session:
        for log_id in range(count):
            current = None
            for (t, what, a, b, c) in logs[log_id]["notes"]:
                if what == "admitted":
                    current = {"phone": log_id, "admission": 1 + sum(1 for x in report["admissions"] if x["phone"] == log_id),
                               "at_s": t / 1e9, "index": a, "epoch": b, "join_to_admitted_ms": c / 1e6 if c >= 0 else None,
                               "barrier_items": None, "barrier_after_ms": None, "catchup_ms": None,
                               "requests": 0, "ready_ms": None, "ready_how": None}
                    report["admissions"].append(current)
                elif current is None:
                    continue
                elif what == "barrier_heard":
                    current["barrier_items"] = a + b
                    current["barrier_after_ms"] = c / 1e6
                elif what == "catchup_request_sent":
                    current["requests"] += 1
                elif what == "catchup_complete" and current["catchup_ms"] is None:
                    current["catchup_ms"] = a / 1e6
                elif what in ("ready", "ready_by_timeout") and current["ready_ms"] is None:
                    current["ready_ms"] = a / 1e6
                    current["ready_how"] = "timeout" if what == "ready_by_timeout" else "summaries"
        leader_notes = logs[0]["notes"]
        close_at = next((t for (t, what, *_ ) in leader_notes if what == "close_sent"), None)
        for log_id in range(count):
            notes = logs[log_id]["notes"]
            row = {"phone": log_id}
            if close_at is not None and log_id != 0:
                closed = next((t for (t, what, *_ ) in notes if what == "closed"), None)
                row["close_ms"] = (closed - offset(0, log_id, close_at) - close_at) / 1e6 if closed is not None else None
            left = next((t for (t, what, *_ ) in notes if what == "left"), None)
            if left is not None:
                marked = next((t for (t, what, a, *_ ) in leader_notes
                               if what == "departure_marked" and t >= left + offset(log_id, 0, left) - 1e9), None)
                row["departure_marked_ms"] = ((marked - offset(log_id, 0, left) - left) / 1e6
                                              if marked is not None else None)
            report["session"].append(row)

    # Ledger and errors, from each phone's closing summary.    # Ledger and errors, from each phone's closing summary.
    for index, rows in enumerate(meta["stdout"]):
        done = next((row for row in rows if row.get("what") == "net_done"), None)
        if done:
            report["ledger"].append({"id": index, **{k: done.get(k) for k in (
                "ledger_max_rolling_bits", "ledger_violations", "deferred", "sent", "send_failures",
                "decode_failures", "events_created", "inputs_delayed", "blackout_drops",
                "heard_before_admission", "from_non_members", "session")}})

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
    session = meta.get("mode") == "session"
    lines = ["# L4–L5 live traffic on the phones" if session else "# L4 live traffic on the phones", "",
             ("L5 sessions (admission, roster epochs, catch-up, readiness, departure, close) over L4 (multicast, "
              "three urgent copies, the rolling ledger, summary-driven repair), with real L6 encoding inside. "
              "Phone 0 leads. Loss and latency count only frames a phone should have heard, while it was a member."
              if session else
              "L4 behavior (multicast, three urgent copies, the rolling ledger, summary-driven repair) with real "
              "L6 encoding inside; every phone holds a fixed index, with no L5 session."), "",
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
              "Latency runs from input onset to the first arrival. *Held* counts inputs the sender kept back for "
              "room in the urgent reserve (7 events a second); *on the air* runs from the first copy sent to the "
              "first arrival.", "",
              "| Sender → receiver | Events | On time | Latency P50 / P95 / max | Held (worst) | On the air P95 / max | "
              "Copies heard | Repaired (worst) | Lost |",
              "|---|---|---|---|---|---|---|---|---|"]
    for e in report["events"]:
        lines.append(f"| {name(e['sender'])} → {e['receiver']} | {e['events']} | {fmt(e['on_time_pct'])} % | "
                     f"{fmt(e['latency_ms_p50'])} / {fmt(e['latency_ms_p95'])} / {fmt(e['latency_ms_max'])} ms | "
                     f"{e['held']} ({fmt(e['held_ms_max'], 0)} ms) | {fmt(e['air_ms_p95'])} / {fmt(e['air_ms_max'])} ms | "
                     f"{fmt(e['copy_delivery_pct'])} % | {e['repaired']} ({fmt(e['repair_ms_max'], 0)} ms) | "
                     f"{e['unrecovered']} |")
    lines += ["", "## Avatar state", "",
              "| Sender → receiver | Loss | Delay P50 / P95 | Age P50 / P95 / max |", "|---|---|---|---|"]
    for st in report["state"]:
        lines.append(f"| {name(st['sender'])} → {st['receiver']} | {fmt(st['loss_pct'], 2)} % | "
                     f"{fmt(st['delay_ms_p50'])} / {fmt(st['delay_ms_p95'])} ms | "
                     f"{fmt(st['age_ms_p50'])} / {fmt(st['age_ms_p95'])} / {fmt(st['age_ms_max'])} ms |")
    if session:
        lines += ["", "## L5 admissions and catch-up", "",
                  "Times run from admission on the newcomer's own clock; the barrier lists every retained item "
                  "the leader held; requests ask for items the newcomer still lacked after the barrier.", "",
                  "| Phone | Admission | Index (epoch) | Join to admitted | Barrier items (after) | Requests | "
                  "Catch-up complete | Ready (how) |", "|---|---|---|---|---|---|---|---|"]
        for a in report["admissions"]:
            lines.append(f"| {name(a['phone'])} | {a['admission']} at {a['at_s']:.1f} s | {a['index']} ({a['epoch']}) | "
                         f"{fmt(a['join_to_admitted_ms'], 0)} ms | {a['barrier_items'] if a['barrier_items'] is not None else '–'} "
                         f"({fmt(a['barrier_after_ms'], 0)} ms) | {a['requests']} | {fmt(a['catchup_ms'], 0)} ms | "
                         f"{fmt(a['ready_ms'], 0)} ms ({a['ready_how'] or '–'}) |")
        lines += ["", "## L5 retained set, departure and close", "",
                  "Every member should end holding the same retained set (live markers and tombstones); the digest "
                  "is a hash of both lists.", "",
                  "| Phone | State at end | Live / tombstones | Digest | Placed / refused / deletes | "
                  "Departure marked after | Close heard after | Heard before admission / from non-members |",
                  "|---|---|---|---|---|---|---|---|"]
        rows = {r["phone"]: r for r in report["session"]}
        digests = set()
        for l in report["ledger"]:
            ses = l.get("session") or {}
            row = rows.get(l["id"], {})
            if ses.get("state") in ("closed", "ready"):
                digests.add(ses.get("retained_digest"))
            lines.append(
                f"| {name(l['id'])} | {ses.get('state', '–')} | {ses.get('live_markers', '–')} / {ses.get('tombstones', '–')} | "
                f"{(ses.get('retained_digest') or '–')[:8]} | {ses.get('markers_placed', '–')} / {ses.get('markers_refused', '–')} / "
                f"{ses.get('deletes_sent', '–')} | {fmt(row.get('departure_marked_ms'), 0)} ms | "
                f"{fmt(row.get('close_ms'), 0)} ms | {l.get('heard_before_admission', '–')} / {l.get('from_non_members', '–')} |")
        report["retained_agree"] = len(digests) == 1
        lines += ["", f"**Retained sets {'agree' if len(digests) == 1 else 'DIFFER'}** across the "
                      f"{sum(1 for l in report['ledger'] if (l.get('session') or {}).get('state') in ('closed', 'ready'))} "
                      "phones still in the session at the end."]
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


