# ar_sim_rs — the Rust port of ar_sim (stages 0 and 1)

The plan, rulings and gate results are in `PLAN_ar_sim_rust_port.md`. The governing rule:
a port changes no result.

## Crates

| Crate | Role | Unsafe |
|---|---|---|
| `cot_codec` | Encoded-wire CoT: XML 2.0 and TAK Protocol v1, specified and lean schemas; byte-identical to the C++ codec | forbidden |
| `sim_rng` | The simulator's nine MT19937-64 streams and their portable draws | forbidden |
| `cpp_reference` | Test only: the C++ codec and the C library behind a C shim, for differential tests | the foreign calls |
| `tak_oracle` | Test only: `prost` types from the official TAK .proto files | none |
| `ar_device` | The on-phone half of the Android harness (`android/`): L6 codec conformance and performance, L4 live multicast traffic per the Network Layer ICD | forbidden |

## Running the gates

The `ar_sim` C++ tree must sit beside this one (`../ar_sim`); `AR_SIM_CPP` overrides.
The prost oracle needs the TAK .proto files (`takproto/` from ATAK-CIV) in `TAKPROTO_DIR`.

- **Linux:** `./gate.sh` builds the C++, regenerates the corpus, checks it against
  `corpus_ref/SHA256SUMS`, and runs every gate (about 90 seconds).
- **Windows (MSVC):** `gate.bat` from a Developer Command Prompt. It unpacks the corpus of
  record from `corpus_ref/`, runs the authoritative gates, then reports for information
  where the MSVC build of the C++ departs from glibc (ruling R6).

Knobs: `AR_SIM_CORPUS` (corpus directory), `DIFF_ITERS` (fuzz cases per form),
`DIFF_SEED`, `CLIB_ITERS` (cases per C library routine).

## Using the codec

```rust
use cot_codec::{CotCodec, CotMessage, MessageKind, WireForm};

let codec = CotCodec::new(WireForm::TakLean, 16, session_seed);
let wire = codec.encode(&CotMessage { kind: MessageKind::State, ..message });
let back = codec.decode(&wire)?; // Err carries the reference's error text
```
