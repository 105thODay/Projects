#!/usr/bin/env python3
"""Android test harness for the ar_sim codec and the Network Layer ICD traffic profile.

Runs on the host the phones connect to (Wi-Fi adb or USB), with Python 3.8+ and adb on PATH; standard
library only. Plug in any number of phones; `qualify` decides what each is fit to test and records it
in the fleet inventory, and every later command uses the phones qualified for it.

  python harness.py devices                 every phone adb sees, with its tier from the inventory
  python harness.py wifi                    switch USB-connected phones to adb over Wi-Fi
  python harness.py fetch --out build       download the CI-built binaries and helper app (needs gh)
  python harness.py qualify --artifacts build --corpus ../corpus_ref/glibc_corpus.zip
  python harness.py fleet                   print the fleet inventory
  python harness.py gate                    golden-corpus gate on every codec-qualified phone
  python harness.py bench                   full codec benchmark on every codec-qualified phone
  python harness.py net                     live multicast sessions of up to 8 phones, then analysis
  python harness.py analyze ar_results/<run>/session_1
  python harness.py all --artifacts build --corpus ../corpus_ref/glibc_corpus.zip

Results land in ar_results/<UTC time>_<command>/ (report.md, report.json, raw output, monitor.csv);
the fleet inventory is ar_results/inventory.json with ar_results/fleet.md beside it. Thresholds for
qualification come from qualify_config.json next to this script.
"""

import argparse
import gzip
import hashlib
import json
import math
import shutil
import subprocess
import sys
import time
import zipfile
from pathlib import Path

from arh.adbio import (REMOTE, RESULTS, Monitor, Phone, adb, json_lines, list_adb, new_run_dir, parallel,
                       parallel_soft, shell, start_helper, monitor_summary)
from arh.netanalysis import analyze, print_matrix, reachability
from arh.netrun import run_session
from arh.qualify import FLEET, TIER_RANK, fleet_lines, load_config, load_inventory, run_qualification, select_phones
from arh.reporting import add_monitor_lines, finish

HERE = Path(__file__).resolve().parent
CORPUS_FILES = ["messages.jsonl", "sim_messages.jsonl", "flips.jsonl"]
SESSION_LIMIT = 8  # players per session, ruled 17 November 2026


def online_phones(selected=None):
    listing = list_adb()
    serials = [s for s, state in listing if state == "device"]
    if selected:
        missing = [s for s in selected if s not in serials]
        if missing:
            sys.exit(f"not online: {', '.join(missing)}")
        serials = selected
    outcomes = parallel_soft(lambda i, s: Phone(s), serials)
    phones = []
    for serial, (phone, error) in zip(serials, outcomes):
        if error:
            print(f"{serial}: could not probe ({error})")
        else:
            phones.append(phone)
    return listing, sorted(phones, key=lambda p: p.serial)


def require(phones, what):
    if not phones:
        sys.exit(f"no phones qualified for {what}: run `python harness.py qualify` first, or pass --all")
    return phones


# ----------------------------------------------------------------------------- commands

def cmd_devices(args):
    listing, phones = online_phones(args.serial)
    inventory = load_inventory()
    for serial, state in listing:
        if state != "device":
            print(json.dumps({"serial": serial, "state": state}))
    for phone in phones:
        latest = inventory.get(phone.phone_id, {}).get("latest", {})
        print(json.dumps({**phone.describe(), "tier": latest.get("tier", "not qualified")}))


def cmd_wifi(args):
    """Moves each USB-connected phone to adb over Wi-Fi on port 5555."""
    for serial, state in list_adb():
        if state != "device":
            print(f"{serial}: {state}; skipped")
            continue
        if ":" in serial:
            print(f"{serial}: already over the network")
            continue
        ip = Phone(serial).ip
        if not ip:
            print(f"{serial}: no wlan0 address; join the test Wi-Fi network first")
            continue
        adb(serial, "tcpip", "5555")
        time.sleep(2)
        print(adb(None, "connect", f"{ip}:5555").strip())
    print("Unplug the USB cables once every phone shows as <ip>:5555 in `adb devices`.")
    print("Android 11+ can pair without USB: Developer options > Wireless debugging > Pair device,")
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
    for line in sums.read_text().splitlines():
        digest, name = line.split()
        actual = hashlib.sha256((Path(folder) / name).read_bytes()).hexdigest()
        if actual != digest:
            sys.exit(f"{name}: checksum mismatch")
    print("artifact checksums match")


def prepare_corpus(corpus_zip):
    """Repacks the golden corpus zip into the gzip files the phones read (about 7 MB)."""
    cache = RESULTS / "corpus_cache"
    cache.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(corpus_zip) as archive:
        for name in CORPUS_FILES:
            target = cache / f"{name}.gz"
            if not target.is_file():
                with archive.open(name) as source, gzip.open(target, "wb", compresslevel=6) as sink:
                    shutil.copyfileobj(source, sink)
    return [cache / f"{name}.gz" for name in CORPUS_FILES]


def net_params(args, **overrides):
    params = {"group": args.group, "duration": args.duration, "warmup": args.warmup, "state_hz": args.state_hz,
              "summary_hz": args.summary_hz, "input_hz": args.input_hz, "catchup_at": args.catchup_at,
              "blackout_ms": args.blackout_ms, "seed": args.seed, "loop": args.loop}
    params.update(overrides)
    return params


def cmd_qualify(args):
    artifacts = Path(args.artifacts)
    verify_artifacts(artifacts)
    corpus = prepare_corpus(Path(args.corpus)) if args.corpus else []
    if not corpus:
        print("no --corpus given: the gate will not run, so no phone can reach the codec or product tier "
              "on correctness evidence")
    config = load_config(args.config)
    listing, phones = online_phones(args.serial)
    if not phones:
        sys.exit("adb sees no usable phones")
    out = new_run_dir("qualify")
    print(f"qualifying {len(phones)} phones; this takes a few minutes per phone, all at once")
    check_params = net_params(args, duration=8, warmup=2, input_hz=0, catchup_at=-1, blackout_ms=0)
    records, lines = run_qualification(listing, phones, artifacts, corpus, artifacts / "ar_mcastlock.apk", config,
                                       out, check_params, args.monitor_interval)
    report = {"config": config, "records": [r.as_dict() for r in records],
              "not_checked": [{"serial": s, "state": st} for s, st in listing if st != "device"]}
    finish(out, report, lines)


def cmd_fleet(args):
    inventory = load_inventory()
    if not inventory:
        sys.exit("the inventory is empty: run `python harness.py qualify` first")
    print("\n".join(fleet_lines(inventory)))
    print(f"\n{len(inventory)} phones; {FLEET}")


def run_everywhere(phones, command, out_dir, label, timeout):
    def run_one(index, phone):
        text = shell(phone.serial, f"cd {REMOTE} && ./ar_device {command(index, phone)}", timeout=timeout,
                     check=False)
        (out_dir / f"{label}_{index}.jsonl").write_text(text)
        return json_lines(text)
    return parallel(run_one, phones)


def cmd_gate(args):
    _, phones = online_phones(args.serial)
    phones = require(select_phones(phones, "codec", args.all), "the gate")
    out = new_run_dir("gate")
    with Monitor(phones, out, 5):
        results = run_everywhere(phones, lambda i, p: f"gate {REMOTE}/corpus", out, "gate", 3600)
    lines = ["# Golden-corpus gate on the phones", "",
             "| Phone | File | Records | Failures | Seconds | us per record |", "|---|---|---|---|---|---|"]
    for phone, rows in zip(phones, results):
        gate_rows = [r for r in rows if r.get("what") == "gate"]
        if not gate_rows:
            lines.append(f"| {phone.model} | (no output: see gate_*.jsonl) | | | | |")
        for row in gate_rows:
            lines.append(f"| {phone.model} | {row['file']} | {row['records']:,} | {row['failures']} | "
                         f"{row['seconds']:.2f} | {row['us_per_record']:.1f} |")
    monitor = monitor_summary(out / "monitor.csv")
    add_monitor_lines(lines, phones, monitor)
    finish(out, {"phones": [p.describe() for p in phones], "gate": results, "monitor": monitor}, lines)


def cmd_bench(args):
    _, phones = online_phones(args.serial)
    phones = require(select_phones(phones, "codec", args.all), "the benchmark")
    out = new_run_dir("bench")
    command = lambda i, p: f"bench rounds={args.rounds} passes={args.passes} sustain_s={args.sustain}"
    with Monitor(phones, out, 5):
        results = run_everywhere(phones, command, out, "bench", 3600)
    lines = ["# Codec speed on the phones", "",
             f"Microseconds per message, {args.rounds} variants per form and kind, best and median of "
             f"{args.passes} passes; the same messages as `compare_speed` on the desktop.", "",
             "## TAK lean (the ICD baseline wire form)", "",
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
    monitor = monitor_summary(out / "monitor.csv")
    add_monitor_lines(lines, phones, monitor)
    finish(out, {"phones": [p.describe() for p in phones], "bench": results, "monitor": monitor}, lines)


def sessions_of(phones, size):
    """Splits phones into the fewest sessions of at most `size`, as even as possible, mixing models."""
    count = math.ceil(len(phones) / size)
    inventory = load_inventory()
    ordered = sorted(phones, key=lambda p: (TIER_RANK.get(inventory.get(p.phone_id, {}).get("latest", {})
                                                           .get("tier"), 9), p.model, p.serial))
    return [ordered[k::count] for k in range(count)]


def cmd_net(args):
    _, phones = online_phones(args.serial)
    phones = require(select_phones(phones, "net", args.all), "live traffic")
    if len(phones) < 2:
        sys.exit("live traffic needs at least two phones qualified for multicast")
    size = min(args.session_size, SESSION_LIMIT) if not args.allow_large else args.session_size
    sessions = sessions_of(phones, size)
    out = new_run_dir("net")
    held = start_helper(phones)
    for phone in phones:
        if not held.get(phone.serial):
            print(f"{phone.model} ({phone.serial}): helper locks NOT confirmed; open 'ar_sim mcast lock' on it")
    print(f"{len(phones)} phones in {len(sessions)} session(s) of up to {size}, run one after another")
    for number, group in enumerate(sessions, start=1):
        if len(group) < 2:
            print(f"session {number}: one phone only; skipped")
            continue
        folder = out / f"session_{number}"
        folder.mkdir()
        print(f"session {number}: " + ", ".join(f"{i} {p.model}" for i, p in enumerate(group)))
        if not args.skip_preflight:
            run_session(group, folder, "preflight", net_params(args, duration=8, warmup=2, input_hz=0,
                                                               catchup_at=-1, blackout_ms=0), args.monitor_interval)
            matrix = reachability(folder, "preflight", len(group))
            print_matrix(group, matrix)
            worst = min(v for (s, r), v in matrix.items() if s != r)
            if worst < 0.5 and not args.force:
                print(f"session {number}: a phone hears less than half of another's frames; skipped "
                      "(check the helper app and the access point; --force runs anyway)")
                continue
        run_session(group, folder, "main", net_params(args), args.monitor_interval)
        analyze(folder, "main")
    print(f"\nsaved to {out}")


def cmd_analyze(args):
    analyze(Path(args.run_dir), args.name)


def cmd_all(args):
    cmd_qualify(args)
    cmd_bench(args)
    cmd_net(args)


# ----------------------------------------------------------------------------- entry

def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--serial", action="append", help="limit to these adb serials (repeatable)")
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("devices").set_defaults(func=cmd_devices)
    sub.add_parser("wifi").set_defaults(func=cmd_wifi)
    sub.add_parser("fleet").set_defaults(func=cmd_fleet)

    fetch = sub.add_parser("fetch")
    fetch.add_argument("--out", default="build")
    fetch.add_argument("--repo", default="105thODay/Projects")
    fetch.add_argument("--branch", default="ar-sim-rust-port")
    fetch.add_argument("--run-id")
    fetch.set_defaults(func=cmd_fetch)

    def add_qualify(p):
        p.add_argument("--artifacts", default="build", help="folder with ar_device-armv7, ar_device-aarch64, ar_mcastlock.apk")
        p.add_argument("--corpus", help="path to corpus_ref/glibc_corpus.zip (the gate needs it)")
        p.add_argument("--config", default=str(HERE / "qualify_config.json"))

    def add_select(p):
        p.add_argument("--all", action="store_true", help="use every online phone, qualified or not")

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
        p.add_argument("--session-size", type=int, default=SESSION_LIMIT, help="phones per session (at most 8)")
        p.add_argument("--allow-large", action="store_true", help="allow sessions above the 8-player limit")
        p.add_argument("--skip-preflight", action="store_true")
        p.add_argument("--force", action="store_true", help="run a session even if its preflight finds poor reachability")
        p.add_argument("--loop", action="store_true", help=argparse.SUPPRESS)

    def add_monitor(p):
        p.add_argument("--monitor-interval", type=float, default=15, help="seconds between monitor samples")

    qualify = sub.add_parser("qualify")
    add_qualify(qualify)
    add_net(qualify)
    add_monitor(qualify)
    qualify.set_defaults(func=cmd_qualify)
    gate = sub.add_parser("gate")
    add_select(gate)
    gate.set_defaults(func=cmd_gate)
    bench = sub.add_parser("bench")
    add_select(bench)
    add_bench(bench)
    bench.set_defaults(func=cmd_bench)
    net = sub.add_parser("net")
    add_select(net)
    add_net(net)
    add_monitor(net)
    net.set_defaults(func=cmd_net)
    analyze_parser = sub.add_parser("analyze")
    analyze_parser.add_argument("run_dir")
    analyze_parser.add_argument("--name", default="main")
    analyze_parser.set_defaults(func=cmd_analyze)
    everything = sub.add_parser("all")
    add_qualify(everything)
    add_select(everything)
    add_bench(everything)
    add_net(everything)
    add_monitor(everything)
    everything.set_defaults(func=cmd_all)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
