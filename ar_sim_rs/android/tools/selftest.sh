#!/bin/sh
# Runs harness.py end to end against four emulated phones on this machine (tools/fake_adb.py):
# setup, gate, bench, a preflight and a short net run with an emulated blackout and a catch-up.
# Checks the reports exist and the gate and ledger are clean. Needs a host build of ar_device.
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
export FAKE_ADB_ROOT="$work/phones" FAKE_ADB_PHONES="emu-1,emu-2,emu-3,emu-4" ADB="$here/tools/fake_adb.py"
chmod +x "$ADB"
cd "$work"
python3 "$here/harness.py" setup --artifacts build --corpus "$corpus"
python3 "$here/harness.py" gate
python3 "$here/harness.py" bench --rounds 200 --passes 3 --sustain 2
python3 "$here/harness.py" net --loop --duration 30 --input-hz 3 --catchup-at 15 --blackout-ms 250 --monitor-interval 5

gate=$(ls -d ar_results/*_gate)
net=$(ls -d ar_results/*_net)
python3 - "$gate" "$net" <<'EOF'
import json, sys
gate = json.load(open(f"{sys.argv[1]}/report.json"))
rows = [r for phone in gate["gate"] for r in phone if r.get("what") == "gate"]
assert len(rows) == 12 and all(r["failures"] == 0 for r in rows), "gate failed on a fake phone"
net = json.load(open(f"{sys.argv[2]}/main_report.json"))
assert all(l["ledger_violations"] == 0 for l in net["ledger"]), "ledger violated"
assert all(e["events"] > 0 for e in net["events"]), "no events measured"
assert all(p["ok"] for p in net["pairs"]), "a clock pair could not be fitted"
print("selftest: gate clean on 4 fake phones; net run analyzed with no ledger violation")
EOF
