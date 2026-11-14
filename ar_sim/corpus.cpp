// corpus.cpp -- `ar_sim corpus <dir>`: golden vectors for the Rust port (stage 0).
//
// Files written:
//   messages.jsonl      synthetic messages: the self-test samples, seeded random messages over
//                       wide value ranges, and hand-picked edge cases, for every wire form and
//                       several (participants, session seed) pairs
//   sim_messages.jsonl  messages the simulator encoded in real runs (all four forms)
//   flips.jsonl         for a subset of messages, every single-byte corruption (xor 0x5A) and
//                       what the decoder made of it
//   rng.jsonl           the first 10,000 raw draws of every random stream, seeds 1 to 3
// Every value is deterministic, so a second run must reproduce every file byte for byte.

#include "corpus.hpp"

#include <cmath>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <vector>

#include "doe_config.hpp"
#include "simulation.hpp"

namespace
{
   const int      cRANDOM_PER_KIND = 50;             // seeded random messages per kind, per codec
   const int      cFLIP_PER_FORM   = 25;             // messages per form whose every byte is corrupted
   const int      cDRAWS           = 10000;
   const uint64_t cGENERATOR_SEED  = 20261114;
   const int      cFORMS[4]        = { WireMode::cXML_SPECIFIED, WireMode::cXML_LEAN, WireMode::cTAK_SPECIFIED, WireMode::cTAK_LEAN };

   struct CodecSetup
   {
      int      mParticipants;
      uint64_t mSeed;
   };
   const CodecSetup cSETUPS[4] = { { 2, 1 }, { 16, 1 }, { 16, 7 }, { 160, 123456789 } };

   std::string HexBytes(const std::string& aBytes)
   {
      static const char* const cDIGITS = "0123456789abcdef";
      std::string out;
      out.reserve(aBytes.size() * 2);
      for (char c : aBytes)
      {
         const unsigned char byte = static_cast<unsigned char>(c);
         out += cDIGITS[byte >> 4];
         out += cDIGITS[byte & 0x0F];
      }
      return out;
   }

   void Bits(std::FILE* aFile, double aValue)
   {
      uint64_t bits = 0;
      std::memcpy(&bits, &aValue, sizeof(bits));
      std::fprintf(aFile, "\"%016llx\"", static_cast<unsigned long long>(bits));
   }

   void BitsArray(std::FILE* aFile, const double* aValues, int aCount)
   {
      std::fputc('[', aFile);
      for (int index = 0; index < aCount; ++index)
      {
         if (index > 0)
         {
            std::fputc(',', aFile);
         }
         Bits(aFile, aValues[index]);
      }
      std::fputc(']', aFile);
   }

   void WriteMessage(std::FILE* aFile, const CotMessage& aMessage)
   {
      std::fprintf(aFile, "{\"kind\":%d,\"time_us\":%lld,\"stale_us\":%lld,\"lat\":", aMessage.mKind,
                   static_cast<long long>(aMessage.mTimeUs), static_cast<long long>(aMessage.mStaleUs));
      Bits(aFile, aMessage.mLat);
      std::fputs(",\"lon\":", aFile);
      Bits(aFile, aMessage.mLon);
      std::fputs(",\"hae\":", aFile);
      Bits(aFile, aMessage.mHae);
      std::fputs(",\"ce\":", aFile);
      Bits(aFile, aMessage.mCe);
      std::fputs(",\"le\":", aFile);
      Bits(aFile, aMessage.mLe);
      std::fprintf(aFile,
                   ",\"owner\":%d,\"sender\":%d,\"entity\":%lld,\"sequence\":%lld,\"authority_epoch\":%d,\"revision\":%d,"
                   "\"sample_us\":%lld,\"onset_us\":%lld,\"deadline_us\":%lld,\"clock_offset_us\":%lld,\"clock_epoch\":%d,"
                   "\"clock_uncertainty_us\":%d,\"frame_epoch\":%d,\"calibration_epoch\":%d,\"pose\":",
                   aMessage.mOwner, aMessage.mSender, static_cast<long long>(aMessage.mEntity), static_cast<long long>(aMessage.mSequence),
                   aMessage.mAuthorityEpoch, aMessage.mRevision, static_cast<long long>(aMessage.mSampleUs),
                   static_cast<long long>(aMessage.mOnsetUs), static_cast<long long>(aMessage.mDeadlineUs),
                   static_cast<long long>(aMessage.mClockOffsetUs), aMessage.mClockEpoch, aMessage.mClockUncertaintyUs,
                   aMessage.mFrameEpoch, aMessage.mCalibrationEpoch);
      BitsArray(aFile, aMessage.mPose, 7);
      std::fputs(",\"velocity\":", aFile);
      BitsArray(aFile, aMessage.mVelocity, 3);
      std::fprintf(aFile,
                   ",\"operation\":%d,\"ack_sequence\":%lld,\"ack_owner\":%d,\"expiry_us\":%lld,\"value\":%llu,\"membership_epoch\":%d,"
                   "\"highest_sequence\":%lld,\"request_owner\":%d,\"missing\":[",
                   aMessage.mOperation, static_cast<long long>(aMessage.mAckSequence), aMessage.mAckOwner,
                   static_cast<long long>(aMessage.mExpiryUs), static_cast<unsigned long long>(aMessage.mValue), aMessage.mMembershipEpoch,
                   static_cast<long long>(aMessage.mHighestSequence), aMessage.mRequestOwner);
      for (size_t index = 0; index < aMessage.mMissing.size(); ++index)
      {
         std::fprintf(aFile, "%s%lld", (index > 0) ? "," : "", static_cast<long long>(aMessage.mMissing[index]));
      }
      std::fputs("],\"calibration_delta\":", aFile);
      BitsArray(aFile, aMessage.mCalibrationDelta, 4);
      std::fprintf(aFile, ",\"calibration_source\":%d,\"calibration_reference\":%d}", aMessage.mCalibrationSource,
                   aMessage.mCalibrationReference);
   }

   // ---------------------------------------------------------------- message generation

   struct Generator
   {
      std::mt19937_64 mRng;

      uint64_t Raw()
      {
         return mRng();
      }
      double Unit()
      {
         return static_cast<double>(mRng() >> 11) * (1.0 / 9007199254740992.0);
      }
      int64_t Range(int64_t aLow, int64_t aHigh)                // inclusive
      {
         return aLow + static_cast<int64_t>(mRng() % static_cast<uint64_t>(aHigh - aLow + 1));
      }
      bool Chance(double aProbability)
      {
         return Unit() < aProbability;
      }
      // A double in [aLow, aHigh]; sometimes an exact binary fraction, so decimal rounding meets exact ties.
      double Value(double aLow, double aHigh)
      {
         if (Chance(0.2))
         {
            const int    shift = static_cast<int>(Range(1, 12));
            const double step  = std::ldexp(1.0, -shift);
            return std::floor((aLow + (aHigh - aLow) * Unit()) / step) * step;
         }
         if (Chance(0.05))
         {
            return -std::ldexp(Unit(), -static_cast<int>(Range(12, 40)));   // rounds to a negative zero on the wire
         }
         return aLow + (aHigh - aLow) * Unit();
      }
      int64_t Time()
      {
         // 1950 to 2100, microseconds
         return Range(-631152000LL * 1000000LL, 4102444800LL * 1000000LL);
      }
   };

   CotMessage RandomMessage(Generator& aGen, int aKind, int aParticipants)
   {
      CotMessage m;
      m.mKind               = aKind;
      m.mTimeUs             = aGen.Time();
      m.mStaleUs            = m.mTimeUs + aGen.Range(0, 60000000);
      m.mLat                = aGen.Value(-90.0, 90.0);
      m.mLon                = aGen.Value(-180.0, 180.0);
      m.mHae                = aGen.Value(-500.0, 9000.0);
      m.mCe                 = aGen.Chance(0.3) ? 9999999.0 : aGen.Value(0.0, 50.0);
      m.mLe                 = aGen.Chance(0.3) ? 9999999.0 : aGen.Value(0.0, 50.0);
      m.mOwner              = static_cast<int>(aGen.Range(0, aParticipants - 1));
      m.mSender             = aGen.Chance(0.7) ? m.mOwner : static_cast<int>(aGen.Range(0, aParticipants - 1));
      m.mEntity             = static_cast<int64_t>(m.mOwner) * CotCodec::cEntityStride + aGen.Range(0, CotCodec::cEntityStride - 1);
      m.mSequence           = aGen.Chance(0.5) ? aGen.Range(0, 1000) : aGen.Range(0, 0xFFFFFFFFFFLL);
      m.mAuthorityEpoch     = static_cast<int>(aGen.Range(0, 100));
      m.mRevision           = static_cast<int>(aGen.Range(0, 100000));
      m.mSampleUs           = aGen.Time();
      m.mOnsetUs            = aGen.Time();
      m.mDeadlineUs         = aGen.Time();
      m.mClockOffsetUs      = aGen.Range(-5000000, 5000000);
      m.mClockEpoch         = static_cast<int>(aGen.Range(0, 1000));
      m.mClockUncertaintyUs = static_cast<int>(aGen.Range(0, 100000));
      m.mFrameEpoch         = static_cast<int>(aGen.Range(0, 1000));
      m.mCalibrationEpoch   = static_cast<int>(aGen.Range(0, 100000));
      for (int index = 0; index < 3; ++index)
      {
         m.mPose[index]     = aGen.Value(-5000.0, 5000.0);
         m.mVelocity[index] = aGen.Value(-20.0, 20.0);
      }
      for (int index = 3; index < 7; ++index)
      {
         m.mPose[index] = aGen.Value(-1.0, 1.0);
      }
      m.mOperation      = static_cast<int>(aGen.Range(0, 2));
      if (aGen.Chance(0.3))
      {
         m.mAckSequence = -1;
         m.mAckOwner    = -1;
      }
      else
      {
         m.mAckOwner    = static_cast<int>(aGen.Range(0, aParticipants - 1));
         m.mAckSequence = aGen.Range(0, 0xFFFFFFFFFFLL);
      }
      m.mExpiryUs        = aGen.Time();
      m.mValue           = aGen.Raw();
      m.mMembershipEpoch = static_cast<int>(aGen.Range(0, 1000));
      m.mHighestSequence = aGen.Range(-1, 0xFFFFFFFFFFLL);
      m.mRequestOwner    = static_cast<int>(aGen.Range(0, aParticipants - 1));
      const int64_t missing = aGen.Range(0, 12);
      for (int64_t index = 0; index < missing; ++index)
      {
         m.mMissing.push_back(aGen.Chance(0.1) ? -aGen.Range(1, 1000) : aGen.Range(0, 0xFFFFFFFFLL));
      }
      for (int index = 0; index < 3; ++index)
      {
         m.mCalibrationDelta[index] = aGen.Value(-2.0, 2.0);
      }
      m.mCalibrationDelta[3]    = aGen.Value(-0.1, 0.1);
      m.mCalibrationSource      = static_cast<int>(aGen.Range(0, 1));
      m.mCalibrationReference   = aGen.Chance(0.4) ? -1 : static_cast<int>(aGen.Range(0, aParticipants - 1));
      return m;
   }

   // Hand-picked values at the edges of every formatter and parser the codec uses.
   std::vector<CotMessage> EdgeMessages(int aParticipants)
   {
      std::vector<CotMessage> out;
      const int64_t times[] = {
         0, 1, -1, 999, 1000, 951782399999999LL,          // 2000-02-28T23:59:59.999999Z
         951868799999999LL,                               // 2000-02-29T23:59:59.999999Z
         1709251199999999LL,                              // 2024-02-29T23:59:59.999999Z
         1735689599999999LL, 1735689600000000LL,          // the end of 2024, the start of 2025
         -62135596800000000LL,                            // 0001-01-01T00:00:00Z
         253402300799999999LL,                            // 9999-12-31T23:59:59.999999Z
         253402300800000000LL,                            // 10000-01-01: five-digit year, which the reader rejects
         -2208988800000001LL                              // 1899-12-31T23:59:59.999999Z
      };
      const double doubles[] = { 0.0, -0.0, 0.5, -0.5, 0.125, 0.0625, 1e-9, -1e-9, 1e15, -1e15, 2.5e-5, 9999999.0,
                                 std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
                                 std::numeric_limits<double>::quiet_NaN(), -std::numeric_limits<double>::quiet_NaN(),
                                 std::numeric_limits<double>::denorm_min(), 123456.78949999999 };
      int index = 0;
      for (int64_t time : times)
      {
         for (int kind = 0; kind < MessageKind::cCOUNT; ++kind)
         {
            CotMessage m;
            m.mKind            = kind;
            m.mTimeUs          = time;
            m.mStaleUs         = time;
            m.mSampleUs        = time;
            m.mOnsetUs         = time;
            m.mDeadlineUs      = time;
            m.mExpiryUs        = time;
            m.mOwner           = index % aParticipants;
            m.mSender          = (index + 1) % aParticipants;
            m.mRequestOwner    = (index + 2) % aParticipants;
            m.mEntity          = static_cast<int64_t>(m.mOwner) * CotCodec::cEntityStride + (index % 3 == 0 ? CotCodec::cEntityStride - 1 : index);
            m.mSequence        = (index % 2 == 0) ? 0 : 0xFFFFFFFFFFFFLL;
            m.mValue           = (index % 2 == 0) ? 0 : 0xFFFFFFFFFFFFFFFFULL;
            m.mClockOffsetUs   = (index % 2 == 0) ? std::numeric_limits<int64_t>::min() : std::numeric_limits<int64_t>::max();
            m.mHighestSequence = (index % 2 == 0) ? -1 : std::numeric_limits<int64_t>::max();
            m.mMissing         = (index % 3 == 0) ? std::vector<int64_t>{} : std::vector<int64_t>{ 0, -1, std::numeric_limits<int64_t>::min() };
            m.mCalibrationReference = (index % 2 == 0) ? -1 : aParticipants - 1;
            m.mAckOwner        = (index % 2 == 0) ? -1 : aParticipants - 1;
            m.mAckSequence     = (index % 2 == 0) ? -1 : 0;
            const size_t count = sizeof(doubles) / sizeof(doubles[0]);
            m.mLat = doubles[(static_cast<size_t>(index) + 0) % count];
            m.mLon = doubles[(static_cast<size_t>(index) + 1) % count];
            m.mHae = doubles[(static_cast<size_t>(index) + 2) % count];
            m.mCe  = doubles[(static_cast<size_t>(index) + 3) % count];
            m.mLe  = doubles[(static_cast<size_t>(index) + 4) % count];
            for (int axis = 0; axis < 7; ++axis)
            {
               m.mPose[axis] = doubles[(static_cast<size_t>(index) + 5 + static_cast<size_t>(axis)) % count];
            }
            for (int axis = 0; axis < 3; ++axis)
            {
               m.mVelocity[axis] = doubles[(static_cast<size_t>(index) + 12 + static_cast<size_t>(axis)) % count];
            }
            for (int axis = 0; axis < 4; ++axis)
            {
               m.mCalibrationDelta[axis] = doubles[(static_cast<size_t>(index) + 15 + static_cast<size_t>(axis)) % count];
            }
            m.mOperation = index % 3;
            out.push_back(m);
            ++index;
         }
      }
      return out;
   }

   // The codec self-test's representative messages (codec_selftest.cpp), three variants each.
   CotMessage Sample(int aKind, int aVariant)
   {
      const int64_t base   = 1794577632000000LL + static_cast<int64_t>(aVariant) * 11111;
      const double  wiggle = 0.001 * static_cast<double>(aVariant % 97);
      CotMessage m;
      m.mKind = aKind;
      m.mTimeUs = base;
      m.mStaleUs = base + 10000000;
      m.mOwner = 3;
      m.mSender = 3;
      m.mClockEpoch = 1;
      m.mClockUncertaintyUs = 1000;
      m.mFrameEpoch = 1;
      m.mCe = 9999999.0;
      m.mLe = 9999999.0;
      m.mEntity = 3 * CotCodec::cEntityStride + 41 + aVariant;
      m.mSequence = 41 + aVariant;
      m.mAuthorityEpoch = 1;
      m.mRevision = 1;
      m.mSampleUs = base - 1200;
      m.mOnsetUs = base - 11100;
      m.mDeadlineUs = m.mOnsetUs + 100000;
      m.mCalibrationEpoch = 4;
      m.mPose[3] = 0.9238795;
      m.mPose[6] = 0.3826834 + wiggle * 0.01;
      m.mExpiryUs = m.mOnsetUs + 30000000;
      m.mValue = 734981u + static_cast<uint64_t>(aVariant);
      m.mLat = 35.0124811 + wiggle * 1e-4;
      m.mLon = -116.4566102;
      m.mHae = 713.3;
      m.mHighestSequence = 41 + aVariant;
      m.mClockOffsetUs = -213;
      m.mMembershipEpoch = 1;
      m.mAckOwner = 7;
      m.mAckSequence = 18 + aVariant;
      m.mRequestOwner = 9;
      m.mMissing = { 17 + aVariant, 18 + aVariant, 21 + aVariant };
      m.mCalibrationDelta[0] = 0.0712;
      m.mCalibrationDelta[1] = -0.0333 + wiggle;
      m.mCalibrationDelta[2] = 0.0041;
      m.mCalibrationDelta[3] = 0.001234;
      m.mCalibrationSource = 1;
      m.mCalibrationReference = 11;
      m.mVelocity[0] = 1.234;
      m.mVelocity[1] = -0.567;
      m.mVelocity[2] = 0.012;
      return m;
   }

   void WriteFlips(std::FILE* aFile, int aForm, int aParticipants, uint64_t aSeed, const CotCodec& aCodec, const std::string& aWire)
   {
      std::fprintf(aFile, "{\"form\":%d,\"n\":%d,\"seed\":%llu,\"wire\":\"%s\",\"flips\":[", aForm, aParticipants,
                   static_cast<unsigned long long>(aSeed), HexBytes(aWire).c_str());
      for (size_t index = 0; index < aWire.size(); ++index)
      {
         std::string bad = aWire;
         bad[index] = static_cast<char>(bad[index] ^ 0x5A);
         CotMessage  back;
         std::string error;
         int         code = 0;
         std::string data;
         if (!aCodec.Decode(bad, back, error))
         {
            data = error;
         }
         else
         {
            data = aCodec.Encode(back);
            code = (data == aWire) ? 1 : 2;
            if (code == 1)
            {
               data.clear();
            }
         }
         std::fprintf(aFile, "%s[%d,\"%s\"]", (index > 0) ? "," : "", code, HexBytes(data).c_str());
      }
      std::fputs("]}\n", aFile);
   }

   std::FILE* Open(const std::string& aPath)
   {
      std::FILE* file = std::fopen(aPath.c_str(), "wb");
      if (file == nullptr)
      {
         std::fprintf(stderr, "cannot write %s\n", aPath.c_str());
      }
      return file;
   }
}

void WriteCorpusRecord(std::FILE* aFile, int aForm, int aParticipants, uint64_t aSeed, const CotCodec& aCodec,
                       const CotMessage& aMessage, const std::string& aWire)
{
   CotMessage  decoded;
   std::string error;
   const bool  ok = aCodec.Decode(aWire, decoded, error);
   std::fprintf(aFile, "{\"form\":%d,\"n\":%d,\"seed\":%llu,\"msg\":", aForm, aParticipants, static_cast<unsigned long long>(aSeed));
   WriteMessage(aFile, aMessage);
   std::fprintf(aFile, ",\"wire\":\"%s\",\"ok\":%s,", HexBytes(aWire).c_str(), ok ? "true" : "false");
   if (ok)
   {
      std::fputs("\"decoded\":", aFile);
      WriteMessage(aFile, decoded);
      std::fprintf(aFile, ",\"reencoded_same\":%s}\n", (aCodec.Encode(decoded) == aWire) ? "true" : "false");
   }
   else
   {
      std::fprintf(aFile, "\"error\":\"%s\"}\n", HexBytes(error).c_str());
   }
}

int RunCorpus(const char* aDir)
{
   const std::string dir(aDir);
   std::FILE* messages = Open(dir + "/messages.jsonl");
   std::FILE* flips    = Open(dir + "/flips.jsonl");
   std::FILE* rng      = Open(dir + "/rng.jsonl");
   if (messages == nullptr || flips == nullptr || rng == nullptr)
   {
      return 1;
   }
   long written = 0;
   long flipped = 0;
   Generator gen{ std::mt19937_64(cGENERATOR_SEED) };
   for (int form : cFORMS)
   {
      int forForm = 0;
      for (const CodecSetup& setup : cSETUPS)
      {
         const CotCodec codec(form, setup.mParticipants, setup.mSeed);
         std::vector<CotMessage> list;
         for (int kind = 0; kind < MessageKind::cCOUNT; ++kind)
         {
            if (setup.mParticipants > 11)                // the samples name participants up to 11
            {
               for (int variant = 0; variant < 3; ++variant)
               {
                  list.push_back(Sample(kind, variant));
               }
            }
            for (int count = 0; count < cRANDOM_PER_KIND; ++count)
            {
               list.push_back(RandomMessage(gen, kind, setup.mParticipants));
            }
         }
         for (const CotMessage& edge : EdgeMessages(setup.mParticipants))
         {
            list.push_back(edge);
         }
         for (size_t index = 0; index < list.size(); ++index)
         {
            const std::string wire = codec.Encode(list[index]);
            WriteCorpusRecord(messages, form, setup.mParticipants, setup.mSeed, codec, list[index], wire);
            ++written;
            if (forForm < cFLIP_PER_FORM && index % 13 == 0)
            {
               WriteFlips(flips, form, setup.mParticipants, setup.mSeed, codec, wire);
               ++forForm;
               ++flipped;
            }
         }
      }
   }

   // Simulator-captured messages: the operating point, repair and takeover paths, and a large roster.
   const std::string simPath = dir + "/sim_messages.jsonl";
   std::fclose(Open(simPath));
   const char* const forms[4] = { "xml_specified", "xml_lean", "tak_specified", "tak_lean" };
   const std::vector<std::vector<std::string>> runs = {
      { "Q2", "nodes=16", "fanout=common", "align=1", "durationS=10", "syncPhases=1", "inputTrain=7", "inputTrainGapUs=154000" },
      { "R1", "nodes=4", "align=1", "durationS=10" },
      { "C0", "nodes=8", "fanout=common", "blockPeriods=3", "align=1", "durationS=20" },
      { "Q2", "nodes=160", "fanout=common", "align=1", "durationS=3" }
   };
   for (const char* form : forms)
   {
      for (const std::vector<std::string>& run : runs)
      {
         DoeConfig config = MakeCase(run[0]);
         for (size_t index = 1; index < run.size(); ++index)
         {
            ApplyOverride(run[index], config);
         }
         ApplyOverride(std::string("wire=") + form, config);
         ApplyOverride("urgentShareBits=180000", config);
         ApplyOverride("lendUrgentShare=0", config);
         config.mCorpusFile = simPath;
         Simulation sim(config);
         sim.Run();
      }
   }

   // Raw draws of every random stream the simulator and the alignment model seed.
   for (uint64_t seed = 1; seed <= 3; ++seed)
   {
      const std::pair<const char*, uint64_t> streams[] = {
         { "setup", seed * 4 + 1 }, { "loss", seed * 4 + 2 }, { "jitter", seed * 4 + 3 }, { "fault", seed * 4 + 4 },
         { "input", seed ^ 0x9E3779B97F4A7C15ULL }, { "align", seed ^ 0xA11C0FFEE0DDF00DULL },
         { "wifi", seed ^ 0x80211E0CA0F1C0DEULL }, { "motion", seed * 7919 + 17 }, { "positioning", seed * 104729 + 3 }
      };
      for (const auto& stream : streams)
      {
         std::mt19937_64 engine(stream.second);
         std::fprintf(rng, "{\"run_seed\":%llu,\"stream\":\"%s\",\"seed\":%llu,\"draws\":[", static_cast<unsigned long long>(seed),
                      stream.first, static_cast<unsigned long long>(stream.second));
         for (int draw = 0; draw < cDRAWS; ++draw)
         {
            std::fprintf(rng, "%s\"%016llx\"", (draw > 0) ? "," : "", static_cast<unsigned long long>(engine()));
         }
         std::fputs("]}\n", rng);
      }
   }
   std::fclose(messages);
   std::fclose(flips);
   std::fclose(rng);
   std::printf("corpus: %ld synthetic messages, %ld corrupted messages, simulator messages in %s, 27 random streams\n",
               written, flipped, simPath.c_str());
   return 0;
}
