# Android test harness

Runs the ar_sim codec and the Network Layer ICD traffic profile on real phones. Each phone runs one static
`ar_device` binary, started over adb, plus a small helper app that holds the Wi-Fi multicast lock. The
host controller, `harness.py`, installs everything, runs the tests on every phone at once, watches battery
temperature and CPU clocks, pulls the logs and writes a report.

| Test | What it measures |
|---|---|
| `gate` | The golden-corpus gate on the phone: 83,647 records must match the reference's bytes, fields and error text |
| `bench` | Encode and decode time per wire form and message kind (the desktop `compare_speed` messages), and 20 s of sustained decoding to show thermal throttling |
| `net` | Live multicast among the phones at the ICD operating point: urgent-event latency against the 59 ms network budget, copies heard, repair, avatar-state loss and age, the leader's 50-item catch-up, ledger compliance, and codec cost inside the run |

## What you need

- adb (Android platform-tools) and Python 3.8 or later on the host. `gh` is optional, for `fetch`.
- The phones on one Wi-Fi network with the host, with Developer options and USB debugging on.
- An access point that passes multicast between clients: client (AP) isolation off. A dedicated router
  for the test is best; guest networks and many enterprise networks block client-to-client traffic.
- The built files: `ar_device-armv7`, `ar_device-aarch64`, `ar_mcastlock.apk`. The `ar_sim Android harness`
  workflow builds them on every push; `python harness.py fetch` downloads the newest.

32-bit phones (the Galaxy S5 and other `armeabi-v7a` devices) get the ARMv7 build; 64-bit phones get the
AArch64 build. Both target Android 6.0 (API 23) and later.

## Running it

```sh
cd ar_sim_rs/android
python harness.py fetch --out build            # or download the ar_android artifact by hand into build/
# Plug each phone in by USB once, accept the debugging prompt, then:
python harness.py wifi                         # switches every USB phone to adb over Wi-Fi, port 5555
python harness.py devices                      # model, Android version, ABI and Wi-Fi address of each
python harness.py setup --artifacts build --corpus ../corpus_ref/glibc_corpus.zip
python harness.py gate
python harness.py bench
python harness.py net                          # 8 s preflight, then 90 s at the ICD operating point
```

`python harness.py all --artifacts build --corpus ../corpus_ref/glibc_corpus.zip` does setup, gate, bench
and net in turn. Every run writes `ar_results/<UTC time>_<test>/` with `report.md`, `report.json`, each
phone's raw output and `monitor.csv`. `python harness.py analyze ar_results/<run>` redoes a net analysis.

Useful `net` options: `--duration`, `--input-hz` (urgent inputs per second per phone), `--state-hz`,
`--summary-hz`, `--catchup-at` (second of the leader's catch-up; -1 turns it off), `--blackout-ms` (an
emulated receive blackout every 10 s, to exercise repair). Phone 0, the first serial in sort order, leads.

## Keep in mind

- **Keep the helper app on screen.** Android drops multicast frames unless an app holds the multicast
  lock, and `ar_device` runs from adb shell, which cannot take it. `setup` and `net` open the app and check
  its log line; if a phone shows "NOT confirmed", open "ar_sim mcast lock" on it by hand. The app also
  holds a Wi-Fi performance lock (low-latency mode on Android 10+) and keeps the screen on.
- **The preflight guards the main run.** It sends 8 s of state only and prints who hears whom. If any
  phone hears less than half of another's frames, `net` stops and names the usual causes.
- **adb over Wi-Fi shares the radio.** The controller only starts the runs, polls the monitor every 15 s,
  and pulls logs afterward. For the cleanest radio numbers, connect adb by USB during `net`; the phones
  still talk to each other over Wi-Fi.
- **No clock sync is needed.** Each phone logs on its own monotonic clock. The analysis fits each pair's
  offset and drift from the fastest frames in each direction, assuming the fastest path is equally fast
  both ways; the report shows the fit for each pair.
- **Latency covers the network only.** It runs from input onset to the first copy's arrival, including the
  wait for a send opportunity. The 100 ms requirement leaves 59 ms for that after 10 ms of input, 30 ms of
  display and 1 ms of uncertainty.
- **What the harness does not yet do.** Catch-up items lost on the air are not repaired (the snapshot
  barrier and newcomer repair of ICD section 8.7 are not built yet), and the blackout is emulated by
  discarding frames in the receiving process, not on the radio.

## Testing the harness without phones

`tools/fake_adb.py` stands in for adb and emulates phones on the host; `tools/selftest.sh` runs every
command against four of them, and the workflow's `selftest` job runs it on each push:

```sh
cargo build --release -p ar_device
sh android/tools/selftest.sh target/release/ar_device corpus_ref/glibc_corpus.zip
```
