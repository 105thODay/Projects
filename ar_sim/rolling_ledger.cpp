#include "rolling_ledger.hpp"

#include <algorithm>

RollingLedger::RollingLedger(SimTime aWindow, int64_t aLimitBits)
   : mWindow(aWindow), mLimitBits(aLimitBits), mSum(0), mNonUrgentSum(0), mRepairSum(0),
     mMaxObserved(0), mTotalCharged(0), mViolations(0)
{
}

void RollingLedger::Prune(SimTime aNow)
{
   while (!mEntries.empty() && mEntries.front().mTime <= aNow - mWindow)
   {
      mSum -= mEntries.front().mBits;
      if (!mEntries.front().mUrgent)
      {
         mNonUrgentSum -= mEntries.front().mBits;
      }
      if (mEntries.front().mRepair)
      {
         mRepairSum -= mEntries.front().mBits;
      }
      mEntries.pop_front();
   }
}

int64_t RollingLedger::SumAt(SimTime aNow)
{
   Prune(aNow);
   return mSum;
}

int64_t RollingLedger::NonUrgentSumAt(SimTime aNow)
{
   Prune(aNow);
   return mNonUrgentSum;
}

int64_t RollingLedger::RepairSumAt(SimTime aNow)
{
   Prune(aNow);
   return mRepairSum;
}

int64_t RollingLedger::SumAhead(SimTime aNow, SimTime aThen)
{
   Prune(aNow);
   int64_t sum = mSum;
   for (const Entry& entry : mEntries)
   {
      if (entry.mTime > aThen - mWindow)
      {
         break;
      }
      sum -= entry.mBits;
   }
   return sum;
}

int64_t RollingLedger::NonUrgentSumAhead(SimTime aNow, SimTime aThen)
{
   Prune(aNow);
   int64_t sum = mNonUrgentSum;
   for (const Entry& entry : mEntries)
   {
      if (entry.mTime > aThen - mWindow)
      {
         break;
      }
      if (!entry.mUrgent)
      {
         sum -= entry.mBits;
      }
   }
   return sum;
}

int64_t RollingLedger::RepairSumAhead(SimTime aNow, SimTime aThen)
{
   Prune(aNow);
   int64_t sum = mRepairSum;
   for (const Entry& entry : mEntries)
   {
      if (entry.mTime > aThen - mWindow)
      {
         break;
      }
      if (entry.mRepair)
      {
         sum -= entry.mBits;
      }
   }
   return sum;
}

bool RollingLedger::Fits(SimTime aNow, int64_t aBits)
{
   return SumAt(aNow) + aBits <= mLimitBits;
}

void RollingLedger::Charge(SimTime aNow, int64_t aBits, bool aUrgent, bool aRepair)
{
   Prune(aNow);
   Entry entry;
   entry.mTime   = aNow;
   entry.mBits   = aBits;
   entry.mUrgent = aUrgent;
   entry.mRepair = aRepair;
   mEntries.push_back(entry);
   mSum += aBits;
   if (!aUrgent)
   {
      mNonUrgentSum += aBits;
   }
   if (aRepair)
   {
      mRepairSum += aBits;
   }
   mTotalCharged += aBits;
   mMaxObserved   = std::max(mMaxObserved, mSum);
   if (mSum > mLimitBits)
   {
      mViolations += 1;
   }
}

int64_t RollingLedger::MaxObserved() const  { return mMaxObserved; }
int64_t RollingLedger::TotalCharged() const { return mTotalCharged; }
int64_t RollingLedger::Violations() const   { return mViolations; }
