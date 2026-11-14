#ifndef TRACE_WRITER_HPP
#define TRACE_WRITER_HPP

#include <cstdint>
#include <cstdio>
#include <string>

#include "sim_time.hpp"

// The profile's four trace ledgers (trace_contract), one CSV each. Times are
// truth-clock microseconds; an unknown time is written as null, never zero.
// Transmit starts also appear in exact ticks (ninths of a microsecond) so an
// outside checker can redo the rolling-window sums without rounding.
class TraceWriter
{
public:
   TraceWriter();
   ~TraceWriter();

   void Open(const std::string& aPrefix, const std::string& aRunId);
   bool Enabled() const;

   void Obligation(int64_t aInputId, int aOrigin, int aRecipient, SimTime aInput, int64_t aEntityUid, int64_t aValue, SimTime aDeadline);
   void Transmission(int64_t aTxId, int64_t aEventUid, int aOrigin, int aSender, const std::string& aRecipients, int aCopy, int aHop,
                     const char* aClass, SimTime aAttemptStart, SimTime aTxStart, SimTime aTxEnd, int64_t aBits, int64_t aBalance,
                     const std::string& aDropReason);
   void Response(int64_t aInputId, int aRecipient, SimTime aReady, int64_t aSuccessTx, SimTime aPublish, SimTime aPhoton,
                 SimTime aUncertainty, bool aCorrect, int aApplyCount, int aRevision, const char* aOutcome, const char* aFailure);
   void Resource(SimTime aTime, int aNode, long aQueueEntries, int64_t aQueueBytes, SimTime aOldestAge, const char* aBusyReason);

private:
   static std::string Us(SimTime aTime);             // microseconds, or "null" when negative

   std::string mRunId;
   FILE*       mObligations;
   FILE*       mTransmissions;
   FILE*       mResponses;
   FILE*       mResources;
};

#endif
