#ifndef SIMULATION_HPP
#define SIMULATION_HPP

#include <array>
#include <cstdio>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <queue>
#include <random>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include "alignment.hpp"
#include "cot_codec.hpp"
#include "doe_config.hpp"
#include "rolling_ledger.hpp"
#include "sim_time.hpp"
#include "trace_writer.hpp"

// Traffic classes, in admission priority order (profile section 05: required
// control service first, urgent inputs by earliest deadline, then state, then repair).
struct FrameClass
{
   static constexpr int cCONTROL = 0;
   static constexpr int cURGENT  = 1;
   static constexpr int cSTATE   = 2;
   static constexpr int cREPAIR  = 3;
   static constexpr int cCOUNT   = 4;
};

// What a frame carries.
struct FrameOp
{
   static constexpr int cCONTROL_FIXTURE = 0;        // clock, membership and receipt placeholder; carries the owner's event summary
   static constexpr int cREPAIR_REQUEST  = 1;        // a recipient lists events it learned it missed
   static constexpr int cEVENT           = 2;        // one copy of a discrete event (create or delete)
   static constexpr int cSTATE           = 3;        // replaceable participant state
   static constexpr int cREPAIR_FIXTURE  = 4;        // background repair placeholder
   static constexpr int cREPAIR_DATA     = 5;        // a missed event, sent again on request
   static constexpr int cTAKEOVER        = 6;        // R1 stimulus: a non-owner claims an entity
   static constexpr int cCALIBRATION     = 7;        // a new calibration epoch (Rev 6 alignment)
};

struct EntityOp
{
   static constexpr int cCREATE = 0;
   static constexpr int cDELETE = 1;
};

// One physical frame. Copied by value through the event queue.
struct Frame
{
   int64_t              mTxId = -1;                  // assigned at transmit start
   int                  mClass = 0;                  // FrameClass
   int                  mOp = 0;                     // FrameOp
   int                  mOrigin = -1;                // original owner of the content
   int                  mSender = -1;                // node making this emission (owner or relay)
   int                  mNextHop = -1;               // addressed receiver of this emission; -1 for a common emission
   int                  mFinalRecipient = -1;        // end recipient; -1 for a common emission
   int                  mHopIndex = 0;               // 0 for the owner's emission
   int                  mCopyIndex = 0;              // urgent copy number
   int                  mEventIndex = -1;            // index into the origin's event list
   int                  mClaimedOwner = -1;          // TAKEOVER: the node claiming authority
   int                  mRevision = 0;               // STATE: the owner's state revision
   int                  mBits = 0;                   // complete coded frame cost
   SimTime              mSampleTime = -1;            // STATE: when the state was sampled
   SimTime              mOriginAttemptStart = -1;    // start of encoding at the owner (t_attempt_start)
   SimTime              mAttemptStart = -1;          // start of encoding for this emission
   SimTime              mMacEnqueue = -1;            // encoded and waiting for the medium
   SimTime              mTxStart = -1;
   SimTime              mTxEnd = -1;
   int64_t              mSummaryComplete = 0;        // CONTROL_FIXTURE: owner's highest fully sent event sequence
   bool                 mReplay = false;             // injected stimulus, not an emission
   std::vector<int64_t> mMissing;                    // REPAIR_REQUEST: sequences wanted
   // Rev 6: encoded wire and alignment content
   std::shared_ptr<const std::string> mWire;         // the encoded CoT message; null in abstract mode
   bool                 mWireOk = true;              // decoded and matched at the first receipt of this emission
   bool                 mMacCharged = false;         // mBits already include the 802.11 MAC framing
   bool                 mCommonLost = false;         // correlated loss drawn once for this emission (Wi-Fi relay)
   int                  mCalEpoch = 0;               // STATE, CONTROL, CALIBRATION: sender's calibration epoch
   double               mCal[4] = { 0.0, 0.0, 0.0, 0.0 };     // cumulative calibration of that epoch
   double               mPose[4] = { 0.0, 0.0, 0.0, 0.0 };    // STATE: published mission pose x y z yaw
   double               mVel[3] = { 0.0, 0.0, 0.0 };
   int                  mCalSource = 0;
   int                  mCalReference = -1;
};

// A discrete event an owner produced.
struct EventRecord
{
   int64_t mUid;                                     // origin * stride + sequence; reused by every copy and retry
   int64_t mEntityUid;                               // the entity it creates or deletes
   int     mEntityOp;                                // EntityOp
   int     mRevision;                                // entity revision this event writes
   int64_t mValue;                                   // the unpredictable value the recipient must show
   SimTime mInputTime;                               // physical onset
   SimTime mReadyTime;                               // ready for scheduling
   int     mFirstObligation;                         // index of its first obligation; -1 when unscored
   int     mJobsOutstanding;                         // urgent copy jobs not yet finished
   bool    mBlockArmed;                              // C0: its blockage window has started
   // Rev 6
   std::shared_ptr<const std::string> mWire;         // encoded once: every copy and repair carries the same bytes
   int64_t mWireBits = 0;
   int     mCalEpoch = 0;                            // owner's calibration epoch when it placed the marker
   double  mPoint[3] = { 0.0, 0.0, 0.0 };            // true world point of the marker
   double  mPublished[3] = { 0.0, 0.0, 0.0 };        // mission point the owner published
   double  mDirection[4] = { 1.0, 0.0, 0.0, 0.0 };   // published pose direction quaternion
};

// The copies of one event still owed to one recipient (or to all, when common).
struct UrgentJob
{
   int     mEvent;                                   // index into the owner's events
   int     mRecipient;                               // -1 for a common emission
   int     mCopiesSent;
   SimTime mNextEligible;                            // earliest opportunity for the next copy
   SimTime mLastAttempt;                             // encoding start of the previous copy
};

// One required response: one input at one recipient. Never dropped from the
// denominator (NET-017).
struct Obligation
{
   int64_t     mInputId;
   int         mOrigin;
   int         mRecipient;
   SimTime     mInputTime;
   SimTime     mReadyTime;
   SimTime     mDeadline;
   int64_t     mEntityUid = -1;
   int64_t     mValue = 0;
   bool        mRejected = false;
   bool        mCorrect = false;
   int         mApplyCount = 0;
   SimTime     mPublish = -1;                        // null until authenticated data reaches the scene consumer
   SimTime     mPhoton = -1;                         // modeled photon time
   SimTime     mAttemptStart = -1;                   // owner's encoding start of the successful copy
   int64_t     mSuccessTx = -1;
   int         mSuccessCopy = -1;                    // -1 when delivered by repair
   const char* mFailure = "";
};

struct PendingState
{
   SimTime mLastSent = -1;                           // when this recipient was last served; the longest-waiting goes first
   bool    mPending = false;
   SimTime mSample = -1;
   int     mRevision = 0;
};

struct EntityState
{
   int  mRevision = 0;
   bool mDeleted = false;                            // tombstone
};

// What a node remembers about one remote owner.
struct RemoteOwner
{
   int64_t           mContig = 0;                    // every event through this sequence was received
   std::set<int64_t> mBeyond;                        // received sequences above mContig
   SimTime           mLastRequest = -1;              // last repair request
   SimTime           mGapSince = -1;                 // when a summary first showed a missing event
   int               mStateRevision = 0;             // newest state revision published
   SimTime           mStateSample = -1;
   SimTime           mStateArrival = -1;
   // Rev 6: the owner's newest published pose and every calibration epoch learned
   double                                mPubPose[4] = { 0.0, 0.0, 0.0, 0.0 };
   double                                mPubVel[3] = { 0.0, 0.0, 0.0 };
   std::map<int, std::array<double, 4>>  mCalibrations;
};

struct RepairJob
{
   int mEvent;
   int mRecipient;
};

struct RepairRequest
{
   int                  mOwner;
   std::vector<int64_t> mMissing;
};

// One participant: PCB 1 content selection and encoding, the PCB 3 ledger, and
// the receive side.
struct Node
{
   Node(int aId, SimTime aWindow, int64_t aLimitBits);

   int                        mId;
   RollingLedger              mLedger;               // NET-002
   int64_t                    mReservedBits;         // admitted, not yet at transmit start
   int64_t                    mReservedNonUrgent;    // the part of mReservedBits outside the urgent class
   int64_t                    mReservedRepair;       // the part of mReservedBits in the repair class
   int64_t                    mPendingUrgentBits;    // copies still owed by ready events
   SimTime                    mOpportunityPhase;
   SimTime                    mDisplayPhase;
   SimTime                    mEncoderFreeAt;        // serial encoder
   int64_t                    mLastEncodeKey;        // content last encoded, for sharedEncode
   SimTime                    mLastEncodeStart;      // when that encoding started
   std::vector<SimTime>       mControlLastSent;      // per fixture slot: last control fixture admitted
   std::vector<SimTime>       mRepairLastSent;       // per fixture slot: last repair fixture admitted
   SimTime                    mBlockUntil;           // C0: emissions before this are blocked
   std::deque<Frame>          mMac;                  // encoded, waiting for the medium
   std::deque<Frame>          mForwardPending;       // relayed frames waiting for budget
   std::vector<EventRecord>   mEvents;               // events this node owns; sequence = index + 1
   std::vector<UrgentJob>     mJobs;
   int64_t                    mCompleteSeq;          // every event through this sequence has had all copies sent
   int                        mOutstandingEvents;    // events with copies still owed
   int64_t                    mTrainIndex;           // position of the next input inside the current engagement
   SimTime                    mTrainStart;           // input instant of the current engagement's first input
   std::vector<int>           mStateTargets;         // recipient per state slot; -1 for a common emission
   std::vector<int>           mFixtureTargets;       // recipient per fixture slot
   std::vector<PendingState>  mPendingState;
   std::vector<char>          mPendingControl;
   std::vector<char>          mPendingRepairFixture;
   std::deque<RepairJob>      mRepairJobs;
   std::deque<RepairRequest>  mRepairRequests;
   int                        mStateRevision;
   // receive side
   // Rev 6
   int                        mAckOwner = -1;        // newest event applied, for the control frame's receipt
   int64_t                    mAckSeq = -1;
   std::vector<char>          mPendingCalibration;   // per fixture slot: a new epoch still to announce
   std::vector<SimTime>       mCalibrationLastSent;
   double                     mStatePose[4] = { 0.0, 0.0, 0.0, 0.0 };   // published at the newest state sample
   double                     mStateVel[3] = { 0.0, 0.0, 0.0 };
   int                        mStateEpoch = 0;
   std::shared_ptr<const std::string> mStateWire;    // encoded state for mStateWireRevision
   int                        mStateWireRevision = -1;
   int64_t                    mUrgentWireBits = 0;   // charged bits of this node's latest urgent event
   std::vector<RemoteOwner>        mRemote;
   std::map<int64_t, EntityState>  mEntities;
   std::unordered_set<int64_t>     mDedup;           // event uids applied (NET-013)
   std::deque<std::pair<SimTime, int64_t>> mDedupOrder;
};

// Everything a run reports (scoring.summary_metrics and the stage checks).
struct RunResult
{
   long obligations = 0, onTime = 0, late = 0, missing = 0, rejected = 0, incorrect = 0, duplicateEffects = 0;
   std::vector<SimTime> latency;                     // delivered-correct observations, at the evaluated endpoint
   SimTime publishMax = -1;                          // ready to publication, worst delivered
   std::vector<SimTime> waitMaxByCopy;               // ready to attempt start, by copy index
   SimTime gapMax = 0, deliveryMax = 0, displayMax = 0;
   long waitViolations = 0, gapViolations = 0, deliveryViolations = 0, publicationViolations = 0, urgentDeferrals = 0;
   std::vector<int64_t> maxRollingBits, chargedBits;
   long ledgerViolations = 0;
   std::vector<int64_t> bitsByClass;
   std::vector<long>    framesByClass;
   SimTime airtime = 0;
   long emissions = 0, deliveries = 0, deliveriesLost = 0, urgentArrivals = 0, lateUrgentArrivals = 0;
   std::vector<long> burstHistogram;                 // consecutive losses on one link; index = length, last bin is "or more"
   long superseded = 0, shedFixtures = 0, dedupSuppressed = 0, revisionRejected = 0, rollbackPrevented = 0;
   long replayRejected = 0, unauthorizedRejected = 0, resurrections = 0, unauthorizedApplied = 0, stateRollbacks = 0;
   long repairRequests = 0, repairFrames = 0, unrecovered = 0;
   SimTime recoveryMax = -1;
   std::vector<SimTime> stateAge;
   long urgentHighEvents = 0, macHigh = 0, forwardHigh = 0, relayDropped = 0;
   int64_t urgentHighBytes = 0;
   bool servicePass = false, profilePass = false, conditionalPass = false;
   // Rev 6: encoded wire
   std::vector<std::vector<int>> wireSizes;          // per message kind: payload bytes of every message built
   long wireEmissionsDecoded = 0, wireDecodeFailures = 0, wireMismatches = 0, wireOversize = 0, wireOversizeUrgent = 0;
   std::string wireFirstError;
   // Rev 6: alignment (metres, horizontal)
   std::vector<double> alignAbsolute, alignRelativeNear, avatarNear, avatarFar, markerNear, markerFar, checkNear, checkFar;
   long checkNearFail = 0, checkFarFail = 0, calibrationEpochs = 0, calibrationFrames = 0, markerUncorrected = 0;
   // Rev 6: 802.11 access-point channel
   long wifiAttempts = 0, wifiCollided = 0, wifiInternal = 0, wifiDropped = 0, wifiGroupFrames = 0, wifiGroupLost = 0;
   long wifiApUnicast = 0, wifiBeacons = 0, wifiApQueueHigh = 0, wifiBuffered = 0;
   SimTime wifiBusy = 0;                             // channel occupancy: data, acknowledgments, beacons
   std::vector<double> wifiAccessUs;                 // station uplink: MAC enqueue to the start of the successful attempt
   std::vector<double> wifiRelayUs;                  // AP: arrival to the start of the successful downlink
};

// Rev 6: one 802.11 MAC frame waiting for the channel.
struct WifiJob
{
   Frame   mFrame;
   int     mTo = -1;                                 // station index, cAP, or cGROUP
   int     mRetries = 0;
   SimTime mEnqueue = 0;
};

// Rev 6: one EDCA function (an access category of one station or of the AP).
struct WifiQueue
{
   std::deque<WifiJob> mJobs;
   int     mCw = 0;
   int     mBackoff = -1;                            // slots left; -1 when none is drawn
   SimTime mActiveSince = 0;                         // when the current backoff began to count
};

struct WifiAir
{
   int     mStation = -1;                            // transmitter: station index or cAP
   int     mAc = -1;                                 // access category; -1 for a beacon
   SimTime mDataEnd = 0;
   bool    mGroup = false;
};

// Discrete-event model of the profile's transport-queue mode: real offered
// frames, byte admission against the rolling ledger, one shared half-duplex
// medium, loss, and routes. I and P are supplied allowances.
class Simulation
{
public:
   explicit Simulation(const DoeConfig& aConfig);
   ~Simulation();
   Simulation(const Simulation&) = delete;              // owns the corpus file handle
   Simulation& operator=(const Simulation&) = delete;

   void             Run();
   const RunResult& Result() const;

private:
   struct Event
   {
      SimTime  mTime;
      int      mOrder;                               // tie-break at equal times: lower first
      uint64_t mSeq;                                 // then insertion order
      int      mKind;
      int      mNode;
      int      mIndex;
      Frame    mFrame;
   };
   struct EventOrder
   {
      bool operator()(const Event& aLhs, const Event& aRhs) const;
   };
   struct EventKind
   {
      static constexpr int cREADY       = 0;         // an input becomes ready for scheduling
      static constexpr int cINPUT       = 1;
      static constexpr int cGENERATE    = 2;         // state sample or fixture due
      static constexpr int cPUBLISH     = 3;         // a delivered frame is decoded
      static constexpr int cENCODE_DONE = 4;
      static constexpr int cTX_END      = 5;
      static constexpr int cOPPORTUNITY = 6;
      static constexpr int cSCRIPT      = 7;         // R1 scripted stimulus
      static constexpr int cSAMPLE      = 8;         // resource trace
      static constexpr int cWIFI_ARB    = 9;         // Rev 6: a backoff may end now
      static constexpr int cWIFI_END    = 10;        // Rev 6: the channel goes idle
      static constexpr int cWIFI_BEACON = 11;        // Rev 6: target beacon transmission time
   };

   // Portable uniform draws: the standard distributions differ between compilers,
   // the engine's raw output does not.
   static double  Unit(std::mt19937_64& aRng);
   static int64_t Below(std::mt19937_64& aRng, int64_t aBound);
   SimTime        NextInputTime(Node& aNode);       // the next input instant under the configured input model

   void Schedule(SimTime aTime, int aKind, int aNode, int aIndex);
   void ScheduleFrame(SimTime aTime, int aKind, int aNode, const Frame& aFrame);

   // Sender side
   void OnInput(Node& aNode);
   void OnReady(Node& aNode, int aInputIndex);
   void OnGenerate(Node& aNode, int aWhat);
   void OnOpportunity(Node& aNode);
   bool Admit(Node& aNode, int aClass, int64_t aBits);
   void Encode(Node& aNode, Frame aFrame);
   static std::vector<size_t> LongestWaitingFirst(const std::vector<SimTime>& aLastSent);
   void OnEncodeDone(Node& aNode, Frame aFrame);
   void TryGrant();
   void OnTxEnd(Frame aFrame);
   int  AddEvent(Node& aNode, int aEntityOp, int64_t aEntityUid, SimTime aInputTime, int aFirstObligation, int64_t aValue = 0);
   void FinishJob(Node& aNode, int aEvent);
   Frame NewFrame(const Node& aNode, int aClass, int aOp, int aRecipient, int64_t aBits) const;
   int  NextHop(const Frame& aFrame) const;
   const char* LossReason(const Frame& aFrame, int aRecipient);

   // Rev 6: encoded wire and alignment
   int64_t    Prepare(Node& aNode, Frame& aFrame);         // build the frame's message and set its charged bits
   void       PrepareEvent(Node& aNode, int aEvent);
   CotMessage Message(const Node& aNode, const Frame& aFrame) const;
   bool       CheckWire(const Frame& aFrame, std::string& aError) const;
   int64_t    Utc(SimTime aTime) const;
   void       Geodetic(const double aMission[3], double& aLat, double& aLon, double& aHae) const;
   void       UpdateAlignment(Node& aNode);
   void       Displayed(int aViewer, const double aMission[2], double aWorld[2]);
   int64_t    UrgentBits(const Node& aNode) const;
   void       LearnCalibration(Node& aNode, const Frame& aFrame);
   bool LinkUp(int aFrom, int aTo, SimTime aTime, const char*& aReason) const;
   void StartEmission(Node& aNode, Frame& aFrame);        // first transmit start: NET-002 charge, reservations, C0, wire sizes
   void DecodeOnce(Frame& aFrame);
   void Deliver(const Frame& aFrame, int aRecipient, SimTime aArrival, std::string& aRecipients, std::string& aDrops);
   int64_t MacBits() const;

   // Rev 6: 802.11 access-point channel (wifi_channel.cpp)
   bool    Wifi() const;
   int     WifiAc(const Frame& aFrame) const;
   SimTime WifiAirtime(int64_t aBits, double aMbps, bool aHt) const;
   SimTime WifiAckTime() const;
   SimTime WifiAifs(int aStation, int aAc) const;
   int     WifiCwMin(int aStation, int aAc) const;
   int     WifiCwMax(int aStation, int aAc) const;
   SimTime WifiCountFrom(int aStation, int aAc) const;     // the slot boundary the backoff counts from
   void    WifiEnqueue(int aStation, int aAc, const WifiJob& aJob);
   void    WifiDraw(int aStation, int aAc);
   void    WifiSchedule();
   void    OnWifiArb(int aGeneration);
   void    OnWifiEnd(int aGeneration);
   void    OnWifiBeacon();
   void    WifiRelay(Frame aFrame, SimTime aArrival);
   void    WifiFail(int aStation, int aAc, bool aInternal);
   bool    Dozing(int aStation) const;

   // Receiver side
   void OnPublish(Node& aNode, const Frame& aFrame);
   void Forward(Node& aNode, const Frame& aFrame);
   void ApplyEvent(Node& aNode, const Frame& aFrame);
   void ApplyState(Node& aNode, const Frame& aFrame);
   void OnControl(Node& aNode, const Frame& aFrame);
   void OnRepairRequest(Node& aNode, const Frame& aFrame);
   void MarkReceived(Node& aNode, int aOwner, int64_t aSeq);
   SimTime PhotonTime(const Node& aNode, SimTime aPublish) const;

   void OnScript(int aStep);
   void SampleResources();
   void Finish();

   DoeConfig                                                  mConfig;
   SimTime                                                    mPeriod;        // urgent opportunity period
   SimTime                                                    mDisplayPeriod; // 90 Hz render frame
   SimTime                                                    mEnd;
   SimTime                                                    mNow;
   uint64_t                                                   mSeq;
   int64_t                                                    mNextTxId;
   int64_t                                                    mNextInputId;
   std::priority_queue<Event, std::vector<Event>, EventOrder> mQueue;
   std::vector<Node>                                          mNodes;
   std::vector<Obligation>                                    mObligations;
   bool                                                       mMediumBusy;
   int                                                        mTransmitter;
   std::vector<long>                                          mLossRun;       // per (sender, recipient): current run of losses
   std::mt19937_64                                            mRngSetup, mRngLoss, mRngJitter, mRngFault;
   std::mt19937_64                                            mRngInput;   // input-interval dither; its own stream, so dither moves no other draw
   int                                                        mScriptEntityEvent; // R1: node 0's event that is created then deleted
   // Rev 6
   std::unique_ptr<CotCodec>                                  mCodec;
   std::unique_ptr<Motion>                                    mMotion;
   std::unique_ptr<PeerPositioning>                           mPositioning;
   AlignmentConfig                                            mAlignConfig;
   std::vector<Alignment>                                     mAlign;
   std::mt19937_64                                            mRngAlign;      // alignment drift and marker ranges; its own stream
   int64_t                                                    mUtcEpochUs;    // synthetic UTC of simulation time zero
   // Rev 6: 802.11 access-point channel; index mNodes.size() is the AP
   std::vector<std::array<WifiQueue, 2>>                      mWifiQueue;
   std::deque<WifiJob>                                        mWifiDtimBuffer; // group frames held for the next DTIM
   std::vector<std::deque<WifiJob>>                           mWifiPsBuffer;   // unicast frames held for a dozing station
   std::vector<WifiAir>                                       mWifiAir;        // the transmissions now on the channel
   bool                                                       mWifiBusy = false;
   SimTime                                                    mWifiIdleSince = 0;
   int                                                        mWifiGeneration = 0;
   bool                                                       mWifiBeaconPending = false;
   long                                                       mWifiBeaconCount = 0;
   std::mt19937_64                                            mRngWifi;
   std::FILE*                                                 mCorpus = nullptr;   // Rust port stage 0 capture
   long                                                       mCorpusCount[MessageKind::cCOUNT] = {};
   TraceWriter                                                mTrace;
   RunResult                                                  mResult;
};

#endif
