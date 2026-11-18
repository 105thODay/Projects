"""Qualifying phones: what each one is fit to test, recorded in a fleet inventory that grows run by run.

The networking model spans OSI layers 4 to 7; the harness tests L6 (the CoT codec: conformance and
performance) and L4 (live multicast traffic, with real L6 encoding inside it).

Every phone adb can see goes through the same checks, in order; a hard failure stops that phone
and leaves the others running:

  static     Android API level, a supported ABI, free storage, a Wi-Fi address
  install    the ar_device binary for its ABI, the gzip corpus, the helper app
  runs       ar_device starts and reports itself (catches loader and libc problems)
  clock      the monotonic clock resolves to 1 us or better
  L6 conformance  all 83,647 golden-corpus records reproduce the reference exactly
  L6 performance  a short benchmark: TAK lean encode and decode against the simulator's 250 us
                  allowances, the codec's share of one core at the ICD load, sustained-rate fall-off
  L4 multicast    after every phone is done: one group session in which each phone must hear, and
                  be heard by, at least half of the others' frames (needs the helper app's lock)

Tiers, best first:

  product             fit to stand in for the fielded device: API and ABI at the product floor, the
                      codec inside its allowances, multicast working
  test                fit for the L6 tests, and for L4 live traffic if multicast works
  conformance_failed  the phone ran the codec and got a different answer: an L6 finding on that
                      platform, not a phone fault
  rejected            could not run the tests; the reasons say why

The inventory (ar_results/inventory.json) is keyed by the phone's hardware serial, so a phone keeps its
record whether it connects by USB or over Wi-Fi; fleet.md beside it lists every phone ever qualified.
"""

import json
import time
from datetime import datetime, timezone
from pathlib import Path

from .adbio import (HELPER_PACKAGE, REMOTE, RESULTS, adb, json_lines, parallel_soft, shell, start_helper)
from .netanalysis import reachability
from .netrun import run_session
from .reporting import fmt

INVENTORY = RESULTS / "inventory.json"
FLEET = RESULTS / "fleet.md"
TIER_RANK = {"product": 0, "test": 1, "conformance_failed": 2, "rejected": 3}
BINARY_FOR_ABI = {"arm64-v8a": "ar_device-aarch64", "armeabi-v7a": "ar_device-armv7", "armeabi": "ar_device-armv7"}

DEFAULT_CONFIG = {
    "harness_min_sdk": 23,
    "product_min_sdk": 29,
    "product_abis": ["arm64-v8a"],
    "encode_allowance_us": 250,
    "decode_allowance_us": 250,
    "max_codec_core_pct": 10.0,
    "max_sustain_drop_pct": 20.0,
    "min_free_mb": 64,
    "max_clock_resolution_ns": 1000,
    "multicast_min_share": 0.5,
    "bench": {"rounds": 500, "passes": 9, "sustain_s": 10},
    "load": {"peers": 7, "state_hz": 15, "summary_hz": 3, "input_hz": 1, "copies": 3, "catchup_items_per_s": 5},
}


def load_config(path):
    config = json.loads(json.dumps(DEFAULT_CONFIG))
    if path and Path(path).is_file():
        loaded = json.loads(Path(path).read_text())
        for key, value in loaded.items():
            if key.startswith("_"):
                continue
            if isinstance(value, dict) and isinstance(config.get(key), dict):
                config[key].update(value)
            else:
                config[key] = value
    return config


def load_inventory():
    return json.loads(INVENTORY.read_text()) if INVENTORY.is_file() else {}


def save_inventory(inventory):
    RESULTS.mkdir(exist_ok=True)
    INVENTORY.write_text(json.dumps(inventory, indent=1, default=str))
    FLEET.write_text("\n".join(fleet_lines(inventory)) + "\n")


class Record:
    def __init__(self, phone):
        self.phone = phone
        self.checks = []
        self.tier = None
        self.info = None
        self.gate = []
        self.bench = {}
        self.codec_core_pct = None
        self.sustain_change_pct = None
        self.helper_installed = False
        self.lock_confirmed = False
        self.multicast = None

    def check(self, name, status, detail=""):
        self.checks.append({"check": name, "status": status, "detail": detail})
        return status

    def failed(self):
        return any(c["status"] == "FAIL" for c in self.checks)

    def as_dict(self):
        return {"phone": self.phone.describe(), "qualified_utc": datetime.now(timezone.utc).isoformat(timespec="seconds"),
                "tier": self.tier, "net_ok": bool(self.multicast and self.multicast["status"] == "PASS"),
                "binary_version": (self.info or {}).get("version"), "checks": self.checks, "gate": self.gate,
                "bench": self.bench, "codec_core_pct": self.codec_core_pct,
                "sustain_change_pct": self.sustain_change_pct, "multicast": self.multicast,
                "reasons": [f"{c['check']}: {c['detail']}" for c in self.checks if c["status"] in ("FAIL", "WARN", "INFO")]}


def codec_core_pct(bench, load):
    """The codec's share of one core at the ICD load: every peer's state, summaries and urgent copies
    decoded, plus the catch-up stream, and this phone's own state, summaries and events encoded."""
    def med(kind, which):
        return bench.get(kind, {}).get(f"{which}_us_median")
    needed = [med(k, w) for k in ("Event", "State", "Control") for w in ("encode", "decode")]
    if any(v is None for v in needed):
        return None
    decode_us = (load["peers"] * (load["state_hz"] * med("State", "decode") + load["summary_hz"] * med("Control", "decode")
                                  + load["input_hz"] * load["copies"] * med("Event", "decode"))
                 + load["catchup_items_per_s"] * med("Event", "decode"))
    encode_us = (load["state_hz"] * med("State", "encode") + load["summary_hz"] * med("Control", "encode")
                 + load["input_hz"] * med("Event", "encode"))
    return (decode_us + encode_us) / 1e6 * 100


def qualify_one(phone, artifacts, corpus_files, apk, config):
    r = Record(phone)
    # static
    r.check("api", "PASS" if phone.sdk >= config["harness_min_sdk"] else "FAIL",
            f"API {phone.sdk} (Android {phone.release}); harness floor {config['harness_min_sdk']}")
    binary_name = next((BINARY_FOR_ABI[a] for a in phone.abis if a in BINARY_FOR_ABI), None)
    r.check("abi", "PASS" if binary_name else "FAIL", f"{','.join(phone.abis) or 'unknown'}")
    if phone.free_mb is None:
        r.check("storage", "WARN", "free space unknown")
    else:
        r.check("storage", "PASS" if phone.free_mb >= config["min_free_mb"] else "FAIL",
                f"{phone.free_mb} MB free in /data/local/tmp")
    r.check("wifi", "PASS" if phone.ip else "WARN",
            f"{phone.ip or 'no wlan0 address'}" + (f", {phone.wifi_mhz} MHz" if phone.wifi_mhz else "")
            + (f", {phone.link_mbps} Mbit/s, {phone.rssi} dBm" if phone.link_mbps else ""))
    if r.failed():
        r.tier = "rejected"
        return r
    # install
    binary = Path(artifacts) / binary_name
    if not binary.is_file():
        r.check("install", "FAIL", f"{binary} is missing")
        r.tier = "rejected"
        return r
    try:
        shell(phone.serial, f"mkdir -p {REMOTE}/corpus")
        adb(phone.serial, "push", str(binary), f"{REMOTE}/ar_device", timeout=300)
        shell(phone.serial, f"chmod 755 {REMOTE}/ar_device")
        for path in corpus_files:
            adb(phone.serial, "push", str(path), f"{REMOTE}/corpus/{path.name}", timeout=600)
        r.check("install", "PASS", f"{binary_name} and the corpus")
    except Exception as error:
        r.check("install", "FAIL", str(error)[:200])
        r.tier = "rejected"
        return r
    if apk and Path(apk).is_file():
        adb(phone.serial, "uninstall", HELPER_PACKAGE, check=False)
        try:
            adb(phone.serial, "install", str(apk), timeout=300)
            r.helper_installed = True
            r.check("helper", "PASS", "helper app installed")
        except Exception as error:
            r.check("helper", "WARN", f"helper app did not install, so no live traffic: {str(error)[:160]}")
    else:
        r.check("helper", "WARN", "no helper app in the artifacts, so no live traffic")
    # runs
    output = shell(phone.serial, f"{REMOTE}/ar_device info", check=False)
    info = next((o for o in json_lines(output) if o.get("what") == "info"), None)
    if not info:
        r.check("runs", "FAIL", f"ar_device did not start: {output.strip()[:200]}")
        r.tier = "rejected"
        return r
    r.info = info
    r.check("runs", "PASS", f"{info['arch']}, {info['cpus']} CPUs {info.get('hardware') or ''}".strip())
    resolution = info.get("clock_resolution_ns", 0)
    r.check("clock", "PASS" if resolution <= config["max_clock_resolution_ns"] else "WARN",
            f"monotonic clock steps of {resolution} ns")
    # gate
    if corpus_files:
        gate_rows = [o for o in json_lines(shell(phone.serial, f"cd {REMOTE} && ./ar_device gate {REMOTE}/corpus",
                                                 timeout=3600, check=False)) if o.get("what") == "gate"]
        r.gate = [{k: row[k] for k in ("file", "records", "failures", "seconds", "first_failures")} for row in gate_rows]
        records = sum(row["records"] for row in gate_rows)
        failures = sum(row["failures"] for row in gate_rows)
        if len(gate_rows) < 3:
            r.check("L6 conformance", "FAIL", "the gate did not finish")
            r.tier = "conformance_failed"
            return r
        if failures:
            r.check("L6 conformance", "FAIL", f"{failures} of {records:,} records differ from the reference: an L6 finding")
            r.tier = "conformance_failed"
            return r
        r.check("L6 conformance", "PASS", f"{records:,} records match the reference, {sum(x['seconds'] for x in gate_rows):.1f} s")
    else:
        r.check("L6 conformance", "WARN", "no corpus given, so the gate did not run")
    # speed
    b = config["bench"]
    rows = json_lines(shell(phone.serial, f"cd {REMOTE} && ./ar_device bench rounds={b['rounds']} passes={b['passes']} "
                                          f"sustain_s={b['sustain_s']}", timeout=1800, check=False))
    for row in rows:
        if row.get("what") == "bench" and row["form"] == "TakLean":
            r.bench[row["kind"]] = {k: row[k] for k in ("bytes", "encode_us_best", "encode_us_median",
                                                        "decode_us_best", "decode_us_median")}
    if not r.bench:
        r.check("L6 performance", "FAIL", "the benchmark did not run")
        r.tier = "rejected"
        return r
    worst_encode = max(v["encode_us_median"] for v in r.bench.values())
    worst_decode = max(v["decode_us_median"] for v in r.bench.values())
    inside = worst_encode <= config["encode_allowance_us"] and worst_decode <= config["decode_allowance_us"]
    r.check("L6 allowance", "PASS" if inside else "WARN",
            f"slowest TAK lean kind: encode {worst_encode:.1f} us, decode {worst_decode:.1f} us (median); "
            f"allowances {config['encode_allowance_us']} and {config['decode_allowance_us']} us")
    r.codec_core_pct = codec_core_pct(r.bench, config["load"])
    if r.codec_core_pct is not None:
        r.check("L6 load", "PASS" if r.codec_core_pct <= config["max_codec_core_pct"] else "WARN",
                f"codec uses {r.codec_core_pct:.2f} % of one core at the ICD load "
                f"(limit {config['max_codec_core_pct']} %)")
    sustain = next((row for row in rows if row.get("what") == "sustain"), None)
    if sustain and len(sustain["decodes_per_second"]) >= 2 and sustain["decodes_per_second"][0]:
        series = sustain["decodes_per_second"]
        r.sustain_change_pct = (series[-1] / series[0] - 1) * 100
        r.check("L6 sustain", "PASS" if r.sustain_change_pct >= -config["max_sustain_drop_pct"] else "WARN",
                f"decode rate changed {r.sustain_change_pct:+.0f} % over {len(series)} s")
    return r


def multicast_check(records, out, params, config, monitor_interval):
    """One session across every phone whose helper confirmed its lock; each must hear and be heard."""
    candidates = [r for r in records if r.tier is None and r.lock_confirmed and r.phone.ip]
    for r in records:
        if r.tier is None and not r.lock_confirmed:
            r.multicast = {"status": "FAIL", "detail": "the helper app did not confirm the multicast lock"}
    if len(candidates) < 2:
        for r in candidates:
            r.multicast = {"status": "UNTESTED", "detail": "fewer than two phones to test multicast with"}
        return
    phones = [r.phone for r in candidates]
    run_session(phones, out, "multicast", params, monitor_interval)
    matrix = reachability(out, "multicast", len(phones))
    count = len(phones)
    threshold = config["multicast_min_share"]

    def shares(index, peers):
        others = [p for p in peers if p != index]
        hears = sum(matrix[(s, index)] for s in others) / len(others) if others else 0
        heard = sum(matrix[(index, r)] for r in others) / len(others) if others else 0
        return hears, heard

    everyone = list(range(count))
    deaf = {i for i in everyone if min(shares(i, everyone)) < threshold}
    working = [i for i in everyone if i not in deaf] or everyone
    for i, r in enumerate(candidates):
        hears, heard = shares(i, working if i not in deaf else everyone)
        ok = i not in deaf and min(hears, heard) >= threshold
        r.multicast = {"status": "PASS" if ok else "FAIL", "hears": hears, "heard_by": heard,
                       "detail": f"hears {hears * 100:.0f} %, heard by {heard * 100:.0f} % of {count - 1} peers"}


def assign_tier(r, config):
    if r.tier:
        return
    status = r.multicast["status"] if r.multicast else "UNTESTED"
    r.check("L4 multicast", {"PASS": "PASS", "FAIL": "WARN", "UNTESTED": "WARN"}[status],
            (r.multicast or {}).get("detail", "not tested"))
    allowance_ok = all(c["status"] == "PASS" for c in r.checks if c["check"] in ("L6 allowance", "L6 load"))
    short = []
    if r.phone.sdk < config["product_min_sdk"]:
        short.append(f"API {r.phone.sdk} is below the product floor {config['product_min_sdk']}")
    if r.phone.abi not in config["product_abis"]:
        short.append(f"{r.phone.abi} is not a product ABI ({', '.join(config['product_abis'])})")
    if not allowance_ok:
        short.append("the codec is outside its allowances")
    if status != "PASS":
        short.append("L4 multicast did not pass")
    r.tier = "test" if short else "product"
    r.check("product", "PASS" if not short else "INFO", "; ".join(short) or "fit to stand in for the fielded device")


def record_results(records):
    inventory = load_inventory()
    for r in records:
        entry = inventory.setdefault(r.phone.phone_id, {"history": []})
        record = r.as_dict()
        entry["latest"] = record
        entry["history"] = (entry["history"] + [{"utc": record["qualified_utc"], "tier": record["tier"],
                                                 "binary_version": record["binary_version"]}])[-20:]
    save_inventory(inventory)
    return inventory


def signed(value):
    return "–" if value is None else f"{value:+.0f} %"


def fleet_lines(inventory):
    rows = sorted(inventory.values(), key=lambda e: (TIER_RANK.get(e["latest"]["tier"], 9),
                                                     e["latest"]["phone"]["model"]))
    lines = ["# Phone fleet", "",
             "Every phone the harness has qualified, best tier first. Encode and decode are TAK lean medians in "
             "microseconds for the event message (L6); codec load is its share of one core at the ICD load.", "",
             "| Tier | Phone | SoC | Android (API) | ABI | L6 event encode / decode | L6 codec load | L6 sustained | L4 multicast | Qualified | Notes |",
             "|---|---|---|---|---|---|---|---|---|---|---|"]
    for entry in rows:
        rec = entry["latest"]
        p = rec["phone"]
        event = rec.get("bench", {}).get("Event", {})
        multicast = (rec.get("multicast") or {}).get("status", "–")
        notes = "; ".join(rec.get("reasons", []))[:160]
        lines.append(
            f"| {rec['tier']} | {p['manufacturer']} {p['model']} ({p['phone_id'][-6:]}) | {p.get('soc') or '–'} | "
            f"{p['android']} ({p['sdk']}) | {p['abi']} | {fmt(event.get('encode_us_median'))} / "
            f"{fmt(event.get('decode_us_median'))} | {fmt(rec.get('codec_core_pct'), 2)} % | "
            f"{signed(rec.get('sustain_change_pct'))} | {multicast} | {rec['qualified_utc'][:10]} | {notes} |")
    return lines


def run_qualification(listing, phones, artifacts, corpus_files, apk, config, out, net_params, monitor_interval):
    """Qualifies every phone; returns the records and writes the run's report and the inventory."""
    outcomes = parallel_soft(lambda i, p: qualify_one(p, artifacts, corpus_files, apk, config), phones)
    records = []
    for phone, (record, error) in zip(phones, outcomes):
        if error:
            record = Record(phone)
            record.check("harness", "FAIL", f"{type(error).__name__}: {str(error)[:200]}")
            record.tier = "rejected"
        records.append(record)
    alive = [r for r in records if r.tier is None and r.helper_installed]
    if alive:
        held = start_helper([r.phone for r in alive])
        for r in alive:
            r.lock_confirmed = held.get(r.phone.serial, False)
    time.sleep(1)
    multicast_check(records, out, net_params, config, monitor_interval)
    for r in records:
        assign_tier(r, config)
    inventory = record_results(records)

    lines = ["# Phone qualification", "",
             f"{len(records)} phones checked; product floor API {config['product_min_sdk']} on "
             f"{', '.join(config['product_abis'])}; harness floor API {config['harness_min_sdk']}.", ""]
    skipped = [(serial, state) for serial, state in listing if state != "device"]
    if skipped:
        lines += ["Not checked, because adb cannot use them yet:", ""]
        for serial, state in skipped:
            hint = {"unauthorized": "accept the USB debugging prompt on the phone",
                    "offline": "reconnect the cable or run `adb reconnect`"}.get(state, "")
            lines.append(f"- {serial}: {state}" + (f" ({hint})" if hint else ""))
        lines.append("")
    ordered = sorted(records, key=lambda x: (TIER_RANK.get(x.tier, 9), x.phone.model))
    lines += ["| Phone | Android (API) | ABI | Tier | L4 live traffic | Why not higher |", "|---|---|---|---|---|---|"]
    for r in ordered:
        why = "; ".join(f"{c['check']}: {c['detail']}" for c in r.checks if c["status"] in ("FAIL", "WARN", "INFO"))
        live = "yes" if r.multicast and r.multicast["status"] == "PASS" else "no"
        lines.append(f"| {r.phone.manufacturer} {r.phone.model} ({r.phone.phone_id[-6:]}) | {r.phone.release} "
                     f"({r.phone.sdk}) | {r.phone.abi} | **{r.tier}** | {live} | {why or '–'} |")
    lines += ["", "## Every check", "", "| Phone | Tier | Check | Result | Detail |", "|---|---|---|---|---|"]
    for r in ordered:
        for index, c in enumerate(r.checks):
            head = f"{r.phone.manufacturer} {r.phone.model} ({r.phone.phone_id[-6:]})" if index == 0 else ""
            tier = r.tier if index == 0 else ""
            lines.append(f"| {head} | {tier} | {c['check']} | {c['status']} | {c['detail']} |")
    lines += ["", f"The fleet inventory now holds {len(inventory)} phones: {FLEET}."]
    return records, lines


def select_phones(phones, need, include_all=False):
    """Phones whose latest qualification fits `need` ('test' for L6, 'live' for L4); prints why the rest sit out."""
    if include_all:
        return phones
    inventory = load_inventory()
    chosen = []
    for phone in phones:
        entry = inventory.get(phone.phone_id)
        if not entry:
            print(f"skipping {phone.model} ({phone.serial}): not qualified yet; run `harness.py qualify`")
            continue
        rec = entry["latest"]
        if rec["tier"] not in ("product", "test"):
            print(f"skipping {phone.model} ({phone.serial}): tier {rec['tier']}")
            continue
        if need == "live" and not rec.get("net_ok"):
            print(f"skipping {phone.model} ({phone.serial}): L4 multicast did not pass at qualification")
            continue
        chosen.append(phone)
    return chosen
