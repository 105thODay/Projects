#include "simulation.hpp"

#include "corpus.hpp"

#include <algorithm>
#include <cmath>

namespace
{
   const int64_t cUID_STRIDE          = 1000000000;  // event uid = origin * stride + sequence
   const int     cGENERATE_STATE      = 0;
   const int     cGENERATE_CONTROL    = 1;
   const int     cGENERATE_REPAIR     = 2;
   const int     cBURST_BINS          = 6;           // loss-run histogram: lengths 1..4, then "5 or more"
   const int     cMAX_REQUESTED       = 8;           // sequences per repair request
   const int     cSTATE_AGE_SAMPLES   = 4;           // ages sampled across each interval a state is on show
   const int64_t cREREQUEST_US        = 250000;      // a recipient repeats a repair request no sooner than this
   const int64_t cDUPLICATE_DELAY_US  = 1000;        // P0: the duplicate trails the original by this
   const int64_t cRESOURCE_SAMPLE_US  = 100000;      // resource trace cadence
   const int64_t cDISPLAY_MILLIHZ     = 90000;       // NET-005/006 render rate
   const int64_t cSCRIPT_CREATE_S     = 13;          // R1: create this long before the partition (past dedup retention)
   const int64_t cSCRIPT_DELETE_S     = 12;          // R1: delete this long before the partition
   const int64_t cSCRIPT_REPLAY_US    = 500000;      // R1: replay the old create this long after the partition heals
   const int64_t cSCRIPT_TAKEOVER_US  = 600000;      // R1: the takeover attempt
   const int64_t cBLACKOUT_FIRST_S    = 2;           // S0: first blackout this long after warmup

   const char* const cCLASS_NAME[FrameClass::cCOUNT] = { "control", "urgent", "state", "repair" };

   // Rev 6: encoded wire and alignment
   const char* const cSYNTHETIC_EPOCH   = "2026-11-13T13:47:12.000000Z";   // UTC of simulation time zero (synthetic, labeled)
   const double  cORIGIN_LAT_DEG        = 35.0123457;    // synthetic surveyed origin of the mission frame
   const double  cORIGIN_LON_DEG        = -116.4567891;
   const double  cORIGIN_HAE_M          = 712.3;
   const double  cEARTH_RADIUS_M        = 6378137.0;
   const double  cPI                    = 3.14159265358979323846;
   const double  cMARKER_MIN_M          = 3.0;           // a marker lands this far ahead of its owner ...
   const double  cMARKER_SPAN_M         = 27.0;          // ... plus up to this much more
   const double  cMARKER_HEIGHT_M       = 1.0;
   const double  cEVENT_STALE_S         = 30.0;          // marker expiry
   const double  cSTATE_STALE_S         = 2.0;
   const double  cCONTROL_STALE_S       = 10.0;
   const int     cCLOCK_UNCERTAINTY_US  = 1000;          // U
   const int64_t cFRESH_STATE_US        = 500000;        // a near peer's published pose this recent can anchor alignment
   const double  cDRIFT_TURN_RAD        = 0.3;           // drift direction random walk per root second
   const double  cDRIFT_VERTICAL        = 0.2;           // vertical drift as a share of horizontal
   const double  cTOLERANCE_M           = 0.0006;        // decoded positions agree with the frame to the coarser format

   double SecondsOf(SimTime aTime)
   {
      return static_cast<double>(aTime) / static_cast<double>(TimeUnit::cPER_S);
   }

   int KindOf(int aOp)
   {
      switch (aOp)
      {
      case FrameOp::cCONTROL_FIXTURE: return MessageKind::cCONTROL;
      case FrameOp::cREPAIR_REQUEST:  return MessageKind::cREPAIR_REQUEST;
      case FrameOp::cSTATE:           return MessageKind::cSTATE;
      case FrameOp::cREPAIR_FIXTURE:  return MessageKind::cREPAIR_STATUS;
      case FrameOp::cCALIBRATION:     return MessageKind::cCALIBRATION;
      default:                        return MessageKind::cEVENT;
      }
   }

   void Quaternion(double aYaw, double aPitch, double aRoll, double aOut[4])
   {
      // z-y-x (yaw, pitch, roll) to w x y z
      const double cy = std::cos(aYaw * 0.5),   sy = std::sin(aYaw * 0.5);
      const double cp = std::cos(aPitch * 0.5), sp = std::sin(aPitch * 0.5);
      const double cr = std::cos(aRoll * 0.5),  sr = std::sin(aRoll * 0.5);
      aOut[0] = cr * cp * cy + sr * sp * sy;
      aOut[1] = sr * cp * cy - cr * sp * sy;
      aOut[2] = cr * sp * cy + sr * cp * sy;
      aOut[3] = cr * cp * sy - sr * sp * cy;
   }
}

// ---------------------------------------------------------------- construction

Node::Node(int aId, SimTime aWindow, int64_t aLimitBits)
   : mId(aId), mLedger(aWindow, aLimitBits), mReservedBits(0), mReservedNonUrgent(0), mReservedRepair(0), mPendingUrgentBits(0),
     mOpportunityPhase(0), mDisplayPhase(0), mEncoderFreeAt(0), mLastEncodeKey(-1), mLastEncodeStart(-1), mBlockUntil(0),
     mCompleteSeq(0), mOutstandingEvents(0), mTrainIndex(0), mTrainStart(0), mStateRevision(0)
{
}

bool Simulation::EventOrder::operator()(const Event& aLhs, const Event& aRhs) const
{
   if (aLhs.mTime != aRhs.mTime)
   {
      return aLhs.mTime > aRhs.mTime;
   }
   if (aLhs.mOrder != aRhs.mOrder)
   {
      return aLhs.mOrder > aRhs.mOrder;
   }
   return aLhs.mSeq > aRhs.mSeq;
}

double Simulation::Unit(std::mt19937_64& aRng)
{
   return static_cast<double>(aRng() >> 11) * (1.0 / 9007199254740992.0);   // 53 bits, [0, 1)
}

int64_t Simulation::Below(std::mt19937_64& aRng, int64_t aBound)
{
   return static_cast<int64_t>(Unit(aRng) * static_cast<double>(aBound));
}

Simulation::Simulation(const DoeConfig& aConfig)
   : mConfig(aConfig), mPeriod(PeriodFromMilliHz(aConfig.mOpportunityMilliHz)), mDisplayPeriod(PeriodFromMilliHz(cDISPLAY_MILLIHZ)),
     mEnd(aConfig.mDurationS * TimeUnit::cPER_S), mNow(0), mSeq(0), mNextTxId(1), mNextInputId(1),
     mMediumBusy(false), mTransmitter(-1),
     mRngSetup(static_cast<uint64_t>(aConfig.mSeed) * 4 + 1), mRngLoss(static_cast<uint64_t>(aConfig.mSeed) * 4 + 2),
     mRngJitter(static_cast<uint64_t>(aConfig.mSeed) * 4 + 3), mRngFault(static_cast<uint64_t>(aConfig.mSeed) * 4 + 4),
     mRngInput(static_cast<uint64_t>(aConfig.mSeed) ^ 0x9E3779B97F4A7C15ULL),
     mScriptEntityEvent(-1),
     mRngAlign(static_cast<uint64_t>(aConfig.mSeed) ^ 0xA11C0FFEE0DDF00DULL),
     mUtcEpochUs(0),
     mRngWifi(static_cast<uint64_t>(aConfig.mSeed) ^ 0x80211E0CA0F1C0DEULL)
{
   const int     count  = static_cast<int>(mConfig.mNodes);
   const bool    common = (mConfig.mFanout == Fanout::cCOMMON);
   const SimTime warmup = mConfig.mWarmupS * TimeUnit::cPER_S;

   mResult.waitMaxByCopy.assign(static_cast<size_t>(mConfig.mCopies), 0);
   mResult.maxRollingBits.assign(static_cast<size_t>(count), 0);
   mResult.chargedBits.assign(static_cast<size_t>(count), 0);
   mResult.bitsByClass.assign(FrameClass::cCOUNT, 0);
   mResult.framesByClass.assign(FrameClass::cCOUNT, 0);
   mResult.burstHistogram.assign(cBURST_BINS, 0);
   mLossRun.assign(static_cast<size_t>(count) * static_cast<size_t>(count), 0);

   if (!mConfig.mTracePrefix.empty())
   {
      mTrace.Open(mConfig.mTracePrefix, mConfig.mCase + "_seed" + std::to_string(mConfig.mSeed));
   }

   mNodes.reserve(static_cast<size_t>(count));
   for (int id = 0; id < count; ++id)
   {
      mNodes.emplace_back(id, FromUs(mConfig.mWindowUs), mConfig.mLimitBits);
      Node& node = mNodes.back();
      node.mRemote.resize(static_cast<size_t>(count));
      node.mOpportunityPhase = (mConfig.mSyncPhases == 1) ? 0 : Below(mRngSetup, mPeriod);
      node.mDisplayPhase     = (mConfig.mRxPhaseFrac >= 0.0)
                             ? static_cast<SimTime>(mConfig.mRxPhaseFrac * static_cast<double>(mDisplayPeriod) + 0.5) + FromUs(mConfig.mPhaseOffsetUs)
                             : Below(mRngSetup, mDisplayPeriod);
      for (int other = 0; other < count; ++other)
      {
         if (other != id && !common)
         {
            node.mStateTargets.push_back(other);
            if (mConfig.mFixturesPerRecipient != 0)
            {
               node.mFixtureTargets.push_back(other);
            }
         }
      }
      if (node.mStateTargets.empty())
      {
         node.mStateTargets.push_back(-1);
      }
      if (node.mFixtureTargets.empty())
      {
         node.mFixtureTargets.push_back(-1);
      }
      node.mPendingState.resize(node.mStateTargets.size());
      node.mPendingControl.assign(node.mFixtureTargets.size(), 0);
      node.mPendingRepairFixture.assign(node.mFixtureTargets.size(), 0);
      node.mControlLastSent.assign(node.mFixtureTargets.size(), -1);
      node.mPendingCalibration.assign(node.mFixtureTargets.size(), 0);
      node.mCalibrationLastSent.assign(node.mFixtureTargets.size(), -1);
      node.mRepairLastSent.assign(node.mFixtureTargets.size(), -1);

      Schedule(node.mOpportunityPhase, EventKind::cOPPORTUNITY, id, 0);

      const int64_t rates[] = { mConfig.mStateMilliHz, mConfig.mControlMilliHz, mConfig.mRepairMilliHz };
      for (int what = 0; what < 3; ++what)
      {
         if (rates[what] > 0)
         {
            const SimTime period = PeriodFromMilliHz(rates[what]);
            Schedule((mConfig.mSyncPhases == 1) ? 0 : Below(mRngSetup, period), EventKind::cGENERATE, id, what);
         }
      }

      if (mConfig.mInputMilliHz > 0)
      {
         const SimTime inputPeriod = PeriodFromMilliHz(mConfig.mInputMilliHz);
         const SimTime ready       = FromUs(mConfig.mInputReadyUs);
         SimTime first = warmup;
         if (mConfig.mInputPhaseFrac >= 0.0)
         {
            // Place event readiness at a chosen phase of this sender's opportunity period.
            const SimTime phase = static_cast<SimTime>(mConfig.mInputPhaseFrac * static_cast<double>(mPeriod) + 0.5) + FromUs(mConfig.mPhaseOffsetUs);
            SimTime readyAt = node.mOpportunityPhase + phase;
            while (readyAt - ready < warmup)
            {
               readyAt += mPeriod;
            }
            first = readyAt - ready;
         }
         else if (mConfig.mSyncPhases == 0)
         {
            first = warmup + Below(mRngSetup, inputPeriod);
         }
         Schedule(first, EventKind::cINPUT, id, 0);
      }
   }

   // Rev 6: encoded wire, motion and alignment. Each has its own random stream, so the
   // abstract model's draws, and so its regression anchors, do not move.
   CotCodec::ParseIsoTime(cSYNTHETIC_EPOCH, mUtcEpochUs);
   mResult.wireSizes.resize(MessageKind::cCOUNT);
   if (mConfig.mWire != WireMode::cABSTRACT)
   {
      mCodec.reset(new CotCodec(static_cast<int>(mConfig.mWire), count, static_cast<uint64_t>(mConfig.mSeed)));
      if (!mConfig.mCorpusFile.empty())
      {
         mCorpus = std::fopen(mConfig.mCorpusFile.c_str(), "ab");
      }
   }
   if (mConfig.mWire != WireMode::cABSTRACT || mConfig.mAlign != 0)
   {
      const double arena = (mConfig.mArenaM > 0) ? static_cast<double>(mConfig.mArenaM) : 10.0 * std::sqrt(static_cast<double>(count));
      mMotion.reset(new Motion(count, arena, static_cast<uint64_t>(mConfig.mSeed) * 7919 + 17));
   }
   if (mConfig.mAlign != 0)
   {
      mAlignConfig.mFarSigmaM      = mConfig.mFarSigmaM;
      mAlignConfig.mFarSigmaVertM  = 2.0 * mConfig.mFarSigmaM;   // vertical GPS error, twice horizontal
      mAlignConfig.mNearSigmaM     = mConfig.mNearSigmaM;
      mAlignConfig.mDriftFraction  = mConfig.mDriftFraction;
      mAlignConfig.mFarGain        = mConfig.mFarGain;
      mAlignConfig.mFarGainNear    = mConfig.mFarGainNear;
      mAlignConfig.mThresholdYawRad = mConfig.mCalThresholdDeg * cPI / 180.0;
      mAlignConfig.mNearGain       = mConfig.mNearGain;
      mAlignConfig.mThresholdM     = mConfig.mCalThresholdM;
      mPositioning.reset(new PeerPositioning(*mMotion, mAlignConfig, count, static_cast<uint64_t>(mConfig.mSeed) * 104729 + 3));
      mAlign.resize(static_cast<size_t>(count));
      for (Alignment& alignment : mAlign)
      {
         // A participant starts aligned from its own GPS-aided solution, so with that solution's error.
         alignment.mError[0] = mAlignConfig.mFarSigmaM * std::sqrt(-2.0 * std::log(Unit(mRngAlign))) * std::cos(2.0 * cPI * Unit(mRngAlign));
         alignment.mError[1] = mAlignConfig.mFarSigmaM * std::sqrt(-2.0 * std::log(Unit(mRngAlign))) * std::cos(2.0 * cPI * Unit(mRngAlign));
         alignment.mError[3] = mAlignConfig.mFarSigmaYawRad * std::sqrt(-2.0 * std::log(Unit(mRngAlign))) * std::cos(2.0 * cPI * Unit(mRngAlign));
         alignment.mDriftDirection = 2.0 * cPI * Unit(mRngAlign);
         alignment.mDriftYawSign   = (Unit(mRngAlign) < 0.5) ? -1.0 : 1.0;
      }
   }

   if (Wifi())
   {
      mWifiQueue.resize(static_cast<size_t>(count) + 1);
      mWifiPsBuffer.resize(static_cast<size_t>(count));
      for (int station = 0; station <= count; ++station)
      {
         for (int ac = 0; ac < 2; ++ac)
         {
            mWifiQueue[static_cast<size_t>(station)][static_cast<size_t>(ac)].mCw = WifiCwMin(station, ac);
         }
      }
      const SimTime interval = FromUs(mConfig.mWifiBeaconUs);
      for (SimTime tbtt = interval; tbtt <= mEnd; tbtt += interval)
      {
         Schedule(tbtt, EventKind::cWIFI_BEACON, 0, 0);
      }
   }

   if (mConfig.mReplayTest != 0 && count >= 2)
   {
      const SimTime partition = mConfig.mPartitionAtS * TimeUnit::cPER_S;
      const SimTime healed    = partition + FromUs(mConfig.mPartitionUs);
      Schedule(partition - cSCRIPT_CREATE_S * TimeUnit::cPER_S, EventKind::cSCRIPT, 0, 0);
      Schedule(partition - cSCRIPT_DELETE_S * TimeUnit::cPER_S, EventKind::cSCRIPT, 0, 1);
      Schedule(healed + FromUs(cSCRIPT_REPLAY_US), EventKind::cSCRIPT, 0, 2);
      Schedule(healed + FromUs(cSCRIPT_TAKEOVER_US), EventKind::cSCRIPT, 0, 3);
   }
   if (mTrace.Enabled())
   {
      Schedule(0, EventKind::cSAMPLE, 0, 0);
   }
}

const RunResult& Simulation::Result() const
{
   return mResult;
}

void Simulation::Schedule(SimTime aTime, int aKind, int aNode, int aIndex)
{
   Event event;
   event.mTime  = aTime;
   event.mOrder = aKind;
   event.mSeq   = mSeq++;
   event.mKind  = aKind;
   event.mNode  = aNode;
   event.mIndex = aIndex;
   mQueue.push(event);
}

void Simulation::ScheduleFrame(SimTime aTime, int aKind, int aNode, const Frame& aFrame)
{
   Event event;
   event.mTime  = aTime;
   event.mOrder = aKind;
   event.mSeq   = mSeq++;
   event.mKind  = aKind;
   event.mNode  = aNode;
   event.mIndex = 0;
   event.mFrame = aFrame;
   mQueue.push(event);
}

// ---------------------------------------------------------------- main loop

void Simulation::Run()
{
   while (!mQueue.empty())
   {
      Event event = mQueue.top();
      mQueue.pop();
      if (event.mTime > mEnd)
      {
         break;
      }
      mNow = event.mTime;
      switch (event.mKind)
      {
      case EventKind::cREADY:
         OnReady(mNodes[static_cast<size_t>(event.mNode)], event.mIndex);
         break;
      case EventKind::cINPUT:
         OnInput(mNodes[static_cast<size_t>(event.mNode)]);
         break;
      case EventKind::cGENERATE:
         OnGenerate(mNodes[static_cast<size_t>(event.mNode)], event.mIndex);
         break;
      case EventKind::cPUBLISH:
         OnPublish(mNodes[static_cast<size_t>(event.mNode)], event.mFrame);
         break;
      case EventKind::cENCODE_DONE:
         OnEncodeDone(mNodes[static_cast<size_t>(event.mNode)], event.mFrame);
         break;
      case EventKind::cTX_END:
         OnTxEnd(event.mFrame);
         break;
      case EventKind::cOPPORTUNITY:
         OnOpportunity(mNodes[static_cast<size_t>(event.mNode)]);
         break;
      case EventKind::cSCRIPT:
         OnScript(event.mIndex);
         break;
      case EventKind::cWIFI_ARB:
         OnWifiArb(event.mIndex);
         break;
      case EventKind::cWIFI_END:
         OnWifiEnd(event.mIndex);
         break;
      case EventKind::cWIFI_BEACON:
         OnWifiBeacon();
         break;
      case EventKind::cSAMPLE:
         SampleResources();
         break;
      default:
         break;
      }
   }
   mNow = mEnd;
   Finish();
}

// ---------------------------------------------------------------- sender: inputs and generation

void Simulation::OnInput(Node& aNode)
{
   const SimTime lastInput = mEnd - mConfig.mDrainS * TimeUnit::cPER_S;
   if (mNow >= lastInput)
   {
      return;                                        // no required inputs during the drain
   }
   for (int64_t burst = 0; burst < mConfig.mBurst; ++burst)
   {
      // NET-017: the required recipient set is frozen here, at onset, whatever the links do next.
      const int     first   = static_cast<int>(mObligations.size());
      const int64_t inputId = mNextInputId++;
      for (const Node& other : mNodes)
      {
         if (other.mId == aNode.mId)
         {
            continue;
         }
         Obligation obligation;
         obligation.mInputId   = inputId;
         obligation.mOrigin    = aNode.mId;
         obligation.mRecipient = other.mId;
         obligation.mInputTime = mNow;
         obligation.mReadyTime = mNow + FromUs(mConfig.mInputReadyUs);
         obligation.mDeadline  = mNow + FromUs(mConfig.mDeadlineUs);
         obligation.mValue     = (inputId * 2654435761LL) % 1000003LL;   // the value the recipient must show
         mObligations.push_back(obligation);
      }
      Schedule(mNow + FromUs(mConfig.mInputReadyUs), EventKind::cREADY, aNode.mId, first);
   }
   Schedule(NextInputTime(aNode), EventKind::cINPUT, aNode.mId, 0);
}

SimTime Simulation::NextInputTime(Node& aNode)
{
   const int64_t train  = std::max<int64_t>(1, mConfig.mInputTrain);
   if (aNode.mTrainIndex == 0)
   {
      aNode.mTrainStart = mNow;                      // this input opened an engagement
   }
   if (aNode.mTrainIndex + 1 < train)
   {
      ++aNode.mTrainIndex;
      return mNow + FromUs(mConfig.mInputTrainGapUs); // the next pull in the same string
   }
   aNode.mTrainIndex = 0;
   // Engagements recur at the mean input rate divided by the inputs each one holds.
   const SimTime cycle = PeriodFromMilliHz(mConfig.mInputMilliHz) * train;
   if (mConfig.mInputMinGapUs > 0)
   {
      // Renewal process with a dead time: no input sooner than the human motor limit after the last one,
      // then an exponential wait chosen so the mean cycle, and so the mean input rate, is unchanged.
      const SimTime used = mNow - aNode.mTrainStart + FromUs(mConfig.mInputMinGapUs);
      const double  mean = static_cast<double>(std::max<SimTime>(cycle - used, 1));
      const double  wait = -mean * std::log(1.0 - Unit(mRngInput));
      return mNow + FromUs(mConfig.mInputMinGapUs) + static_cast<SimTime>(wait);
   }
   SimTime next = aNode.mTrainStart + cycle;
   if (mConfig.mInputDitherUs > 0)
   {
      // Spread each interval uniformly over +/- half the dither; the mean rate is unchanged.
      const SimTime dither = FromUs(mConfig.mInputDitherUs);
      next += Below(mRngInput, dither + 1) - dither / 2;
   }
   return next;
}

void Simulation::OnReady(Node& aNode, int aFirstObligation)
{
   const int     recipients = static_cast<int>(mNodes.size()) - 1;
   const int64_t bytes      = (static_cast<int64_t>(aNode.mOutstandingEvents) + 1) * UrgentBits(aNode) / 8;
   if (aNode.mOutstandingEvents >= mConfig.mUrgentEventCap || bytes > mConfig.mUrgentByteCap)
   {
      // NET-015: a rejected required input stays in the denominator as a failure.
      for (int offset = 0; offset < recipients; ++offset)
      {
         Obligation& obligation = mObligations[static_cast<size_t>(aFirstObligation + offset)];
         obligation.mRejected = true;
         obligation.mFailure  = "urgent_queue_overflow";
         if (mTrace.Enabled())
         {
            mTrace.Obligation(obligation.mInputId, obligation.mOrigin, obligation.mRecipient, obligation.mInputTime, -1,
                              obligation.mValue, obligation.mDeadline);
         }
      }
      return;
   }
   const SimTime inputTime = mObligations[static_cast<size_t>(aFirstObligation)].mInputTime;
   const int     index     = AddEvent(aNode, EntityOp::cCREATE, -1, inputTime, aFirstObligation,
                                      mObligations[static_cast<size_t>(aFirstObligation)].mValue);
   for (int offset = 0; offset < recipients; ++offset)
   {
      Obligation& obligation = mObligations[static_cast<size_t>(aFirstObligation + offset)];
      obligation.mEntityUid = aNode.mEvents[static_cast<size_t>(index)].mEntityUid;
      aNode.mEvents[static_cast<size_t>(index)].mValue = obligation.mValue;
      if (mTrace.Enabled())
      {
         mTrace.Obligation(obligation.mInputId, obligation.mOrigin, obligation.mRecipient, obligation.mInputTime,
                           obligation.mEntityUid, obligation.mValue, obligation.mDeadline);
      }
   }
}

int Simulation::AddEvent(Node& aNode, int aEntityOp, int64_t aEntityUid, SimTime aInputTime, int aFirstObligation, int64_t aValue)
{
   EventRecord record;
   const int64_t sequence = static_cast<int64_t>(aNode.mEvents.size()) + 1;
   record.mUid             = static_cast<int64_t>(aNode.mId) * cUID_STRIDE + sequence;
   record.mEntityUid       = (aEntityUid < 0) ? record.mUid : aEntityUid;
   record.mEntityOp        = aEntityOp;
   record.mRevision        = (aEntityOp == EntityOp::cCREATE) ? 1 : 2;
   record.mValue           = aValue;
   record.mInputTime       = aInputTime;
   record.mReadyTime       = mNow;
   record.mFirstObligation = aFirstObligation;
   record.mJobsOutstanding = 0;
   record.mBlockArmed      = false;
   const int index = static_cast<int>(aNode.mEvents.size());

   if (mMotion)
   {
      // The marker lands ahead of its owner. The owner publishes it where its own alignment puts it.
      const TruthPose owner = mMotion->At(aNode.mId, mNow);
      const double    range = cMARKER_MIN_M + cMARKER_SPAN_M * Unit(mRngAlign);
      const double*   error = mAlign.empty() ? nullptr : mAlign[static_cast<size_t>(aNode.mId)].mError;
      const double    yaw   = owner.mYaw + ((error != nullptr) ? error[3] : 0.0);
      record.mPoint[0] = owner.mX + range * std::cos(owner.mYaw);
      record.mPoint[1] = owner.mY + range * std::sin(owner.mYaw);
      record.mPoint[2] = cMARKER_HEIGHT_M;
      record.mPublished[0] = owner.mX + ((error != nullptr) ? error[0] : 0.0) + range * std::cos(yaw);
      record.mPublished[1] = owner.mY + ((error != nullptr) ? error[1] : 0.0) + range * std::sin(yaw);
      record.mPublished[2] = cMARKER_HEIGHT_M + ((error != nullptr) ? error[2] : 0.0);
      record.mCalEpoch = mAlign.empty() ? 0 : mAlign[static_cast<size_t>(aNode.mId)].mEpoch;
      Quaternion(yaw, owner.mPitch, owner.mRoll, record.mDirection);
   }
   for (int target : aNode.mStateTargets)            // the same recipient set state uses: each peer, or one common emission
   {
      UrgentJob job;
      job.mEvent        = index;
      job.mRecipient    = target;
      job.mCopiesSent   = 0;
      job.mNextEligible = mNow;
      job.mLastAttempt  = -1;
      aNode.mJobs.push_back(job);
      record.mJobsOutstanding += 1;
   }
   aNode.mEvents.push_back(record);
   PrepareEvent(aNode, index);
   aNode.mPendingUrgentBits += mConfig.mCopies * aNode.mEvents.back().mWireBits * static_cast<int64_t>(aNode.mStateTargets.size());
   aNode.mOutstandingEvents += 1;
   mResult.urgentHighEvents = std::max(mResult.urgentHighEvents, static_cast<long>(aNode.mOutstandingEvents));
   mResult.urgentHighBytes  = std::max(mResult.urgentHighBytes, static_cast<int64_t>(aNode.mOutstandingEvents) * UrgentBits(aNode) / 8);
   return index;
}

void Simulation::FinishJob(Node& aNode, int aEvent)
{
   EventRecord& record = aNode.mEvents[static_cast<size_t>(aEvent)];
   record.mJobsOutstanding -= 1;
   if (record.mJobsOutstanding == 0)
   {
      aNode.mOutstandingEvents -= 1;
      while (aNode.mCompleteSeq < static_cast<int64_t>(aNode.mEvents.size()) &&
             aNode.mEvents[static_cast<size_t>(aNode.mCompleteSeq)].mJobsOutstanding == 0)
      {
         aNode.mCompleteSeq += 1;                    // the summary a control frame carries
      }
   }
}

void Simulation::OnGenerate(Node& aNode, int aWhat)
{
   if (aWhat == cGENERATE_STATE)
   {
      if (!mAlign.empty())
      {
         UpdateAlignment(aNode);
      }
      if (mMotion)
      {
         const TruthPose truth = mMotion->At(aNode.mId, mNow);
         const double*   error = mAlign.empty() ? nullptr : mAlign[static_cast<size_t>(aNode.mId)].mError;
         aNode.mStatePose[0] = truth.mX + ((error != nullptr) ? error[0] : 0.0);
         aNode.mStatePose[1] = truth.mY + ((error != nullptr) ? error[1] : 0.0);
         aNode.mStatePose[2] = truth.mZ + ((error != nullptr) ? error[2] : 0.0);
         aNode.mStatePose[3] = truth.mYaw + ((error != nullptr) ? error[3] : 0.0);
         aNode.mStateVel[0]  = truth.mVx;
         aNode.mStateVel[1]  = truth.mVy;
         aNode.mStateVel[2]  = truth.mVz;
         aNode.mStateEpoch   = mAlign.empty() ? 0 : mAlign[static_cast<size_t>(aNode.mId)].mEpoch;
      }
      aNode.mStateRevision += 1;
      for (PendingState& pending : aNode.mPendingState)
      {
         if (pending.mPending)
         {
            mResult.superseded += 1;                 // unsent old state gives way to the newest authorized state
         }
         pending.mPending  = true;
         pending.mSample   = mNow;
         pending.mRevision = aNode.mStateRevision;
      }
      Schedule(mNow + PeriodFromMilliHz(mConfig.mStateMilliHz), EventKind::cGENERATE, aNode.mId, aWhat);
      return;
   }
   std::vector<char>& pending = (aWhat == cGENERATE_CONTROL) ? aNode.mPendingControl : aNode.mPendingRepairFixture;
   for (char& slot : pending)
   {
      if (slot != 0)
      {
         mResult.shedFixtures += 1;
      }
      slot = 1;
   }
   const int64_t rate = (aWhat == cGENERATE_CONTROL) ? mConfig.mControlMilliHz : mConfig.mRepairMilliHz;
   Schedule(mNow + PeriodFromMilliHz(rate), EventKind::cGENERATE, aNode.mId, aWhat);
}

// ---------------------------------------------------------------- sender: opportunity, admission, encoding

Frame Simulation::NewFrame(const Node& aNode, int aClass, int aOp, int aRecipient, int64_t aBits) const
{
   Frame frame;
   frame.mClass          = aClass;
   frame.mOp             = aOp;
   frame.mOrigin         = aNode.mId;
   frame.mSender         = aNode.mId;
   frame.mFinalRecipient = aRecipient;
   frame.mBits           = static_cast<int>(aBits);
   frame.mNextHop        = (aRecipient < 0) ? -1 : NextHop(frame);
   return frame;
}

// The receiver of the emission about to be made. Relays are the other
// participants in ascending order; a route never uses more hops than there are
// relays to carry it.
int Simulation::NextHop(const Frame& aFrame) const
{
   std::vector<int> relays;
   for (const Node& node : mNodes)
   {
      if (node.mId != aFrame.mOrigin && node.mId != aFrame.mFinalRecipient)
      {
         relays.push_back(node.mId);
      }
   }
   const int hops = static_cast<int>(std::min<int64_t>(mConfig.mHops, static_cast<int64_t>(relays.size()) + 1));
   if (aFrame.mHopIndex >= hops - 1)
   {
      return aFrame.mFinalRecipient;
   }
   // Spread the relay duty: which participant carries a pair's traffic depends on the pair.
   const size_t first = static_cast<size_t>(aFrame.mOrigin + aFrame.mFinalRecipient);
   return relays[(first + static_cast<size_t>(aFrame.mHopIndex)) % relays.size()];
}

// Byte admission, checked before encoding. A frame admitted now cannot start
// before its encoding ends, so the test uses the window as it will stand then:
// charges that age out in the meantime are not counted against it. Every frame
// admitted this way can start without passing the ceiling. A non-urgent frame
// must also leave room for every copy still owed by a ready urgent event
// (section 03: protect pending urgent copies).
bool Simulation::Admit(Node& aNode, int aClass, int64_t aBits)
{
   const SimTime earliestStart = mNow + FromUs(mConfig.mEncodeUs);
   const int64_t committed     = aNode.mLedger.SumAhead(mNow, earliestStart) + aNode.mReservedBits;
   if (committed + aBits > mConfig.mLimitBits)
   {
      return false;
   }
   if (aClass != FrameClass::cURGENT)
   {
      if (committed + aBits + aNode.mPendingUrgentBits > mConfig.mLimitBits)
      {
         return false;
      }
      const int64_t nonUrgent = aNode.mLedger.NonUrgentSumAhead(mNow, earliestStart) + aNode.mReservedNonUrgent;
      const int64_t urgentHeld = (mConfig.mLendUrgentShare == 0) ? mConfig.mUrgentShareBits : 0;
      if (nonUrgent + aBits > mConfig.mLimitBits - urgentHeld)
      {
         return false;                               // the urgent share is never lent
      }
      if (mConfig.mRepairShareBits > 0 && aClass != FrameClass::cREPAIR)
      {
         // Control and state may not spend the repair share; repair may spend their unused room.
         const int64_t repair = aNode.mLedger.RepairSumAhead(mNow, earliestStart) + aNode.mReservedRepair;
         if (nonUrgent - repair + aBits > mConfig.mLimitBits - urgentHeld - mConfig.mRepairShareBits)
         {
            return false;
         }
      }
      aNode.mReservedNonUrgent += aBits;
      if (aClass == FrameClass::cREPAIR)
      {
         aNode.mReservedRepair += aBits;
      }
   }
   aNode.mReservedBits += aBits;
   return true;
}

// Slot indices ordered so the recipient served longest ago comes first (section 05:
// rank replaceable state by age). Ties keep slot order.
std::vector<size_t> Simulation::LongestWaitingFirst(const std::vector<SimTime>& aLastSent)
{
   std::vector<size_t> order(aLastSent.size());
   for (size_t index = 0; index < order.size(); ++index)
   {
      order[index] = index;
   }
   std::stable_sort(order.begin(), order.end(), [&aLastSent](size_t aLhs, size_t aRhs) { return aLastSent[aLhs] < aLastSent[aRhs]; });
   return order;
}

// Serial encoder: t_attempt_start is the start of this frame's encoding. With
// sharedEncode, the same content bound for several recipients is encoded once
// and every copy takes that one encoding's start.
void Simulation::Encode(Node& aNode, Frame aFrame)
{
   const int64_t contentKey = (aFrame.mHopIndex != 0) ? -1
                            : ((static_cast<int64_t>(aFrame.mOp) << 56) | (static_cast<int64_t>(aFrame.mEventIndex + 1) << 28) |
                               (static_cast<int64_t>(aFrame.mCopyIndex) << 24) | static_cast<int64_t>(aFrame.mRevision & 0xFFFFFF));
   SimTime start = std::max(mNow, aNode.mEncoderFreeAt);
   if (mConfig.mSharedEncode != 0 && contentKey >= 0 && contentKey == aNode.mLastEncodeKey && aNode.mLastEncodeStart >= mNow)
   {
      start = aNode.mLastEncodeStart;                // already being encoded for another recipient
   }
   else
   {
      aNode.mEncoderFreeAt = start + FromUs(mConfig.mEncodeUs);
   }
   aNode.mLastEncodeKey   = contentKey;
   aNode.mLastEncodeStart = start;
   aFrame.mAttemptStart = start;
   if (aFrame.mHopIndex == 0)
   {
      aFrame.mOriginAttemptStart = start;
   }

   if (aFrame.mOp == FrameOp::cEVENT && aFrame.mHopIndex == 0)
   {
      // NET-008: every copy's eligible start, measured at the actual encoding start.
      const EventRecord& record = aNode.mEvents[static_cast<size_t>(aFrame.mEventIndex)];
      const SimTime wait  = start - record.mReadyTime;
      const size_t  copy  = static_cast<size_t>(aFrame.mCopyIndex);
      mResult.waitMaxByCopy[copy] = std::max(mResult.waitMaxByCopy[copy], wait);
      if (wait > static_cast<SimTime>(aFrame.mCopyIndex + 1) * mPeriod)
      {
         mResult.waitViolations += 1;
      }
   }
   ScheduleFrame(start + FromUs(mConfig.mEncodeUs), EventKind::cENCODE_DONE, aNode.mId, aFrame);
}

void Simulation::OnOpportunity(Node& aNode)
{
   std::vector<Frame> urgent;
   std::vector<Frame> others;

   // Relayed urgent copies that found no budget on arrival go first: they are already late.
   for (size_t index = 0; index < aNode.mForwardPending.size();)
   {
      const Frame& waiting = aNode.mForwardPending[index];
      if (waiting.mClass == FrameClass::cURGENT && Admit(aNode, waiting.mClass, waiting.mBits))
      {
         urgent.push_back(waiting);
         aNode.mForwardPending.erase(aNode.mForwardPending.begin() + static_cast<std::ptrdiff_t>(index));
      }
      else
      {
         ++index;
      }
   }

   // 1. Required control service: real repair requests, then the control fixtures.
   const int64_t requestBits = (mConfig.mRepairRequestBits > 0) ? mConfig.mRepairRequestBits : mConfig.mControlBits;
   while (!aNode.mRepairRequests.empty())
   {
      Frame frame = NewFrame(aNode, FrameClass::cCONTROL, FrameOp::cREPAIR_REQUEST, aNode.mRepairRequests.front().mOwner, requestBits);
      frame.mMissing = aNode.mRepairRequests.front().mMissing;
      if (!Admit(aNode, FrameClass::cCONTROL, Prepare(aNode, frame)))
      {
         break;
      }
      others.push_back(frame);
      aNode.mRepairRequests.pop_front();
      mResult.repairRequests += 1;
   }
   for (size_t slot : LongestWaitingFirst(aNode.mControlLastSent))
   {
      if (aNode.mPendingControl[slot] == 0)
      {
         continue;
      }
      Frame frame = NewFrame(aNode, FrameClass::cCONTROL, FrameOp::cCONTROL_FIXTURE, aNode.mFixtureTargets[slot], mConfig.mControlBits);
      frame.mSummaryComplete = aNode.mCompleteSeq;
      if (!mAlign.empty())
      {
         const Alignment& alignment = mAlign[static_cast<size_t>(aNode.mId)];
         frame.mCalEpoch = alignment.mEpoch;
         std::copy(alignment.mAnnounced, alignment.mAnnounced + 4, frame.mCal);
      }
      if (Admit(aNode, FrameClass::cCONTROL, Prepare(aNode, frame)))
      {
         aNode.mControlLastSent[slot] = mNow;
         others.push_back(frame);
         aNode.mPendingControl[slot] = 0;
      }
   }
   // A new calibration epoch is announced at once, as control traffic (Rev 6).
   for (size_t slot : LongestWaitingFirst(aNode.mCalibrationLastSent))
   {
      if (aNode.mPendingCalibration[slot] == 0)
      {
         continue;
      }
      const Alignment& alignment = mAlign[static_cast<size_t>(aNode.mId)];
      Frame frame = NewFrame(aNode, FrameClass::cCONTROL, FrameOp::cCALIBRATION, aNode.mFixtureTargets[slot], mConfig.mControlBits);
      frame.mCalEpoch     = alignment.mEpoch;
      frame.mCalSource    = alignment.mLastSource;
      frame.mCalReference = alignment.mLastReference;
      frame.mSampleTime   = mNow;
      std::copy(alignment.mAnnounced, alignment.mAnnounced + 4, frame.mCal);
      if (Admit(aNode, FrameClass::cCONTROL, Prepare(aNode, frame)))
      {
         aNode.mCalibrationLastSent[slot] = mNow;
         aNode.mPendingCalibration[slot] = 0;
         others.push_back(frame);
         mResult.calibrationFrames += (mNow >= mConfig.mWarmupS * TimeUnit::cPER_S) ? 1 : 0;
      }
   }

   // 2. Urgent copies, earliest deadline first (jobs are held in readiness order).
   //    A copy goes at the first eligible opportunity and the next two, without waiting for a receipt.
   for (UrgentJob& job : aNode.mJobs)
   {
      if (job.mCopiesSent >= mConfig.mCopies || job.mNextEligible > mNow)
      {
         continue;
      }
      const EventRecord& record = aNode.mEvents[static_cast<size_t>(job.mEvent)];
      if (!Admit(aNode, FrameClass::cURGENT, record.mWireBits))
      {
         mResult.urgentDeferrals += 1;               // a throttled source is a real scheduling delay
         continue;
      }
      Frame frame = NewFrame(aNode, FrameClass::cURGENT, FrameOp::cEVENT, job.mRecipient, record.mWireBits);
      frame.mEventIndex = job.mEvent;
      frame.mCopyIndex  = job.mCopiesSent;
      frame.mWire       = record.mWire;              // blind copies are the same bytes
      frame.mMacCharged = true;                      // the record's cost already includes any MAC framing
      urgent.push_back(frame);
      aNode.mPendingUrgentBits -= record.mWireBits;
      job.mCopiesSent   += 1;
      job.mNextEligible  = mNow + mPeriod;
   }

   // Encode urgent frames first so their starts are not pushed by lower classes.
   SimTime encoderAt = std::max(mNow, aNode.mEncoderFreeAt);
   for (const Frame& frame : urgent)
   {
      if (frame.mOp == FrameOp::cEVENT && frame.mHopIndex == 0)
      {
         for (UrgentJob& job : aNode.mJobs)
         {
            if (job.mEvent == frame.mEventIndex && job.mRecipient == frame.mFinalRecipient)
            {
               if (job.mLastAttempt >= 0)
               {
                  const SimTime gap = encoderAt - job.mLastAttempt;
                  mResult.gapMax = std::max(mResult.gapMax, gap);
                  if (gap > mPeriod)
                  {
                     mResult.gapViolations += 1;
                  }
               }
               job.mLastAttempt = encoderAt;
               break;
            }
         }
      }
      Encode(aNode, frame);
      encoderAt = aNode.mEncoderFreeAt;
   }
   for (size_t index = 0; index < aNode.mJobs.size();)
   {
      if (aNode.mJobs[index].mCopiesSent >= mConfig.mCopies)
      {
         const int event = aNode.mJobs[index].mEvent;
         aNode.mJobs.erase(aNode.mJobs.begin() + static_cast<std::ptrdiff_t>(index));
         FinishJob(aNode, event);
      }
      else
      {
         ++index;
      }
   }
   for (const Frame& frame : others)
   {
      Encode(aNode, frame);
   }

   // Relayed control, state and repair, in class order, after this node's own urgent copies.
   for (int cls = FrameClass::cCONTROL; cls < FrameClass::cCOUNT; ++cls)
   {
      for (size_t index = 0; index < aNode.mForwardPending.size();)
      {
         const Frame& waiting = aNode.mForwardPending[index];
         if (waiting.mClass == cls && cls != FrameClass::cURGENT && Admit(aNode, waiting.mClass, waiting.mBits))
         {
            Encode(aNode, waiting);
            aNode.mForwardPending.erase(aNode.mForwardPending.begin() + static_cast<std::ptrdiff_t>(index));
         }
         else
         {
            ++index;
         }
      }
   }

   // 3. Replaceable state from what is left.
   std::vector<SimTime> stateLastSent;
   for (const PendingState& pending : aNode.mPendingState)
   {
      stateLastSent.push_back(pending.mLastSent);
   }
   for (size_t slot : LongestWaitingFirst(stateLastSent))
   {
      PendingState& pending = aNode.mPendingState[slot];
      if (!pending.mPending)
      {
         continue;
      }
      Frame frame = NewFrame(aNode, FrameClass::cSTATE, FrameOp::cSTATE, aNode.mStateTargets[slot], mConfig.mStateBits);
      frame.mSampleTime = pending.mSample;
      frame.mRevision   = pending.mRevision;
      frame.mCalEpoch   = aNode.mStateEpoch;
      std::copy(aNode.mStatePose, aNode.mStatePose + 4, frame.mPose);
      std::copy(aNode.mStateVel, aNode.mStateVel + 3, frame.mVel);
      if (Admit(aNode, FrameClass::cSTATE, Prepare(aNode, frame)))
      {
         Encode(aNode, frame);
         pending.mPending  = false;
         pending.mLastSent = mNow;
      }
   }

   // 4. Repair: requested events, then the background fixture.
   while (!aNode.mRepairJobs.empty())
   {
      const EventRecord& record = aNode.mEvents[static_cast<size_t>(aNode.mRepairJobs.front().mEvent)];
      if (!Admit(aNode, FrameClass::cREPAIR, record.mWireBits))
      {
         break;
      }
      Frame frame = NewFrame(aNode, FrameClass::cREPAIR, FrameOp::cREPAIR_DATA, aNode.mRepairJobs.front().mRecipient, record.mWireBits);
      frame.mEventIndex = aNode.mRepairJobs.front().mEvent;
      frame.mWire       = record.mWire;
      frame.mMacCharged = true;
      Encode(aNode, frame);
      aNode.mRepairJobs.pop_front();
      mResult.repairFrames += 1;
   }
   for (size_t slot : LongestWaitingFirst(aNode.mRepairLastSent))
   {
      if (aNode.mPendingRepairFixture[slot] == 0)
      {
         continue;
      }
      Frame frame = NewFrame(aNode, FrameClass::cREPAIR, FrameOp::cREPAIR_FIXTURE, aNode.mFixtureTargets[slot], mConfig.mRepairBits);
      if (Admit(aNode, FrameClass::cREPAIR, Prepare(aNode, frame)))
      {
         aNode.mRepairLastSent[slot] = mNow;
         Encode(aNode, frame);
         aNode.mPendingRepairFixture[slot] = 0;
      }
   }

   Schedule(mNow + mPeriod, EventKind::cOPPORTUNITY, aNode.mId, 0);
}

// ---------------------------------------------------------------- the shared medium

void Simulation::OnEncodeDone(Node& aNode, Frame aFrame)
{
   aFrame.mMacEnqueue = mNow;
   if (Wifi())
   {
      DecodeOnce(aFrame);                            // the same bytes reach every receiver through the AP
      WifiJob job;
      job.mFrame   = aFrame;
      job.mTo      = static_cast<int>(mNodes.size());   // the AP
      job.mEnqueue = mNow;
      WifiEnqueue(aNode.mId, WifiAc(aFrame), job);
      return;
   }
   aNode.mMac.push_back(aFrame);
   mResult.macHigh = std::max(mResult.macHigh, static_cast<long>(aNode.mMac.size()));
   TryGrant();
}

// One collision domain, half duplex: one emission at a time, granted to the
// frame that has waited longest (ties to the lowest node id). No collisions.
void Simulation::TryGrant()
{
   if (mMediumBusy)
   {
      return;
   }
   Node* chosen = nullptr;
   for (Node& node : mNodes)
   {
      if (!node.mMac.empty() && (chosen == nullptr || node.mMac.front().mMacEnqueue < chosen->mMac.front().mMacEnqueue))
      {
         chosen = &node;
      }
   }
   if (chosen == nullptr)
   {
      return;
   }
   Frame frame = chosen->mMac.front();
   chosen->mMac.pop_front();
   StartEmission(*chosen, frame);
   frame.mTxEnd = mNow + (static_cast<int64_t>(frame.mBits) * TimeUnit::cPER_S + mConfig.mPhyBps - 1) / mConfig.mPhyBps;
   mMediumBusy  = true;
   mTransmitter = chosen->mId;
   ScheduleFrame(frame.mTxEnd, EventKind::cTX_END, chosen->mId, frame);
}

// The emission's first transmit start: NET-002 charges it here, delivered or not, and
// its admission reservation is released. C0 blockage arms on an event's first copy.
void Simulation::StartEmission(Node& aNode, Frame& aFrame)
{
   const bool urgent = (aFrame.mClass == FrameClass::cURGENT);
   const bool repair = (aFrame.mClass == FrameClass::cREPAIR);
   aFrame.mTxId    = mNextTxId++;
   aFrame.mTxStart = mNow;
   aNode.mLedger.Charge(mNow, aFrame.mBits, urgent, repair);
   aNode.mReservedBits -= aFrame.mBits;
   if (!urgent)
   {
      aNode.mReservedNonUrgent -= aFrame.mBits;
   }
   if (repair)
   {
      aNode.mReservedRepair -= aFrame.mBits;
   }
   if (mConfig.mBlockPeriods > 0 && aFrame.mOp == FrameOp::cEVENT && aFrame.mHopIndex == 0)
   {
      EventRecord& record = aNode.mEvents[static_cast<size_t>(aFrame.mEventIndex)];
      if (!record.mBlockArmed)
      {
         record.mBlockArmed = true;                  // C0: a blockage begins with the event's first copy
         aNode.mBlockUntil  = mNow + mConfig.mBlockPeriods * mPeriod;
      }
   }
   if (aFrame.mWire)
   {
      const int size = static_cast<int>(aFrame.mWire->size());
      mResult.wireSizes[static_cast<size_t>(KindOf(aFrame.mOp))].push_back(size);
      if (size + mConfig.mAuthBytes > mConfig.mDatagramLimitBytes)
      {
         mResult.wireOversize += 1;
         if (urgent)
         {
            mResult.wireOversizeUrgent += 1;         // the profile: an urgent event must fit the transport unit
         }
      }
   }
}

// Every receiver gets the same bytes, so one decode per emission decides for all of them.
void Simulation::DecodeOnce(Frame& aFrame)
{
   if (!aFrame.mWire || !mCodec)
   {
      return;
   }
   std::string error;
   aFrame.mWireOk = CheckWire(aFrame, error);
   mResult.wireEmissionsDecoded += 1;
   if (!aFrame.mWireOk)
   {
      mResult.wireDecodeFailures += 1;
      if (mResult.wireFirstError.empty())
      {
         mResult.wireFirstError = error;
      }
   }
}

// 802.11 MAC header, LLC/SNAP and FCS, charged with the frame when the channel is Wi-Fi.
int64_t Simulation::MacBits() const
{
   return (mConfig.mChannel == Channel::cWIFI_AP && mConfig.mChargeMac != 0) ? 8 * static_cast<int64_t>(mConfig.mWifiMacBytes) : 0;
}

bool Simulation::LinkUp(int aFrom, int aTo, SimTime aTime, const char*& aReason) const
{
   if (mConfig.mPartitionUs > 0)
   {
      const SimTime start = mConfig.mPartitionAtS * TimeUnit::cPER_S;
      if (aTime >= start && aTime < start + FromUs(mConfig.mPartitionUs) && ((aFrom == 0) != (aTo == 0)))
      {
         aReason = "partition";
         return false;
      }
   }
   if (mConfig.mBlackoutUs > 0)
   {
      const SimTime first = (mConfig.mWarmupS + cBLACKOUT_FIRST_S) * TimeUnit::cPER_S;
      const SimTime every = mConfig.mBlackoutEveryS * TimeUnit::cPER_S;
      if (aTime >= first && (aTime - first) % every < FromUs(mConfig.mBlackoutUs))
      {
         aReason = "blackout";
         return false;
      }
   }
   return true;
}

const char* Simulation::LossReason(const Frame& aFrame, int aRecipient)
{
   const char* reason = nullptr;
   if (!LinkUp(aFrame.mSender, aRecipient, aFrame.mTxStart, reason))
   {
      return reason;
   }
   if (aFrame.mOp == FrameOp::cEVENT && aFrame.mHopIndex == 0 && aFrame.mCopyIndex < mConfig.mEraseCopies)
   {
      return "erased_copy";                          // Q1/Q2
   }
   if (aFrame.mTxStart < mNodes[static_cast<size_t>(aFrame.mSender)].mBlockUntil)
   {
      return "blocked";                              // C0
   }
   return nullptr;
}

void Simulation::OnTxEnd(Frame aFrame)
{
   mMediumBusy  = false;
   mTransmitter = -1;
   Node& sender = mNodes[static_cast<size_t>(aFrame.mSender)];
   mResult.airtime   += aFrame.mTxEnd - aFrame.mTxStart;
   mResult.emissions += 1;
   mResult.bitsByClass[static_cast<size_t>(aFrame.mClass)]   += aFrame.mBits;
   mResult.framesByClass[static_cast<size_t>(aFrame.mClass)] += 1;

   DecodeOnce(aFrame);
   const bool oneDraw = (aFrame.mNextHop < 0 && mConfig.mCorrelatedLoss != 0);
   aFrame.mCommonLost = oneDraw && mConfig.mLossProb > 0.0 && Unit(mRngLoss) < mConfig.mLossProb;

   std::string recipients;
   std::string drops;
   for (const Node& node : mNodes)
   {
      const int id = node.mId;
      if (id == aFrame.mSender || (aFrame.mNextHop >= 0 && id != aFrame.mNextHop))
      {
         continue;
      }
      Deliver(aFrame, id, aFrame.mTxEnd, recipients, drops);
   }
   if (mTrace.Enabled())
   {
      const int64_t eventUid = (aFrame.mEventIndex >= 0)
                             ? mNodes[static_cast<size_t>(aFrame.mOrigin)].mEvents[static_cast<size_t>(aFrame.mEventIndex)].mUid : -1;
      mTrace.Transmission(aFrame.mTxId, eventUid, aFrame.mOrigin, aFrame.mSender, recipients, aFrame.mCopyIndex, aFrame.mHopIndex,
                          cCLASS_NAME[aFrame.mClass], aFrame.mAttemptStart, aFrame.mTxStart, aFrame.mTxEnd, aFrame.mBits,
                          mConfig.mLimitBits - sender.mLedger.SumAt(aFrame.mTxStart), drops);
   }
   TryGrant();
}

// One recipient's copy of an emission whose last bit reaches it at aArrival: the test
// case's losses, then propagation, jitter, reordering, duplication and decoding.
void Simulation::Deliver(const Frame& aFrame, int aRecipient, SimTime aArrival, std::string& aRecipients, std::string& aDrops)
{
   const bool  directional = (mConfig.mLossDirection == 0) || (mConfig.mLossDirection == 1 && aFrame.mSender == 0);
   const bool  oneDraw     = (aFrame.mNextHop < 0 && mConfig.mCorrelatedLoss != 0);
   const char* reason      = LossReason(aFrame, aRecipient);
   if (reason == nullptr && mConfig.mLossProb > 0.0)
   {
      const bool applies = directional || (mConfig.mLossDirection == 2 && aRecipient == 0);
      if (oneDraw)
      {
         reason = aFrame.mCommonLost ? "random_loss" : nullptr;
      }
      else if (applies && Unit(mRngLoss) < mConfig.mLossProb)
      {
         reason = "random_loss";                     // R0: independent per transmission and recipient
      }
   }
   if (!aRecipients.empty())
   {
      aRecipients += ";";
   }
   aRecipients += std::to_string(aRecipient);
   mResult.deliveries += 1;
   long& run = mLossRun[static_cast<size_t>(aFrame.mSender) * mNodes.size() + static_cast<size_t>(aRecipient)];
   if (reason != nullptr)
   {
      mResult.deliveriesLost += 1;
      run += 1;
      if (!aDrops.empty())
      {
         aDrops += ";";
      }
      aDrops += std::to_string(aRecipient) + ":" + reason;
      return;
   }
   if (run > 0)
   {
      mResult.burstHistogram[static_cast<size_t>(std::min<long>(run, cBURST_BINS - 1))] += 1;
      run = 0;
   }
   SimTime arrival = aArrival + FromUs(mConfig.mPropagationUs);
   if (mConfig.mJitterMaxUs > 0)
   {
      arrival += Below(mRngJitter, FromUs(mConfig.mJitterMaxUs) + 1);
   }
   if (mConfig.mReorderProb > 0.0 && Unit(mRngFault) < mConfig.mReorderProb)
   {
      arrival += FromUs(mConfig.mReorderUs);         // P0: held back, so it lands behind newer frames
   }
   ScheduleFrame(arrival + FromUs(mConfig.mDecodeUs), EventKind::cPUBLISH, aRecipient, aFrame);
   if (mConfig.mDupProb > 0.0 && Unit(mRngFault) < mConfig.mDupProb)
   {
      ScheduleFrame(arrival + FromUs(mConfig.mDecodeUs + cDUPLICATE_DELAY_US), EventKind::cPUBLISH, aRecipient, aFrame);
   }
}

// ---------------------------------------------------------------- receiver

void Simulation::OnPublish(Node& aNode, const Frame& aFrame)
{
   if (!aFrame.mWireOk)
   {
      return;                                        // a frame that does not decode is a lost frame
   }
   if (aFrame.mFinalRecipient >= 0 && aFrame.mFinalRecipient != aNode.mId)
   {
      Forward(aNode, aFrame);
      return;
   }
   switch (aFrame.mOp)
   {
   case FrameOp::cCONTROL_FIXTURE:
      LearnCalibration(aNode, aFrame);
      OnControl(aNode, aFrame);
      break;
   case FrameOp::cCALIBRATION:
      LearnCalibration(aNode, aFrame);
      break;
   case FrameOp::cREPAIR_REQUEST:
      OnRepairRequest(aNode, aFrame);
      break;
   case FrameOp::cEVENT:
   case FrameOp::cREPAIR_DATA:
   case FrameOp::cTAKEOVER:
      ApplyEvent(aNode, aFrame);
      break;
   case FrameOp::cSTATE:
      ApplyState(aNode, aFrame);
      break;
   default:
      break;
   }
}

// A relay re-emits the frame. The emission is charged to the relay's own ledger.
void Simulation::Forward(Node& aNode, const Frame& aFrame)
{
   Frame frame = aFrame;
   frame.mSender   = aNode.mId;
   frame.mHopIndex = aFrame.mHopIndex + 1;
   frame.mNextHop  = NextHop(frame);
   Prepare(aNode, frame);                            // the relay is the sender of its own emission
   if (frame.mClass == FrameClass::cURGENT && Admit(aNode, frame.mClass, frame.mBits))
   {
      Encode(aNode, frame);                          // an urgent copy is passed on at once; D counts every hop
      return;
   }
   // Everything else waits for this relay's next opportunity and takes its turn by class.
   // NET-015: the relay backlog is bounded. Replaceable state gives way first.
   if (frame.mOp == FrameOp::cSTATE)
   {
      for (Frame& waiting : aNode.mForwardPending)
      {
         if (waiting.mOp == FrameOp::cSTATE && waiting.mOrigin == frame.mOrigin && waiting.mFinalRecipient == frame.mFinalRecipient)
         {
            waiting = frame;                         // newest authorized state replaces the unsent older one
            mResult.superseded += 1;
            return;
         }
      }
   }
   if (static_cast<int64_t>(aNode.mForwardPending.size()) >= mConfig.mRelayBacklogCap)
   {
      mResult.relayDropped += 1;
      for (size_t index = 0; index < aNode.mForwardPending.size(); ++index)
      {
         if (aNode.mForwardPending[index].mClass != FrameClass::cURGENT)
         {
            aNode.mForwardPending.erase(aNode.mForwardPending.begin() + static_cast<std::ptrdiff_t>(index));
            aNode.mForwardPending.push_back(frame);
            return;
         }
      }
      return;                                        // only urgent frames wait: the newcomer is refused and logged
   }
   aNode.mForwardPending.push_back(frame);
   mResult.forwardHigh = std::max(mResult.forwardHigh, static_cast<long>(aNode.mForwardPending.size()));
}

void Simulation::MarkReceived(Node& aNode, int aOwner, int64_t aSeq)
{
   RemoteOwner& remote = aNode.mRemote[static_cast<size_t>(aOwner)];
   if (aSeq <= remote.mContig)
   {
      return;
   }
   remote.mBeyond.insert(aSeq);
   while (!remote.mBeyond.empty() && *remote.mBeyond.begin() == remote.mContig + 1)
   {
      remote.mContig += 1;
      remote.mBeyond.erase(remote.mBeyond.begin());
   }
}

SimTime Simulation::PhotonTime(const Node& aNode, SimTime aPublish) const
{
   // The effect shows in the first render frame that starts at or after publication;
   // the rest of the P allowance is rendering, video transport and scanout.
   SimTime frames = (aPublish - aNode.mDisplayPhase + mDisplayPeriod - 1) / mDisplayPeriod;
   if (aPublish < aNode.mDisplayPhase)
   {
      frames = 0;
   }
   const SimTime frameStart = aNode.mDisplayPhase + frames * mDisplayPeriod;
   return frameStart + (FromUs(mConfig.mDisplayUs) - mDisplayPeriod);
}

// NET-013 and NET-014: authenticate the claimed owner, apply each event at most
// once, never resurrect a deleted entity, never accept an older revision.
void Simulation::ApplyEvent(Node& aNode, const Frame& aFrame)
{
   const EventRecord& record = mNodes[static_cast<size_t>(aFrame.mOrigin)].mEvents[static_cast<size_t>(aFrame.mEventIndex)];
   const bool urgentCopy = (aFrame.mOp == FrameOp::cEVENT && !aFrame.mReplay);
   if (urgentCopy)
   {
      mResult.urgentArrivals += 1;
      if (mNow - record.mReadyTime > FromUs(Profile::cPUBLICATION_BOUND_US))
      {
         mResult.lateUrgentArrivals += 1;            // arrived, but past the publication bound
      }
   }
   const int entityOwner = static_cast<int>(record.mEntityUid / cUID_STRIDE);
   const int claimed     = (aFrame.mOp == FrameOp::cTAKEOVER) ? aFrame.mClaimedOwner : aFrame.mOrigin;
   if (claimed != entityOwner)
   {
      mResult.unauthorizedRejected += 1;             // a disconnect grants no takeover
      return;
   }

   const SimTime retention = mConfig.mDedupRetentionMs * TimeUnit::cPER_MS;
   while (!aNode.mDedupOrder.empty() &&
          (aNode.mDedupOrder.front().first < mNow - retention || static_cast<int64_t>(aNode.mDedupOrder.size()) > mConfig.mDedupEntries))
   {
      aNode.mDedup.erase(aNode.mDedupOrder.front().second);
      aNode.mDedupOrder.pop_front();
   }
   if (aNode.mDedup.count(record.mUid) > 0)
   {
      mResult.dedupSuppressed += 1;
      return;
   }
   EntityState& entity = aNode.mEntities[record.mEntityUid];
   if (entity.mDeleted && record.mEntityOp == EntityOp::cCREATE)
   {
      mResult.replayRejected += 1;                   // tombstone: an old create cannot bring the entity back
      return;
   }
   if (record.mRevision <= entity.mRevision)
   {
      mResult.revisionRejected += 1;                 // forgotten by dedup, still refused by revision
      return;
   }
   entity.mRevision = record.mRevision;
   entity.mDeleted  = (record.mEntityOp == EntityOp::cDELETE);
   aNode.mDedup.insert(record.mUid);
   aNode.mDedupOrder.push_back(std::make_pair(mNow, record.mUid));
   MarkReceived(aNode, aFrame.mOrigin, static_cast<int64_t>(aFrame.mEventIndex) + 1);

   if (record.mFirstObligation < 0)
   {
      return;
   }
   const int offset = (aNode.mId < aFrame.mOrigin) ? aNode.mId : aNode.mId - 1;
   Obligation& obligation = mObligations[static_cast<size_t>(record.mFirstObligation + offset)];
   obligation.mApplyCount += 1;
   if (obligation.mApplyCount > 1)
   {
      mResult.duplicateEffects += 1;
      return;
   }
   aNode.mAckOwner = aFrame.mOrigin;
   aNode.mAckSeq   = static_cast<int64_t>(aFrame.mEventIndex) + 1;
   if (!mAlign.empty() && record.mEntityOp == EntityOp::cCREATE && mNow >= mConfig.mWarmupS * TimeUnit::cPER_S)
   {
      // Where this recipient shows the marker, against where it truly is. The owner's
      // calibration epochs since it placed the marker move it; a missing epoch cannot.
      const RemoteOwner& remote = aNode.mRemote[static_cast<size_t>(aFrame.mOrigin)];
      double mission[2] = { record.mPublished[0], record.mPublished[1] };
      if (!remote.mCalibrations.empty() && remote.mCalibrations.rbegin()->first > record.mCalEpoch)
      {
         std::map<int, std::array<double, 4>>::const_iterator then = remote.mCalibrations.find(record.mCalEpoch);
         if (then != remote.mCalibrations.end() || record.mCalEpoch == 0)
         {
            const std::array<double, 4>& latest = remote.mCalibrations.rbegin()->second;
            const double base[2] = { (then != remote.mCalibrations.end()) ? then->second[0] : 0.0,
                                     (then != remote.mCalibrations.end()) ? then->second[1] : 0.0 };
            mission[0] += latest[0] - base[0];
            mission[1] += latest[1] - base[1];
         }
         else
         {
            mResult.markerUncorrected += 1;
         }
      }
      double world[2];
      Displayed(aNode.mId, mission, world);
      const double error = std::hypot(world[0] - record.mPoint[0], world[1] - record.mPoint[1]);
      (mPositioning->Near(aNode.mId, aFrame.mOrigin, mNow) ? mResult.markerNear : mResult.markerFar).push_back(error);
   }
   obligation.mPublish      = mNow;
   obligation.mPhoton       = PhotonTime(aNode, mNow);
   obligation.mCorrect      = (record.mValue == obligation.mValue && record.mEntityUid == obligation.mEntityUid);
   obligation.mSuccessTx    = aFrame.mTxId;
   obligation.mSuccessCopy  = urgentCopy ? aFrame.mCopyIndex : -1;
   obligation.mAttemptStart = aFrame.mOriginAttemptStart;
   if (urgentCopy)
   {
      // NET-009: the successful attempt's own D, every hop and queue included, never clamped.
      const SimTime delivery = mNow - aFrame.mOriginAttemptStart;
      mResult.deliveryMax = std::max(mResult.deliveryMax, delivery);
      if (delivery > FromUs(mConfig.mStageDeliveryUs))
      {
         mResult.deliveryViolations += 1;
      }
   }
}

void Simulation::ApplyState(Node& aNode, const Frame& aFrame)
{
   RemoteOwner& remote = aNode.mRemote[static_cast<size_t>(aFrame.mOrigin)];
   if (aFrame.mRevision <= remote.mStateRevision)
   {
      mResult.rollbackPrevented += 1;                // an older or repeated revision never replaces a newer one
      return;
   }
   if (remote.mStateSample >= 0)
   {
      // The state being replaced was on show from its arrival until now.
      for (int sample = 0; sample < cSTATE_AGE_SAMPLES; ++sample)
      {
         const SimTime instant = remote.mStateArrival + ((2 * sample + 1) * (mNow - remote.mStateArrival)) / (2 * cSTATE_AGE_SAMPLES);
         mResult.stateAge.push_back(instant - remote.mStateSample);
      }
   }
   remote.mStateRevision = aFrame.mRevision;
   remote.mStateSample   = aFrame.mSampleTime;
   remote.mStateArrival  = mNow;
   std::copy(aFrame.mPose, aFrame.mPose + 4, remote.mPubPose);
   std::copy(aFrame.mVel, aFrame.mVel + 3, remote.mPubVel);
   if (mAlign.empty() || mNow < mConfig.mWarmupS * TimeUnit::cPER_S)
   {
      return;
   }
   // Where this recipient shows the peer now, extrapolated from the published pose.
   const TruthPose peer = mMotion->At(aFrame.mOrigin, mNow);
   const double    age  = SecondsOf(mNow - aFrame.mSampleTime);
   const double    mission[2] = { aFrame.mPose[0] + aFrame.mVel[0] * age, aFrame.mPose[1] + aFrame.mVel[1] * age };
   double world[2];
   Displayed(aNode.mId, mission, world);
   const bool near = mPositioning->Near(aNode.mId, aFrame.mOrigin, mNow);
   (near ? mResult.avatarNear : mResult.avatarFar).push_back(std::hypot(world[0] - peer.mX, world[1] - peer.mY));

   // Consistency check: the published pose against the positioning function's fix.
   const PeerFix fix  = mPositioning->Query(aNode.mId, aFrame.mOrigin, mNow);
   const double  lead = SecondsOf(fix.mTimestamp - aFrame.mSampleTime);
   const double  expected[2] = { aFrame.mPose[0] + aFrame.mVel[0] * lead, aFrame.mPose[1] + aFrame.mVel[1] * lead };
   double measured[2] = { fix.mPose[0], fix.mPose[1] };
   if (fix.mNear)
   {
      const TruthPose me  = mMotion->At(aNode.mId, fix.mTimestamp);
      const double*   own = mAlign[static_cast<size_t>(aNode.mId)].mError;
      const double    yaw = me.mYaw + own[3];
      measured[0] = me.mX + own[0] + std::cos(yaw) * fix.mPose[0] - std::sin(yaw) * fix.mPose[1];
      measured[1] = me.mY + own[1] + std::sin(yaw) * fix.mPose[0] + std::cos(yaw) * fix.mPose[1];
   }
   const double residual = std::hypot(measured[0] - expected[0], measured[1] - expected[1]);
   if (fix.mNear)
   {
      mResult.checkNear.push_back(residual);
      mResult.checkNearFail += (residual > mConfig.mCheckNearM) ? 1 : 0;
   }
   else
   {
      mResult.checkFar.push_back(residual);
      mResult.checkFarFail += (residual > mConfig.mCheckFarM) ? 1 : 0;
   }
}

Simulation::~Simulation()
{
   if (mCorpus != nullptr)
   {
      std::fclose(mCorpus);
   }
}

// ---------------------------------------------------------------- Rev 6: encoded wire

int64_t Simulation::Utc(SimTime aTime) const
{
   return mUtcEpochUs + aTime / TimeUnit::cPER_US;
}

void Simulation::Geodetic(const double aMission[3], double& aLat, double& aLon, double& aHae) const
{
   const double degrees = 180.0 / cPI;
   aLat = cORIGIN_LAT_DEG + aMission[1] / cEARTH_RADIUS_M * degrees;
   aLon = cORIGIN_LON_DEG + aMission[0] / (cEARTH_RADIUS_M * std::cos(cORIGIN_LAT_DEG / degrees)) * degrees;
   aHae = cORIGIN_HAE_M + aMission[2];
}

CotMessage Simulation::Message(const Node& aNode, const Frame& aFrame) const
{
   CotMessage message;
   message.mKind               = KindOf(aFrame.mOp);
   message.mOwner              = aFrame.mOrigin;
   message.mSender             = aFrame.mSender;
   message.mClockEpoch         = 1;
   message.mClockUncertaintyUs = cCLOCK_UNCERTAINTY_US;
   message.mFrameEpoch         = 1;
   message.mTimeUs             = Utc(mNow);
   message.mStaleUs            = Utc(mNow) + static_cast<int64_t>(cCONTROL_STALE_S * 1e6);
   message.mCe                 = 9999999.0;
   message.mLe                 = 9999999.0;
   switch (message.mKind)
   {
   case MessageKind::cEVENT:
   {
      const EventRecord& record = mNodes[static_cast<size_t>(aFrame.mOrigin)].mEvents[static_cast<size_t>(aFrame.mEventIndex)];
      message.mOwner          = (aFrame.mOp == FrameOp::cTAKEOVER) ? aFrame.mClaimedOwner : aFrame.mOrigin;
      message.mEntity         = record.mEntityUid;
      message.mSequence       = static_cast<int64_t>(aFrame.mEventIndex) + 1;
      message.mAuthorityEpoch = 1;
      message.mRevision       = record.mRevision;
      message.mSampleUs       = Utc(record.mReadyTime);
      message.mOnsetUs        = Utc(record.mInputTime);
      message.mDeadlineUs     = Utc(record.mInputTime + FromUs(mConfig.mDeadlineUs));
      message.mCalibrationEpoch = record.mCalEpoch;
      std::copy(record.mDirection, record.mDirection + 4, message.mPose + 3);
      message.mOperation      = (aFrame.mOp == FrameOp::cTAKEOVER) ? Operation::cTAKEOVER
                              : (record.mEntityOp == EntityOp::cDELETE ? Operation::cDELETE : Operation::cCREATE);
      message.mExpiryUs       = Utc(record.mInputTime) + static_cast<int64_t>(cEVENT_STALE_S * 1e6);
      message.mValue          = static_cast<uint64_t>(record.mValue);
      message.mTimeUs         = Utc(record.mReadyTime);   // every copy is the same message
      message.mStaleUs        = message.mExpiryUs;
      Geodetic(record.mPublished, message.mLat, message.mLon, message.mHae);
      message.mCe = 1.5;
      message.mLe = 2.0;
      break;
   }
   case MessageKind::cSTATE:
   {
      message.mEntity           = static_cast<int64_t>(aFrame.mOrigin) * CotCodec::cEntityStride;
      message.mSequence         = aFrame.mRevision;
      message.mAuthorityEpoch   = 1;
      message.mRevision         = aFrame.mRevision;
      message.mSampleUs         = Utc(aFrame.mSampleTime);
      message.mCalibrationEpoch = aFrame.mCalEpoch;
      message.mPose[0] = aFrame.mPose[0];
      message.mPose[1] = aFrame.mPose[1];
      message.mPose[2] = aFrame.mPose[2];
      const TruthPose truth = mMotion ? const_cast<Motion&>(*mMotion).At(aFrame.mOrigin, aFrame.mSampleTime) : TruthPose();
      Quaternion(aFrame.mPose[3], truth.mPitch, truth.mRoll, message.mPose + 3);
      std::copy(aFrame.mVel, aFrame.mVel + 3, message.mVelocity);
      message.mTimeUs  = Utc(aFrame.mSampleTime);
      message.mStaleUs = message.mTimeUs + static_cast<int64_t>(cSTATE_STALE_S * 1e6);
      Geodetic(aFrame.mPose, message.mLat, message.mLon, message.mHae);
      message.mCe = 1.5;
      message.mLe = 2.0;
      break;
   }
   case MessageKind::cCONTROL:
      message.mHighestSequence  = aFrame.mSummaryComplete;
      message.mClockOffsetUs    = (static_cast<int64_t>(aFrame.mSender) * 7919) % 2000 - 1000;   // synthetic
      message.mMembershipEpoch  = 1;
      message.mAckOwner         = aNode.mAckOwner;
      message.mAckSequence      = aNode.mAckSeq;
      message.mCalibrationEpoch = aFrame.mCalEpoch;
      std::copy(aFrame.mCal, aFrame.mCal + 4, message.mCalibrationDelta);
      break;
   case MessageKind::cREPAIR_REQUEST:
      message.mRequestOwner = aFrame.mFinalRecipient;
      message.mMissing      = aFrame.mMissing;
      break;
   case MessageKind::cREPAIR_STATUS:
      message.mHighestSequence = aNode.mCompleteSeq;
      break;
   case MessageKind::cCALIBRATION:
      message.mSampleUs             = Utc(aFrame.mSampleTime);
      message.mCalibrationEpoch     = aFrame.mCalEpoch;
      message.mCalibrationSource    = aFrame.mCalSource;
      message.mCalibrationReference = aFrame.mCalReference;
      std::copy(aFrame.mCal, aFrame.mCal + 4, message.mCalibrationDelta);
      break;
   default:
      break;
   }
   return message;
}

// Build the frame's real message and charge what it costs: payload, UDP/IP and
// authentication. In abstract mode the configured cost stands.
int64_t Simulation::Prepare(Node& aNode, Frame& aFrame)
{
   if (!mCodec)
   {
      if (!aFrame.mMacCharged)
      {
         aFrame.mBits      += static_cast<int>(MacBits());
         aFrame.mMacCharged = true;
      }
      return aFrame.mBits;
   }
   const bool ownState = (aFrame.mOp == FrameOp::cSTATE && aFrame.mSender == aFrame.mOrigin);
   if (ownState && aNode.mStateWireRevision == aFrame.mRevision && aNode.mStateWire)
   {
      aFrame.mWire = aNode.mStateWire;             // one encoding per state revision, whatever the recipient
   }
   else
   {
      const CotMessage message = Message(aNode, aFrame);
      aFrame.mWire = std::make_shared<const std::string>(mCodec->Encode(message));
      if (mCorpus != nullptr && mCorpusCount[message.mKind] < mConfig.mCorpusPerKind)
      {
         mCorpusCount[message.mKind] += 1;
         WriteCorpusRecord(mCorpus, static_cast<int>(mConfig.mWire), static_cast<int>(mNodes.size()),
                           static_cast<uint64_t>(mConfig.mSeed), *mCodec, message, *aFrame.mWire);
      }
      if (ownState)
      {
         aNode.mStateWire         = aFrame.mWire;
         aNode.mStateWireRevision = aFrame.mRevision;
      }
   }
   aFrame.mBits = static_cast<int>(8 * (static_cast<int64_t>(aFrame.mWire->size()) + mConfig.mUdpIpBytes + mConfig.mAuthBytes) + MacBits());
   aFrame.mMacCharged = true;
   return aFrame.mBits;
}

void Simulation::PrepareEvent(Node& aNode, int aEvent)
{
   EventRecord& record = aNode.mEvents[static_cast<size_t>(aEvent)];
   Frame frame = NewFrame(aNode, FrameClass::cURGENT, FrameOp::cEVENT, -1, mConfig.mUrgentBits);
   frame.mEventIndex = aEvent;
   Prepare(aNode, frame);
   record.mWire     = frame.mWire;
   record.mWireBits = frame.mBits;
   aNode.mUrgentWireBits = frame.mBits;
}

// Queue caps count bytes: the configured cost in abstract mode, the latest encoded event otherwise.
int64_t Simulation::UrgentBits(const Node& aNode) const
{
   return (aNode.mUrgentWireBits > 0) ? aNode.mUrgentWireBits : mConfig.mUrgentBits;
}

// Decode the emission and check that it says what the sender meant.
bool Simulation::CheckWire(const Frame& aFrame, std::string& aError) const
{
   CotMessage message;
   if (!mCodec->Decode(*aFrame.mWire, message, aError))
   {
      return false;
   }
   const CotMessage expected = Message(mNodes[static_cast<size_t>(aFrame.mSender)], aFrame);
   bool same = (message.mKind == expected.mKind && message.mOwner == expected.mOwner && message.mSender == aFrame.mSender);
   switch (message.mKind)
   {
   case MessageKind::cEVENT:
      same = same && message.mEntity == expected.mEntity && message.mSequence == expected.mSequence &&
             message.mRevision == expected.mRevision && message.mValue == expected.mValue && message.mOperation == expected.mOperation &&
             message.mOnsetUs == expected.mOnsetUs && message.mCalibrationEpoch == expected.mCalibrationEpoch;
      break;
   case MessageKind::cSTATE:
      same = same && message.mEntity == expected.mEntity && message.mRevision == expected.mRevision &&
             message.mCalibrationEpoch == expected.mCalibrationEpoch &&
             std::fabs(message.mPose[0] - expected.mPose[0]) <= cTOLERANCE_M && std::fabs(message.mPose[1] - expected.mPose[1]) <= cTOLERANCE_M &&
             message.mSampleUs == expected.mSampleUs;
      break;
   case MessageKind::cCONTROL:
      same = same && message.mHighestSequence == expected.mHighestSequence && message.mCalibrationEpoch == expected.mCalibrationEpoch;
      break;
   case MessageKind::cREPAIR_REQUEST:
      same = same && message.mRequestOwner == expected.mRequestOwner && message.mMissing == expected.mMissing;
      break;
   case MessageKind::cCALIBRATION:
      same = same && message.mCalibrationEpoch == expected.mCalibrationEpoch &&
             std::fabs(message.mCalibrationDelta[0] - expected.mCalibrationDelta[0]) <= cTOLERANCE_M;
      break;
   default:
      break;
   }
   if (!same)
   {
      aError = "decoded message differs from what was sent";
   }
   return same;
}

// ---------------------------------------------------------------- Rev 6: serverless alignment

// Where a participant shows a mission-frame point in the world: its tracking frame
// maps the mission frame through its own alignment, error and all.
void Simulation::Displayed(int aViewer, const double aMission[2], double aWorld[2])
{
   const TruthPose viewer = mMotion->At(aViewer, mNow);
   const double*   error  = mAlign[static_cast<size_t>(aViewer)].mError;
   const double    dx = aMission[0] - (viewer.mX + error[0]);
   const double    dy = aMission[1] - (viewer.mY + error[1]);
   const double    c  = std::cos(-error[3]);
   const double    s  = std::sin(-error[3]);
   aWorld[0] = viewer.mX + c * dx - s * dy;
   aWorld[1] = viewer.mY + s * dx + c * dy;
}

void Simulation::LearnCalibration(Node& aNode, const Frame& aFrame)
{
   if (mAlign.empty())
   {
      return;
   }
   std::array<double, 4> calibration = { { aFrame.mCal[0], aFrame.mCal[1], aFrame.mCal[2], aFrame.mCal[3] } };
   aNode.mRemote[static_cast<size_t>(aFrame.mOrigin)].mCalibrations[aFrame.mCalEpoch] = calibration;
}

// One alignment step, at the participant's state rate: tracking drift, then the
// corrections its own far solution and its near peers allow. A correction past the
// threshold opens a new calibration epoch, announced over the participant network.
void Simulation::UpdateAlignment(Node& aNode)
{
   Alignment&      alignment = mAlign[static_cast<size_t>(aNode.mId)];
   double*         error     = alignment.mError;
   const TruthPose me        = mMotion->At(aNode.mId, mNow);
   const double    dt        = (alignment.mLastUpdate < 0) ? 0.0 : SecondsOf(mNow - alignment.mLastUpdate);
   alignment.mLastUpdate = mNow;
   auto gauss = [this]() { return std::sqrt(-2.0 * std::log(Unit(mRngAlign))) * std::cos(2.0 * cPI * Unit(mRngAlign)); };
   const bool measuring = mNow >= mConfig.mWarmupS * TimeUnit::cPER_S;

   // Drift grows with distance walked, in a slowly turning direction; heading drifts at a steady rate.
   alignment.mDriftDirection += cDRIFT_TURN_RAD * std::sqrt(dt) * gauss();
   const double walked = std::hypot(me.mVx, me.mVy) * dt * mAlignConfig.mDriftFraction;
   error[0] += walked * std::cos(alignment.mDriftDirection);
   error[1] += walked * std::sin(alignment.mDriftDirection);
   error[2] += cDRIFT_VERTICAL * walked * gauss();
   error[3] += alignment.mDriftYawSign * mAlignConfig.mDriftYawRadPerS * dt;

   double change[4] = { 0.0, 0.0, 0.0, 0.0 };
   const PeerFix own    = mPositioning->OwnFar(aNode.mId, mNow);
   const bool    newFar = (own.mTimestamp != alignment.mLastFar);
   // Near peers (960 Hz relative fix, joined to each peer's published pose at its sample time).
   double nearSum[4] = { 0.0, 0.0, 0.0, 0.0 };
   int    nearCount  = 0;
   double nearest    = 1e9;
   for (const Node& peer : mNodes)
   {
      if (peer.mId == aNode.mId || !mPositioning->Near(aNode.mId, peer.mId, mNow))
      {
         continue;
      }
      const RemoteOwner& remote = aNode.mRemote[static_cast<size_t>(peer.mId)];
      if (remote.mStateSample < 0 || mNow - remote.mStateSample > FromUs(cFRESH_STATE_US) ||
          !mPositioning->Near(aNode.mId, peer.mId, remote.mStateSample))
      {
         continue;
      }
      const PeerFix   fix  = mPositioning->Query(aNode.mId, peer.mId, remote.mStateSample);
      const TruthPose then = mMotion->At(aNode.mId, fix.mTimestamp);
      const double    lead = SecondsOf(fix.mTimestamp - remote.mStateSample);
      const double    yaw  = then.mYaw + error[3];
      const double    measured[4] = { then.mX + error[0] + std::cos(yaw) * fix.mPose[0] - std::sin(yaw) * fix.mPose[1],
                                      then.mY + error[1] + std::sin(yaw) * fix.mPose[0] + std::cos(yaw) * fix.mPose[1],
                                      then.mZ + error[2] + fix.mPose[2],
                                      yaw + fix.mPose[3] };
      const double    published[4] = { remote.mPubPose[0] + remote.mPubVel[0] * lead, remote.mPubPose[1] + remote.mPubVel[1] * lead,
                                       remote.mPubPose[2] + remote.mPubVel[2] * lead, remote.mPubPose[3] };
      for (int axis = 0; axis < 4; ++axis)
      {
         nearSum[axis] += measured[axis] - published[axis];
      }
      nearCount += 1;
      const double range = std::hypot(fix.mPose[0], fix.mPose[1]);
      if (range < nearest)
      {
         nearest = range;
         alignment.mLastReference = peer.mId;
      }
      if (measuring)
      {
         const double* other = mAlign[static_cast<size_t>(peer.mId)].mError;
         mResult.alignRelativeNear.push_back(std::hypot(error[0] - other[0], error[1] - other[1]));
      }
   }
   // Own far solution (5 Hz): pull toward the GPS-aided mission pose. A participant that a
   // near peer anchors takes it at a much lower gain, so meter-class GPS noise does not
   // undo the centimeter-class relative alignment (ruling A23).
   if (newFar)
   {
      alignment.mLastFar = own.mTimestamp;
      const TruthPose then = mMotion->At(aNode.mId, own.mTimestamp);
      const double    estimate[4] = { then.mX + error[0], then.mY + error[1], then.mZ + error[2], then.mYaw + error[3] };
      const double    gain = (nearCount > 0) ? mAlignConfig.mFarGainNear : mAlignConfig.mFarGain;
      for (int axis = 0; axis < 4; ++axis)
      {
         change[axis] -= gain * (estimate[axis] - own.mPose[axis]);
      }
      alignment.mLastSource    = 0;
      alignment.mLastReference = -1;
   }
   if (nearCount > 0)
   {
      for (int axis = 0; axis < 4; ++axis)
      {
         change[axis] -= mAlignConfig.mNearGain * nearSum[axis] / nearCount;
      }
      alignment.mLastSource = 1;
   }
   for (int axis = 0; axis < 4; ++axis)
   {
      error[axis] += change[axis];
      alignment.mCorrection[axis] += change[axis];
   }
   if (measuring)
   {
      mResult.alignAbsolute.push_back(std::hypot(error[0], error[1]));
   }

   const double moved = std::sqrt(std::pow(alignment.mCorrection[0] - alignment.mAnnounced[0], 2) +
                                  std::pow(alignment.mCorrection[1] - alignment.mAnnounced[1], 2) +
                                  std::pow(alignment.mCorrection[2] - alignment.mAnnounced[2], 2));
   if (moved > mAlignConfig.mThresholdM || std::fabs(alignment.mCorrection[3] - alignment.mAnnounced[3]) > mAlignConfig.mThresholdYawRad)
   {
      alignment.mEpoch += 1;
      std::copy(alignment.mCorrection, alignment.mCorrection + 4, alignment.mAnnounced);
      std::fill(aNode.mPendingCalibration.begin(), aNode.mPendingCalibration.end(), 1);
      mResult.calibrationEpochs += measuring ? 1 : 0;
   }
}

// A control frame carries its owner's summary: the highest sequence for which
// every copy has been sent. A recipient that still lacks one asks for it.
void Simulation::OnControl(Node& aNode, const Frame& aFrame)
{
   if (mConfig.mRepair == 0)
   {
      return;
   }
   RemoteOwner& remote = aNode.mRemote[static_cast<size_t>(aFrame.mOrigin)];
   if (aFrame.mSummaryComplete <= remote.mContig)
   {
      remote.mGapSince = -1;
      return;
   }
   if (remote.mGapSince < 0)
   {
      remote.mGapSince = mNow;                       // a copy may still be in flight: ask only if the gap outlives one summary
      return;
   }
   if (remote.mLastRequest >= 0 && mNow - remote.mLastRequest < FromUs(cREREQUEST_US))
   {
      return;
   }
   RepairRequest request;
   request.mOwner = aFrame.mOrigin;
   for (int64_t seq = remote.mContig + 1; seq <= aFrame.mSummaryComplete && static_cast<int>(request.mMissing.size()) < cMAX_REQUESTED; ++seq)
   {
      if (remote.mBeyond.count(seq) == 0)
      {
         request.mMissing.push_back(seq);
      }
   }
   if (!request.mMissing.empty())
   {
      remote.mLastRequest = mNow;
      aNode.mRepairRequests.push_back(request);
   }
}

void Simulation::OnRepairRequest(Node& aNode, const Frame& aFrame)
{
   for (int64_t seq : aFrame.mMissing)
   {
      const int event = static_cast<int>(seq) - 1;
      if (event < 0 || event >= static_cast<int>(aNode.mEvents.size()))
      {
         continue;
      }
      bool queued = false;
      for (const RepairJob& job : aNode.mRepairJobs)
      {
         if (job.mEvent == event && job.mRecipient == aFrame.mOrigin)
         {
            queued = true;
            break;
         }
      }
      if (!queued)
      {
         RepairJob job;
         job.mEvent     = event;
         job.mRecipient = aFrame.mOrigin;
         aNode.mRepairJobs.push_back(job);
      }
   }
}

// ---------------------------------------------------------------- R1 script, resources

void Simulation::OnScript(int aStep)
{
   Node& owner = mNodes[0];
   if (aStep == 0)
   {
      mScriptEntityEvent = AddEvent(owner, EntityOp::cCREATE, -1, mNow, -1);
      return;
   }
   if (aStep == 1)
   {
      AddEvent(owner, EntityOp::cDELETE, owner.mEvents[static_cast<size_t>(mScriptEntityEvent)].mEntityUid, mNow, -1);
      return;
   }
   Frame frame;
   frame.mOrigin = 0;
   frame.mSender = 0;
   frame.mReplay = true;
   if (aStep == 2)
   {
      // Replay the old create after the newer delete, once dedup has forgotten it.
      frame.mOp         = FrameOp::cEVENT;
      frame.mEventIndex = mScriptEntityEvent;
      for (Node& node : mNodes)
      {
         if (node.mId != 0)
         {
            frame.mFinalRecipient = node.mId;
            OnPublish(node, frame);
         }
      }
      return;
   }
   // Node 1 claims an entity node 0 owns.
   int target = -1;
   for (int index = static_cast<int>(owner.mEvents.size()) - 1; index >= 0; --index)
   {
      if (owner.mEvents[static_cast<size_t>(index)].mFirstObligation >= 0)
      {
         target = index;
         break;
      }
   }
   if (target < 0)
   {
      return;
   }
   frame.mOp           = FrameOp::cTAKEOVER;
   frame.mEventIndex   = target;
   frame.mClaimedOwner = 1;
   for (Node& node : mNodes)
   {
      if (node.mId != 1)
      {
         frame.mFinalRecipient = node.mId;
         OnPublish(node, frame);
      }
   }
}

void Simulation::SampleResources()
{
   for (const Node& node : mNodes)
   {
      SimTime oldest = -1;
      for (const UrgentJob& job : node.mJobs)
      {
         oldest = std::max(oldest, mNow - node.mEvents[static_cast<size_t>(job.mEvent)].mReadyTime);
      }
      int64_t bytes = static_cast<int64_t>(node.mOutstandingEvents) * UrgentBits(node) / 8;
      for (const Frame& frame : node.mMac)
      {
         bytes += frame.mBits / 8;
      }
      const char* busy = !mMediumBusy ? "idle" : (mTransmitter == node.mId ? "transmitting" : "medium_busy");
      mTrace.Resource(mNow, node.mId, static_cast<long>(node.mJobs.size() + node.mMac.size()), bytes, oldest, busy);
   }
   Schedule(mNow + FromUs(cRESOURCE_SAMPLE_US), EventKind::cSAMPLE, 0, 0);
}

// ---------------------------------------------------------------- scoring

void Simulation::Finish()
{
   for (long run : mLossRun)
   {
      if (run > 0)
      {
         mResult.burstHistogram[static_cast<size_t>(std::min<long>(run, cBURST_BINS - 1))] += 1;
      }
   }
   for (Node& node : mNodes)
   {
      const size_t id = static_cast<size_t>(node.mId);
      mResult.maxRollingBits[id] = node.mLedger.MaxObserved();
      mResult.chargedBits[id]    = node.mLedger.TotalCharged();
      mResult.ledgerViolations  += static_cast<long>(node.mLedger.Violations());
      if (mScriptEntityEvent >= 0 && node.mId != 0)
      {
         const int64_t entityUid = mNodes[0].mEvents[static_cast<size_t>(mScriptEntityEvent)].mEntityUid;
         std::map<int64_t, EntityState>::const_iterator found = node.mEntities.find(entityUid);
         if (found != node.mEntities.end() && !found->second.mDeleted)
         {
            mResult.resurrections += 1;              // the deleted entity is live again here
         }
      }
   }

   // Outage windows, for recovery time.
   std::vector<std::pair<SimTime, SimTime>> outages;
   if (mConfig.mPartitionUs > 0)
   {
      const SimTime start = mConfig.mPartitionAtS * TimeUnit::cPER_S;
      outages.push_back(std::make_pair(start, start + FromUs(mConfig.mPartitionUs)));
   }
   if (mConfig.mBlackoutUs > 0)
   {
      const SimTime every = mConfig.mBlackoutEveryS * TimeUnit::cPER_S;
      for (SimTime start = (mConfig.mWarmupS + cBLACKOUT_FIRST_S) * TimeUnit::cPER_S; start < mEnd; start += every)
      {
         outages.push_back(std::make_pair(start, start + FromUs(mConfig.mBlackoutUs)));
      }
   }

   const SimTime deadline    = FromUs(mConfig.mDeadlineUs);
   const SimTime uncertainty = FromUs(mConfig.mUncertaintyUs);
   const SimTime display     = FromUs(mConfig.mDisplayUs);
   const SimTime bound       = FromUs(Profile::cPUBLICATION_BOUND_US);
   // Each obligation is classified exactly once. Nothing leaves the denominator.
   for (const Obligation& obligation : mObligations)
   {
      mResult.obligations += 1;
      const char* outcome = "missing";
      SimTime latency = -1;
      if (obligation.mRejected)
      {
         mResult.rejected += 1;
         outcome = "rejected";
      }
      else if (obligation.mApplyCount == 0)
      {
         mResult.missing += 1;
      }
      else
      {
         latency = (mConfig.mEndpoint == Endpoint::cPUBLICATION)
                 ? (obligation.mPublish - obligation.mInputTime) + display + uncertainty
                 : (obligation.mPhoton - obligation.mInputTime) + uncertainty;
         const SimTime publication = obligation.mPublish - obligation.mReadyTime;
         mResult.publishMax = std::max(mResult.publishMax, publication);
         mResult.displayMax = std::max(mResult.displayMax, obligation.mPhoton - obligation.mPublish);
         if (publication > bound)
         {
            mResult.publicationViolations += 1;
         }
         if (!obligation.mCorrect)
         {
            mResult.incorrect += 1;
            outcome = "incorrect";
         }
         else
         {
            mResult.latency.push_back(latency);
            if (latency <= deadline && obligation.mApplyCount == 1)
            {
               mResult.onTime += 1;
               outcome = "on_time_correct";
            }
            else
            {
               mResult.late += 1;                    // applied for convergence; the deadline failure stands
               outcome = "late_correct";
            }
         }
      }
      for (const std::pair<SimTime, SimTime>& outage : outages)
      {
         if (obligation.mInputTime >= outage.first - deadline && obligation.mInputTime < outage.second && !obligation.mRejected)
         {
            if (obligation.mApplyCount == 0)
            {
               mResult.unrecovered += 1;
            }
            else if (obligation.mPublish > outage.second)
            {
               mResult.recoveryMax = std::max(mResult.recoveryMax, obligation.mPublish - outage.second);
            }
         }
      }
      if (mTrace.Enabled() && !obligation.mRejected)
      {
         mTrace.Response(obligation.mInputId, obligation.mRecipient, obligation.mReadyTime, obligation.mSuccessTx, obligation.mPublish,
                         (mConfig.mEndpoint == Endpoint::cMODELED_PHOTON) ? obligation.mPhoton : -1, uncertainty,
                         obligation.mCorrect, obligation.mApplyCount, 1, outcome,
                         (obligation.mApplyCount == 0) ? "no_copy_published" : ((latency > deadline) ? "deadline" : ""));
      }
      else if (mTrace.Enabled())
      {
         mTrace.Response(obligation.mInputId, obligation.mRecipient, obligation.mReadyTime, -1, -1, -1, uncertainty, false, 0, 0, outcome, obligation.mFailure);
      }
   }

   bool withinCeiling = (mResult.ledgerViolations == 0);
   for (int64_t bits : mResult.maxRollingBits)
   {
      withinCeiling = withinCeiling && (bits <= mConfig.mLimitBits);
   }
   const bool invariants = (mResult.duplicateEffects == 0 && mResult.unauthorizedApplied == 0 &&
                            mResult.resurrections == 0 && mResult.stateRollbacks == 0);
   mResult.servicePass     = (mResult.onTime == mResult.obligations) && withinCeiling && invariants;
   mResult.profilePass     = mResult.servicePass && mResult.waitViolations == 0 && mResult.gapViolations == 0 &&
                             mResult.deliveryViolations == 0 && mResult.displayMax <= display &&
                             mResult.wireOversizeUrgent == 0 && mResult.wireDecodeFailures == 0;
   mResult.conditionalPass = (mResult.missing == 0 && mResult.rejected == 0 && mResult.incorrect == 0 &&
                              mResult.publicationViolations == 0 && mResult.duplicateEffects == 0);
}
