#ifndef ANALYTIC_CHECKS_HPP
#define ANALYTIC_CHECKS_HPP

#include <cstdint>

#include "sim_time.hpp"

// The profile's scoring rule for one response obligation (scoring.response_pass):
// correct, applied exactly once, and elapsed plus uncertainty at most the deadline.
// aElapsed < 0 means the photon time is null. All limits are inclusive.
bool ResponsePasses(SimTime aElapsed, SimTime aUncertainty, bool aCorrect, int aApplyCount, SimTime aDeadline);

// Worst-phase upper bound L = I + (k + 1) T + D + P + U, in exact ticks.
SimTime WorstPhaseUpperBound(int64_t aOpportunityMilliHz, int aErasedBeforeSuccess,
                             SimTime aInput, SimTime aDelivery, SimTime aDisplay, SimTime aUncertainty);

// README step 1: verify every timing vector, every boundary vector, and the
// rolling-window edges against the generated profile data. Returns failures.
int RunAnalyticChecks();

#endif
