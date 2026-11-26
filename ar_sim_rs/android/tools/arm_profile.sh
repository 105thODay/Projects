#!/bin/sh
# Profiles the networking model natively on a 4-core 64-bit Arm Linux machine (a GitHub arm64
# runner, a Raspberry Pi 5 or Compute Module 5): L6 conformance, L6 performance, and one
# 8-player L4-L5 session on loopback with each process's CPU share. Uses the fake adb, so no
# phones are needed.
#
# Usage: android/tools/arm_profile.sh <path to host ar_device> <path to glibc_corpus.zip>
set -eu
binary=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
corpus=$(cd "$(dirname "$2")" && pwd)/$(basename "$2")
here=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
mkdir -p "$work/build"
cp "$binary" "$work/build/ar_device-armv7"
cp "$binary" "$work/build/ar_device-aarch64"
echo placeholder > "$work/build/ar_mcastlock.apk"
export FAKE_ADB_ROOT="$work/phones" FAKE_ADB_PHONES="px-1,px-2,px-3,px-4,px-5,px-6,px-7,px-8"
export ADB="$here/tools/fake_adb.py"
chmod +x "$ADB"
cd "$work"
echo "== machine"; lscpu | grep -E "Architecture|Model name|^CPU\(s\)|Vendor ID|Model:|max MHz|BogoMIPS" || true
"$binary" info
echo "== qualify (L6 conformance on 8 processes at once, short benchmark, multicast)"
python3 "$here/harness.py" qualify --artifacts build --corpus "$corpus" --loop --monitor-interval 30 | sed -n '/^| Phone | Android/,/^$/p'
echo "== L6 performance, one process alone"
"$binary" bench rounds=2000 passes=25 sustain_s=20 | grep -E '"TakLean"|bench_total|sustain'
echo "== 8-player L4-L5 session, 60 s"
python3 "$here/harness.py" traffic --loop --duration 60 --monitor-interval 30 --skip-preflight > traffic.log
sed -n '/^## Urgent events/,/^## Avatar state/p;/^## L5 admissions/,/phones still in/p;/^## Ledger/,/^## L6 codec cost/p;/^## L6 codec cost/,/^## Phone condition/p' traffic.log
echo "== CPU share of one core, per process"
grep -ho '"cpu_pct_of_one_core":[0-9.]*' ar_results/*_traffic/session_1/main_stdout_*.jsonl
