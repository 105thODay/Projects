#!/bin/sh
g++ -std=c++17 -O2 -Wall -Wextra -Wconversion -Wno-sign-conversion alignment.cpp analytic_checks.cpp ar_sim.cpp codec_selftest.cpp corpus.cpp cot_codec.cpp doe_config.cpp rf_feasibility.cpp rolling_ledger.cpp simulation.cpp trace_writer.cpp wifi_channel.cpp -o ar_sim
