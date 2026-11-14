#ifndef CORPUS_HPP
#define CORPUS_HPP

#include <cstdint>
#include <cstdio>
#include <string>

#include "cot_codec.hpp"

// Stage 0 of the Rust port (PLAN_ar_sim_rust_port.md): golden vectors from the C++
// reference. Every record is one JSON object per line. Doubles are written as their IEEE
// 754 bit patterns in hex, so no decimal conversion stands between the two codecs.

// One message: the codec's parameters, the message as given, its wire bytes, and what
// this codec decodes from those bytes (or the error it reports).
void WriteCorpusRecord(std::FILE* aFile, int aForm, int aParticipants, uint64_t aSeed, const CotCodec& aCodec,
                       const CotMessage& aMessage, const std::string& aWire);

// `ar_sim corpus <dir>`: synthetic and edge-case messages, simulator-captured messages,
// per-flip corruption outcomes and random-stream draws. Returns 0 on success.
int RunCorpus(const char* aDir);

#endif
