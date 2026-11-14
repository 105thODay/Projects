// codec_selftest.cpp -- `ar_sim codec [dumpDir]`: round-trip, size and speed checks for
// the encoded-wire CoT codec, in every wire mode and for every message kind.
//
// Round trip: encode, decode, re-encode; the two encodings must be byte-identical.
// Sizes: payload bytes per kind and mode, and the charged size (payload + UDP/IP + auth).
// Speed: mean encode and decode time per message on this machine.
// With a dump directory, each TAK-mode encoding is written there as <mode>_<kind>.bin,
// so tools/tak_crosscheck.py can parse it with the protobuf classes generated from the
// TAK project's own .proto files.

#include "codec_selftest.hpp"
#include "cot_codec.hpp"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
   const char* const cMODE_NAME[WireMode::cCOUNT] = { "abstract", "xml_specified", "xml_lean", "tak_specified", "tak_lean" };
   const char* const cKIND_NAME[MessageKind::cCOUNT] = { "event", "state", "control", "repair_request", "repair_status", "calibration" };
   const int         cPARTICIPANTS = 16;
   const int         cUDP_IP_BYTES = 28;
   const int         cAUTH_BYTES   = 28;
   const int         cDATAGRAM_LIMIT = 1200;
   const int         cBENCH_ROUNDS = 2000;

   // A representative message of each kind; values vary with aVariant so the benchmark
   // does not encode one constant message.
   CotMessage Sample(int aKind, int aVariant)
   {
      const int64_t base = 1794577632000000LL + static_cast<int64_t>(aVariant) * 11111;   // 2026-11-13T13:47:12Z
      const double  wiggle = 0.001 * static_cast<double>(aVariant % 97);
      CotMessage m;
      m.mKind               = aKind;
      m.mTimeUs             = base;
      m.mStaleUs            = base + 10000000;
      m.mOwner              = 3;
      m.mSender             = 3;
      m.mClockEpoch         = 1;
      m.mClockUncertaintyUs = 1000;
      m.mFrameEpoch         = 1;
      m.mCe                 = 9999999.0;
      m.mLe                 = 9999999.0;
      switch (aKind)
      {
      case MessageKind::cEVENT:
         m.mEntity           = 3 * CotCodec::cEntityStride + 41 + aVariant;
         m.mSequence         = 41 + aVariant;
         m.mAuthorityEpoch   = 1;
         m.mRevision         = 1;
         m.mSampleUs         = base - 1200;
         m.mOnsetUs          = base - 11100;
         m.mDeadlineUs       = m.mOnsetUs + 100000;
         m.mCalibrationEpoch = 4;
         m.mPose[3] = 0.9238795; m.mPose[4] = 0.0; m.mPose[5] = 0.0; m.mPose[6] = 0.3826834 + wiggle * 0.01;
         m.mOperation        = Operation::cCREATE;
         m.mExpiryUs         = m.mOnsetUs + 30000000;
         m.mValue            = 734981u + static_cast<uint64_t>(aVariant);
         m.mStaleUs          = m.mExpiryUs;
         m.mLat = 35.0124811 + wiggle * 1e-4; m.mLon = -116.4566102; m.mHae = 713.3;
         m.mCe = 1.5; m.mLe = 2.0;
         break;
      case MessageKind::cSTATE:
         m.mEntity           = 3 * CotCodec::cEntityStride;
         m.mSequence         = 900 + aVariant;
         m.mAuthorityEpoch   = 1;
         m.mRevision         = 900 + aVariant;
         m.mSampleUs         = base;
         m.mCalibrationEpoch = 4;
         m.mPose[0] = 12.3456 + wiggle; m.mPose[1] = -7.6543; m.mPose[2] = 1.6123;
         m.mPose[3] = 0.9659258; m.mPose[4] = 0.0123457; m.mPose[5] = -0.0034567; m.mPose[6] = 0.2588190;
         m.mVelocity[0] = 1.234; m.mVelocity[1] = -0.567; m.mVelocity[2] = 0.012;
         m.mStaleUs = base + 2000000;
         m.mLat = 35.0122766; m.mLon = -116.4566535 + wiggle * 1e-4; m.mHae = 713.9;
         m.mCe = 1.5; m.mLe = 2.0;
         break;
      case MessageKind::cCONTROL:
         m.mHighestSequence  = 41 + aVariant;
         m.mClockOffsetUs    = -213;
         m.mMembershipEpoch  = 1;
         m.mAckOwner         = 7;
         m.mAckSequence      = 18 + aVariant;
         m.mCalibrationEpoch = 4;
         m.mCalibrationDelta[0] = 0.0712; m.mCalibrationDelta[1] = -0.0333 + wiggle; m.mCalibrationDelta[2] = 0.0041; m.mCalibrationDelta[3] = 0.001234;
         break;
      case MessageKind::cREPAIR_REQUEST:
         m.mRequestOwner = 9;
         m.mMissing      = { 17 + aVariant, 18 + aVariant, 21 + aVariant };
         break;
      case MessageKind::cREPAIR_STATUS:
         m.mHighestSequence = 41 + aVariant;
         break;
      case MessageKind::cCALIBRATION:
         m.mSampleUs             = base;
         m.mCalibrationEpoch     = 5;
         m.mCalibrationSource    = 1;
         m.mCalibrationReference = 11;
         m.mCalibrationDelta[0] = 0.0812 + wiggle; m.mCalibrationDelta[1] = -0.0333; m.mCalibrationDelta[2] = 0.0041; m.mCalibrationDelta[3] = 0.001834;
         break;
      default:
         break;
      }
      return m;
   }
}

int RunCodecSelfTest(const char* aDumpDir)
{
   int failures = 0;
   std::printf("codec self-test: %d participants, charged = payload + %d B UDP/IP + %d B auth; datagram limit %d B\n",
               cPARTICIPANTS, cUDP_IP_BYTES, cAUTH_BYTES, cDATAGRAM_LIMIT);
   std::printf("%-14s %-15s %8s %8s %10s %10s  %s\n", "mode", "kind", "payload", "charged", "encode_us", "decode_us", "check");
   for (int mode = WireMode::cXML_SPECIFIED; mode < WireMode::cCOUNT; ++mode)
   {
      const CotCodec codec(mode, cPARTICIPANTS, 1);
      for (int kind = 0; kind < MessageKind::cCOUNT; ++kind)
      {
         const std::string first = codec.Encode(Sample(kind, 0));
         CotMessage  decoded;
         std::string error;
         bool        ok = codec.Decode(first, decoded, error);
         std::string second;
         if (ok)
         {
            second = codec.Encode(decoded);
            ok = (second == first);
            if (!ok)
            {
               error = "re-encoding differs";
            }
         }
         if (ok && decoded.mKind != kind)
         {
            ok = false;
            error = "kind changed";
         }
         const int  payload = static_cast<int>(first.size());
         const bool oversize = payload + cAUTH_BYTES > cDATAGRAM_LIMIT;

         // Benchmark over varied messages.
         std::vector<CotMessage>  messages;
         std::vector<std::string> wires;
         for (int i = 0; i < cBENCH_ROUNDS; ++i)
         {
            messages.push_back(Sample(kind, i));
         }
         const auto t0 = std::chrono::steady_clock::now();
         for (const CotMessage& message : messages)
         {
            wires.push_back(codec.Encode(message));
         }
         const auto t1 = std::chrono::steady_clock::now();
         int roundFail = 0;
         for (size_t i = 0; i < wires.size(); ++i)
         {
            CotMessage back;
            std::string why;
            if (!codec.Decode(wires[i], back, why) || codec.Encode(back) != wires[i])
            {
               roundFail += 1;
            }
         }
         const auto t2 = std::chrono::steady_clock::now();
         const double encodeUs = std::chrono::duration<double, std::micro>(t1 - t0).count() / cBENCH_ROUNDS;
         // Decode time includes one re-encode for the check; subtract the encode mean.
         const double decodeUs = std::chrono::duration<double, std::micro>(t2 - t1).count() / cBENCH_ROUNDS - encodeUs;
         ok = ok && roundFail == 0;
         failures += ok ? 0 : 1;
         std::printf("%-14s %-15s %8d %8d %10.2f %10.2f  %s%s%s\n", cMODE_NAME[mode], cKIND_NAME[kind], payload,
                     payload + cUDP_IP_BYTES + cAUTH_BYTES, encodeUs, decodeUs, ok ? "pass" : "FAIL ",
                     ok ? "" : error.c_str(), oversize ? "  (exceeds datagram)" : "");
         if (aDumpDir != nullptr && (mode == WireMode::cTAK_SPECIFIED || mode == WireMode::cTAK_LEAN))
         {
            const std::string path = std::string(aDumpDir) + "/" + cMODE_NAME[mode] + "_" + cKIND_NAME[kind] + ".bin";
            if (FILE* file = std::fopen(path.c_str(), "wb"))
            {
               std::fwrite(first.data(), 1, first.size(), file);
               std::fclose(file);
            }
         }
         if (aDumpDir != nullptr && (mode == WireMode::cXML_SPECIFIED || mode == WireMode::cXML_LEAN))
         {
            const std::string path = std::string(aDumpDir) + "/" + cMODE_NAME[mode] + "_" + cKIND_NAME[kind] + ".xml";
            if (FILE* file = std::fopen(path.c_str(), "wb"))
            {
               std::fwrite(first.data(), 1, first.size(), file);
               std::fclose(file);
            }
         }
      }
   }
   // Corruption must be caught, never mis-decoded: flip each byte of one lean event in turn.
   {
      const CotCodec    codec(WireMode::cTAK_LEAN, cPARTICIPANTS, 1);
      const std::string wire = codec.Encode(Sample(MessageKind::cEVENT, 0));
      int rejected = 0, silent = 0;
      for (size_t i = 0; i < wire.size(); ++i)
      {
         std::string bad = wire;
         bad[i] = static_cast<char>(bad[i] ^ 0x5A);
         CotMessage  back;
         std::string why;
         if (!codec.Decode(bad, back, why))
         {
            rejected += 1;
         }
         else if (codec.Encode(back) == wire)
         {
            silent += 1;    // decoded to the original: harmless
         }
      }
      std::printf("corruption sweep (tak_lean event, %zu single-byte flips): %d rejected, %d decoded to the original, %zu decoded to a different message\n",
                  wire.size(), rejected, silent, wire.size() - static_cast<size_t>(rejected + silent));
      std::printf("  (a different message is what the transport's authentication tag exists to stop; the codec alone cannot)\n");
   }
   std::printf("codec self-test: %d failures\n", failures);
   return failures == 0 ? 0 : 1;
}
