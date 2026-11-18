#!/usr/bin/env python3
"""Android test harness for the ar_sim codec and the Network Layer ICD traffic profile.

Runs on the host the phones are connected to (over Wi-Fi adb, or USB), with Python 3.8+ and
adb on PATH; standard library only. Each phone runs one static `ar_device` binary from
/data/local/tmp/ar, started over adb shell, plus the small helper app that holds the Wi-Fi
multicast lock.

  python harness.py devices                      list the phones adb sees
  python harness.py wifi                         switch USB-connected phones to adb over Wi-Fi
  python harness.py fetch --out build            download the CI-built binaries and app (needs gh)
  python harness.py setup --artifacts build --corpus ../corpus_ref/glibc_corpus.zip
  python harness.py gate                         golden-corpus gate on every phone
  python harness.py bench                        encode/decode speed on every phone
  python harness.py net                          live multicast traffic among all phones, then analysis
  python harness.py analyze ar_results/<run>     re-run the analysis of a saved net run
  python harness.py all --artifacts build --corpus ../corpus_ref/glibc_corpus.zip

Results land in ar_results/<UTC time>_<command>/: raw device output, monitor samples
(battery temperature and CPU clocks), the pulled logs, and report.md / report.json.
"""

import argparse
import bisect
import gzip
import json
import math
import os
import re
import shutil
import statistics
import subprocess
import sys
import threading
import time
import zipfile
from datetime import datetime, timezone
from pathlib import Path

REMOTE = "/data/local/tmp/ar"
HELPER_PACKAGE = "org.arsim.mcastlock"
HELPER_ACTIVITY = HELPER_PACKAGE + "/.MainActivity"
BINARY_FOR_ABI = {
    "arm64-v8a": ["ar_device-aarch64", "ar_device-armv7"],
    "armeabi-v7a": ["ar_device-armv7"],
    "armeabi": ["ar_device-armv7"],
}
CORPUS_FILES = ["messages.jsonl", "sim_messages.jsonl", "flips.jsonl"]
CATCHUP_BASE = 1_000_000
NETWORK_BUDGET_MS = 59.0  # 100 ms less 10 input, 30 display, 1 uncertainty (requirements, section 02)
KIND_NAMES = {0: "event", 1: "state", 2: "summary", 3: "repair_request", 4: "repair_status", 5: "calibration"}

ADB = os.environ.get("ADB", "adb")


# ----------------------------------------------------------------------------- adb plumbing

def adb(serial, *args, timeout=120, check=True):
    command = [ADB] + (["-s", serial] if serial else []) + list(args)
    result = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
    if check and result.returncode != 0:
        raise RuntimeError(f"{' '.join(command)} failed: {result.stderr.strip() or result.stdout.strip()}")
    return result.stdout


def shell(serial, command, timeout=120, check=True):
    return adb(serial, "shell", command, timeout=timeout, check=check)


def json_lines(text):
    """The JSON objects in a device's output; other lines are ignored."""
    objects = []
    for line in text.splitlines():
        line = line.strip()
        if line.startswith("{"):
            try:
                objects.append(json.loads(line))
            except json.JSONDecodeError:
                pass
    return objects


class Device:
    def __init__(self, serial):
        self.serial = serial
        props = shell(serial, "getprop ro.product.model; getprop ro.build.version.release; "
                              "getprop ro.build.version.sdk; getprop ro.product.cpu.abi").splitlines()
        props = [p.strip() for p in props] + [""] * 4
        self.model, self.release, self.sdk, self.abi = props[:4]
        self.ip = wlan_ip(serial)

    def describe(self):
        return {"serial": self.serial, "model": self.model, "android": self.release, "sdk": self.sdk,
                "abi": self.abi, "wlan_ip": self.ip}


def wlan_ip(serial):
    text = shell(serial, "ip -4 -o addr show wlan0 2>/dev/null || ifconfig wlan0", check=False)
    match = re.search(r"inet (?:addr:)?(\d+\.\d+\.\d+\.\d+)", text)
    return match.group(1) if match else ""


def online_serials():
    serials = []
    for line in adb(None, "devices").splitlines()[1:]:
        parts = line.split()
        if len(parts) >= 2 and parts[1] == "device":
            serials.append(parts[0])
    return serials


def devices(selected=None):
    serials = online_serials()
    if selected:
        missing = [s for s in selected if s not in serials]
        if missing:
            sys.exit(f"not online: {', '.join(missing)}")
        serials = selected
    if not serials:
        sys.exit("adb sees no phones: connect them (python harness.py wifi), then retry")
    found = [Device(s) for s in serials]
    # Stable ids: sorted by serial, so a rerun gives each phone the same id.
    return sorted(found, key=lambda d: d.serial)


def parallel(function, items):
    """Runs function(item) on a thread per item; returns results in order, re-raising errors."""
    results = [None] * len(items)
    errors = [None] * len(items)

    def work(index, item):
        try:
            results[index] = function(index, item)
        except Exception as error:  # reported after every thread finishes
            errors[index] = error

    threads = [threading.Thread(target=work, args=(i, item)) for i, item in enumerate(items)]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    for error in errors:
        if error:
            raise error
    return results


def new_run_dir(command):
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    path = Path("ar_results") / f"{stamp}_{command}"
    path.mkdir(parents=True, exist_ok=False)
    return path


# ----------------------------------------------------------------------------- monitoring

class Monitor:
    """Samples battery temperature and CPU clocks on every phone while a test runs."""

    def __init__(self, phones, out_dir, interval_s):
        self.phones = phones
        self.path = out_dir / "monitor.csv"
        self.interval = interval_s
        self.stop_event = threading.Event()
        self.thread = threading.Thread(target=self.loop, daemon=True)

    def sample(self, phone):
        text = shell(phone.serial,
                     "dumpsys battery | grep -i temperature; "
                     "cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_cur_freq 2>/dev/null",
                     timeout=20, check=False)
        temperature = ""
        freqs = []
        for line in text.splitlines():
            line = line.strip()
            if "temperature" in line.lower():
                digits = re.findall(r"-?\d+", line)
                if digits:
                    temperature = f"{int(digits[0]) / 10:.1f}"
            elif line.isdigit():
                freqs.append(str(int(line) // 1000))
        return temperature, "/".join(freqs)

    def loop(self):
        with open(self.path, "w") as out:
            out.write("utc,serial,battery_c,cpu_mhz\n")
            while not self.stop_event.is_set():
                stamp = datetime.now(timezone.utc).isoformat(timespec="seconds")
                for phone in self.phones:
                    try:
                        temperature, freqs = self.sample(phone)
                    except Exception:
                        temperature, freqs = "", ""
                    out.write(f"{stamp},{phone.serial},{temperature},{freqs}\n")
                out.flush()
                self.stop_event.wait(self.interval)

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, *exc):
        self.stop_event.set()
        self.thread.join(timeout=30)


def monitor_summary(path):
    """Per phone: battery temperature first and last, and the highest CPU clock seen first and last."""
    rows = {}
    if not Path(path).is_file():
        return rows
    for line in Path(path).read_text().splitlines()[1:]:
        parts = line.split(",")
        if len(parts) < 4 or not parts[2]:
            continue
        freqs = [int(f) for f in parts[3].split("/") if f]
        rows.setdefault(parts[1], []).append((float(parts[2]), max(freqs) if freqs else 0))
    return {serial: {"battery_c_first": s[0][0], "battery_c_last": s[-1][0],
                     "battery_c_max": max(x[0] for x in s),
                     "cpu_max_mhz_first": s[0][1], "cpu_max_mhz_last": s[-1][1]}
            for serial, s in rows.items()}


# ----------------------------------------------------------------------------- commands

def cmd_devices(args):
    for phone in devices(args.serial):
        print(json.dumps(phone.describe()))


def cmd_wifi(args):
    """Moves each USB-connected phone to adb over Wi-Fi on port 5555."""
    for serial in online_serials():
        if ":" in serial:
            print(f"{serial}: already over the network")
            continue
        ip = wlan_ip(serial)
        if not ip:
            print(f"{serial}: no wlan0 address; join the test Wi-Fi network first")
            continue
        adb(serial, "tcpip", "5555")
        time.sleep(2)
        print(adb(None, "connect", f"{ip}:5555").strip())
    print("Unplug the USB cables once every phone shows as <ip>:5555 in `adb devices`.")
    print("Android 11+ can also pair without USB: Developer options > Wireless debugging > Pair,")
    print("then `adb pair <ip>:<pairing port>` and `adb connect <ip>:<port>`.")


def cmd_fetch(args):
    """Downloads the newest successful CI build of the Android artifacts with the gh CLI."""
    if not shutil.which("gh"):
        sys.exit("gh is not installed: download the ar_android artifact from the Actions page instead")
    run_id = args.run_id
    if not run_id:
        listing = subprocess.run(["gh", "run", "list", "--repo", args.repo, "--workflow", "android_harness.yml",
                                  "--branch", args.branch, "--status", "success", "--limit", "1",
                                  "--json", "databaseId"], capture_output=True, text=True, check=True)
        runs = json.loads(listing.stdout)
        if not runs:
            sys.exit("no successful android_harness.yml run on that branch yet")
        run_id = str(runs[0]["databaseId"])
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    subprocess.run(["gh", "run", "download", run_id, "--repo", args.repo, "--name", "ar_android",
                    "--dir", str(out)], check=True)
    print(f"downloaded run {run_id} to {out}")
    verify_artifacts(out)


def verify_artifacts(folder):
    sums = Path(folder) / "SHA256SUMS"
    if not sums.is_file():
        print("no SHA256SUMS beside the artifacts; skipping the check")
        return
    import hashlib
    for line in sums.read_text().splitlines():
        digest, name = line.split()
        actual = hashlib.sha256((Path(folder) / name).read_bytes()).hexdigest()
        if actual != digest:
            sys.exit(f"{name}: checksum mismatch")
    print("artifact checksums match")


def prepare_corpus(corpus_zip, cache):
    """Repacks the golden corpus zip into the gzip files the phones read (about 7 MB)."""
    cache.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(corpus_zip) as archive:
        for name in CORPUS_FILES:
            target = cache / f"{name}.gz"
            if target.is_file():
                continue
            with archive.open(name) as source, gzip.open(target, "wb", compresslevel=6) as sink:
                shutil.copyfileobj(source, sink)
    return [cache / f"{name}.gz" for name in CORPUS_FILES]


def cmd_setup(args):
    artifacts = Path(args.artifacts)
    verify_artifacts(artifacts)
    corpus = prepare_corpus(Path(args.corpus), Path("ar_results") / "corpus_cache") if args.corpus else []
    apk = artifacts / "ar_mcastlock.apk"
    phones = devices(args.serial)

    def setup_one(index, phone):
        candidates = [artifacts / name for name in BINARY_FOR_ABI.get(phone.abi, [])]
        binary = next((path for path in candidates if path.is_file()), None)
        if binary is None:
            raise RuntimeError(f"{phone.serial}: no binary for ABI {phone.abi!r} in {artifacts}")
        shell(phone.serial, f"mkdir -p {REMOTE}/corpus")
        adb(phone.serial, "push", str(binary), f"{REMOTE}/ar_device", timeout=300)
        shell(phone.serial, f"chmod 755 {REMOTE}/ar_device")
        for path in corpus:
            adb(phone.serial, "push", str(path), f"{REMOTE}/corpus/{path.name}", timeout=600)
        if apk.is_file():
            adb(phone.serial, "uninstall", HELPER_PACKAGE, check=False)
            adb(phone.serial, "install", str(apk), timeout=300)
        info = json_lines(shell(phone.serial, f"{REMOTE}/ar_device info"))
        return {**phone.describe(), "binary": binary.name, "info": info[0] if info else None}

    for line in parallel(setup_one, phones):
        print(json.dumps(line))
    start_helper(phones)


def start_helper(phones):
    """Opens the helper app on every phone, keeps the screen on, and confirms the locks."""
    for phone in phones:
        shell(phone.serial, "svc power stayon true", check=False)
        shell(phone.serial, "input keyevent KEYCODE_WAKEUP", check=False)
        shell(phone.serial, "logcat -c", check=False)
        shell(phone.serial, f"am start -n {HELPER_ACTIVITY}", check=False)
    time.sleep(3)
    for phone in phones:
        log = shell(phone.serial, "logcat -d -s ar_sim:I", check=False)
        held = "ar_sim locks held" in log
        print(f"{phone.serial} ({phone.model}): helper locks {'held' if held else 'NOT confirmed'}")
        if not held:
            print("  open the 'ar_sim mcast lock' app on that phone by hand and leave it on screen")


def run_everywhere(phones, command, out_dir, label, timeout):
    def run_one(index, phone):
        text = shell(phone.serial, f"cd {REMOTE} && ./ar_device {command(index, phone)}", timeout=timeout,
                     check=False)
        (out_dir / f"{label}_{index}.jsonl").write_text(text)
        return json_lines(text)
    return parallel(run_one, phones)


def cmd_gate(args):
    phones = devices(args.serial)
    out = new_run_dir("gate")
    with Monitor(phones, out, 5):
        results = run_everywhere(phones, lambda i, p: f"gate {REMOTE}/corpus", out, "gate", 1800)
    report = {"phones": [p.describe() for p in phones], "gate": results,
              "monitor": monitor_summary(out / "monitor.csv")}
    lines = ["# Golden-corpus gate on the phones", "",
             "| Phone | File | Records | Failures | Seconds | us per record |", "|---|---|---|---|---|---|"]
    for phone, rows in zip(phones, results):
        gate_rows = [r for r in rows if r.get("what") == "gate"]
        if not gate_rows:
            lines.append(f"| {phone.model} | (no output: see gate_*.jsonl) | | | | |")
        for row in gate_rows:
            lines.append(f"| {phone.model} | {row['file']} | {row['records']:,} | {row['failures']} | "
                         f"{row['seconds']:.2f} | {row['us_per_record']:.1f} |")
    finish(out, report, lines)


def cmd_bench(args):
    phones = devices(args.serial)
    out = new_run_dir("bench")
    command = lambda i, p: f"bench rounds={args.rounds} passes={args.passes} sustain_s={args.sustain}"
    with Monitor(phones, out, 5):
        results = run_everywhere(phones, command, out, "bench", 3600)
    report = {"phones": [p.describe() for p in phones], "bench": results,
              "monitor": monitor_summary(out / "monitor.csv")}
    lines = ["# Codec speed on the phones", "",
             f"Microseconds per message, {args.rounds} variants per form and kind, best and median of "
             f"{args.passes} passes; the same messages as `compare_speed` on the desktop.", ""]
    lines += ["## TAK lean (the ICD baseline wire form)", "",
              "| Phone | Kind | Bytes | Encode best | Encode median | Decode best | Decode median |",
              "|---|---|---|---|---|---|---|"]
    for phone, rows in zip(phones, results):
        for row in rows:
            if row.get("what") == "bench" and row["form"] == "TakLean":
                lines.append(f"| {phone.model} | {row['kind']} | {row['bytes']} | {row['encode_us_best']:.2f} | "
                             f"{row['encode_us_median']:.2f} | {row['decode_us_best']:.2f} | "
                             f"{row['decode_us_median']:.2f} |")
    lines += ["", "## All four wire forms, summed over the six kinds", "",
              "| Phone | Encode sum, best | Decode sum, best | Sustained decode, first to last second |",
              "|---|---|---|---|"]
    for phone, rows in zip(phones, results):
        total = next((r for r in rows if r.get("what") == "bench_total"), None)
        sustain = next((r for r in rows if r.get("what") == "sustain"), None)
        if total:
            rate = ""
            if sustain and sustain["decodes_per_second"]:
                series = sustain["decodes_per_second"]
                rate = f"{series[0]:,} to {series[-1]:,} per s ({(series[-1] / series[0] - 1) * 100:+.0f} %)"
            lines.append(f"| {phone.model} | {total['encode_us_sum_best']:.1f} us | "
                         f"{total['decode_us_sum_best']:.1f} us | {rate} |")
    add_monitor_lines(lines, phones, report["monitor"])
    finish(out, report, lines)


def add_monitor_lines(lines, phones, monitor):
    if not monitor:
        return
    lines += ["", "## Phone condition during the run", "",
              "| Phone | Battery temperature, first to last (max) | Highest CPU clock, first to last |",
              "|---|---|---|"]
    for phone in phones:
        m = monitor.get(phone.serial)
        if m:
            lines.append(f"| {phone.model} | {m['battery_c_first']:.1f} to {m['battery_c_last']:.1f} °C "
                         f"({m['battery_c_max']:.1f}) | {m['cpu_max_mhz_first']} to {m['cpu_max_mhz_last']} MHz |")


def finish(out, report, lines):
    (out / "report.json").write_text(json.dumps(report, indent=1))
    (out / "report.md").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))
    print(f"\nsaved to {out}")


def net_command(args, index, phone, count, out_name, duration, input_hz, catchup_at, blackout_ms):
    words = [
        "net", f"id={index}", f"n={count}", f"group={args.group}", f"iface={phone.ip}",
        f"duration_s={duration}", f"warmup_s={args.warmup}", f"state_hz={args.state_hz}",
        f"summary_hz={args.summary_hz}", f"input_hz={input_hz}", f"catchup_at_s={catchup_at}",
        f"blackout_ms={blackout_ms}", f"seed={args.seed}", f"out={REMOTE}/{out_name}",
    ]
    if args.loop:
        words.append("loop=1")  # host emulation only: every process shares one machine
    return " ".join(words)


def run_net(args, phones, out, name, duration, input_hz, catchup_at, blackout_ms):
    count = len(phones)
    for phone in phones:
        shell(phone.serial, f"rm -rf {REMOTE}/{name}", check=False)
    command = lambda i, p: net_command(args, i, p, count, name, duration, input_hz, catchup_at, blackout_ms)
    with Monitor(phones, out, args.monitor_interval):
        results = run_everywhere(phones, command, out, f"{name}_stdout", duration + 120)
    for index, phone in enumerate(phones):
        target = out / f"{name}_{index}"
        target.mkdir(exist_ok=True)
        for log in (f"tx_{index}.csv", f"rx_{index}.csv"):
            adb(phone.serial, "pull", f"{REMOTE}/{name}/{log}", str(target / log), timeout=300, check=False)
    meta = {"phones": [p.describe() for p in phones], "stdout": results, "name": name,
            "duration_s": duration, "warmup_s": args.warmup, "input_hz": input_hz,
            "state_hz": args.state_hz, "summary_hz": args.summary_hz, "catchup_at_s": catchup_at,
            "blackout_ms": blackout_ms, "group": args.group}
    (out / f"{name}_meta.json").write_text(json.dumps(meta, indent=1))
    return meta


def cmd_net(args):
    phones = devices(args.serial)
    missing = [p.serial for p in phones if not p.ip]
    if missing:
        sys.exit(f"no Wi-Fi address on {', '.join(missing)}")
    subnets = {".".join(p.ip.split(".")[:3]) for p in phones}
    if len(subnets) > 1:
        print(f"warning: the phones sit on different /24 subnets {sorted(subnets)}; multicast may not cross")
    out = new_run_dir("net")
    start_helper(phones)
    if not args.skip_preflight:
        print("preflight: 8 s of state and summaries only, to check every phone hears every other")
        run_net(args, phones, out, "preflight", 8, 0, -1, 0)
        matrix = reachability(out, "preflight", len(phones))
        print_matrix(phones, matrix)
        worst = min((v for (s, r), v in matrix.items() if s != r), default=0)
        if worst < 0.5 and not args.force:
            sys.exit("some phone hears less than half of another's state frames: check that the helper app is "
                     "on screen on every phone and that the access point allows multicast between clients "
                     "(client isolation off); rerun with --force to go on anyway")
    print(f"main run: {args.duration} s")
    run_net(args, phones, out, "main", args.duration, args.input_hz, args.catchup_at, args.blackout_ms)
    analyze(out, "main")


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


def cmd_analyze(args):
    analyze(Path(args.run_dir), args.name)


def cmd_all(args):
    cmd_setup(args)
    cmd_gate(args)
    cmd_bench(args)
    cmd_net(args)


# ----------------------------------------------------------------------------- net analysis

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

    # Codec cost inside the live run.
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
    add_monitor_lines(lines, [type("P", (), {"serial": p["serial"], "model": p["model"]}) for p in phones],
                      monitor_summary(out / "monitor.csv"))
    (out / f"{name}_report.json").write_text(json.dumps(report, indent=1, default=str))
    (out / f"{name}_report.md").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))
    print(f"\nsaved to {out}")
    return report


def fmt(value, digits=1):
    if value is None or (isinstance(value, float) and math.isnan(value)):
        return "–"
    return f"{value:,.{digits}f}"


def net_report_lines(report):
    phones = report["phones"]
    meta = report["meta"]
    name = lambda i: f"{i} {phones[i]['model']}"
    lines = ["# Live multicast traffic on the phones", "",
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
    lines += ["", "## Codec cost inside the run (microseconds)", "",
              "| Phone | Kind | Encode P50 / P99 | Decode P50 / P99 |", "|---|---|---|---|"]
    for c in report["codec"]:
        lines.append(f"| {name(c['id'])} | {c['kind']} | {fmt(c['encode_us_p50'])} / {fmt(c['encode_us_p99'])} | "
                     f"{fmt(c['decode_us_p50'])} / {fmt(c['decode_us_p99'])} |")
    return lines


# ----------------------------------------------------------------------------- entry

def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--serial", action="append", help="limit to these adb serials (repeatable)")
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("devices").set_defaults(func=cmd_devices)
    sub.add_parser("wifi").set_defaults(func=cmd_wifi)

    fetch = sub.add_parser("fetch")
    fetch.add_argument("--out", default="build")
    fetch.add_argument("--repo", default="105thODay/projects")
    fetch.add_argument("--branch", default="ar-sim-rust-port")
    fetch.add_argument("--run-id")
    fetch.set_defaults(func=cmd_fetch)

    def add_setup(p):
        p.add_argument("--artifacts", default="build", help="folder with ar_device-armv7, ar_device-aarch64, ar_mcastlock.apk")
        p.add_argument("--corpus", help="path to corpus_ref/glibc_corpus.zip (needed for gate)")

    def add_bench(p):
        p.add_argument("--rounds", type=int, default=2000)
        p.add_argument("--passes", type=int, default=25)
        p.add_argument("--sustain", type=int, default=20, help="seconds of sustained decoding")

    def add_net(p):
        p.add_argument("--duration", type=float, default=90)
        p.add_argument("--warmup", type=float, default=5)
        p.add_argument("--state-hz", type=float, default=15)
        p.add_argument("--summary-hz", type=float, default=3)
        p.add_argument("--input-hz", type=float, default=1, help="mean urgent inputs per second per phone")
        p.add_argument("--catchup-at", type=float, default=45, help="leader sends the 50-item catch-up at this second; -1 off")
        p.add_argument("--blackout-ms", type=float, default=0, help="emulated receive blackout every 10 s")
        p.add_argument("--group", default="239.255.82.6:18206")
        p.add_argument("--seed", type=int, default=1)
        p.add_argument("--monitor-interval", type=float, default=15, help="seconds between monitor samples")
        p.add_argument("--skip-preflight", action="store_true")
        p.add_argument("--force", action="store_true", help="run even if the preflight finds poor reachability")
        p.add_argument("--loop", action="store_true", help=argparse.SUPPRESS)

    setup = sub.add_parser("setup")
    add_setup(setup)
    setup.set_defaults(func=cmd_setup)
    sub.add_parser("gate").set_defaults(func=cmd_gate)
    bench = sub.add_parser("bench")
    add_bench(bench)
    bench.set_defaults(func=cmd_bench)
    net = sub.add_parser("net")
    add_net(net)
    net.set_defaults(func=cmd_net)
    analyze_parser = sub.add_parser("analyze")
    analyze_parser.add_argument("run_dir")
    analyze_parser.add_argument("--name", default="main")
    analyze_parser.set_defaults(func=cmd_analyze)
    everything = sub.add_parser("all")
    add_setup(everything)
    add_bench(everything)
    add_net(everything)
    everything.set_defaults(func=cmd_all)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
