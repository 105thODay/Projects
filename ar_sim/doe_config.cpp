#include "doe_config.hpp"

#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace
{
   struct IntKnob
   {
      const char*          mName;                    // key on the command line
      int64_t DoeConfig::* mField;                   // where it lands
   };
   struct RealKnob
   {
      const char*         mName;
      double DoeConfig::* mField;
   };

   const IntKnob cINT_KNOBS[] =
   {
      { "nodes",              &DoeConfig::mNodes },
      { "durationS",          &DoeConfig::mDurationS },
      { "warmupS",            &DoeConfig::mWarmupS },
      { "drainS",             &DoeConfig::mDrainS },
      { "seed",               &DoeConfig::mSeed },
      { "deadlineUs",         &DoeConfig::mDeadlineUs },
      { "inputReadyUs",       &DoeConfig::mInputReadyUs },
      { "stageDeliveryUs",    &DoeConfig::mStageDeliveryUs },
      { "displayUs",          &DoeConfig::mDisplayUs },
      { "uncertaintyUs",      &DoeConfig::mUncertaintyUs },
      { "copies",             &DoeConfig::mCopies },
      { "phyBps",             &DoeConfig::mPhyBps },
      { "encodeUs",           &DoeConfig::mEncodeUs },
      { "decodeUs",           &DoeConfig::mDecodeUs },
      { "propagationUs",      &DoeConfig::mPropagationUs },
      { "jitterMaxUs",        &DoeConfig::mJitterMaxUs },
      { "burst",              &DoeConfig::mBurst },
      { "stateBits",          &DoeConfig::mStateBits },
      { "urgentBits",         &DoeConfig::mUrgentBits },
      { "controlBits",        &DoeConfig::mControlBits },
      { "repairBits",         &DoeConfig::mRepairBits },
      { "hops",               &DoeConfig::mHops },
      { "fixturesPerRecipient", &DoeConfig::mFixturesPerRecipient },
      { "sharedEncode",       &DoeConfig::mSharedEncode },
      { "limitBits",          &DoeConfig::mLimitBits },
      { "windowUs",           &DoeConfig::mWindowUs },
      { "urgentShareBits",    &DoeConfig::mUrgentShareBits },
      { "lendUrgentShare",    &DoeConfig::mLendUrgentShare },
      { "inputDitherUs",      &DoeConfig::mInputDitherUs },
      { "inputMinGapUs",      &DoeConfig::mInputMinGapUs },
      { "inputTrain",         &DoeConfig::mInputTrain },
      { "inputTrainGapUs",    &DoeConfig::mInputTrainGapUs },
      { "repairRequestBits",  &DoeConfig::mRepairRequestBits },
      { "repairShareBits",    &DoeConfig::mRepairShareBits },
      { "syncPhases",         &DoeConfig::mSyncPhases },
      { "phaseOffsetUs",      &DoeConfig::mPhaseOffsetUs },
      { "lossDirection",      &DoeConfig::mLossDirection },
      { "correlatedLoss",     &DoeConfig::mCorrelatedLoss },
      { "eraseCopies",        &DoeConfig::mEraseCopies },
      { "blockPeriods",       &DoeConfig::mBlockPeriods },
      { "blackoutUs",         &DoeConfig::mBlackoutUs },
      { "blackoutEveryS",     &DoeConfig::mBlackoutEveryS },
      { "reorderUs",          &DoeConfig::mReorderUs },
      { "partitionUs",        &DoeConfig::mPartitionUs },
      { "partitionAtS",       &DoeConfig::mPartitionAtS },
      { "replayTest",         &DoeConfig::mReplayTest },
      { "repair",             &DoeConfig::mRepair },
      { "urgentEventCap",     &DoeConfig::mUrgentEventCap },
      { "urgentByteCap",      &DoeConfig::mUrgentByteCap },
      { "dedupEntries",       &DoeConfig::mDedupEntries },
      { "dedupRetentionMs",   &DoeConfig::mDedupRetentionMs },
      { "relayBacklogCap",    &DoeConfig::mRelayBacklogCap },
      { "udpIpBytes",         &DoeConfig::mUdpIpBytes },
      { "authBytes",          &DoeConfig::mAuthBytes },
      { "datagramLimitBytes", &DoeConfig::mDatagramLimitBytes },
      { "align",              &DoeConfig::mAlign },
      { "arenaM",             &DoeConfig::mArenaM },
      { "wifiMacBytes",       &DoeConfig::mWifiMacBytes },
      { "wifiRetryLimit",     &DoeConfig::mWifiRetryLimit },
      { "wifiBeaconUs",       &DoeConfig::mWifiBeaconUs },
      { "wifiDtimPeriod",     &DoeConfig::mWifiDtimPeriod },
      { "wifiDozing",         &DoeConfig::mWifiDozing },
      { "corpusPerKind",      &DoeConfig::mCorpusPerKind },
      { "wifiVoice",          &DoeConfig::mWifiVoice },
      { "chargeMac",          &DoeConfig::mChargeMac },
   };
   const IntKnob cRATE_KNOBS[] =                     // given in Hz, stored in millihertz
   {
      { "opportunityHz",      &DoeConfig::mOpportunityMilliHz },
      { "inputHz",            &DoeConfig::mInputMilliHz },
      { "stateHz",            &DoeConfig::mStateMilliHz },
      { "controlHz",          &DoeConfig::mControlMilliHz },
      { "repairHz",           &DoeConfig::mRepairMilliHz },
   };
   const RealKnob cREAL_KNOBS[] =
   {
      { "lossProb",           &DoeConfig::mLossProb },
      { "farSigmaM",          &DoeConfig::mFarSigmaM },
      { "nearSigmaM",         &DoeConfig::mNearSigmaM },
      { "driftFraction",      &DoeConfig::mDriftFraction },
      { "farGain",            &DoeConfig::mFarGain },
      { "farGainNear",        &DoeConfig::mFarGainNear },
      { "calThresholdDeg",    &DoeConfig::mCalThresholdDeg },
      { "nearGain",           &DoeConfig::mNearGain },
      { "calThresholdM",      &DoeConfig::mCalThresholdM },
      { "checkNearM",         &DoeConfig::mCheckNearM },
      { "checkFarM",          &DoeConfig::mCheckFarM },
      { "wifiDataMbps",       &DoeConfig::mWifiDataMbps },
      { "wifiBasicMbps",      &DoeConfig::mWifiBasicMbps },
      { "wifiAckMbps",        &DoeConfig::mWifiAckMbps },
      { "dupProb",            &DoeConfig::mDupProb },
      { "reorderProb",        &DoeConfig::mReorderProb },
      { "inputPhaseFrac",     &DoeConfig::mInputPhaseFrac },
      { "rxPhaseFrac",        &DoeConfig::mRxPhaseFrac },
   };
   const double cMILLI = 1000.0;
}

std::vector<std::string> CaseNames()
{
   return { "Q0", "Q1", "Q2", "R0", "J0", "C0", "S0", "P0", "O0", "R1" };
}

DoeConfig MakeCase(const std::string& aCase)
{
   DoeConfig config;
   config.mCase = aCase;
   if (aCase == "Q0")
   {
      // Clean path: the starter workload as delivered.
   }
   else if (aCase == "Q1")
   {
      config.mEraseCopies = 1;                       // erase urgent copy index 0
   }
   else if (aCase == "Q2")
   {
      config.mEraseCopies = 2;                       // erase urgent copy indices 0 and 1
   }
   else if (aCase == "R0")
   {
      config.mLossProb = 0.03;                       // sweep 0, 0.01, 0.03 and lossDirection 0, 1, 2
   }
   else if (aCase == "J0")
   {
      config.mJitterMaxUs = 5000;                    // sweep 0, 5000, 10000; combine with eraseCopies
   }
   else if (aCase == "C0")
   {
      config.mBlockPeriods = 2;                      // sweep 1, 2, 3, 5 consecutive copy periods
   }
   else if (aCase == "S0")
   {
      config.mLossProb   = 0.05;                     // sweep 0.05, 0.10
      config.mBlackoutUs = 100000;                   // sweep 50000, 100000, 250000
   }
   else if (aCase == "P0")
   {
      config.mDupProb     = 0.01;
      config.mReorderProb = 0.01;
      config.mReorderUs   = 20000;                   // also 5000
   }
   else if (aCase == "O0")
   {
      config.mBurst      = 4;                        // sweep 1, 2, 4
      config.mSyncPhases = 1;
   }
   else if (aCase == "R1")
   {
      config.mPartitionUs = 1000000;
      config.mReplayTest  = 1;
   }
   else
   {
      throw std::invalid_argument("unknown case: " + aCase);
   }
   return config;
}

bool ApplyOverride(const std::string& aOverride, DoeConfig& aConfig)
{
   const std::string::size_type eq = aOverride.find('=');
   if (eq == std::string::npos)
   {
      return false;
   }
   const std::string key   = aOverride.substr(0, eq);
   const std::string value = aOverride.substr(eq + 1);

   if (key == "fanout")
   {
      if (value == "unicast")
      {
         aConfig.mFanout = Fanout::cUNICAST;
         return true;
      }
      if (value == "common")
      {
         aConfig.mFanout = Fanout::cCOMMON;
         return true;
      }
      return false;
   }
   if (key == "endpoint")
   {
      if (value == "publication")
      {
         aConfig.mEndpoint = Endpoint::cPUBLICATION;
         return true;
      }
      if (value == "modeled_photon")
      {
         aConfig.mEndpoint = Endpoint::cMODELED_PHOTON;
         return true;
      }
      return false;
   }
   if (key == "wire")
   {
      const char* const names[] = { "abstract", "xml_specified", "xml_lean", "tak_specified", "tak_lean" };
      for (int index = 0; index < 5; ++index)
      {
         if (value == names[index])
         {
            aConfig.mWire = index;
            return true;
         }
      }
      return false;
   }
   if (key == "channel")
   {
      if (value == "ideal" || value == "wifi_ap")
      {
         aConfig.mChannel = (value == "ideal") ? Channel::cIDEAL : Channel::cWIFI_AP;
         return true;
      }
      return false;
   }
   if (key == "group")
   {
      if (value == "basic" || value == "unicast")
      {
         aConfig.mGroupMode = (value == "basic") ? GroupMode::cBASIC_RATE : GroupMode::cTO_UNICAST;
         return true;
      }
      return false;
   }
   if (key == "trace")
   {
      aConfig.mTracePrefix = value;
      return true;
   }
   if (key == "corpus")
   {
      aConfig.mCorpusFile = value;
      return true;
   }
   for (const IntKnob& knob : cINT_KNOBS)
   {
      if (key == knob.mName)
      {
         aConfig.*(knob.mField) = std::atoll(value.c_str());
         return true;
      }
   }
   for (const IntKnob& knob : cRATE_KNOBS)
   {
      if (key == knob.mName)
      {
         aConfig.*(knob.mField) = static_cast<int64_t>(std::atof(value.c_str()) * cMILLI + 0.5);
         return true;
      }
   }
   for (const RealKnob& knob : cREAL_KNOBS)
   {
      if (key == knob.mName)
      {
         aConfig.*(knob.mField) = std::atof(value.c_str());
         return true;
      }
   }
   return false;
}

std::string DescribeConfig(const DoeConfig& aConfig)
{
   std::string text = "case=" + aConfig.mCase;
   text += (aConfig.mFanout == Fanout::cCOMMON) ? " fanout=common" : " fanout=unicast";
   text += (aConfig.mEndpoint == Endpoint::cMODELED_PHOTON) ? " endpoint=modeled_photon" : " endpoint=publication";
   const char* const wires[] = { "abstract", "xml_specified", "xml_lean", "tak_specified", "tak_lean" };
   text += std::string(" wire=") + wires[(aConfig.mWire >= 0 && aConfig.mWire < 5) ? aConfig.mWire : 0];
   text += (aConfig.mChannel == Channel::cWIFI_AP) ? " channel=wifi_ap" : " channel=ideal";
   text += (aConfig.mGroupMode == GroupMode::cTO_UNICAST) ? " group=unicast" : " group=basic";
   char buffer[96];
   for (const IntKnob& knob : cINT_KNOBS)
   {
      std::snprintf(buffer, sizeof(buffer), " %s=%lld", knob.mName, static_cast<long long>(aConfig.*(knob.mField)));
      text += buffer;
   }
   for (const IntKnob& knob : cRATE_KNOBS)
   {
      std::snprintf(buffer, sizeof(buffer), " %s=%g", knob.mName, static_cast<double>(aConfig.*(knob.mField)) / cMILLI);
      text += buffer;
   }
   for (const RealKnob& knob : cREAL_KNOBS)
   {
      std::snprintf(buffer, sizeof(buffer), " %s=%g", knob.mName, aConfig.*(knob.mField));
      text += buffer;
   }
   return text;
}
