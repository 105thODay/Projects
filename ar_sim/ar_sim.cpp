// Networked AR Renderer: networking requirements simulator.
//
// Usage:
//   ar_sim analytic                         README step 1: timing, deadline and ledger oracle checks
//   ar_sim codec [dumpDir]                  encoded-wire CoT round trip, sizes and speed (Rev 6)
//   ar_sim corpus <dir>                     golden vectors for the Rust port (stage 0)
//   ar_sim F0 [guardUs]                     video and ranging coexistence (RF feasibility gate)
//   ar_sim <case> [nSeeds] [key=value ...]  transport-queue run; cases Q0 Q1 Q2 R0 J0 C0 S0 P0 O0 R1
//   ar_sim phase <case> [key=value ...]     12 x 12 x 3 input and display phase sweep
//
// This is the profile's transport-queue mode. The input (I) and display (P)
// stages are supplied allowances, so every result is a CONDITIONAL networking
// result: local motion-to-photon, optical photon timing and the local estimators
// are not assessed. With wire=<mode> every frame carries a real CoT encoding whose
// size is charged and which every emission's receivers decode (Rev 6).

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "analytic_checks.hpp"
#include "codec_selftest.hpp"
#include "corpus.hpp"
#include "doe_config.hpp"
#include "rf_feasibility.hpp"
#include "simulation.hpp"

namespace
{
   const int    cPHASE_STEPS      = 12;              // J0 phase_fraction_grid
   const int    cPHASE_DURATION_S = 20;              // each phase point: 14 inputs per node at one fixed phase
   const int    cPHASE_WARMUP_S   = 5;
   const double cBITS_PER_KBIT    = 1000.0;

   double Quantile(std::vector<double> aSamples, double aFraction)
   {
      if (aSamples.empty())
      {
         return -1.0;
      }
      std::sort(aSamples.begin(), aSamples.end());
      const size_t last = aSamples.size() - 1;
      return aSamples[std::min(static_cast<size_t>(aFraction * static_cast<double>(last) + 0.5), last)];
   }

   // "P50/P95/max" of a sample set, or "none".
   std::string Spread(const std::vector<double>& aSamples, double aScale, const char* aFormat)
   {
      if (aSamples.empty())
      {
         return "none";
      }
      char text[96];
      std::snprintf(text, sizeof(text), aFormat, aScale * Quantile(aSamples, 0.5), aScale * Quantile(aSamples, 0.95),
                    aScale * Quantile(aSamples, 1.0));
      return text;
   }

   double Percentile(std::vector<SimTime> aSamples, double aFraction)
   {
      if (aSamples.empty())
      {
         return -1.0;
      }
      std::sort(aSamples.begin(), aSamples.end());
      const size_t last = aSamples.size() - 1;
      const size_t rank = static_cast<size_t>(aFraction * static_cast<double>(last) + 0.5);
      return ToMs(aSamples[std::min(rank, last)]);
   }

   void PrintRun(const DoeConfig& aConfig, const RunResult& aResult)
   {
      const double seconds = static_cast<double>(aConfig.mDurationS);
      const long   failed  = aResult.obligations - aResult.onTime;
      std::printf("seed %lld: obligations %ld  onTime %ld  late %ld  missing %ld  rejected %ld  incorrect %ld  dupEffects %ld  missFraction %.6f"
                  "  L(P50/P95/P99/max) %.3f/%.3f/%.3f/%.3f ms over %zu delivered-correct  publication(max) %.3f ms (bound 59)\n",
                  static_cast<long long>(aConfig.mSeed), aResult.obligations, aResult.onTime, aResult.late, aResult.missing, aResult.rejected,
                  aResult.incorrect, aResult.duplicateEffects,
                  (aResult.obligations > 0) ? static_cast<double>(failed) / static_cast<double>(aResult.obligations) : 0.0,
                  Percentile(aResult.latency, 0.50), Percentile(aResult.latency, 0.95), Percentile(aResult.latency, 0.99),
                  Percentile(aResult.latency, 1.00), aResult.latency.size(), ToMs(aResult.publishMax));

      std::printf("stage: W(max by copy)");
      for (SimTime wait : aResult.waitMaxByCopy)
      {
         std::printf(" %.3f", ToMs(wait));
      }
      std::printf(" ms  copyGap(max) %.3f ms  D(max) %.3f ms  P(max) %.3f ms  violations W %ld gap %ld D %ld publication %ld  urgentDeferred %ld\n",
                  ToMs(aResult.gapMax), ToMs(aResult.deliveryMax), ToMs(aResult.displayMax), aResult.waitViolations, aResult.gapViolations,
                  aResult.deliveryViolations, aResult.publicationViolations, aResult.urgentDeferrals);

      int64_t maxRolling = 0;
      int64_t maxCharged = 0;
      int64_t sumCharged = 0;
      for (size_t node = 0; node < aResult.maxRollingBits.size(); ++node)
      {
         maxRolling = std::max(maxRolling, aResult.maxRollingBits[node]);
         maxCharged = std::max(maxCharged, aResult.chargedBits[node]);
         sumCharged += aResult.chargedBits[node];
      }
      const double nodes = static_cast<double>(aResult.chargedBits.size());
      std::printf("load: maxRolling %lld bits (ceiling %lld)  meanCharged(node mean/max) %.1f/%.1f kbps  byClass(control/urgent/state/repair) %.1f/%.1f/%.1f/%.1f kbps per node"
                  "  airtime %.2f%%  stateSuperseded %ld  fixturesShed %ld  ledgerViolations %ld\n",
                  static_cast<long long>(maxRolling), static_cast<long long>(aConfig.mLimitBits),
                  static_cast<double>(sumCharged) / nodes / seconds / cBITS_PER_KBIT, static_cast<double>(maxCharged) / seconds / cBITS_PER_KBIT,
                  static_cast<double>(aResult.bitsByClass[FrameClass::cCONTROL]) / nodes / seconds / cBITS_PER_KBIT,
                  static_cast<double>(aResult.bitsByClass[FrameClass::cURGENT]) / nodes / seconds / cBITS_PER_KBIT,
                  static_cast<double>(aResult.bitsByClass[FrameClass::cSTATE]) / nodes / seconds / cBITS_PER_KBIT,
                  static_cast<double>(aResult.bitsByClass[FrameClass::cREPAIR]) / nodes / seconds / cBITS_PER_KBIT,
                  100.0 * ToMs(aResult.airtime) / (seconds * 1000.0), aResult.superseded, aResult.shedFixtures, aResult.ledgerViolations);

      std::printf("net: emissions %ld  deliveries %ld  lost %ld (%.3f%%)  lossRuns(1/2/3/4/5+) %ld/%ld/%ld/%ld/%ld  lateUrgentArrivals %ld of %ld"
                  "  stateAge(P50/P95/max) %.1f/%.1f/%.1f ms\n",
                  aResult.emissions, aResult.deliveries, aResult.deliveriesLost,
                  (aResult.deliveries > 0) ? 100.0 * static_cast<double>(aResult.deliveriesLost) / static_cast<double>(aResult.deliveries) : 0.0,
                  aResult.burstHistogram[1], aResult.burstHistogram[2], aResult.burstHistogram[3], aResult.burstHistogram[4], aResult.burstHistogram[5],
                  aResult.lateUrgentArrivals, aResult.urgentArrivals,
                  Percentile(aResult.stateAge, 0.50), Percentile(aResult.stateAge, 0.95), Percentile(aResult.stateAge, 1.00));

      std::printf("integrity: dedupSuppressed %ld  revisionRejected %ld  staleStateRejected %ld  replayRejected %ld  unauthorizedRejected %ld"
                  "  resurrections %ld  unauthorizedApplied %ld  repair(requests/frames) %ld/%ld  recovery(max) %s ms  unrecovered %ld\n",
                  aResult.dedupSuppressed, aResult.revisionRejected, aResult.rollbackPrevented, aResult.replayRejected, aResult.unauthorizedRejected,
                  aResult.resurrections, aResult.unauthorizedApplied, aResult.repairRequests, aResult.repairFrames,
                  (aResult.recoveryMax < 0) ? "null" : std::to_string(ToMs(aResult.recoveryMax)).c_str(), aResult.unrecovered);

      std::printf("queues: urgent high-water %ld events %lld bytes (cap %lld/%lld)  mac %ld frames  relay backlog %ld frames (dropped %ld)\n",
                  aResult.urgentHighEvents, static_cast<long long>(aResult.urgentHighBytes), static_cast<long long>(aConfig.mUrgentEventCap),
                  static_cast<long long>(aConfig.mUrgentByteCap), aResult.macHigh, aResult.forwardHigh, aResult.relayDropped);

      if (aConfig.mWire != WireMode::cABSTRACT)
      {
         static const char* const cKIND[MessageKind::cCOUNT] = { "event", "state", "control", "repairReq", "repairStatus", "calibration" };
         std::printf("wire:");
         for (int kind = 0; kind < MessageKind::cCOUNT; ++kind)
         {
            std::vector<double> sizes(aResult.wireSizes[static_cast<size_t>(kind)].begin(), aResult.wireSizes[static_cast<size_t>(kind)].end());
            std::printf(" %s %s", cKIND[kind], Spread(sizes, 1.0, "%.0f/%.0f/%.0f").c_str());
         }
         std::printf(" B payload (P50/P95/max)  decoded %ld emissions  decodeFail %ld  oversize %ld (urgent %ld)%s%s\n",
                     aResult.wireEmissionsDecoded, aResult.wireDecodeFailures, aResult.wireOversize, aResult.wireOversizeUrgent,
                     aResult.wireFirstError.empty() ? "" : "  first error: ", aResult.wireFirstError.c_str());
      }
      if (aConfig.mChannel == Channel::cWIFI_AP)
      {
         std::vector<double> access(aResult.wifiAccessUs), relay(aResult.wifiRelayUs);
         std::printf("wifi: busy %.2f%%  attempts %ld  collided %ld (%.2f%%)  internal %ld  dropped %ld  group %ld (lost %ld)  apUnicast %ld  buffered %ld"
                     "  beacons %ld  apQueue(max) %ld  access %s us  relay %s us (P50/P95/max)\n",
                     100.0 * ToMs(aResult.wifiBusy) / (seconds * 1000.0), aResult.wifiAttempts, aResult.wifiCollided,
                     (aResult.wifiAttempts > 0) ? 100.0 * static_cast<double>(aResult.wifiCollided) / static_cast<double>(aResult.wifiAttempts) : 0.0,
                     aResult.wifiInternal, aResult.wifiDropped, aResult.wifiGroupFrames, aResult.wifiGroupLost, aResult.wifiApUnicast,
                     aResult.wifiBuffered, aResult.wifiBeacons, aResult.wifiApQueueHigh, Spread(access, 1.0, "%.0f/%.0f/%.0f").c_str(),
                     Spread(relay, 1.0, "%.0f/%.0f/%.0f").c_str());
      }
      if (aConfig.mAlign != 0)
      {
         std::printf("align: absolute %s m  relativeNear %s m  epochs %ld  calibrationFrames %ld\n",
                     Spread(aResult.alignAbsolute, 1.0, "%.3f/%.3f/%.3f").c_str(), Spread(aResult.alignRelativeNear, 1.0, "%.3f/%.3f/%.3f").c_str(),
                     aResult.calibrationEpochs, aResult.calibrationFrames);
         std::printf("render: avatar near %s far %s m  marker near %s far %s m (uncorrected %ld)  check near %s m (>%.2f: %ld) far %s m (>%.1f: %ld)\n",
                     Spread(aResult.avatarNear, 1.0, "%.3f/%.3f/%.3f").c_str(), Spread(aResult.avatarFar, 1.0, "%.3f/%.3f/%.3f").c_str(),
                     Spread(aResult.markerNear, 1.0, "%.3f/%.3f/%.3f").c_str(), Spread(aResult.markerFar, 1.0, "%.3f/%.3f/%.3f").c_str(),
                     aResult.markerUncorrected, Spread(aResult.checkNear, 1.0, "%.3f/%.3f/%.3f").c_str(), aConfig.mCheckNearM, aResult.checkNearFail,
                     Spread(aResult.checkFar, 1.0, "%.3f/%.3f/%.3f").c_str(), aConfig.mCheckFarM, aResult.checkFarFail);
      }
      std::printf("verdict: service %s  profile %s  conditional-network %s  [photons %s; local MTP, estimators%s: not assessed]\n",
                  aResult.servicePass ? "PASS" : "FAIL", aResult.profilePass ? "PASS" : "FAIL", aResult.conditionalPass ? "PASS" : "FAIL",
                  (aConfig.mEndpoint == Endpoint::cMODELED_PHOTON) ? "modeled" : "not assessed, P allowance used",
                  (aConfig.mWire == WireMode::cABSTRACT) ? ", CoT encoding" : "");
   }

   bool ApplyAll(int aFirst, int aArgc, char** aArgv, DoeConfig& aConfig, int& aSeeds)
   {
      for (int arg = aFirst; arg < aArgc; ++arg)
      {
         const std::string text(aArgv[arg]);
         if (text.find('=') == std::string::npos)
         {
            aSeeds = std::atoi(aArgv[arg]);
         }
         else if (!ApplyOverride(text, aConfig))
         {
            std::printf("unknown override: %s\n", aArgv[arg]);
            return false;
         }
      }
      return true;
   }

   int RunPhaseSweep(DoeConfig aConfig)
   {
      aConfig.mEndpoint  = Endpoint::cMODELED_PHOTON;
      aConfig.mDurationS = cPHASE_DURATION_S;
      aConfig.mWarmupS   = cPHASE_WARMUP_S;
      std::printf("ar_sim phase sweep: %s\n", DescribeConfig(aConfig).c_str());
      const int64_t offsets[] = { -1, 0, 1 };
      long   runs = 0, servicePasses = 0, profilePasses = 0, obligations = 0, onTime = 0;
      double worst = 0.0, worstPublication = 0.0, worstWait = 0.0;
      double worstInput = 0.0, worstRx = 0.0;
      int64_t worstOffset = 0;
      for (int input = 0; input < cPHASE_STEPS; ++input)
      {
         for (int rx = 0; rx < cPHASE_STEPS; ++rx)
         {
            for (int64_t offset : offsets)
            {
               DoeConfig point = aConfig;
               point.mInputPhaseFrac = static_cast<double>(input) / cPHASE_STEPS;
               point.mRxPhaseFrac    = static_cast<double>(rx) / cPHASE_STEPS;
               point.mPhaseOffsetUs  = offset;
               Simulation sim(point);
               sim.Run();
               const RunResult& result = sim.Result();
               runs          += 1;
               servicePasses += result.servicePass ? 1 : 0;
               profilePasses += result.profilePass ? 1 : 0;
               obligations   += result.obligations;
               onTime        += result.onTime;
               const double latency = Percentile(result.latency, 1.00);
               if (latency > worst)
               {
                  worst       = latency;
                  worstInput  = point.mInputPhaseFrac;
                  worstRx     = point.mRxPhaseFrac;
                  worstOffset = offset;
               }
               worstPublication = std::max(worstPublication, ToMs(result.publishMax));
               for (SimTime wait : result.waitMaxByCopy)
               {
                  worstWait = std::max(worstWait, ToMs(wait));
               }
            }
         }
      }
      std::printf("phase: runs %ld  service PASS %ld  profile PASS %ld  obligations %ld  onTime %ld  worst L %.6f ms at inputPhase %.4f rxPhase %.4f offset %lld us"
                  "  worst publication %.6f ms  worst W %.6f ms\n",
                  runs, servicePasses, profilePasses, obligations, onTime, worst, worstInput, worstRx, static_cast<long long>(worstOffset),
                  worstPublication, worstWait);
      std::printf("verdict: phase sweep %s\n", (servicePasses == runs) ? "PASS" : "FAIL");
      return (servicePasses == runs) ? 0 : 1;
   }
}

int main(int argc, char** argv)
{
   if (argc < 2)
   {
      std::printf("usage: ar_sim analytic | codec [dumpDir] | F0 [guardUs] | <case> [nSeeds] [key=value ...] | phase <case> [key=value ...]\ncases:");
      for (const std::string& name : CaseNames())
      {
         std::printf(" %s", name.c_str());
      }
      std::printf("\n");
      return 1;
   }
   const std::string command(argv[1]);
   if (command == "analytic")
   {
      return RunAnalyticChecks() == 0 ? 0 : 1;
   }
   if (command == "corpus" && argc > 2)
   {
      return RunCorpus(argv[2]);
   }
   if (command == "codec")
   {
      return RunCodecSelfTest((argc > 2) ? argv[2] : nullptr);
   }
   if (command == "F0")
   {
      RunRfFeasibility((argc > 2) ? std::atoll(argv[2]) : 0);
      return 0;
   }
   int seeds = 1;
   if (command == "phase")
   {
      if (argc < 3)
      {
         std::printf("usage: ar_sim phase <case> [key=value ...]\n");
         return 1;
      }
      DoeConfig config = MakeCase(argv[2]);
      if (!ApplyAll(3, argc, argv, config, seeds))
      {
         return 1;
      }
      return RunPhaseSweep(config);
   }

   DoeConfig config = MakeCase(command);
   if (!ApplyAll(2, argc, argv, config, seeds))
   {
      return 1;
   }
   std::printf("ar_sim %s\n", DescribeConfig(config).c_str());
   std::printf("note: transport-queue mode; PHY, encode, decode and propagation are illustrative inputs, packet costs are %s\n",
               (config.mWire == WireMode::cABSTRACT) ? "abstract" : "the encoded CoT sizes");
   const int64_t firstSeed = config.mSeed;
   const std::string tracePrefix = config.mTracePrefix;
   for (int replicate = 0; replicate < seeds; ++replicate)
   {
      config.mSeed = firstSeed + replicate;
      if (!tracePrefix.empty() && seeds > 1)
      {
         config.mTracePrefix = tracePrefix + "_seed" + std::to_string(config.mSeed);
      }
      Simulation sim(config);
      sim.Run();
      PrintRun(config, sim.Result());
   }
   return 0;
}
