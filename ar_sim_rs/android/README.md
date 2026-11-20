# Android test harness

Tests the ar_sim networking model on real phones, as many and as varied as you can collect. The networking
model specifies OSI layers 4 to 7 of a serverless AR game; this harness tests three of them:

| Layer | What it covers | Harness test |
|---|---|---|
| L7 Application | Avatar state, urgent events, markers placed and deleted, one authorized writer per entity | message content in `traffic`; markers and deletes exercise the retained set |
| L6 Presentation | The CoT codec: XML 2.0 or TAK v1 protobuf, lean schema; the draft L5 kinds | `conformance` (the golden-corpus gate) and `performance` (speed) |
| L5 Session | Leader, admission, roster epochs, catch-up, join readiness, departure, close | `traffic` |
| L4 Transport | Multicast, three urgent copies, the rolling 200 kbit/s ledger, deduplication, summary-driven repair | `traffic` |

"Codec" here means only the L6 encoder/decoder: the `cot_codec` crate, byte-identical to the C++ reference, and
`cot_draft`, which carries the draft kinds the reference does not (the session record, join request, snapshot
barrier, catch-up request and marker delete).

Plug a phone in, run `qualify`, and the harness decides what that phone is fit to test, records it in a
fleet inventory, and from then on includes it in every test it qualified for. Each phone runs one static
`ar_device` binary, started over adb, plus a small helper app that holds the Wi-Fi multicast lock. The host
controller, `harness.py`, needs Python 3.8+ and adb, nothing else.

## Qualification

`qualify` runs the same checks on every phone adb can see, all at once. A hard failure stops that phone
and leaves the others going.

| Check | Passes when |
|---|---|
| api | Android API 23 (6.0) or later, the harness floor |
| abi | the phone runs 32- or 64-bit ARM |
| storage | 64 MB free for the binary and corpus |
| wifi | the phone has a Wi-Fi address (needed only for L4 traffic) |
| install, runs | the binary for its ABI installs and starts |
| clock | the monotonic clock resolves 1 µs or better |
| L6 conformance | all 83,647 golden-corpus records reproduce the reference exactly |
| L6 allowance | TAK lean encode and decode medians fit the simulator's 250 µs allowances |
| L6 load | the codec needs at most 10 % of one core at the ICD load (7 peers, 15 Hz state, 3 Hz summaries) |
| L6 sustain | the decode rate falls less than 20 % over a sustained run (thermal throttling) |
| L4 multicast | in one session of every phone, it hears and is heard by at least half of its peers' frames |
| product | API 29+ on 64-bit ARM, the codec inside its allowances, L4 multicast working |

Each phone lands in one tier, and the report says why it got no higher:

| Tier | Meaning | Used by |
|---|---|---|
| **product** | Fit to stand in for the fielded device (the Beam Pro class) | conformance, performance, traffic |
| **test** | Fit for the L6 tests; joins L4 traffic if multicast passed | conformance, performance, traffic if multicast passed |
| **conformance_failed** | Ran the codec and got a different answer: an L6 finding on that platform, not a phone fault | nothing until investigated |
| **rejected** | Cannot run the tests (too old, not ARM, no space, binary will not start) | nothing |

The thresholds live in `qualify_config.json`. The product floor (API 29, 64-bit ARM) follows the XREAL SDK
and is a proposal awaiting a ruling; change it there.

The inventory, `ar_results/inventory.json`, keys each phone by its hardware serial, so a phone keeps its
record whether it connects by USB or Wi-Fi, and requalifying it updates the same entry. `ar_results/fleet.md`
lists every phone ever qualified, best tier first, with its L6 codec speed.

## Running it

```sh
cd ar_sim_rs/android
python harness.py fetch --out build        # or download the ar_android artifact by hand into build/
# Plug each phone in by USB once and accept the debugging prompt, then:
python harness.py wifi                     # optional: move every USB phone to adb over Wi-Fi
python harness.py qualify --artifacts build --corpus ../corpus_ref/glibc_corpus.zip
python harness.py fleet                    # the inventory so far
python harness.py performance              # L6: full codec benchmark on every test-qualified phone
python harness.py traffic                  # L4: live traffic among every multicast-qualified phone
```

`conformance`, `performance` and `traffic` also answer to their old names, `gate`, `bench` and `net`.

Add phones at any time: plug them in and run `qualify` again. Phones already qualified are rechecked
alongside the new ones.

`traffic` splits the phones into sessions of at most 8, the ruled player limit, mixing models, and runs the
sessions one after another so they never share the air. Each session gets an 8 s preflight with fixed indices,
then 90 s at the ICD operating point as a real L5 session, and its own report under
`ar_results/<run>/session_<n>/`:

- Phone 0 creates the session and leads. Every other phone joins as a candidate, is admitted, catches up and
  only then sends.
- With three or more phones, phone 1 leaves at 25 s and rejoins at 35 s at its old index, and the last phone
  joins late, at 50 s, into a session that already holds markers and tombstones.
- Every phone places markers (0.1 a second) and deletes them (0.02 a second) against the 50-item cap.
- The leader closes the session 3 s before the end.

The report adds, per admission: time from join request to admission, the barrier's item count, catch-up
requests, catch-up time and time to ready. Per phone it gives the retained set at the end, the departure and
close delays, and whether every member ends with the same retained set. Useful options: `--duration`,
`--input-hz`, `--state-hz`, `--summary-hz`, `--marker-hz`, `--delete-hz`, `--late-join-at`, `--leave-at`,
`--rejoin-at` (-1 turns a step off), `--blackout-ms` (an emulated receive blackout every 10 s, to exercise
repair), `--session-size`, and `--fixed` for L4 alone with fixed indices.

`conformance`, `performance` and `traffic` take `--all` to include phones regardless of tier, and
`--serial` (repeatable) to pick phones by adb serial.

## What you need

- adb (Android platform-tools) and Python 3.8 or later. `gh` is optional, for `fetch`.
- Phones with Developer options and USB debugging on, on one Wi-Fi network with the host.
- An access point that passes multicast between clients (client isolation off). A dedicated router is best.
- The built files `ar_device-armv7`, `ar_device-aarch64` and `ar_mcastlock.apk`, from the
  `ar_sim Android harness` workflow; `fetch` downloads the newest.

## Keep in mind

- **Keep the helper app on screen.** Android drops multicast unless an app holds the multicast lock, and a
  program started from adb cannot take one. The harness opens the app and checks its log line; a phone that
  never confirms it stays out of L4 traffic.
- **adb over Wi-Fi shares the radio.** The controller starts runs, samples the monitor every 15 s and pulls
  logs afterward. For the cleanest L4 numbers, connect adb by USB during `traffic`.
- **No clock sync is needed.** The analysis fits each pair's clock offset and drift from the fastest frames
  in each direction, assuming the fastest path is equally fast both ways.
- **Latency covers the network only:** input onset to the first copy's arrival, including the wait for a send
  opportunity, against the 59 ms left after 10 ms of input, 30 ms of display and 1 ms of uncertainty.
- **Readiness waits by roster index.** A newcomer stays silent until it holds everything the barrier lists and
  has heard a summary at its admission epoch from every present member with a *lower* index. The R17 proposal
  ("from every member") deadlocks when two newcomers are admitted together; ordering by index cannot. A
  newcomer still waiting a full catch-up window (10 s) after its catch-up becomes ready anyway, and the report
  says "timeout".
- **What L5 does not do yet:** transfer leadership, remove a member, extend the session, or refuse a join with
  a reason the candidate sees (the leader just stays silent); these await rulings (HICD D-02, D-06, D-07). The
  blackout is emulated in the receiving process, not on the radio.

## Testing the harness without phones

`tools/fake_adb.py` stands in for adb and emulates phones on the host, from a Galaxy S5 and a Beam Pro to
phones that should be rejected. `tools/selftest.sh` qualifies ten of them and runs every command; the
workflow's `selftest` job runs it on each push:

```sh
cargo build --release -p ar_device
sh android/tools/selftest.sh target/release/ar_device corpus_ref/glibc_corpus.zip
```
