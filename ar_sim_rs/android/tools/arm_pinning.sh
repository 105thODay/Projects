#!/bin/sh
# Core pinning study on a 4-core machine (CM5 class). One device under test (DUT) runs the
# networking model, a renderer stand-in (90 Hz, 8 ms of CPU a frame) and a tracker stand-in
# (200 Hz, 2 ms a frame) on two cores; seven peer devices share the other cores. Three cases:
#
#   floating   renderer, tracker and networking all float on the DUT's two cores
#   pinned     the renderer alone on one core; tracker and networking on the other
#   pinned_rt  as pinned, with the renderer under SCHED_FIFO (needs sudo; skipped without)
#
# Usage: arm_pinning.sh <ar_device> [seconds]     env: PEER_CPUS=0-1 SHARED_CPU=2 RENDER_CPU=3
set -eu
binary=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
seconds=${2:-45}
here=$(cd "$(dirname "$0")/.." && pwd)
peers=${PEER_CPUS:-0-1}; shared=${SHARED_CPU:-2}; render=${RENDER_CPU:-3}
work=$(mktemp -d)
export FAKE_ADB_ROOT="$work/phones" FAKE_ADB_PHONES="px-1,px-2,px-3,px-4,px-5,px-6,px-7,px-8"
export ADB="$here/tools/fake_adb.py"
chmod +x "$ADB"
for n in 1 2 3 4 5 6 7 8; do mkdir -p "$work/phones/px-$n/ar"; cp "$binary" "$work/phones/px-$n/ar/ar_device"; done
cd "$work"
run_case() {
    name=$1; dut=$2; render_prefix=$3; tracker_cpus=$4
    export FAKE_ADB_CPUS="px-1=$dut;px-2=$peers;px-3=$peers;px-4=$peers;px-5=$peers;px-6=$peers;px-7=$peers;px-8=$peers"
    $render_prefix "$binary" render hz=90 work_ms=8 duration_s=$((seconds + 6)) label=render > "$name.render.json" &
    taskset -c "$tracker_cpus" "$binary" render hz=200 work_ms=2 duration_s=$((seconds + 6)) label=tracker > "$name.tracker.json" &
    python3 "$here/harness.py" traffic --all --loop --skip-preflight --duration "$seconds" --leave-at 10 --rejoin-at 15 \
        --late-join-at 20 --monitor-interval 30 > "$name.traffic.log" 2>&1
    wait
    report=$(ls -d ar_results/*_traffic | tail -1)/session_1/main_report.json
    python3 - "$name" "$report" "$name.render.json" "$name.tracker.json" <<'PY'
import json, sys
name, report = sys.argv[1], json.load(open(sys.argv[2]))
for path in sys.argv[3:]:
    r = json.loads(open(path).read().strip().splitlines()[-1])
    print(f"{name:10} {r['label']:8} frames {r['frames']} missed {r['missed']} "
          f"start late p50/p99/max {r['start_late_ms']['p50']:.2f}/{r['start_late_ms']['p99']:.2f}/{r['start_late_ms']['max']:.2f} ms "
          f"frame p50/p99/max {r['frame_ms']['p50']:.2f}/{r['frame_ms']['p99']:.2f}/{r['frame_ms']['max']:.2f} ms")
out = [e for e in report["events"] if e["sender"] == 0]
inn = [s for s in report["state"] if s["receiver"] == 0]
print(f"{name:10} network  DUT events {max(e['events'] for e in out)} on time min {min(e['on_time_pct'] for e in out):.1f} % "
      f"latency max {max(e['latency_ms_max'] for e in out):.1f} ms held {max(e['held'] for e in out)} | "
      f"state age at DUT p95 max {max(s['age_ms_p95'] for s in inn):.1f} ms | "
      f"violations {sum(l['ledger_violations'] for l in report['ledger'])} retained agree {report.get('retained_agree')}")
PY
    sleep 2
}
lscpu | grep -E "Model name|^CPU\(s\)" || true
run_case floating "$shared,$render" "taskset -c $shared,$render" "$shared,$render"
run_case pinned "$shared" "taskset -c $render" "$shared"
if sudo -n true 2>/dev/null; then
    run_case pinned_rt "$shared" "sudo chrt -f 50 taskset -c $render" "$shared"
fi
