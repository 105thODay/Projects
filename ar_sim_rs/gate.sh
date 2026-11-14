#!/bin/sh
# Stage 0 and stage 1 gates for the Rust port, on Linux (glibc). Run from ar_sim_rs, with
# the ar_sim C++ tree beside it (../ar_sim) and, for the prost oracle, the TAK .proto files
# in TAKPROTO_DIR (ATAK-CIV takproto/).
set -e
here=$(pwd)
rustc --version
g++ --version | head -1
ldd --version | head -1

echo "== gate 0: the C++ reference writes the corpus of record, byte for byte"
(cd ../ar_sim && ./build.sh)
rm -rf corpus && mkdir corpus
../ar_sim/ar_sim corpus corpus
(cd corpus && sha256sum -c ../corpus_ref/SHA256SUMS)
export AR_SIM_CORPUS="$here/corpus"

echo "== style"
cargo fmt --all --check
cargo clippy --workspace --all-targets --release -- -D warnings

echo "== gate 1: codec and random streams against the corpus (authoritative)"
cargo test --release -p cot_codec -p sim_rng -- --include-ignored --nocapture

echo "== gate 1: the C library emulation against glibc"
CLIB_ITERS=${CLIB_ITERS:-1000000} cargo test --release -p cpp_reference --test clib_oracle

echo "== gate 1: differential fuzzing against the C++ reference"
DIFF_ITERS=${DIFF_ITERS:-1000000} cargo test --release -p cpp_reference --test differential -- --nocapture

echo "== gate 1: TAK wire against prost and the official .proto files"
cargo test --release -p tak_oracle -- --include-ignored --nocapture

echo "== speed, Rust against C++ (informational)"
cargo run --release -q -p cpp_reference --example compare_speed

echo "ALL STAGE 1 GATES PASSED"
