// wifi_channel.cpp -- Rev 6: an 802.11 infrastructure channel (one access point, one BSS)
// in place of the ideal medium. Selected with channel=wifi_ap.
//
// What is modeled
//   EDCA contention (IEEE 802.11): per access category AIFS, slotted backoff drawn
//   from [0, CW], frozen while the channel is busy; CW doubles after a failed unicast
//   attempt up to CWmax and resets after a success; a frame is dropped after the retry
//   limit. Two transmissions that start in the same slot collide and both fail. Two
//   access categories of one station that reach zero together: the higher one transmits
//   and the lower one takes a collision (an internal collision).
//   Every participant frame goes up to the AP as unicast at the data rate and is
//   acknowledged. The AP relays a common emission as one group-addressed frame at the
//   basic rate, with no acknowledgment and no retry (group=basic), or converts it to one
//   acknowledged unicast frame per recipient (group=unicast). A frame for one recipient
//   is relayed as unicast.
//   Beacons at every target beacon transmission time, sent after PIFS. If any station is
//   in power save, the AP holds every group frame until after the next DTIM beacon; a
//   unicast frame for a dozing station waits for the next beacon.
//   NET-002: each station attempt, retries included, is charged to the station's ledger
//   at its start with the MAC framing (chargeMac=1). The AP's relayed copies are not a
//   participant's emissions; they are counted as channel time.
//
// What is not modeled (rulings in the handoff)
//   Hidden stations, capture, rate adaptation, frame errors other than collisions, block
//   acknowledgment and aggregation, PS-Poll and U-APSD signalling frames, EIFS, TXOP
//   bursting, and AP forwarding delay. The test cases' losses (erasures, blockage, random
//   loss, blackout, partition) are end-to-end and applied at final delivery, as on the
//   ideal medium.

#include <algorithm>

#include "simulation.hpp"

namespace
{
   const int64_t cSLOT_US          = 9;
   const int64_t cSIFS_US          = 16;
   const int64_t cHT_PREAMBLE_US   = 36;             // HT-mixed: L-STF, L-LTF, L-SIG, HT-SIG, HT-STF, one HT-LTF
   const int64_t cLEGACY_PREAMBLE_US = 20;
   const int64_t cSYMBOL_US        = 4;
   const int64_t cSERVICE_TAIL_BITS = 22;            // 16 service bits and 6 tail bits
   const int64_t cACK_BITS         = 14 * 8;
   const int64_t cBEACON_BITS      = 260 * 8;        // a beacon with a typical set of elements
   const int     cGROUP            = -2;
   const int     cVO               = 0;
   const int     cBE               = 1;
   const int     cBEACON_AC        = -1;
   const char* const cCLASS_NAME_WIFI[FrameClass::cCOUNT] = { "control", "urgent", "state", "repair" };
}

bool Simulation::Wifi() const
{
   return mConfig.mChannel == Channel::cWIFI_AP;
}

bool Simulation::Dozing(int aStation) const
{
   return aStation >= 0 && aStation < static_cast<int>(mNodes.size()) && aStation < mConfig.mWifiDozing;
}

// Urgent and control traffic rides AC_VO when wifiVoice=1; state and repair ride AC_BE.
int Simulation::WifiAc(const Frame& aFrame) const
{
   if (mConfig.mWifiVoice != 0 && (aFrame.mClass == FrameClass::cURGENT || aFrame.mClass == FrameClass::cCONTROL))
   {
      return cVO;
   }
   return cBE;
}

// OFDM duration: preamble plus whole 4 us symbols for service, data and tail bits.
SimTime Simulation::WifiAirtime(int64_t aBits, double aMbps, bool aHt) const
{
   const int64_t perSymbol = static_cast<int64_t>(aMbps * static_cast<double>(cSYMBOL_US) + 0.5);
   const int64_t symbols   = (cSERVICE_TAIL_BITS + aBits + perSymbol - 1) / perSymbol;
   return FromUs((aHt ? cHT_PREAMBLE_US : cLEGACY_PREAMBLE_US) + symbols * cSYMBOL_US);
}

SimTime Simulation::WifiAckTime() const
{
   return WifiAirtime(cACK_BITS, mConfig.mWifiAckMbps, false);
}

// The default EDCA parameter set of IEEE 802.11: AP values for the AP, station values otherwise.
SimTime Simulation::WifiAifs(int aStation, int aAc) const
{
   const bool ap = (aStation == static_cast<int>(mNodes.size()));
   int aifsn = 3;
   if (aAc == cBEACON_AC)
   {
      aifsn = 1;                                     // PIFS
   }
   else if (aAc == cVO)
   {
      aifsn = ap ? 1 : 2;
   }
   return FromUs(cSIFS_US + aifsn * cSLOT_US);
}

int Simulation::WifiCwMin(int /*aStation*/, int aAc) const
{
   return (aAc == cVO) ? 3 : 15;
}

int Simulation::WifiCwMax(int aStation, int aAc) const
{
   const bool ap = (aStation == static_cast<int>(mNodes.size()));
   return (aAc == cVO) ? 7 : (ap ? 63 : 1023);
}

// The slot boundary from which this function's backoff counts down: AIFS after the
// channel last went idle, or the first boundary after the backoff was drawn.
SimTime Simulation::WifiCountFrom(int aStation, int aAc) const
{
   const WifiQueue& queue = mWifiQueue[static_cast<size_t>(aStation)][static_cast<size_t>(aAc)];
   const SimTime    base  = mWifiIdleSince + WifiAifs(aStation, aAc);
   if (queue.mActiveSince <= base)
   {
      return base;
   }
   const SimTime slot = FromUs(cSLOT_US);
   return base + ((queue.mActiveSince - base + slot - 1) / slot) * slot;
}

void Simulation::WifiDraw(int aStation, int aAc)
{
   WifiQueue& queue   = mWifiQueue[static_cast<size_t>(aStation)][static_cast<size_t>(aAc)];
   queue.mBackoff     = static_cast<int>(Below(mRngWifi, queue.mCw + 1));
   queue.mActiveSince = mNow;
}

void Simulation::WifiEnqueue(int aStation, int aAc, const WifiJob& aJob)
{
   WifiQueue& queue = mWifiQueue[static_cast<size_t>(aStation)][static_cast<size_t>(aAc)];
   queue.mJobs.push_back(aJob);
   if (queue.mBackoff < 0)
   {
      WifiDraw(aStation, aAc);
   }
   const std::array<WifiQueue, 2>& both = mWifiQueue[static_cast<size_t>(aStation)];
   const long depth = static_cast<long>(both[0].mJobs.size() + both[1].mJobs.size());
   if (aStation == static_cast<int>(mNodes.size()))
   {
      mResult.wifiApQueueHigh = std::max(mResult.wifiApQueueHigh, depth);
   }
   else
   {
      mResult.macHigh = std::max(mResult.macHigh, depth);
   }
   WifiSchedule();
}

// When the channel is idle, wake at the earliest instant a backoff can end.
void Simulation::WifiSchedule()
{
   if (mWifiBusy)
   {
      return;
   }
   SimTime earliest = -1;
   if (mWifiBeaconPending)
   {
      earliest = std::max(mWifiIdleSince + WifiAifs(static_cast<int>(mNodes.size()), cBEACON_AC), mNow);
   }
   for (size_t station = 0; station < mWifiQueue.size(); ++station)
   {
      for (int ac = 0; ac < 2; ++ac)
      {
         const WifiQueue& queue = mWifiQueue[station][static_cast<size_t>(ac)];
         if (queue.mJobs.empty())
         {
            continue;
         }
         const SimTime start = WifiCountFrom(static_cast<int>(station), ac) + queue.mBackoff * FromUs(cSLOT_US);
         earliest = (earliest < 0) ? start : std::min(earliest, start);
      }
   }
   if (earliest >= 0)
   {
      mWifiGeneration += 1;
      Schedule(std::max(earliest, mNow), EventKind::cWIFI_ARB, 0, mWifiGeneration);
   }
}

// A failed attempt: a collision (external or internal). Unicast retries with a doubled
// window until the retry limit; a group frame is simply lost.
void Simulation::WifiFail(int aStation, int aAc, bool aInternal)
{
   WifiQueue& queue = mWifiQueue[static_cast<size_t>(aStation)][static_cast<size_t>(aAc)];
   WifiJob&   job   = queue.mJobs.front();
   if (aInternal)
   {
      mResult.wifiInternal += 1;
   }
   if (job.mTo == cGROUP)
   {
      mResult.wifiGroupLost += 1;
      const long recipients = static_cast<long>(mNodes.size()) - 1;
      mResult.deliveries     += recipients;
      mResult.deliveriesLost += recipients;
      queue.mJobs.pop_front();
      queue.mCw      = WifiCwMin(aStation, aAc);
      queue.mBackoff = -1;
   }
   else
   {
      job.mRetries += 1;
      if (job.mRetries > mConfig.mWifiRetryLimit)
      {
         mResult.wifiDropped += 1;
         queue.mJobs.pop_front();
         queue.mCw      = WifiCwMin(aStation, aAc);
         queue.mBackoff = -1;
      }
      else
      {
         queue.mCw      = std::min(2 * queue.mCw + 1, WifiCwMax(aStation, aAc));
         queue.mBackoff = -1;
      }
   }
   if (!queue.mJobs.empty())
   {
      WifiDraw(aStation, aAc);
   }
}

void Simulation::OnWifiArb(int aGeneration)
{
   if (aGeneration != mWifiGeneration || mWifiBusy)
   {
      return;                                        // superseded by a later schedule
   }
   const int     ap   = static_cast<int>(mNodes.size());
   const SimTime slot = FromUs(cSLOT_US);

   // Who starts now, and who keeps counting.
   std::vector<std::pair<int, int>> winners;
   const bool beacon = mWifiBeaconPending && std::max(mWifiIdleSince + WifiAifs(ap, cBEACON_AC), mNow) == mNow;
   for (size_t station = 0; station < mWifiQueue.size(); ++station)
   {
      for (int ac = 0; ac < 2; ++ac)
      {
         WifiQueue& queue = mWifiQueue[station][static_cast<size_t>(ac)];
         if (queue.mJobs.empty())
         {
            continue;
         }
         const SimTime from  = WifiCountFrom(static_cast<int>(station), ac);
         const SimTime start = from + queue.mBackoff * slot;
         if (start == mNow)
         {
            winners.emplace_back(static_cast<int>(station), ac);
         }
         else if (mNow > from)
         {
            queue.mBackoff -= static_cast<int>((mNow - from) / slot);    // slots counted before the channel went busy
            queue.mActiveSince = mNow;
         }
      }
   }
   if (!beacon && winners.empty())
   {
      WifiSchedule();
      return;
   }

   // Internal contention: the beacon, then the higher access category, wins within a station.
   std::vector<std::pair<int, int>> transmitters;
   if (beacon)
   {
      transmitters.emplace_back(ap, cBEACON_AC);
   }
   for (const std::pair<int, int>& winner : winners)
   {
      const bool apBusy  = beacon && winner.first == ap;
      const bool voFirst = (winner.second == cBE) &&
                           std::find(winners.begin(), winners.end(), std::make_pair(winner.first, cVO)) != winners.end();
      if (apBusy || voFirst)
      {
         WifiFail(winner.first, winner.second, true);
      }
      else
      {
         transmitters.push_back(winner);
      }
   }
   const bool collision = transmitters.size() > 1;
   SimTime    busyEnd   = mNow;
   mWifiAir.clear();
   for (const std::pair<int, int>& transmitter : transmitters)
   {
      WifiAir air;
      air.mStation = transmitter.first;
      air.mAc      = transmitter.second;
      if (air.mAc == cBEACON_AC)
      {
         air.mDataEnd = mNow + WifiAirtime(cBEACON_BITS, mConfig.mWifiBasicMbps, false);
         busyEnd      = std::max(busyEnd, air.mDataEnd);
         mWifiAir.push_back(air);
         continue;
      }
      WifiJob&      job  = mWifiQueue[static_cast<size_t>(air.mStation)][static_cast<size_t>(air.mAc)].mJobs.front();
      Frame&        frame = job.mFrame;
      const int64_t airBits = frame.mBits + (frame.mMacCharged ? 0 : 8 * static_cast<int64_t>(mConfig.mWifiMacBytes));
      air.mGroup   = (job.mTo == cGROUP);
      air.mDataEnd = mNow + (air.mGroup ? WifiAirtime(airBits, mConfig.mWifiBasicMbps, false)
                                        : WifiAirtime(airBits, mConfig.mWifiDataMbps, true));
      const SimTime end = air.mGroup ? air.mDataEnd : air.mDataEnd + FromUs(cSIFS_US) + WifiAckTime();
      busyEnd = std::max(busyEnd, end);
      if (air.mStation != ap)
      {
         // A participant's attempt: charged to its ledger at its start, retries included.
         Node& node = mNodes[static_cast<size_t>(air.mStation)];
         if (frame.mTxStart < 0)
         {
            StartEmission(node, frame);
            mResult.emissions += 1;
         }
         else
         {
            frame.mTxId = mNextTxId++;
            node.mLedger.Charge(mNow, frame.mBits, frame.mClass == FrameClass::cURGENT, frame.mClass == FrameClass::cREPAIR);
         }
         frame.mTxEnd = air.mDataEnd;
         mResult.wifiAttempts += 1;
         mResult.bitsByClass[static_cast<size_t>(frame.mClass)]   += frame.mBits;
         mResult.framesByClass[static_cast<size_t>(frame.mClass)] += 1;
         if (mTrace.Enabled())
         {
            const int64_t eventUid = (frame.mEventIndex >= 0)
                                   ? mNodes[static_cast<size_t>(frame.mOrigin)].mEvents[static_cast<size_t>(frame.mEventIndex)].mUid : -1;
            mTrace.Transmission(frame.mTxId, eventUid, frame.mOrigin, air.mStation, "ap", frame.mCopyIndex, frame.mHopIndex,
                                cCLASS_NAME_WIFI[frame.mClass], frame.mAttemptStart, mNow, air.mDataEnd, frame.mBits,
                                mConfig.mLimitBits - node.mLedger.SumAt(mNow), collision ? "ap:collision" : "");
         }
      }
      mWifiAir.push_back(air);
   }
   if (collision)
   {
      // No acknowledgment comes: the channel is idle again after the acknowledgment timeout.
      SimTime longest = mNow;
      for (const WifiAir& air : mWifiAir)
      {
         longest = std::max(longest, air.mDataEnd);
      }
      busyEnd = longest + FromUs(cSIFS_US) + WifiAckTime();
   }
   mWifiBusy = true;
   mResult.wifiBusy += busyEnd - mNow;
   mResult.airtime  += busyEnd - mNow;
   mWifiGeneration  += 1;
   Schedule(busyEnd, EventKind::cWIFI_END, 0, mWifiGeneration);
}

void Simulation::OnWifiEnd(int aGeneration)
{
   if (aGeneration != mWifiGeneration)
   {
      return;
   }
   mWifiBusy      = false;
   mWifiIdleSince = mNow;
   const int  ap        = static_cast<int>(mNodes.size());
   const bool collision = mWifiAir.size() > 1;
   const std::vector<WifiAir> air = mWifiAir;
   mWifiAir.clear();
   for (const WifiAir& transmission : air)
   {
      if (transmission.mAc == cBEACON_AC)
      {
         mWifiBeaconPending = false;
         mWifiBeaconCount  += 1;
         mResult.wifiBeacons += 1;
         // Dozing stations wake for every beacon and collect their buffered unicast frames.
         for (size_t station = 0; station < mWifiPsBuffer.size(); ++station)
         {
            while (!mWifiPsBuffer[station].empty())
            {
               const WifiJob job = mWifiPsBuffer[station].front();
               mWifiPsBuffer[station].pop_front();
               WifiEnqueue(ap, WifiAc(job.mFrame), job);
            }
         }
         if (mConfig.mWifiDtimPeriod > 0 && mWifiBeaconCount % mConfig.mWifiDtimPeriod == 0)
         {
            while (!mWifiDtimBuffer.empty())
            {
               const WifiJob job = mWifiDtimBuffer.front();
               mWifiDtimBuffer.pop_front();
               WifiEnqueue(ap, WifiAc(job.mFrame), job);
            }
         }
         continue;
      }
      if (collision)
      {
         mResult.wifiCollided += 1;
         WifiFail(transmission.mStation, transmission.mAc, false);
         continue;
      }
      WifiQueue& queue = mWifiQueue[static_cast<size_t>(transmission.mStation)][static_cast<size_t>(transmission.mAc)];
      const WifiJob job = queue.mJobs.front();
      queue.mJobs.pop_front();
      queue.mCw      = WifiCwMin(transmission.mStation, transmission.mAc);
      queue.mBackoff = -1;
      if (!queue.mJobs.empty())
      {
         WifiDraw(transmission.mStation, transmission.mAc);
      }
      const SimTime start = transmission.mDataEnd - (transmission.mGroup
                          ? WifiAirtime(job.mFrame.mBits + (job.mFrame.mMacCharged ? 0 : 8 * static_cast<int64_t>(mConfig.mWifiMacBytes)),
                                        mConfig.mWifiBasicMbps, false)
                          : WifiAirtime(job.mFrame.mBits + (job.mFrame.mMacCharged ? 0 : 8 * static_cast<int64_t>(mConfig.mWifiMacBytes)),
                                        mConfig.mWifiDataMbps, true));
      if (transmission.mStation != ap)
      {
         mResult.wifiAccessUs.push_back(ToMs(start - job.mEnqueue) * 1000.0);
         WifiRelay(job.mFrame, transmission.mDataEnd);
         continue;
      }
      mResult.wifiRelayUs.push_back(ToMs(start - job.mEnqueue) * 1000.0);
      // A receiver publishes no earlier than now: its copy ended before the acknowledgment.
      const SimTime arrival = std::max(transmission.mDataEnd, mNow - FromUs(mConfig.mPropagationUs + mConfig.mDecodeUs));
      std::string recipients;
      std::string drops;
      if (job.mTo == cGROUP)
      {
         for (const Node& node : mNodes)
         {
            if (node.mId != job.mFrame.mSender)
            {
               Deliver(job.mFrame, node.mId, arrival, recipients, drops);
            }
         }
      }
      else
      {
         Deliver(job.mFrame, job.mTo, arrival, recipients, drops);
      }
   }
   WifiSchedule();
}

void Simulation::OnWifiBeacon()
{
   mWifiBeaconPending = true;
   WifiSchedule();
}

// The AP has a participant's frame: relay it to its recipients.
void Simulation::WifiRelay(Frame aFrame, SimTime /*aArrival*/)
{
   const int  ap       = static_cast<int>(mNodes.size());
   const bool anyDoze  = mConfig.mWifiDozing > 0;
   const bool oneDraw  = (aFrame.mNextHop < 0 && mConfig.mCorrelatedLoss != 0);
   aFrame.mCommonLost  = oneDraw && mConfig.mLossProb > 0.0 && Unit(mRngLoss) < mConfig.mLossProb;
   const int  ac       = WifiAc(aFrame);
   WifiJob job;
   job.mFrame   = aFrame;
   job.mEnqueue = mNow;
   if (aFrame.mNextHop < 0 && mConfig.mGroupMode == GroupMode::cBASIC_RATE)
   {
      job.mTo = cGROUP;
      mResult.wifiGroupFrames += 1;
      if (anyDoze)
      {
         mResult.wifiBuffered += 1;
         mWifiDtimBuffer.push_back(job);             // held until after the next DTIM beacon
      }
      else
      {
         WifiEnqueue(ap, ac, job);
      }
      return;
   }
   for (const Node& node : mNodes)
   {
      const bool addressed = (aFrame.mNextHop < 0) ? (node.mId != aFrame.mSender) : (node.mId == aFrame.mNextHop);
      if (!addressed)
      {
         continue;
      }
      job.mTo = node.mId;
      mResult.wifiApUnicast += 1;
      if (Dozing(node.mId))
      {
         mResult.wifiBuffered += 1;
         mWifiPsBuffer[static_cast<size_t>(node.mId)].push_back(job);
      }
      else
      {
         WifiEnqueue(ap, ac, job);
      }
   }
}
