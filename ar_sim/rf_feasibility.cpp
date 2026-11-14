#include "rf_feasibility.hpp"

#include <cstdio>

#include "profile_vectors.hpp"

int RunRfFeasibility(long long aGuardUs)
{
   // All quantities in microseconds as doubles: this is schedule arithmetic on the
   // study's figures, not the event simulation.
   const double framePeriodUs  = 1.0e9 / static_cast<double>(Profile::cVIDEO_MILLIHZ);
   const double burstUs        = static_cast<double>(Profile::cVIDEO_BITS_PER_EYE_FRAME) * 1.0e6 / static_cast<double>(Profile::cVIDEO_BURST_BPS);
   const double grossQuietUs   = framePeriodUs - burstUs;
   const double netQuietUs     = grossQuietUs - 2.0 * static_cast<double>(aGuardUs);
   const double superframeUs   = 1.0e6 / static_cast<double>(Profile::cRANGING_PAIR_HZ);
   const int    framesPerSuper = static_cast<int>(superframeUs / framePeriodUs + 0.5);
   const double rangingUs      = static_cast<double>(Profile::cRANGING_OCCUPANCY_US);
   const double exchangeUs     = rangingUs / static_cast<double>(Profile::cRANGING_PAIRS);

   std::printf("F0 video and ranging coexistence (inherited study; guard %lld us at each window edge)\n", aGuardUs);
   std::printf("  video burst %.3f ms of each %.3f ms frame; gross quiet %.3f ms; net quiet %.3f ms\n",
               burstUs / 1000.0, framePeriodUs / 1000.0, grossQuietUs / 1000.0, netQuietUs / 1000.0);
   std::printf("  ranging: %lld nodes, %lld pairs at %lld Hz, %.3f ms per %.3f ms superframe (%.4f ms per exchange)\n",
               static_cast<long long>(Profile::cRANGING_NODES), static_cast<long long>(Profile::cRANGING_PAIRS),
               static_cast<long long>(Profile::cRANGING_PAIR_HZ), rangingUs / 1000.0, superframeUs / 1000.0, exchangeUs / 1000.0);

   const double availableUs = static_cast<double>(framesPerSuper) * grossQuietUs;
   std::printf("  gross quiet per superframe %.3f ms (%d windows) against %.3f ms needed: deficit %.3f ms\n",
               availableUs / 1000.0, framesPerSuper, rangingUs / 1000.0, (rangingUs - availableUs) / 1000.0);

   // Place whole exchanges: an exchange cannot straddle a video burst.
   int placed = 0;
   for (int window = 0; window < framesPerSuper; ++window)
   {
      const int fits = (netQuietUs > 0.0) ? static_cast<int>(netQuietUs / exchangeUs) : 0;
      for (int slot = 0; slot < fits && placed < Profile::cRANGING_PAIRS; ++slot)
      {
         placed += 1;
      }
   }
   const int unplaced = static_cast<int>(Profile::cRANGING_PAIRS) - placed;
   std::printf("  whole exchanges placed %d of %lld; airtime conflicts (exchanges with no slot) %d per superframe, %d per second\n",
               placed, static_cast<long long>(Profile::cRANGING_PAIRS), unplaced, unplaced * static_cast<int>(Profile::cRANGING_PAIR_HZ));
   std::printf("  peer data: no quiet time remains for participant traffic once ranging is placed\n");
   std::printf("verdict: F0 %s\n", (unplaced > 0) ? "INFEASIBLE as written (expected: the profile requires this to be flagged)" : "feasible");
   return unplaced;
}
