# PLAN — Staged Rust port of `ar_sim`, codec first

_2026-11-14. Reference: `ar_sim` Rev 7 (C++17, 6,469 lines). Companion to `HANDOFF_ar_sim.md`._
_Status: stages 0 and 1 complete on Linux; the stage 1 Windows gate (gate 7) awaits a run on the MSVC workstation._

## 0. Aim and governing rule

Port `ar_sim` to Rust in stages, starting with the CoT codec, so that the codec the
simulator measures is the codec a fielded phone runs, and so that the model builds and
tests on the program's Windows/MSVC workstation without the untested `.bat` path.

**The governing rule: a port changes no result.** Every stage ends when the Rust code
reproduces the C++ reference byte for byte on the gates listed for that stage. Speed
work, new features and output-changing fixes wait until the port is complete (stage 7),
and each then gets its own revision and re-baseline, as with the C++ code.

## 1. Why the port is feasible bit for bit

| C++ property | Rust counterpart | Check |
|---|---|---|
| Event queue: `std::priority_queue`, total order (time, kind order, insertion sequence) | `BinaryHeap<Reverse<(time, order, seq)>>` | Same order by construction; checked by anchors 2 and 3 |
| Time in integer ninths of a microsecond (`int64_t`) | `i64` newtype | Exact |
| `std::mt19937_64`, raw 64-bit output only (no standard distributions) | `rand_mt::Mt64`, `next_u64()` (crate `sim_rng`) | **Passed:** 27 streams × 10,000 draws identical |
| C library calls in the codec: `snprintf("%.*f")`, `strtod`, `strtoll`, `strtoull`, `sscanf` | `cot_codec::clib`, an exact emulation of glibc 2.39 in the C locale | **Passed:** 1,000,000 generated cases per routine against live glibc; see finding F1 |
| `std::sin/cos/log/sqrt/hypot` (46 call sites, alignment and motion) | `f64` methods, which call the platform math library | Bit-identical with glibc on Linux; see ruling R3 for Windows |

## 2. Workspace layout (as built)

```
ar_sim_rs/
  Cargo.toml                 workspace; rust-version = "1.97" (a toolchain file needs the rustup
                             server, which this sandbox cannot reach)
  gate.sh, gate.bat          every stage 0 and stage 1 gate, Linux and Windows/MSVC
  corpus_ref/                the corpus of record (zip, 9.6 MB) and its SHA-256 sums
  crates/cot_codec/          stage 1: message model, C library emulation, XML subset parser,
                             TAK v1 writer/reader; #![forbid(unsafe_code)]; no dependencies
  crates/sim_rng/            stage 1 (for stage 4): the nine MT19937-64 streams and their draws
  crates/cpp_reference/      test only: the C++ codec and the C library behind a C ABI shim,
                             for differential tests; the workspace's only unsafe code
  crates/tak_oracle/         test only: prost types from the official TAK .proto files
  crates/cot_codec_ffi/      stage 2 (to come): C ABI and generated header for the C++ simulator
  crates/cot_codec_mobile/   stage 3 (optional, to come): UniFFI bindings
  crates/ar_core/, ar_sim/   stages 4 to 6 (to come)
```

The Python tools stay as they are. They call `./ar_sim` with key=value arguments and
parse its text, so the Rust binary must print the same lines.

## 3. Stages

### Stage 0 — Freeze the reference and capture golden vectors — **complete**

`ar_sim corpus <dir>` (new `corpus.cpp`, a capture hook in `simulation.cpp`, knobs
`corpus=` and `corpusPerKind=`) writes four files:

| File | Contents |
|---|---|
| `messages.jsonl` | 6,360 synthetic messages: self-test samples, 50 seeded random messages per kind and setup over wide ranges, hand-picked edges (years 0001, 1899, 9999, 10000, leap days, ±0, ±inf, ±NaN, subnormals, `INT64_MIN/MAX`, full-range values); 4 forms × 4 setups (2, 16, 16 and 160 participants) |
| `sim_messages.jsonl` | 21,360 messages the simulator encoded in Q2 (16 and 160 players), R1 and C0 runs, all four forms |
| `flips.jsonl` | 100 messages with every single-byte corruption (55,927) and the decoder's outcome |
| `rng.jsonl` | the first 10,000 raw draws of all nine streams for run seeds 1 to 3 |

**Gate passed:** the corpus regenerates with identical hashes (`corpus_ref/SHA256SUMS`).

### Stage 1 — The Rust codec crate — **complete on Linux**

Ported: the message model (enums for form, kind and operation), field tables,
identifiers, calendar and ISO times, the strict XML subset parser, the hand-written TAK
writer and reader (ruling R2), and the C library behavior the wire depends on (finding F1).

| Gate | Result |
|---|---|
| 1. Corpus encodes byte-identically, all four forms | **Pass:** 27,720 of 27,720 |
| 2. Decode gives the same fields bit for bit, or the same error text; re-encoding matches | **Pass:** 27,660 decoded, 60 rejected alike (the year-10000 edges) |
| 3. Corruption sweep, per flip | **Pass:** 55,927 of 55,927 (53,479 rejected, 928 to the original, 1,520 to another message), same error bytes and re-encodings |
| 4. `prost` from the official TAK .proto files | **Pass:** 13,860 TAK messages parse and re-serialize byte-identically; 13,848 cross-checked field by field |
| 5. Differential fuzzing against the C++ decoder, in-process | **Pass:** 4,000,000 mutated messages (1,000,000 per form), 0 disagreements in outcome, error text, fields or re-encoding; it found finding F2 first |
| 5a. C library emulation against live glibc | **Pass:** 1,000,000 generated cases per routine |
| 5b. Random streams | **Pass:** 270,000 draws identical |
| 6. Speed (same process, same 2,000 messages, best of 25) | Rust encodes in 0.64 and decodes in 0.72 of the C++ time over all 24 form-kind cells. Lean TAK event: C++ 5.59 / 5.74 µs, Rust 3.31 / 5.22 µs |
| 7. `cargo test` on the Windows workstation with MSVC | **Open:** run `gate.bat` (see ruling R6) |

The gates showed why both the corpus and the fuzzer are needed: a planted fault (reject a
trailing comma in `missing`) passes the corpus, because no message carries one, and the
fuzzer catches it within 300,000 cases.

### Stage 2 — The Rust codec inside the C++ simulator (strangler step)

Expose encode and decode through a small C ABI with a generated header. Behind a build
switch, the C++ `CotCodec` forwards to the Rust library; Cargo builds it as a static
library that g++ or MSVC links. The `cpp_reference` shim already proves the message
layout across the boundary in the opposite direction.

**Gates:** with the Rust codec linked, `ar_sim codec`, `wire_points_tak_lean.md`,
`ar_summary_tak.md` and a subset of the player limits come out byte-identical to Rev 7.

### Stage 3 — Phone packaging (optional; ruling R4)

Generate Kotlin and Swift bindings with UniFFI; build Android libraries with `cargo-ndk`
and an iOS XCFramework. If the game runs in Unity, use the third-party C# bindings
generator instead.

**Gates:** the corpus test passes on an Android device and an iPhone; measured encode and
decode times replace the profile's illustrative 250 µs encode allowance.

### Stage 4 — Simulator core, abstract sizes (about 3,400 lines)

Port in the order the anchors test it: `sim_time`, the generated `profile_vectors`,
`doe_config`, `rolling_ledger`, the random streams (done: `sim_rng`), then `simulation`,
`trace_writer`, `analytic_checks`, `rf_feasibility` and the command line. One
`Simulation` owns `Vec<Node>`; events and frames refer to nodes by index.

**Gates, in order:** anchor 1 (`analytic`), anchor 4 (`F0`), anchor 2 (`Q0` seed 1),
anchor 3 (`phase Q2`); then `ar_matrix.txt`, `ar_matrix_200.txt` and `ar_matrix_100.txt`
identical line for line; then `verify_trace.py` agrees on the traced runs.

### Stage 5 — Encoded wire and alignment (about 320 lines plus integration)

**Gates:** `wire_points.md`, `wire_points_tak_lean.md`, `ar_summary_tak.md`,
`human_input_tak.md` and `player_limits_tak_lean_ideal.md` identical on Linux. Note for
R3: the alignment model's `Gauss` draws through `std::log` and `std::cos`.

### Stage 6 — The 802.11 access-point channel (496 lines)

**Gates:** `player_limits.md`, `beacon_sweep.md` and
`wire_points_channel-wifi_ap_wifiVoice-0.md` identical; `verify_trace.py` agrees on the
three Rev 6 Wi-Fi traces.

### Stage 7 — Retire the C++ build, then improve (ruling R5)

One change per revision, each re-baselined: shared frames; a grid index for the near-peer
search; parallel seeds in one process; fixed-size histograms. Then rerun the player
limits past 160 participants. Lean decode is the codec's slowest Rust path (parity with
C++ in three cells); its roster lookup is linear and is the first candidate.

## 4. Rulings

**Decided**

- **R1. Code style: standard Rust** (rustfmt defaults, snake_case), keeping the house
  standard's substance: symbolic constants, explicit conversions, data-only structs,
  comment blocks on public items. `cargo fmt --check` and `clippy -D warnings` are gates.
- **R2. Protobuf: port the hand-written TAK writer and reader**; `prost` is a test oracle
  only (`tak_oracle`), and confirms the writer emits standard protobuf.
- **R6. The C library behavior of record is glibc 2.39** (decided 2026-11-14). The Rust
  codec reproduces it on every platform (finding F1); the shipped corpus is the
  reference. The C++ built with MSVC calls the UCRT, which spells at least NaN
  differently (`-nan(ind)`), so the C++ itself writes different bytes on Windows for such
  values. A Windows-made corpus is never the reference. `gate.bat` runs the
  authoritative gates against the shipped corpus, then reports, for information, where
  the MSVC build of the C++ departs from glibc.
- **R7. C++ bounds fix (finding F2), folded into Rev 7** (decided 2026-11-14; the
  separate-revision rule is waived for this fix). `CotCodec::Participant` returns empty
  text for an index outside the roster, which is what the Rust codec does. Output is
  unchanged: the corpus regenerates with identical hashes, and the analytic, Q0 and codec
  anchors hold.

**Open**

- **R3. Math library.** Platform `f64` functions match the C++ bit for bit on Linux but may
  differ in the last bit on Windows, as the C++ does today. A pure-Rust math library gives
  identical alignment numbers on every platform but re-baselines the alignment figures
  once, after stage 5.
- **R4. Phone target.** Native Kotlin and Swift, Unity (C#) or Unreal (C++).
- **R5. The C++ build after stage 6.** Retire it, or keep it frozen as the reference.
- **R8. XML nesting depth.** Both parsers recurse once per nested element. A 1,200-byte
  datagram holds at most about 400 levels, which neither stack notices; a stream or file
  could hold more and overflow either. Recommended for stage 3: a depth limit in the Rust
  parser, which departs from the C++ only where the C++ would crash.

## 5. Findings

- **F1. The wire depends on the C library, not only on the codec.** The C++ codec's bytes
  and its accept/reject decisions follow glibc in ways a plain Rust port would miss:
  NaN prints as `nan`/`-nan` and infinity as `inf`; `snprintf` into a 64-byte buffer cuts
  very large values at 63 characters; `strtod` reads hex floats, `nan(n-char-sequence)`
  payloads and partial exponents; `strtoll` reads `0x` prefixes and saturates; `sscanf`
  `%4d` takes leading space and a sign within its width; and every parser stops at the
  first NUL byte (so `"\0abc"` reads as the integer 0). `cot_codec::clib` reproduces each
  of these, documented per routine.
- **F2. A malformed lean message could crash a C++ receiver (fixed in Rev 7, R7).** A lean
  entity identifier whose local number reaches the stride (one inserted hex digit does it)
  decodes to an owner past the roster; the C++ then read past the roster vector while
  building the expected uid. That is undefined behavior; in the fuzzer it threw
  `std::bad_alloc`. Any peer, or any corruption the transport's authentication does not
  catch, could trigger it. The Rust decoder rejects such a message ("envelope uid does not
  match the detail"), and the fixed C++ now does the same.

## 6. Risks

- **A gate fails on a last digit.** The corpus and per-message diffs locate it; the rule is
  to fix the port, never the gate.
- **MSVC formatting (decided, R6).** The Rust codec does not depend on the platform C
  library; the C++ does.

## Sources

- `rand_mt` (MT19937-64 as `Mt64`, raw `next_u64` output): https://docs.rs/rand_mt
- rustfmt configuration (stability of `brace_style`, `control_brace_style`):
  https://rust.googlesource.com/rustfmt/+/refs/heads/subtree/Configurations.md
- UniFFI (Kotlin, Swift; third-party C#; cargo-ndk; XCFramework):
  https://github.com/mozilla/uniffi-rs, https://docs.rs/crate/uniffi/latest
- TAK Protocol v1 .proto files: https://github.com/deptofdefense/AndroidTacticalAssaultKit-CIV (`takproto/`)
