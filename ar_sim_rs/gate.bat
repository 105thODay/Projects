@echo off
rem Stage 1 gates for the Rust port on Windows with MSVC. Run from a "Developer Command
rem Prompt for VS" in ar_sim_rs, with the ar_sim C++ tree beside it (..\ar_sim).
rem
rem The corpus of record was made by the C++ reference on Linux (glibc 2.39), and the Rust
rem codec reproduces that behavior on every platform. The C++ reference built with MSVC calls
rem the UCRT instead, which spells some values differently (NaN, for one). So:
rem   AUTHORITATIVE gates run Rust against the shipped corpus and must pass;
rem   INFORMATIONAL steps compare the MSVC build of the C++ with glibc and report where
rem   the C++ itself behaves differently on Windows. Send me their output.
setlocal
set HERE=%CD%

echo == unpack and verify the corpus of record
if not exist corpus\messages.jsonl powershell -NoProfile -Command "Expand-Archive -Force corpus_ref\glibc_corpus.zip corpus"
powershell -NoProfile -Command "$bad = 0; Get-Content corpus_ref\SHA256SUMS | ForEach-Object { $h, $f = $_ -split '\s+'; if ((Get-FileHash ('corpus\' + $f) -Algorithm SHA256).Hash.ToLower() -ne $h) { Write-Host ('HASH MISMATCH ' + $f); $bad = 1 } }; exit $bad"
if errorlevel 1 exit /b 1
set AR_SIM_CORPUS=%HERE%\corpus

echo == style
cargo fmt --all --check
if errorlevel 1 exit /b 1
cargo clippy --workspace --all-targets --release -- -D warnings
if errorlevel 1 exit /b 1

echo == AUTHORITATIVE: codec and random streams against the corpus of record
cargo test --release -p cot_codec -p sim_rng -- --include-ignored --nocapture
if errorlevel 1 exit /b 1
if defined TAKPROTO_DIR (
   cargo test --release -p tak_oracle -- --include-ignored --nocapture
   if errorlevel 1 exit /b 1
) else (
   echo TAKPROTO_DIR not set: prost oracle skipped
)
echo AUTHORITATIVE GATES PASSED

echo == INFORMATIONAL: the MSVC build of the C++ reference
pushd ..\ar_sim
call build.bat
popd
if exist corpus_msvc rmdir /s /q corpus_msvc
mkdir corpus_msvc
..\ar_sim\ar_sim.exe corpus corpus_msvc
for %%f in (messages flips rng sim_messages) do (
   fc /b corpus\%%f.jsonl corpus_msvc\%%f.jsonl > nul && echo %%f.jsonl: MSVC build matches glibc || echo %%f.jsonl: MSVC build DIFFERS from glibc
)
where python > nul 2> nul && python tools\corpus_diff.py corpus corpus_msvc 5

echo == INFORMATIONAL: UCRT against the glibc emulation
cargo test --release -p cpp_reference --test clib_oracle

echo == INFORMATIONAL: differential fuzzing against the MSVC build
cargo test --release -p cpp_reference --test differential -- --nocapture

echo == INFORMATIONAL: speed, Rust against C++ (MSVC)
cargo run --release -q -p cpp_reference --example compare_speed

echo == done: the authoritative gates passed; the informational steps above report MSVC behavior
exit /b 0
