#include "trace_writer.hpp"

TraceWriter::TraceWriter()
   : mObligations(nullptr), mTransmissions(nullptr), mResponses(nullptr), mResources(nullptr)
{
}

TraceWriter::~TraceWriter()
{
   FILE* files[] = { mObligations, mTransmissions, mResponses, mResources };
   for (FILE* file : files)
   {
      if (file != nullptr)
      {
         std::fclose(file);
      }
   }
}

void TraceWriter::Open(const std::string& aPrefix, const std::string& aRunId)
{
   mRunId         = aRunId;
   mObligations   = std::fopen((aPrefix + "_input_obligations.csv").c_str(), "w");
   mTransmissions = std::fopen((aPrefix + "_transmissions.csv").c_str(), "w");
   mResponses     = std::fopen((aPrefix + "_responses.csv").c_str(), "w");
   mResources     = std::fopen((aPrefix + "_resources.csv").c_str(), "w");
   if (!Enabled())
   {
      return;
   }
   std::fprintf(mObligations, "run_id,input_id,origin_id,recipient_id,t_input_us,expected_entity_uid,expected_value,required_eye,test_region,response_deadline_us\n");
   std::fprintf(mTransmissions, "run_id,tx_id,event_uid,origin_id,sender_id,recipient_ids,copy_index,hop_index,traffic_class,t_attempt_start_us,t_tx_start_us,t_tx_end_us,encoded_bytes,charged_bits,budget_balance_bits,drop_reason,t_tx_start_ticks\n");
   std::fprintf(mResponses, "run_id,input_id,recipient_id,t_ready_us,successful_tx_id,t_publish_us,t_photon_us,uncertainty_us,effect_correct,apply_count,authority_epoch,revision,eye,test_region,failure_reason,outcome\n");
   std::fprintf(mResources, "timestamp_us,node_id,queue_entries,queue_bytes,oldest_event_age_us,rf_busy_reason,local_estimator_deadline_misses,local_render_deadline_misses\n");
}

bool TraceWriter::Enabled() const
{
   return mObligations != nullptr && mTransmissions != nullptr && mResponses != nullptr && mResources != nullptr;
}

std::string TraceWriter::Us(SimTime aTime)
{
   if (aTime < 0)
   {
      return "null";
   }
   char buffer[40];
   std::snprintf(buffer, sizeof(buffer), "%.3f", ToUs(aTime));
   return buffer;
}

void TraceWriter::Obligation(int64_t aInputId, int aOrigin, int aRecipient, SimTime aInput, int64_t aEntityUid, int64_t aValue, SimTime aDeadline)
{
   // Eye and scan region are not modeled in the transport-queue mode: null.
   std::fprintf(mObligations, "%s,%lld,%d,%d,%s,%s,%lld,null,null,%s\n", mRunId.c_str(), static_cast<long long>(aInputId),
                aOrigin, aRecipient, Us(aInput).c_str(), (aEntityUid < 0) ? "null" : std::to_string(aEntityUid).c_str(),
                static_cast<long long>(aValue), Us(aDeadline).c_str());
}

void TraceWriter::Transmission(int64_t aTxId, int64_t aEventUid, int aOrigin, int aSender, const std::string& aRecipients, int aCopy, int aHop,
                               const char* aClass, SimTime aAttemptStart, SimTime aTxStart, SimTime aTxEnd, int64_t aBits, int64_t aBalance,
                               const std::string& aDropReason)
{
   std::fprintf(mTransmissions, "%s,%lld,%s,%d,%d,%s,%d,%d,%s,%s,%s,%s,%lld,%lld,%lld,%s,%lld\n", mRunId.c_str(), static_cast<long long>(aTxId),
                (aEventUid < 0) ? "null" : std::to_string(aEventUid).c_str(), aOrigin, aSender, aRecipients.c_str(), aCopy, aHop, aClass,
                Us(aAttemptStart).c_str(), Us(aTxStart).c_str(), Us(aTxEnd).c_str(), static_cast<long long>(aBits / 8),
                static_cast<long long>(aBits), static_cast<long long>(aBalance), aDropReason.c_str(), static_cast<long long>(aTxStart));
}

void TraceWriter::Response(int64_t aInputId, int aRecipient, SimTime aReady, int64_t aSuccessTx, SimTime aPublish, SimTime aPhoton,
                           SimTime aUncertainty, bool aCorrect, int aApplyCount, int aRevision, const char* aOutcome, const char* aFailure)
{
   std::fprintf(mResponses, "%s,%lld,%d,%s,%s,%s,%s,%s,%d,%d,1,%d,null,null,%s,%s\n", mRunId.c_str(), static_cast<long long>(aInputId), aRecipient,
                Us(aReady).c_str(), (aSuccessTx < 0) ? "null" : std::to_string(aSuccessTx).c_str(), Us(aPublish).c_str(), Us(aPhoton).c_str(),
                Us(aUncertainty).c_str(), aCorrect ? 1 : 0, aApplyCount, aRevision, aFailure, aOutcome);
}

void TraceWriter::Resource(SimTime aTime, int aNode, long aQueueEntries, int64_t aQueueBytes, SimTime aOldestAge, const char* aBusyReason)
{
   // The local estimators and renderer are not modeled: their miss counts are null (NET-005 not assessed).
   std::fprintf(mResources, "%s,%d,%ld,%lld,%s,%s,null,null\n", Us(aTime).c_str(), aNode, aQueueEntries,
                static_cast<long long>(aQueueBytes), Us(aOldestAge).c_str(), aBusyReason);
}
