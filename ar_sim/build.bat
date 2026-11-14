@echo off
rem Build ar_sim with MSVC. Run from a "Developer Command Prompt for VS".
cl /nologo /EHsc /O2 /std:c++17 /W4 alignment.cpp analytic_checks.cpp ar_sim.cpp codec_selftest.cpp corpus.cpp cot_codec.cpp doe_config.cpp rf_feasibility.cpp rolling_ledger.cpp simulation.cpp trace_writer.cpp wifi_channel.cpp /Fe:ar_sim.exe
if errorlevel 1 exit /b 1
echo.
echo Gate 1 (must end "analytic checks: 0 failures"):
ar_sim.exe analytic
echo.
echo Regression anchor (seed 1 must read: obligations 228 onTime 228 ... L(P50/P95/P99/max) 51.792/51.792/51.792/51.792 ms):
ar_sim.exe Q0
echo.
echo Rev 6 gate (must end "codec self-test: 0 failures"):
ar_sim.exe codec
