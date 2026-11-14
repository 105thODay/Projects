# HANDOFF — Networked AR Renderer Networking Simulator (`ar_sim`)

_Rev 7, 2026-11-13: the ideal medium again, with TAK Protocol v1 (lean) as the wire format for every run; the matrix, the human-input search, the 16-participant battery and the player-count limits rerun that way (section 3e)._
_Rev 6: encoded-wire CoT in the simulator, serverless anchor sharing over the peer positioning function, an 802.11 access-point channel with contention and DTIM buffering, and the player-count limits rerun on it (section 3d)._
_Rev 5: measured CoT encodings in place of abstract frame sizes, and human motor limits on input (section 3c)._
_Rev 4: a held-back repair share, non-periodic (dithered) inputs, the knob search redone for them, and the full matrix at 200 B and 100 B (section 3b). Rev 4 replaces Rev 3's section 3b and corrects the Rev 3 figures that came from periodic inputs._
_Rev 3: the matrix at 200 B. Rev 2: the knob search of section 3a, the `sharedEncode` knob, and longest-waiting-first state service. Built against the Networked AR Networking Test Package, version 1.0
(11 November 2026): the requirements document, `Networked_AR_Simulation_Profile.json`,
and the 18-row requirement register._

## 0. What this is

A discrete-event model of the profile's **transport-queue mode**. Participants offer real
frames; each client admits them against the NET-002 rolling ledger; one shared half-duplex
medium carries them; every input is scored at every required recipient against the 100 ms
limit. The input (I) and display (P) stages are supplied allowances, so every result is a
**conditional networking result**. The simulator answers the profile's question: which
operating envelope passes, and what breaks it.

The design under test differs in kind from `fps_sim`. It has no leader and no replicated
log. Each entity has one authorized writer; a discrete input goes out as three blind
copies at consecutive 90 Hz opportunities; a recipient applies it at most once.

## 1. Files (house C++ standard; `-Wall -Wextra -Wconversion` clean)

| file | owns |
|---|---|
| `profile_vectors.hpp` | GENERATED from the package JSON by `tools/make_profile_header.py`: retained limits, allocations, starter workload, 12 timing vectors, 6 boundary vectors. Never retyped. |
| `sim_time.hpp` | truth time in ninths of a microsecond, so 1/90 s, 1/60 s, 1/45 s, 1/30 s, 1/7.5 s and 1 µs are all exact |
| `doe_config.hpp/.cpp` | every knob, the ten case presets, `key=value` overrides |
| `rolling_ledger.hpp/.cpp` | NET-002: charges at transmit start, window (t − 1 s, t], look-ahead sums for admission |
| `simulation.hpp/.cpp` | nodes, opportunities, admission, serial encoder, shared medium, loss and faults, receiver integrity checks, repair, scoring; Rev 6: message building, decode-once checking, alignment and calibration epochs |
| `cot_codec.hpp/.cpp` | Rev 6: CoT encoder and decoder for six message kinds in four wire forms (XML as specified and lean, TAK Protocol v1 as specified and lean); a strict XML subset parser; a hand-written protobuf writer and reader |
| `codec_selftest.hpp/.cpp` | Rev 6: `ar_sim codec`: byte-identical round trip for every form and kind, sizes, encode and decode time, a single-byte corruption sweep; writes TAK and XML samples for the cross-check |
| `alignment.hpp/.cpp` | Rev 6: random-waypoint motion, the peer positioning function stub (far 5 Hz GPS-aided mission-frame pose, near 960 Hz relative pose within 5 m), alignment state |
| `wifi_channel.cpp` | Rev 6: the 802.11 access-point channel (`channel=wifi_ap`): EDCA contention, collisions, acknowledgments and retries, AP relay, beacons, DTIM and power-save buffering |
| `trace_writer.hpp/.cpp` | the four trace ledgers of the profile's trace contract, as CSV |
| `analytic_checks.hpp/.cpp` | README step 1: timing vectors, deadline boundaries, ledger edges |
| `rf_feasibility.hpp/.cpp` | case F0: the inherited video and ranging schedule |
| `ar_sim.cpp` | CLI and the report lines |
| `tools/make_matrix.py` | writes `run_matrix.sh` and `run_matrix.bat` (97 run groups). With a frame size it writes `run_matrix_<size>.sh/.bat`: the same list at that size plus the section 3b operating points (`200`: 218 run groups, about 3 min; `100`: 188) |
| `tools/summarize_matrix.py` | `ar_matrix.txt` → `ar_summary.md`, `ar_summary.csv`; `summarize_matrix.py ar_matrix_200.txt ar_summary_200` for a sized run |
| `tools/point_table.py` | summary CSVs → `point_tables.md`: one compact table per operating point |
| `tools/knob_search.py` | section 3a: sweeps the assumed workload knobs at 8, 12 and 16 participants → `knob_search_<fanout>.csv` (864 candidates, about 90 s each fan-out). `knob_search.py unicast dither` repeats it for non-periodic inputs → `knob_search_unicast_dither.csv` |
| `tools/knob_ladder.py` | reads that CSV → the least-changed passing settings per player count and frame size (`knob_ladder_<fanout>.md`; add `dither` for `knob_ladder_unicast_dither.md`) |
| `tools/cot_fixtures.py` | section 3c: draft CoT fixtures for the urgent event, state record, control summary and repair request; measures XML, DEFLATE, session-dictionary DEFLATE, TAK Protocol v1 and a binary AR detail → `cot_sizes.md/.csv`, `cot_fixtures/*.xml`. Needs the TAK `.proto` files (below) and `pip install grpcio-tools` |
| `tools/human_input_search.py` | section 3c: measured sizes × 8/12/16 participants × unicast/common × four human input models → `human_input.md/.csv` |
| `tools/common_point_tests.py` | section 3c: the full test set on the 16-participant common-delivery points → `common_points.md/.csv` |
| `tools/verify_trace.py` | independent re-scoring of a traced run, sharing no code with the simulator; on the Wi-Fi channel each station attempt, retries included, is one emission |
| `tools/tak_crosscheck.py` | Rev 6: parses the codec's TAK v1 bytes with protobuf classes generated from the ATAK-CIV `.proto` files and checks they are canonical |
| `tools/wire_point_tests.py` | Rev 6: the section 3c battery at 16 participants with encoded wire and alignment → `wire_points.md/.csv`; extra arguments go to every run (`channel=wifi_ap wifiVoice=0` → `wire_points_channel-wifi_ap_wifiVoice-0.md`) |
| `tools/player_limits.py` | Rev 6: player-count limits for 2 cases × 5 workloads × 6 channels → `player_limits.md/.csv` (cached; about 30 min from empty). Rev 7: `wire=tak_lean channels=ideal` → `player_limits_tak_lean_ideal.md/.csv` (about 11 min) |
| Rev 7 TAK runs | `make_matrix.py tak`, run `run_matrix_tak.sh`, `summarize_matrix.py ar_matrix_tak.txt ar_summary_tak` (94 groups, 4 min); `human_input_search.py tak`; `wire_point_tests.py forms=tak_lean` |
| `profile/` | the package's JSON profile and requirement register, as delivered |

Build (Linux check): `./build.sh`, which runs `g++ -std=c++17 -O2 -Wall -Wextra -Wconversion -Wno-sign-conversion alignment.cpp analytic_checks.cpp ar_sim.cpp codec_selftest.cpp cot_codec.cpp doe_config.cpp rf_feasibility.cpp rolling_ledger.cpp simulation.cpp trace_writer.cpp wifi_channel.cpp -o ar_sim`
Windows: `build.bat` from a Developer Command Prompt. **The `.bat` files have not been run;
only the shell versions were executed.**

```
ar_sim analytic                         timing, deadline and ledger oracle checks
ar_sim codec [dumpDir]                  encoded-wire CoT round trip, sizes and speed (Rev 6)
ar_sim F0 [guardUs]                     video and ranging coexistence
ar_sim <case> [nSeeds] [key=value ...]  cases Q0 Q1 Q2 R0 J0 C0 S0 P0 O0 R1
ar_sim phase <case> [key=value ...]     12 x 12 x 3 input and display phase sweep
```

Add `trace=<prefix>` to any run to write the four ledgers, then
`python tools/verify_trace.py <prefix>`.

The TAK Protocol `.proto` files are not copied into this tree. Fetch them from the public
ATAK-CIV repository, then point the fixture tool at them:

```
git clone --depth 1 --filter=blob:none --sparse https://github.com/deptofdefense/AndroidTacticalAssaultKit-CIV.git takrepo
git -C takrepo sparse-checkout set --no-cone "takproto/*"
python tools/cot_fixtures.py takrepo/takproto cot
mkdir codec_dump && ./ar_sim codec codec_dump
python -I tools/tak_crosscheck.py takrepo/takproto codec_dump
```

Rev 6 knobs, all off by default so the anchors hold: `wire=abstract|xml_specified|xml_lean|tak_specified|tak_lean`,
`align=1` (with `farSigmaM nearSigmaM driftFraction farGain farGainNear nearGain calThresholdM calThresholdDeg
checkNearM checkFarM arenaM`), `channel=ideal|wifi_ap` (with `group=basic|unicast wifiVoice wifiDozing
wifiDtimPeriod wifiBeaconUs wifiDataMbps wifiBasicMbps wifiAckMbps wifiMacBytes wifiRetryLimit chargeMac`),
`udpIpBytes authBytes datagramLimitBytes`, and `syncPhases=2`.

## 2. VERIFIED BASELINE — regression anchors

1. `ar_sim analytic` ends `analytic checks: 0 failures` (12 timing vectors, 6 boundary
   vectors, 6 ledger edges).
2. `ar_sim Q0`, seed 1, must print:

```
seed 1: obligations 228  onTime 228  late 0  missing 0  rejected 0  incorrect 0  dupEffects 0  missFraction 0.000000  L(P50/P95/P99/max) 51.792/51.792/51.792/51.792 ms over 228 delivered-correct  publication(max) 10.792 ms (bound 59)
```

3. `ar_sim phase Q2` must print `worst L 76.114111 ms` and `worst W 33.332333 ms`
   (three periods less one microsecond: the +1 µs boundary point).
4. `ar_sim F0` must print `deficit 4.968 ms` and `INFEASIBLE as written`.

Random draws use the engine's raw output, not the standard distributions, so these lines
should match across compilers. That has not been confirmed on MSVC.

5. (Rev 6) `ar_sim codec` ends `codec self-test: 0 failures`, and `tak_crosscheck.py` ends
   `TAK cross-check: 12 files, 0 failures`. The Rev 6 features draw from their own random
   streams, so anchors 1 to 4 are unchanged with them off; they were rechecked after every
   Rev 6 change.

## 3. Results (97 run groups, 405 runs, about 25 s; full tables in `ar_summary.md`)

### What passes

| check | result |
|---|---|
| Q0, Q1, Q2, ten seeds each | 2,280 of 2,280 obligations on time in each; service and profile pass 10 of 10 |
| Phase sweeps, 432 points each | all pass; worst L 53.9 / 65.0 / 76.1 ms for 0 / 1 / 2 erased copies (modeled photon) |
| Q2 phase sweep with D held at its 20 ms limit | all pass; worst L 93.7 ms against the 94.33 ms bound |
| Two-node ledger | 146.2 kbps mean per sender (147.2 kbps while inputs run); worst rolling second 152,000 bits |
| Jitter to 10 ms with two erased copies | all on time; worst L 84.0 ms |
| Independent loss at 0, 1, 3 % in each direction | zero misses in 2,280 obligations per setting |
| Duplicates, reordering, old revisions (P0) | no duplicate effect; stale state refused; all on time |
| Common delivery, 2 to 32 participants | flat 146.2 kbps per sender; all on time; airtime 23 % at 16 and 47 % at 32 on the 10 Mbit/s PHY |
| Trace check | the independent re-scoring agrees with the simulator on every traced run: seven in Rev 1, one each in Rev 4, 5 and 7, three in Rev 6 (one flags finding 43's ceiling breaches, as the simulator does) |

### What breaks it

1. **The lending rule as written fails as soon as offered load exceeds the ceiling.**
   Section 03 permits lending unused shares "only while those reservations remain
   feasible," and the reservations it names are the pending urgent copies. That
   protects copies already owed and nothing else. Once routine traffic
   fills the ledger, a new input finds no room and waits for old charges to age out.
   Three participants on unicast: 539 of 2,052 obligations late (26 %), worst wait 243 ms.
   With a standing urgent reserve that is never lent, the same case passes 2,052 of 2,052.

2. **The standing reserve must hold a burst plus one more copy set, not the average rate.**
   When inputs recur about one window apart, the previous burst is still in the ledger
   as the new one starts. Four simultaneous inputs per second: 51 % late under lending,
   51 % late with the 64 kbit share reserved, 0 % late with 104 kbit reserved. The price
   is routine state, cut from 96 to 60 kbps.

3. **Unicast does not scale; the ceiling is spent on copies.** With a sized reserve,
   inputs stay on time while state starves: 77 kbps of state at three participants
   (age P95 284 ms), 44 kbps at four (771 ms), and none at eight. At sixteen the design
   collapses. Common delivery, charged once per emission, removes the limit.
   **Multicast support is the single most important unfrozen input.**

4. **With common delivery the limit becomes airtime.** Sixteen participants offer
   2.3 Mbit/s. They pass at a 10 or 5 Mbit/s PHY and fail completely at 2 Mbit/s.

5. **A slow PHY eats the D allowance.** Two erased copies pass down to 1 Mbit/s
   (D 12.8 ms). At 500 kbit/s D reaches 36.9 ms and a third of obligations are late.
   At 200 kbit/s, a PHY equal to the quota, nothing arrives in time.

6. **Opportunity rate.** With the modeled 2.1 ms delivery, 60 Hz survives two erasures
   (worst L 90.7 ms) and 45 Hz survives one. The profile's analytic vectors, which
   assume D at its 20 ms limit, are stricter and still hold.

7. **Correlated loss is the real threat; independent loss is not.** A blockage covering
   three copy periods (33 ms) makes every input late: the repair path delivers them
   roughly 300 to 550 ms after the input. Blackouts of 50, 100 and 250 ms every 10 s make 12, 24 and 36 of
   2,280 obligations late. Ten percent independent loss costs 2 of 2,280.

8. **Two erasures plus 1 % loss.** About 1 % of obligations are late at 8, 16 and 32
   participants: the third copy is the only one left, and it is lost one time in a hundred.

9. **Relays are charged again at every hop.** At four participants, two-hop routes
   need a 160 kbit reserve and leave 0.7 kbps for state.

10. **F0 is infeasible, as the profile says.** Gross quiet time is 13.782 ms per
    superframe against 18.750 ms of ranging: a 4.968 ms deficit. With whole exchanges
    only 24 of 36 fit, so 12 exchanges per superframe have no slot, before any guard
    time and with nothing left for participant traffic.

### Integrity (NET-013, NET-014)

Across all 405 runs: zero duplicate effects, zero ledger violations, zero resurrected entities, zero
unauthorized updates applied. In R1 the replayed create is refused by its tombstone,
the takeover attempt is refused, and inputs made during the 1 s partition converge
within 265 ms of the heal. They remain deadline failures, as the profile requires.

## 3a. Knob search: 8 to 16 participants inside the ceiling (Rev 2)

The retained limits stay fixed: 100 ms response, 200 kbit per rolling second per client.
Everything the profile labels a starter assumption is treated as a knob. Each candidate
runs 60 s over three seeds with the bounded erasures its copy count allows (two erased
of three copies; one erased of two), a standing urgent reserve sized by finding 2, and
`sharedEncode=1`. A candidate passes when every input is on time. State age is reported,
not gated. Unicast, 864 candidates: 551 pass, 306 are skipped because reserve plus
control alone exceed the ceiling, 7 fail. Of the 551, 77 keep every input on time but break a
stage bound of the profile (mostly 400 B and larger frames); the ladder excludes them.

**The per-sender unicast budget** (R recipients, frame of u bits, c copies):

    reserve  = c·R·u  (+ R·u when inputs recur within one window)
    control  = R · controlBits · controlHz
    state    = whatever is left; age follows from stateBits · stateHz · R against it

At the starter values and R = 15 the reserve alone is 384 kbit. Frame size multiplies
every term, so it is the only knob that reaches 16 by itself.

**Least-changed passing settings, unicast** (full table in `knob_ladder_unicast.md`):

| players | frame | state | control fixture | repair fixture | copies | state age P95 | charged | airtime |
|---|---|---|---|---|---|---|---|---|
| 8 | 100 B | 15 Hz | 7.5 Hz | 5 Hz | 3 | 100 ms | 191 kbps | 15 % |
| 8 | 200 B | 10 Hz | 1 Hz | off | 3 | 99 ms | 153 kbps | 12 % |
| 8 | 200 B | 15 Hz | 1 Hz | 5 Hz | 3 | 198 ms | 185 kbps | 15 % |
| 8 | 400 B | 5 Hz | 1 Hz | off | 2, at 0.5 input/s | 188 ms | 155 kbps | 12 % |
| 12 | 100 B | 15 Hz | 1 Hz | off | 3 | 71 ms | 165 kbps | 20 % |
| 12 | 200 B | 5 Hz | 1 Hz | off | 3 | 188 ms | 153 kbps | 18 % |
| 16 | 100 B | 10 Hz | 1 Hz | off | 3 | 100 ms | 164 kbps | 26 % |
| 16 | 100 B | 15 Hz | 1 Hz | off | 3 | 151 ms | 183 kbps | 29 % |
| 16 | 200 B | 5 Hz | 1 Hz | off | 2, at 0.5 input/s | 204 ms | 166 kbps | 26 % |

No 800 B candidate holds state age P95 under 210 ms at any player count from 8 up:
inputs stay on time but state starves. 400 B does so only at 8, and only with every
other knob turned.

**Common delivery** (`knob_ladder_common.md`): the starter workload passes unchanged at
8, 12 and 16 (145 kbps, state age P95 about 70 ms). No knob is needed; the frame-size
knob only buys margin (400 B: 89 kbps; 200 B: 48 kbps; 100 B: 28 kbps).

**Knob ranking, by what each buys on unicast**

1. Frame size (800 → 200 or 100 B). Decisive. Implies a negotiated compact CoT form
   under NET-003; 800 B is roughly a CoT XML event, 100 to 200 B a binary one.
2. Repair fixture off (repair sent only on request). Frees R × 16 kbit at 400 B frames.
3. Control fixture 7.5 → 1 or 2 Hz. Slows loss detection: a repair request waits for
   the next summary, so correlated-loss recovery (finding 7) gets slower. Not rerun here.
4. State rate 15 → 10 or 5 Hz. Sets the age floor directly (about 70 / 100 / 190 ms).
5. Copies 3 → 2. Smallest saving, and it gives up the two-erasure claim of NET-012.
6. Input rate 1 → 0.5 per second. Drops the extra copy set from the reserve. This is a
   workload claim about users, not a design choice.

**Limits of this search.** It tested the bounded erasures only, with strictly periodic
inputs. Section 3b shows both limits matter: real inputs need a bigger reserve, and
the all-fire phase sweep fails points this search passed. Frame size is abstract: no
encoder was built to show a 100 B CoT event exists.

**Redone for non-periodic inputs (Rev 4, `knob_ladder_unicast_dither.md`).** Inputs vary
±100 ms about their mean interval, the 8 kbit repair share is held back, and the reserve
covers two whole bursts at one input per second (finding 17). Least-changed passing
settings, control fixture 1 Hz or as shown:

| players | frame | state | control fixture | repair fixture | copies | inputs/s | state age P95 |
|---|---|---|---|---|---|---|---|
| 8 | 100 B | 15 Hz | 7.5 Hz | off | 3 | 1 | 70 ms |
| 8 | 200 B | 10 Hz | 1 Hz | off | 3 | 0.5 | 99 ms |
| 8 | 200 B | 5 Hz | 1 Hz | 5 Hz | 3 | 1 | 186 ms |
| 12 | 100 B | 15 Hz | 1 Hz | off | 3 | 1 | 105 ms |
| 12 | 200 B | 5 Hz | 1 Hz | off | 3 | 0.5 | 188 ms |
| 16 | 100 B | 10 Hz | 1 Hz | off | 3 | 1 | 184 ms |
| 16 | 100 B | 10 Hz | 2 Hz | off | 3 | 0.5 | 100 ms |

Nothing at 200 B carries 16 participants with state age under 210 ms.

## 3b. The full matrix at 200 B and 100 B frames (Rev 4; tables in `ar_summary_200.md`, `ar_summary_100.md`, `point_tables.md`)

Urgent copies, state records and control frames all take the stated size. Sections 1
to 5 of the matrix are the Rev 1 list at that size, with the profile's periodic inputs
and no repair share. Section 6 runs every test on unicast operating points, each with
control fixture 1 Hz, repair sent on request, `sharedEncode=1`, the 8 kbit repair share
held back, and inputs dithered ±100 ms (fixed instants in the phase sweep and the
simultaneous-input rows, which need them). The reserve is 2·c·R·u at one input per
second and c·R·u at 0.5 (finding 17).

**Two participants.** Every Rev 1 verdict holds: 48.6 kbps at 200 B, 28.3 kbps at 100 B,
against 146.2 at 800 B. All phase sweeps pass. Two erasures stay on time down to a
200 kbit/s PHY. The time-bound failures remain: a three-period blockage, blackouts,
45 Hz with two erasures, 30 Hz with one.

**Operating points, unicast** (3 seeds; "late" counts late and missing obligations):

| test | 8 @ 200 B, state 10 Hz | 12 @ 200 B, 5 Hz | 8 @ 100 B, 15 Hz | 12 @ 100 B, 15 Hz | 16 @ 100 B, 10 Hz |
|---|---|---|---|---|---|
| charged / state age P95 | 155 kbps / 126 ms | 136 kbps / 414 ms | 106 kbps / 70 ms | 163 kbps / 104 ms | 153 kbps / 185 ms |
| clean, and two erasures | pass | pass | pass | pass | pass |
| phase sweep, all fire together | 432 of 432 | 0 of 432; 24 % late | 432 of 432 | 432 of 432 | 295 of 432; 1.6 % late |
| jitter 5 and 10 ms with two erasures | pass | pass | pass | pass | pass |
| independent loss 1 / 3 / 5 / 10 % | 0 / 2 / 5 / 18 late | 0 / 3 / 7 / 0.1 % | 0 / 0 / 2 / 0.1 % | 0 / 1 / 5 / 0.1 % | 0 / 4 / 9 / 0.1 % |
| two erasures plus 1 % loss and 5 ms jitter | 1.0 % late | 1.1 % | 0.9 % | 0.9 % | 1.1 % |
| blockage, 1 / 2 copy periods | pass / 1.7 % | pass / 5.4 % | pass / 3.7 % | pass / 4.4 % | pass / 12.8 % |
| blockage, 3 or 5 copy periods | 99 to 100 % late | same | same | same | same |
| blackout 50 / 100 / 250 ms every 10 s | 0.4 / 0.9 / 2.4 % | 0.3 / 1.0 / 2.6 % | 0.4 / 0.9 / 2.4 % | 0.3 / 1.0 / 2.6 % | 0.3 / 1.1 / 2.8 % |
| duplicates and reordering | on time | on time | on time | on time | on time |
| 2 simultaneous inputs | on time; W bound broken; state 211 ms | on time; W bound broken; state 836 ms | on time | on time; W bound broken | on time; W bound broken |
| 4 simultaneous inputs | on time; W bound broken; state 6.2 s | reserve exceeds ceiling | on time | on time; W bound broken | reserve exceeds ceiling |
| partition 1 s, replay, takeover | integrity holds | integrity holds | integrity holds | integrity holds | integrity holds |
| 2-hop routes | on time; state 1.1 s | reserve exceeds ceiling | on time; state 219 ms | on time; state 765 ms | on time; state 1.7 s |
| PHY 5 / 2 / 1 Mbit/s with two erasures | pass / 1.2 % / all | pass / 36 % / all | pass / pass / 17 % | pass / 20 % / all | pass / all / all |
| opportunities 60 / 45 Hz with two erasures | pass / 44 % | pass / 47 % | pass / 43 % | pass / 44 % | pass / 45 % |

The two 16-participant points at 200 B are in `point_tables.md`. With three copies state
age is 1.0 s; with two copies at 0.5 input/s it is 303 ms, and 1 % loss or a one-period
blockage already makes inputs late.

"Duplicates and reordering" fails the profile at every point although every input is
on time: the stimulus delays 1 % of frames by 20 ms, which by itself breaks the 20 ms
delivery bound for the few copies that succeed after a delay.

**What the Rev 4 runs show**

11. **100 B frames carry 12 participants through every bounded test**, the all-fire
    phase sweep included, with state age P95 104 ms. Eight participants pass at 100 B
    with 70 ms state age, and at 200 B with 126 ms.
12. **Past that, the limit is airtime when everyone fires together.** One copy from every
    participant to every other is N(N−1)·u bits. It must fit in one 11.1 ms copy
    period: at 10 Mbit/s that is 111 kbit. 12 @ 100 B needs 10.6 ms and passes; 12 @
    200 B needs 21.1 ms and fails; 16 @ 100 B needs 19.2 ms and fails 1.6 % of the time.
    The ledger never sees this, because each client stays inside its own ceiling. For 16
    on unicast the frame would have to be about 58 B; common delivery needs N·u instead.
13. **Sixteen on unicast is marginal at 100 B and not viable at 200 B.** At 100 B it passes
    every test but the all-fire sweep.
14. **A two-period blockage costs 2 to 13 % of inputs** at every multi-participant point:
    queueing delays the first copy, so the third can still fall inside the blockage.
15. **Recovery time is set by detection cadence and by room.** A lost input is detected
    only after its owner's summary shows the gap twice, so with a 1 Hz control fixture
    and room to spare, recovery after a three-period blockage has P95 1.9 s at 8 @ 100 B,
    2.7 s at 12 @ 100 B and 9.1 s at 16 @ 100 B. A faster fixture helps only where there
    is room: at 8 @ 100 B, 7.5 Hz cuts that P95 to 0.36 s; at 16 @ 100 B it makes it worse
    (36 s), because the fixture takes the bits repair needed. Without room repair starves:
    at 12 @ 200 B, when every input is lost, median recovery is 23 s. The non-urgent
    classes may use only the ceiling less the urgent reserve, and the reserve sized by
    finding 17 sits idle while repair waits. The 8 kbit repair share changed no result
    measurably, since recovering one lost input costs R·u (17.6 kbit at 12 @ 200 B);
    16 kbit trims the two-period tails (P95 744 → 447 ms at 12 @ 200 B) and costs state
    age (431 → 503 ms). Lending the idle urgent reserve to repair was not tried.
16. **Simultaneous inputs cost reserve linearly.** Two at once stay on time at every point,
    but only 8 @ 100 B keeps the urgent-service (W) bound; four at once do not fit at 12
    @ 200 B or 16 @ 100 B.
17. **Non-periodic inputs need a reserve of two whole bursts.** Finding 2's c·R·u + R·u holds
    only for strictly periodic inputs. With intervals as short as 0.9 s, two bursts fall
    inside one rolling second; at 12 @ 200 B the smaller reserve deferred third copies by
    up to 121 ms and made 13 % of inputs late. The fix is a reserve of 2·c·R·u at one
    input per second, and its price is state age: 188 → 414 ms at 12 @ 200 B, 97 → 126 ms
    at 8 @ 200 B. The governing quantity is the shortest interval between one
    participant's inputs, not the mean rate. That is a workload claim to rule on.
18. **Every blackout of 50 ms or more costs inputs** (0.3 to 0.4 % at 50 ms). Rev 3 showed
    some 50 ms rows passing because periodic inputs and a 10 s blackout cycle kept a
    fixed phase.

**Corrected from Rev 3.** The blockage rows' recovery maxima near 114 s, and their missing
obligations, came from the 1 Hz phase lock between inputs and the control fixture
(A15); they are gone with dithered inputs. Rev 3's repair starvation at 12 participants
is real, and finding 15 now says when it happens. Rev 3's 12- and 16-participant 200 B
state ages (188, 202, 506 ms) assumed periodic inputs; the Rev 4 values are 414, 303
and 1,045 ms.

## 3c. Measured CoT sizes and human-limited input (Rev 5)

### What a real CoT event costs (`cot_sizes.md`; fixtures in `cot_fixtures/`)

The profile names every field an AR message must carry but not the schema, so the `<ar_*>`
detail elements are a draft (ruling A18). Charged size adds 28 B of UDP/IPv4 and 28 B of
assumed authentication (A19). Charged bytes, largest of five instances:

| message | profile abstract | CoT XML as specified | CoT XML lean | TAK v1 lean | XML + session dictionary, as specified / lean | binary AR detail, lean |
|---|---|---|---|---|---|---|
| urgent event (and repair data) | 800 | 1,275 | 712 | 556 | 236 / 195 | 301 |
| state record | 800 | 991 | 606 | 450 | 170 / 172 | 271 |
| control summary | 400 | 720 | 440 | 262 | 122 / 91 | 178 |
| repair request | 400 | 582 | 394 | 215 | 89 / 86 | 164 |

19. **Real CoT XML is larger than the profile's 800 B stand-in.** As specified (UUIDs,
    ISO microsecond times, the profile's field names) the urgent event is 1,219 B of
    payload with the XML declaration ATAK sends. That exceeds the profile's 1,200 B
    datagram limit, and the profile says such an urgent event fails. The lean schema (8-hex
    identifiers, integer microsecond times, two- to four-letter names) halves it and
    stays plain CoT XML 2.0, so it needs no negotiation under NET-003.
20. **TAK Protocol v1 saves only the envelope.** Version 1 may add only fields a receiver
    can safely ignore, so the AR detail rides inside as an XML string. It saves 156 to
    212 B of payload per urgent event (656 → 500 B lean; 1,219 → 1,007 B as specified). The round trip preserves the envelope
    and the detail text exactly. A binary AR detail needs a new TAK Protocol version.
21. **Session-dictionary DEFLATE is the smallest form.** Each datagram is compressed on
    its own against a dictionary both ends build at session join, so a lost datagram
    costs nothing else. It inflates to the original CoT XML, but on the wire it is not
    XML 2.0, so it needs the same negotiation as a TAK binary form.
22. **100 B frames are out of reach with CoT.** UDP/IPv4 and authentication alone take
    56 B. Rev 4's 100 B points have no measured encoding behind them.

### What a human trigger finger can offer (sources at the end of this section)

An index finger at maximum effort taps about 6.5 times a second (154 ms apart);
practiced players burst to 10 to 13 a second; the best pistol shooters reach splits of
0.10 to 0.15 s; the famous button-mashing record is 16 a second. The profile's mean of
one input a second says nothing about bursts. Four input models keep that mean
(ruling A17): a renewal process that never puts two inputs closer than 154 ms, and
engagements of 4 or 7 pulls 154 ms apart or 10 pulls 100 ms apart. The renewal model
alone put five inputs inside one second within ten minutes of one participant.

### What survives (`human_input.md`, `common_points.md`)

23. **Unicast cannot carry human-rate bursts at 8 to 16 participants with any measured
    encoding.** Each input costs c·R·u, and a burst multiplies it. The best case is 8
    participants with session-dictionary DEFLATE, a reserve for 4 inputs a second and
    state at 5 Hz: renewal input on time, state age 187 ms. A 7-pull string at 8
    participants is 15 % late even there. Plain CoT XML on unicast fails at 8.
24. **Common delivery carries 16 participants in plain lean CoT XML.** With a reserve for
    7 inputs a second (one finger at its maximum), every bounded test passes: two
    erasures, the phase sweep, all 16 firing 7-pull strings in lockstep (worst L 88 ms),
    jitter, 1 % loss, a 2 Mbit/s PHY. State age 100 ms at 10 Hz, 68 kbps per sender.
    10-pull strings at 100 ms splits make 20 % late in lean XML, because the reserve
    covers 7; TAK v1 lean and the dictionary form carry them. As-specified XML on common
    delivery fails: at 1,275 B the ceiling promises only 6 inputs a second.
25. **The common-delivery points fail where every point fails**: blockages of two or
    more copy periods, blackouts, and two erasures combined with random loss.

Sources: maximal-effort finger tapping, 6.46 ± 0.72 Hz
(https://www.jstage.jst.go.jp/article/jpa/23/4/23_4_105/_article); Shourijeh,
musculoskeletal simulation, maximum about 6 Hz
(https://asmedc.silverchair.com/computationalnonlinear/article-abstract/12/5/051009/474515/Estimation-of-Maximum-Finger-Tapping-Frequency);
pistol split times (https://www.speer.com/speer-stories/improving-split-times.html);
short tapping bursts of 10 to 13 a second, a hobbyist compilation and the weakest source here
(https://canyouclickit.pages.dev/mobile/tps-world-records);
Takahashi Meijin's 16 presses a second
(https://www.nintendolife.com/news/2022/05/random-at-63-takahashi-meijin-still-boasts-a-ludicrously-quick-trigger-finger);
TAK Protocol rules, `protocol.txt` in the ATAK-CIV repository.

## 3d. Encoded wire, serverless alignment, and an 802.11 access-point channel (Rev 6)

### Encoded-wire CoT (`ar_sim codec`, `tools/tak_crosscheck.py`, `wire_points.md`)

With `wire=<form>` every frame the simulator sends is a real CoT message, built from the
frame's own content (owner, entity, sequence, revision, input onset, deadline, clock
epoch, published pose, calibration epoch, acknowledgment, the obligation's value) and
charged at its encoded size plus 28 B UDP/IPv4 and 28 B authentication. The receivers of
each emission decode it once and check it against what the sender meant; a frame that
fails is dropped as lost.

| form | event | state | control | calibration | repair request | encode / decode µs (event) |
|---|---|---|---|---|---|---|
| XML, as specified | 1,214 (**1,270**) | 925 (981) | 811 (867) | 751 (807) | 541 (597) | 9.4 / 11.5 |
| XML, lean | 601 (657) | 506 (562) | 432 (488) | 409 (465) | 341 (397) | 5.3 / 8.5 |
| TAK v1, as specified | 1,002 (1,058) | 713 (769) | 566 (622) | 506 (562) | 296 (352) | 8.8 / 10.1 |
| TAK v1, lean | 445 (501) | 350 (406) | 243 (299) | 220 (276) | 150 (206) | 5.4 / 4.8 |

Payload bytes, charged bytes in parentheses; times are means over 2,000 varied messages on
the Linux build machine, against the profile's 250 µs encode allowance.

26. **Every form round-trips byte for byte.** The self-test encodes, decodes and re-encodes
    all six kinds in all four forms; the twelve TAK samples parse with protobuf classes
    generated from the ATAK-CIV `.proto` files and re-serialize to the same bytes, so the
    hand-written writer is canonical TAK Protocol v1.
27. **In the simulator, no emission failed to decode or disagreed with its sender** in
    either 16-participant battery (ideal and Wi-Fi), nor in a 48-participant lean TAK
    run on the converted-unicast AP channel.
28. **The codec cannot detect every corruption.** Of 445 single-byte flips of a lean TAK
    event, 398 are rejected, 22 decode to the original, and 25 decode to a different valid
    message. Only the authentication tag (A19) stops those.
29. **The Rev 6 lean schema is 55 B smaller than Rev 5's lean fixture** (657 against 712 B
    charged for XML): one- and two-letter names, one-letter codes for operation and
    provenance, empty fields omitted (A20).
30. **As specified, neither form is admissible at 16 participants.** The urgent reserve
    for 7 inputs a second alone is 215,544 bits in XML (and the event exceeds the 1,200 B
    datagram with authentication) and 179,928 bits in TAK v1, leaving too little of the
    200,000-bit ledger for 10 Hz state.

**Expected performance holds on the ideal medium.** The section 3c battery at 16
participants, rerun with real messages and alignment, gives the same verdict in every
row as Rev 5 for lean XML and lean TAK v1. Each sender costs 70 kbps in lean XML (Rev 5:
68) and 50 in lean TAK (Rev 5: 50); calibration adds what the smaller schema saved. The
10-pull lockstep strings are 12.3 % late in lean XML (Rev 5: 20 %). The profile failures
on the duplicates, 3 % loss, blackout and 2 Mbit/s rows occur with Rev 5's abstract sizes
too: the 20 ms reorder delay and 2 Mbit/s serialization break the 20 ms delivery bound,
and a lost repair breaks publication.

### Serverless anchor sharing over the positioning function

The user's peer positioning connection is a function call on a separate radio, not
charged to NET-002. Far (beyond 5 m) it returns the peer's 5 Hz GPS-aided fusion solution
in the mission frame; near (within 5 m) it returns the peer's 960 Hz pose relative to the
caller, in the caller's body axes. Each returns pose and timestamp. Peers still publish
their pose over the participant network at 10 Hz.

Each participant carries an alignment error (x, y, z, heading) between its tracking frame
and the mission frame. Tracking drifts 1 % of distance walked and 0.5° a minute. At each
state sample a participant corrects toward its own far solution (A21), and toward every
near peer by joining the 960 Hz relative fix to that peer's published pose. When its
correction since the last announcement passes 0.1 m or 0.5°, it opens a calibration epoch
and announces it as a control-class frame to every fixture target; control frames also
carry the latest epoch. A receiver moves a marker placed under an older epoch by the
owner's correction since then (A24). Error values and gains are illustrative (A22).

| measure (16 participants, lean XML, ideal medium) | P50 | P95 |
|---|---|---|
| avatar error, peer within 5 m | 0.02 m | 0.26 m |
| avatar error, peer beyond 5 m | 0.9 m | 2.2 m |
| marker error, owner within 5 m | 0.04 m | 0.27 m |
| marker error, owner beyond 5 m | 1.0 m | 2.2 m |
| control and calibration traffic | 10.1 kbps per sender (lean TAK 6.0) | |

P95 values are the worst seed of the battery's two-erasure row; P50 values come from the
seed-1 sweep that chose the gains.

31. **Near peers align to centimeters only if the GPS pull is turned down while a near peer
    anchors** (A23). With one far gain for both cases, each phone follows its own 1.5 m
    GPS wander and the near relative error has a median of 0.26 m. Cutting the far gain
    tenfold while a near peer is fresh brings it to 5 cm; raising the near gain from 0.1
    to 0.3 brings it to about 3 cm (avatar error 2 cm).
32. **The near P95 is first contact.** Two players walking into range bring independent
    GPS-level alignments; the near correction closes the gap in about a second.
33. **Calibration traffic follows GPS wander, not tracking drift.** Epochs scale with the
    far solution's error: about 6 kbps per sender at 1.5 m, 4 at 0.75 m, 2 at 0.3 m.
34. **Far rendering is GPS-limited by construction.** A participant beyond 5 m is shown
    where its own GPS-aided solution and the viewer's alignment put it; nothing in the
    design can do better than the far solution's error.

### 802.11 access-point channel (`channel=wifi_ap`; `wifi_channel.cpp`)

All participants and one AP share one BSS. EDCA contention with the default parameter
set, collisions, acknowledgments and up to 7 retries; the AP relays every participant
frame, either as one group frame at the 6 Mbit/s basic rate with no acknowledgment and no
retry (`group=basic`), or converted to acknowledged unicast per recipient
(`group=unicast`). Beacons every 102.4 ms; with any station in power save the AP holds
group frames until after the next DTIM beacon and unicast frames until the dozing
station's next beacon. Every station attempt, retries included, is charged to its ledger
with 38 B of MAC framing. Rulings A27 to A31 list what is and is not modeled.

**Player-count limits** (`player_limits.md`). The largest count with every obligation on
time in three 30 s seeds; lean XML, alignment on, the Rev 5 operating point otherwise.
"Together" fires every participant at one instant each second; "strings" fires 7-pull
strings 154 ms apart in lockstep. With aligned clocks every device's 90 Hz opportunity
clock shares one phase (the adversarial bound); with own clocks each keeps its own phase
(`syncPhases=2`).

| channel | normal input | together, aligned clocks | strings, aligned clocks | together, own clocks | strings, own clocks |
|---|---|---|---|---|---|
| ideal 10 Mbit/s | 128 / **112** | 40 / **24** | 40 / **20** | 96 / **28** | 64 / **28** |
| AP, group at basic rate, AC_VO | 16 / **2** | 2 / **<2** | <2 / **<2** | 6 / **4** | 6 / **2** |
| AP, group at basic rate, all AC_BE | 32 / **<2** | 4 / **<2** | 2 / **<2** | 8 / **2** | 4 / **<2** |
| AP, converted to unicast, AC_VO | 14 / **14** | 4 / **4** | <2 / **<2** | 8 / **6** | 4 / **4** |
| either relay, one phone in power save | <2 / **<2** | <2 / **<2** | <2 / **<2** | <2 / **<2** | <2 / **<2** |

Each cell: no injected loss / **two erasures on every urgent event**. "<2" means two
participants already fail.

35. **The ideal-medium limits reproduce Rev 5's answers under two erasures** (24 together,
    20 strings against Rev 5's 22), and calibration traffic takes normal input from Rev 5's
    128 to 112.
36. **On an access point, the 16-player claim survives only for normal input with no
    injected loss.** Every relayed frame crosses the channel twice, and the group copy at
    the basic rate takes about eight times the airtime of the uplink copy.
37. **Group frames break the two-erasure guarantee.** A group frame has no acknowledgment
    and no retry, so a collision deletes that copy for every recipient. NET-012 assumes
    the medium adds no loss of its own; with two copies erased, any collision of the third
    makes the input late. Two erasures fail at 4 participants on AC_VO and at 2 on AC_BE.
38. **Converting group frames to unicast restores the guarantee and caps the count at 14.**
    Retries make each delivery reliable (the two-erasure limit equals the clean limit), but
    every emission becomes N−1 downlink frames.
39. **AC_VO collapses with many stations.** Its contention window never exceeds 7, so 16
    stations firing together keep colliding; on AC_BE the window grows to 1,023 and the
    normal-input limit doubles to 32. At 64 participants on AC_VO, 81 % of attempts
    collide.
40. **Retries eat the urgent reserve.** Every retry is charged to NET-002, as the profile
    requires. In lockstep strings with aligned clocks, 17 % of attempts collide even at 2
    participants; the retries use the room the 7-input reserve held for later copies, and
    copies wait for the ledger. A larger reserve starves state instead (P95 age 101 → 960
    ms).
41. **One phone in power save fails the design at any size.** The AP must then hold every
    group frame for the DTIM beacon: up to 102.4 ms at the usual interval, more than the
    whole 100 ms budget. A 20 TU (20.48 ms) beacon interval puts every input on time at 8
    participants with one phone dozing, in either relay mode (`beacon_sweep.md`), for about
    2 % more channel time. Android apps can keep the radio awake: power save must turn off
    while an app holds a `WIFI_MODE_FULL_LOW_LATENCY` or `WIFI_MODE_FULL_HIGH_PERF` lock,
    and the low-latency lock works only in the foreground with the screen on. I found no
    public iOS equivalent.
42. **The 16-player battery on the best AP setting** (group at basic rate, all AC_BE;
    `wire_points_channel-wifi_ap_wifiVoice-0.md`): random loss alone stays on time in lean
    XML (lean TAK: 2 and 5 of 39,345 late), but every row with two erasures is 1.3 to
    3.0 % late, the phase sweep fails every point, and lockstep strings are 61 to 84 %
    late. The PHY rows repeat the two-erasure row, because `phyBps` belongs to the ideal
    medium.
43. **MAC retries can break NET-002 itself.** The ledger admits a frame once; the driver
    retries it without asking. At 16 participants with lockstep strings, retries lift a
    station's charged rolling sum to 378 to 490 kbit on AC_VO (2,293 to 3,963 charges over
    the ceiling per 30 s seed) and to 217 to 241 kbit on AC_BE. Normal input stays under the
    ceiling except with unicast conversion (one seed peaked at 223 kbit, 5 charges over).
    At 4 participants on unicast fan-out, where each station already runs at the ceiling,
    80 charges exceed it; `verify_trace.py` reports each one, in agreement with the
    simulator's `ledgerViolations`. If NET-002 counts retries, as the profile says, the
    ledger needs a standing retry margin or the profile must count only first attempts.

Sources: Android Wi-Fi low-latency mode
(https://source.android.com/docs/core/connect/wifi-low-latency); Android Compatibility
Definition, section 7.4, power save under Wi-Fi locks
(https://android.googlesource.com/platform/compatibility/cdd/+/refs/tags/android-platform-12.1.0_r10/7_hardware-compatibility/7_4_data-connectivity.md);
Apple, Investigating Network Latency Problems (https://developer.apple.com/forums/thread/45210);
EDCA parameters and group addressing, IEEE 802.11-2020; multicast over Wi-Fi, RFC 9119.

## 3e. TAK Protocol v1 on the ideal medium (Rev 7)

Every run below uses the ideal medium (`channel=ideal`, ruling A2) and sends every frame as
a real lean TAK Protocol v1 message (`wire=tak_lean`, section 3d) with alignment on. The
reserves are sized from the largest lean TAK event, 513 B charged. The knob defaults stay
abstract so the regression anchors hold (A32).

### The profile matrix (`ar_summary_tak.md`; Rev 1's 800 B results in `ar_summary.md`)

Two participants, the starter workload, every case and sweep of the Rev 1 list:

| check | 800 B abstract (Rev 1) | TAK v1 lean (Rev 7) |
|---|---|---|
| charged per sender, starter workload | 146.2 kbps | 89.4 kbps |
| Q0, Q1, Q2 and their phase sweeps | all on time; worst L 53.9 / 65.0 / 76.1 ms | the same |
| three participants, unicast, lending as written | 26 % late | on time |
| four participants, unicast, lending as written | 49 % late | 38 % late |
| four participants, unicast, standing reserve | on time; state age 771 ms | on time; 343 ms |
| eight participants, unicast, standing reserve | on time; no state | on time; state 12 kbps, age 2.0 s |
| sixteen participants, unicast, lending as written | 100 % late | 90 % late |
| common delivery, 2 to 32 participants | on time; 146 kbps | on time; 90 kbps; state age 69 ms |
| sixteen, common delivery, PHY 2 / 1 Mbit/s | all late / all late | on time / all late |
| two participants, PHY 500 / 200 kbit/s | a third late / all late | on time / all late |
| four simultaneous inputs, lending as written | 51 % late | on time |
| unchanged | | three-period blockage all late; blackouts 12 / 24 / 36 of 2,280 late; 45 Hz with two erasures 67 % late; two erasures with 1 % loss and 5 ms jitter about 1 % late at 8, 16 and 32 |

44. **Message size moved the load limits, not the timing.** The worst-case latencies and
    every timing failure (blockage, blackout, opportunity rate) are the same as at 800 B;
    every figure that depends on bits per second improved roughly by the ratio of the
    sizes.
45. **Unicast still does not scale.** Lean TAK carries three participants on unicast where
    800 B could not, and four with a standing reserve at a third of the state age. At
    eight, state gets 12 kbps and is two seconds old.

### Human-limited input (`human_input_tak.md`)

46. **Common delivery in lean TAK carries 8, 12 and 16 participants under every input
    model, 10-pull strings included**, with state age P95 98 to 100 ms. The ceiling can
    promise 15 inputs a second against the 10 a trained burst needs.
47. **As specified, TAK v1 promises only 7 inputs a second.** On common delivery, renewal
    input is on time; 4-pull strings are 4.5 % late; a reserve for 7-pull strings keeps
    them on time but starves state (age 22 to 31 s); 10-pull strings are 31 % late.
48. **On unicast both forms fail at 8 to 16.** The best case, lean at 8 participants with
    renewal input, is 21 % late with no state left.

### The 16-participant battery (`wire_points_tak_lean.md`)

Identical to section 3d's lean TAK column: every bounded test passes, the phase sweep and
10-pull lockstep strings included, at 50 kbps per sender and 8 % airtime. The failures are
the ones every point has: blockages of two or more copy periods, blackouts, partition, two
erasures with random loss, and 3 % loss (3 of 39,345 late). Alignment and calibration:
avatar near P95 0.29 m, far 2.2 m; control traffic 6.0 kbps.

### Player-count limits (`player_limits_tak_lean_ideal.md`)

Common delivery, lean TAK, alignment on, three 30 s seeds; the workloads of section 3d.
Each cell: no injected loss / **two erasures**; lean XML (section 3d) in parentheses.

| normal input | together, aligned clocks | strings, aligned clocks | together, own clocks | strings, own clocks |
|---|---|---|---|---|
| 160 / **160** (128 / 112) | 64 / **32** (40 / 24) | 64 / **28** (40 / 20) | 128 / **40** (96 / 28) | 80 / **40** (64 / 28) |

49. **Normal input reaches 160 participants under two erasures**, where lean XML stopped at
    112. At 192 the channel is 93 % busy and 7 % of inputs are late.
50. **Synchronized fire still sets the practical limit**: 28 to 40 participants under two
    erasures, depending on whether device clocks are aligned. Every participant's copies
    must cross the one channel within a copy period; a smaller message only buys more
    participants per period.

## 4. Requirement coverage

| requirement | status in this simulator |
|---|---|
| NET-001 remote response | assessed, conditionally: publication plus the I, P and U allowances; a modeled-photon endpoint is available |
| NET-002 transmit ceiling | assessed exactly; rechecked by `verify_trace.py`. On the Wi-Fi channel, MAC retries can breach it (finding 43) |
| NET-003 CoT format | **assessed in encoded-wire mode** (Rev 6; Rev 7 runs everything in TAK v1 lean): every frame is a CoT message in XML or TAK v1, charged at its size and decoded by its receivers; the AR detail schema is still a draft (A18, A20) |
| NET-004 serverless | assessed: no server or sequencer exists in the model |
| NET-005 local operation | **not assessed**: estimators and renderer are not modeled; the trace writes null |
| NET-006 display, local MTP | **not assessed** |
| NET-007 input processing | supplied allowance, fixed at 10 ms |
| NET-008 urgent service | assessed at actual encoding starts, per copy |
| NET-009 successful delivery | assessed, never clamped |
| NET-010 display allowance | allowance, or modeled as the next 90 Hz frame |
| NET-011 uncertainty | applied once |
| NET-012 loss bursts | assessed (Q1, Q2, phase sweeps) |
| NET-013 event retention | assessed (P0, dedup, revision checks) |
| NET-014 authority | assessed (R1: tombstone, takeover) |
| NET-015 bounded work | assessed for urgent, state, relay and dedup stores; durable transactions are not modeled |
| NET-016 physical feasibility | F0 arithmetic only; the integrated RF and display mode is **not built**. Rev 6 adds an 802.11 access-point channel as a candidate medium, not the DDL |
| NET-017 complete denominator | assessed; every obligation is classified once |
| NET-018 scaling | assessed: participants 2 to 192 on the ideal medium and the AP channel, both fan-outs (Rev 6) |

## 5. Modeling rulings made to run this (each can be overruled)

- A1. **Serial encoder.** One frame at a time, 250 µs each; urgent frames encode first.
  At eight participants on unicast this alone puts the last recipient's copy 1.5 ms past
  the NET-008 bound. `sharedEncode=1` (Rev 2, default 0) encodes identical content once
  for all unicast recipients and removes that violation; section 3a uses it.
- A2. **Medium.** One collision domain, granted to the longest-waiting frame; no
  collisions, no guard time, no MAC overhead beyond the charged frame cost. This is the
  default (`channel=ideal`); Rev 6 adds the 802.11 access-point channel (A27 to A30).
- A3. **Admission precedes encoding** and tests the ledger as it will stand when
  encoding ends. A frame admitted this way can always start within the ceiling.
- A4. **Lending defaults to the document's rule** (`lendUrgentShare=1`). The standing
  reserve is `lendUrgentShare=0 urgentShareBits=<n>`.
- A5. **Fixtures multiply per recipient on unicast**, as section 06 reads
  (`fixturesPerRecipient=0` sends them once).
- A6. **Repair.** A control frame carries its owner's highest fully sent event sequence.
  A recipient asks for a missing event once the gap outlives one summary. Requests and
  repair frames are charged in addition to the fixtures.
- A7. **Modeled photon.** The effect shows in the first 90 Hz frame at or after
  publication, plus a fixed 18.9 ms, so P never exceeds 30 ms.
- A8. **C0 blockage** silences the origin for k opportunity periods from each event's
  first copy. **S0 blackouts** recur every 10 s. **R1** cuts participant 0 from the rest.
- A9. **R1 stimuli** (the replayed create and the takeover claim) are injected
  deliveries, not charged emissions.
- A10. **Relays** are other participants in the same collision domain; relay duty is
  spread by origin and recipient.
- A12. **State and fixture service order** (Rev 2). Each opportunity serves the
  recipient that has waited longest, per section 05's ranking by age. Rev 1 used a fixed
  order and starved later recipients; only state-age figures moved (finding 3).
- A13. **Standing reserve in the knob search** is c·R·u plus R·u at one input per
  second, rounded up to 800 bits, never lent.
- A14. **Repair share** (Rev 4, `repairShareBits`, default 0 so the anchors hold). Control
  and state may not spend it; repair may spend their unused room. The operating points
  use the profile's 8 kbit.
- A15. **Input dither** (Rev 4, `inputDitherUs`, default 0). Each input interval varies
  uniformly by ± half the dither on its own random stream, so no other draw moves. The
  operating points use ±100 ms. This removes the phase lock that Rev 3 found between
  1 Hz inputs and the 1 Hz control fixture, and between inputs and the 10 s blackouts.
- A16. **Reserve for dithered inputs** is 2·c·R·u at one input per second and c·R·u at 0.5,
  rounded up to 800 bits (finding 17). Where it would exceed 184 kbit, the run uses 184
  and the label says so: the 8 kbit repair share and some control must still fit.
- A17. **Human input models** (Rev 5, `inputMinGapUs`, `inputTrain`, `inputTrainGapUs`; all off by
  default). Each keeps the profile's mean rate. A trigger pull is one required input.
- A18. **Draft CoT schema** (Rev 5). Element and type names (`ar_event`, `b-m-p-s-m`,
  `t-x-ar-c`) are placeholders for the type catalog the profile still owes. The lean
  schema's random 32-bit event identifier has a 9 % chance of a collision in a 30-minute,
  16-player session; the event identifier should be owner plus sequence, which costs
  nothing more.
- A19. **Charged overhead** (Rev 5): 28 B UDP/IPv4 and 28 B authentication (12 B nonce, 16 B
  tag). PHY and MAC overhead are not included on the ideal medium; the DDL PHY is not
  selected. On the Rev 6 Wi-Fi channel the MAC framing and every retry are charged (A29).
- A11. **Loss runs** are counted per link over all frames, not per event.
- A20. **Encoded wire** (Rev 6). Times are labeled synthetic UTC from 2026-11-13T13:47:12Z;
  the mission frame has a synthetic surveyed origin (35.0123457, −116.4567891, 712.3 m);
  clock offsets are synthetic. The Rev 6 lean schema uses one- and two-letter names,
  one-letter codes, owner-plus-sequence event identifiers (A18's fix) and omits empty
  fields. State reuses one encoding per revision; a relay re-encodes with itself as
  sender.
- A21. **A participant's own far solution is available** to it (the function returns the
  caller's own GPS-aided pose as well as its peers').
- A22. **Illustrative alignment values**: far solution 1.5 m horizontal, 3 m vertical and
  2° heading error, Gauss-Markov with a 30 s correlation; near fix 2 cm and 0.5°; tracking
  drift 1 % of distance walked and 0.5° a minute; random-waypoint motion at 1.4 m/s
  (20 % of legs at 4 m/s) in a square of side 10·√N m; markers land 3 to 30 m ahead.
  None is a qualified number.
- A23. **Gains from a sweep, not a filter design.** Far gain 0.02 per 5 Hz solution, 0.002
  while a near peer with a pose under 0.5 s old anchors the participant; near gain 0.3
  per state sample; a new epoch past 0.1 m or 0.5°. A Kalman filter would weigh the two
  sources by their errors and should replace this.
- A24. **Marker correction is translation only.** A receiver moves a marker by the owner's
  correction between the marker's epoch and the latest it holds; heading corrections are
  carried but not applied, and a marker whose epoch the receiver never learned is shown
  uncorrected.
- A25. **Calibration frames** are control class, sent once per fixture target as soon as an
  epoch opens, longest-waiting target first; the 1 Hz control fixture also carries the
  latest epoch.
- A26. **Consistency checks report, never reject.** A published pose is compared with the
  positioning fix; residuals past 0.5 m (near) or 5 m (far) are counted.
- A27. **Wi-Fi channel scope.** One BSS, every station in range of every other: no hidden
  stations, capture, rate adaptation, non-collision frame errors, aggregation, block
  acknowledgment, TXOP bursts, EIFS, PS-Poll or U-APSD frames, or AP forwarding delay.
  Data 65 Mbit/s with a 36 µs HT preamble; basic rate 6 Mbit/s, acknowledgments 24 Mbit/s,
  20 µs legacy preamble; 260 B beacons sent after PIFS. A beacon cannot collide.
- A28. **Test-case losses stay end-to-end.** Erasures, blockage, random loss, blackout and
  partition apply at final delivery, after the MAC; the channel adds collisions on top.
- A29. **What NET-002 charges on Wi-Fi.** 38 B of MAC framing on every frame and every
  station attempt, retries included, at its start. Retries are not admitted in advance,
  so they come out of later admissions. The AP's relayed copies charge no participant.
- A30. **Access categories.** Urgent and control frames ride AC_VO, state and repair AC_BE;
  `wifiVoice=0` puts everything on AC_BE. AP and stations use the default EDCA set.
- A31. **Synchronized input with own clocks** (`syncPhases=2`). Every participant's input
  falls at one instant, but each device's opportunity, state and fixture clocks keep their
  own random phases. `syncPhases=1` is Rev 5's adversarial bound.
- A32. **TAK Protocol v1 lean is the operating wire format** (Rev 7). The runs pass
  `wire=tak_lean align=1`; the knob defaults stay `wire=abstract align=0 channel=ideal`, so
  anchors 1 to 4 still check the abstract model. Making TAK the default would move them.

## 6. Open items, priority order

_Renumbered in Rev 6; Rev 5's item 4 (encoded-wire mode) is done._

1. **Choose the radio topology before anything else** (findings 36 to 41). Through a
   standard access point the design carries 16 players only with normal input and no
   injected loss; the profile's two-erasure bound holds only with group frames converted
   to unicast, and only up to 14. Options to rule on: an AP that converts group frames to
   unicast (or offers 802.11aa groupcast with retries), a peer-to-peer link with no AP, or
   a looser loss bound for this medium.
2. **Forbid power save during play** (finding 41): require a low-latency Wi-Fi lock on
   Android, find the iOS answer, and set a 20 TU beacon interval on the AP; otherwise one
   dozing phone makes every input late.
3. **Rule on what NET-002 counts on Wi-Fi** (finding 43, A29): every retry, which no
   application can bound in advance, or first attempts only, with retries counted as
   channel time.
4. **Replace the alignment gains with a filter** (A23) and get qualified error figures for
   both positioning modes (A22).
5. **Freeze multicast (common delivery) support** (findings 23, 24). With human-rate input
   it decides whether 8 to 16 participants work at all; it also lets plain CoT XML stand.
6. **Rule on the AR detail schema** (A18, A20, A32): Rev 7 carries the lean detail inside
   TAK v1's `xmlDetail`; rule on its names, integer times and the type catalog the profile
   owes, and on whether TAK v1 lean becomes the default (which moves the anchors).
7. **Rule on the burst the design promises** (findings 23, 24, 40, 46): 7 inputs a second
   covers one finger at its maximum; 10 covers trained bursts, which lean TAK v1 carries
   on common delivery; on Wi-Fi the reserve must also cover retries.
8. **Choose the control fixture rate against recovery time** (finding 15).
9. **Decide the reservation rule.** Findings 1 and 2 say the profile's lending sentence
   needs a standing reserve sized to a burst plus one copy set. Until that is ruled, every
   multi-participant result depends on a knob.
10. **Integrated RF and display mode**: video bursts and receive-quiet windows gating peer
    frames. Needs architecture version 5, which was not in the package.
11. **Run the `.bat` files on Windows** and confirm the anchors match.
12. **Burst loss at a matched mean**, and blockage sweeps between two and three copy
    periods, where the bounded claim ends.
13. **Durable transactions and receipts** (section 04) are not modeled.
