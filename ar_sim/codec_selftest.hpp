// codec_selftest.hpp -- `ar_sim codec [dumpDir]`: encoded-wire CoT codec checks.
#ifndef CODEC_SELFTEST_HPP
#define CODEC_SELFTEST_HPP

// Returns 0 when every mode and kind round-trips byte for byte.
int RunCodecSelfTest(const char* aDumpDir);

#endif
