#ifndef ROLLING_LEDGER_HPP
#define ROLLING_LEDGER_HPP

#include <cstdint>
#include <deque>

#include "sim_time.hpp"

// NET-002 ledger for one client. Every physical emission is charged its complete
// coded bit cost at its transmit start. The charged sum over the half-open window
// (t - window, t] may never exceed the ceiling. This is the profile's exact rule:
// a token bucket is not equivalent, and fixed calendar bins miss straddling bursts.
class RollingLedger
{
public:
   RollingLedger(SimTime aWindow, int64_t aLimitBits);

   // Charged bits inside (aNow - window, aNow]. Entries at or before aNow - window age out.
   int64_t SumAt(SimTime aNow);
   int64_t NonUrgentSumAt(SimTime aNow);
   int64_t RepairSumAt(SimTime aNow);

   // The same sums as they will stand at a later instant aThen >= aNow if nothing
   // more is charged: charges that age out by aThen are left out. Used at admission,
   // because a frame admitted at aNow cannot start before its encoding ends.
   int64_t SumAhead(SimTime aNow, SimTime aThen);
   int64_t NonUrgentSumAhead(SimTime aNow, SimTime aThen);
   int64_t RepairSumAhead(SimTime aNow, SimTime aThen);

   // True when aBits more may start at aNow without passing the ceiling.
   bool    Fits(SimTime aNow, int64_t aBits);

   // Record an emission starting at aNow. Charges must arrive in time order.
   // aRepair marks repair-class emissions, so a held-back repair share can be enforced.
   void    Charge(SimTime aNow, int64_t aBits, bool aUrgent, bool aRepair = false);

   int64_t MaxObserved() const;                      // largest window sum seen at any charge instant
   int64_t TotalCharged() const;                     // all bits ever charged
   int64_t Violations() const;                       // charges that left the window above the ceiling

private:
   struct Entry
   {
      SimTime mTime;                                 // transmit start
      int64_t mBits;                                 // complete coded frame cost
      bool    mUrgent;                               // urgent-class emission
      bool    mRepair;                               // repair-class emission
   };

   void Prune(SimTime aNow);

   SimTime           mWindow;                        // rolling window length
   int64_t           mLimitBits;                     // ceiling
   std::deque<Entry> mEntries;                       // charges still inside some window
   int64_t           mSum;                           // bits in mEntries
   int64_t           mNonUrgentSum;                  // bits in mEntries outside the urgent class
   int64_t           mRepairSum;                     // bits in mEntries of the repair class
   int64_t           mMaxObserved;                   // high-water window sum
   int64_t           mTotalCharged;                  // lifetime bits
   int64_t           mViolations;                    // ceiling breaches (must stay zero)
};

#endif
