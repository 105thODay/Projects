#!/bin/sh
# Runs harness.py end to end against ten emulated phones on this machine (tools/fake_adb.py):
# qualify (every tier and an unauthorized phone), fleet, L6 conformance and performance (the second
# through its old alias), and L4 traffic split into two sessions with an emulated blackout and a
# catch-up. Checks tiers, conformance and the ledger.
# Needs a host build of ar_device.
#
# Usage: android/tools/selftest.sh <path to host ar_device> <path to glibc_corpus.zip>
set -eu
binary=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
corpus=$(cd "$(dirname "$2")" && pwd)/$(basename "$2")
here=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/build"
cp "$binary" "$work/build/ar_device-armv7"
cp "$binary" "$work/build/ar_device-aarch64"
echo placeholder > "$work/build/ar_mcastlock.apk"
cat > "$work/light.json" <<'JSON'
{"bench": {"rounds": 200, "passes": 3, "sustain_s": 2}}
JSON
export FAKE_ADB_ROOT="$work/phones"
export FAKE_ADB_PHONES="s5-1,px-2,beam-3,px-4,px-5,beam-6,old-7,x86-8,deaf-9,unauth-10"
export ADB="$here/tools/fake_adb.py"
chmod +x "$ADB"
cd "$work"
H="$here/harness.py"
python3 "$H" qualify --artifacts build --corpus "$corpus" --config "$work/light.json" --loop --monitor-interval 5
python3 "$H" fleet
python3 "$H" conformance
python3 "$H" bench --rounds 200 --passes 3 --sustain 2
python3 "$H" traffic --loop --duration 30 --input-hz 3 --catchup-at 15 --blackout-ms 250 --monitor-interval 5 --session-size 4

python3 - <<'PY'
import glob, json
inventory = json.load(open("ar_results/inventory.json"))
tiers = {e["latest"]["phone"]["serial"]: (e["latest"]["tier"], e["latest"]["net_ok"]) for e in inventory.values()}
expected = {"s5-1": ("test", True), "px-2": ("product", True), "beam-3": ("product", True),
            "px-4": ("product", True), "px-5": ("product", True), "beam-6": ("product", True),
            "old-7": ("rejected", False), "x86-8": ("rejected", False), "deaf-9": ("test", False)}
assert tiers == expected, f"tiers {tiers}"
gate = json.load(open(glob.glob("ar_results/*_conformance/report.json")[0]))
rows = [r for phone in gate["gate"] for r in phone if r.get("what") == "gate"]
assert len(rows) == 3 * 7 and all(r["failures"] == 0 for r in rows), "gate"
sessions = sorted(glob.glob("ar_results/*_traffic/session_*/main_report.json"))
assert len(sessions) == 2, f"{len(sessions)} sessions"
for path in sessions:
    net = json.load(open(path))
    assert len(net["phones"]) == 3
    assert all(l["ledger_violations"] == 0 for l in net["ledger"]), "ledger violated"
    assert all(e["events"] > 0 for e in net["events"]), "no events measured"
    assert all(p["ok"] for p in net["pairs"]), "a clock pair could not be fitted"
qualify = json.load(open(glob.glob("ar_results/*_qualify/report.json")[0]))
assert qualify["not_checked"] == [{"serial": "unauth-10", "state": "unauthorized"}]
print("selftest: 10 fake phones qualified into the expected tiers; L6 conformance clean on 7; two L4 traffic sessions analyzed clean")
PY
