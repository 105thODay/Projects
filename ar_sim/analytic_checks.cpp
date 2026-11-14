#include "analytic_checks.hpp"

#include <cmath>
#include <cstdio>

#include "profile_vectors.hpp"
#include "rolling_ledger.hpp"

namespace
{
   const double cVECTOR_TOLERANCE_MS = 1.0e-9;       // the profile carries full-precision vectors

   int Report(bool aPass, const char* aText)
   {
      std::printf("  %s  %s\n", aPass ? "pass" : "FAIL", aText);
      return aPass ? 0 : 1;
   }
}

bool ResponsePasses(SimTime aElapsed, SimTime aUncertainty, bool aCorrect, int aApplyCount, SimTime aDeadline)
{
   if (aElapsed < 0 || !aCorrect || aApplyCount != 1)
   {
      return false;
   }
   return aElapsed + aUncertainty <= aDeadline;
}

SimTime WorstPhaseUpperBound(int64_t aOpportunityMilliHz, int aErasedBeforeSuccess,
                             SimTime aInput, SimTime aDelivery, SimTime aDisplay, SimTime aUncertainty)
{
   const SimTime period = PeriodFromMilliHz(aOpportunityMilliHz);
   return aInput + static_cast<SimTime>(aErasedBeforeSuccess + 1) * period + aDelivery + aDisplay + aUncertainty;
}

int RunAnalyticChecks()
{
   int  failures = 0;
   char text[200];
   const SimTime deadline = FromUs(Profile::cDEADLINE_US);

   std::printf("timing vectors (L = I + (k+1)T + D + P + U, exact):\n");
   for (int index = 0; index < Profile::cTIMING_VECTOR_COUNT; ++index)
   {
      const TimingVector& vector = Profile::cTIMING_VECTORS[index];
      const SimTime bound = WorstPhaseUpperBound(static_cast<int64_t>(vector.mOpportunitiesHz) * 1000, vector.mErasedBeforeSuccess,
                                                 FromUs(vector.mInputMs * 1000), FromUs(vector.mDeliveryMs * 1000),
                                                 FromUs(vector.mDisplayMs * 1000), FromUs(vector.mUncertaintyMs * 1000));
      const bool valueOk   = std::fabs(ToMs(bound) - vector.mLatencyUpperMs) <= cVECTOR_TOLERANCE_MS;
      const bool verdictOk = ((bound <= deadline) == vector.mExpectedPass);
      // Displayed to six places rounded upward, as the document's tables are.
      const double shown = std::ceil(ToMs(bound) * 1.0e6 - 1.0e-6) / 1.0e6;
      std::snprintf(text, sizeof(text), "%-22s %.6f ms  %s", vector.mCase, shown, (bound <= deadline) ? "within 100 ms" : "over 100 ms");
      failures += Report(valueOk && verdictOk, text);
   }

   std::printf("deadline boundary vectors (inclusive limit):\n");
   for (int index = 0; index < Profile::cBOUNDARY_VECTOR_COUNT; ++index)
   {
      const BoundaryVector& vector = Profile::cBOUNDARY_VECTORS[index];
      const SimTime elapsed = (vector.mElapsedUs < 0) ? -1 : FromUs(vector.mElapsedUs);
      const bool verdict = ResponsePasses(elapsed, FromUs(vector.mUncertaintyUs), vector.mCorrect, vector.mApplyCount, deadline);
      std::snprintf(text, sizeof(text), "elapsed %lld us, U %lld us, correct %d, applied %d -> %s",
                    static_cast<long long>(vector.mElapsedUs), static_cast<long long>(vector.mUncertaintyUs),
                    vector.mCorrect ? 1 : 0, vector.mApplyCount, verdict ? "pass" : "fail");
      failures += Report(verdict == vector.mExpectedPass, text);
   }

   std::printf("rolling-window edges (window (t - 1 s, t], charge at transmit start):\n");
   const SimTime window = FromUs(Profile::cWINDOW_US);
   {
      RollingLedger ledger(window, Profile::cLIMIT_BITS);
      const bool full = ledger.Fits(0, Profile::cLIMIT_BITS);
      ledger.Charge(0, Profile::cLIMIT_BITS, false);
      failures += Report(full && ledger.Violations() == 0, "200000 bits at one instant in an empty ledger: allowed exactly at the ceiling");
      failures += Report(!ledger.Fits(1, 1), "one more bit one tick later: denied");
      failures += Report(!ledger.Fits(window - 1, 1), "one more bit one tick before the charge ages out: denied");
      failures += Report(ledger.Fits(window, 1), "one more bit exactly 1 s after the charge: allowed (the charge has left the window)");
   }
   {
      // A burst that straddles a calendar-second boundary: 120000 bits at 0.9 s and
      // 120000 at 1.1 s pass any fixed one-second bin and must fail the rolling rule.
      RollingLedger ledger(window, Profile::cLIMIT_BITS);
      ledger.Charge(FromUs(900000), 120000, false);
      failures += Report(!ledger.Fits(FromUs(1100000), 120000), "boundary-straddling burst (120000 + 120000 bits, 0.2 s apart): denied");
      failures += Report(ledger.Fits(FromUs(1900000), 120000), "the same second burst exactly 1 s after the first: allowed");
   }

   std::printf("analytic checks: %d failure%s\n", failures, failures == 1 ? "" : "s");
   return failures;
}
