#ifndef DOE_CONFIG_HPP
#define DOE_CONFIG_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "profile_vectors.hpp"

// How an application copy reaches its recipients. Named constants, not an enum.
struct Fanout
{
   static constexpr int cUNICAST = 0;                // one physical emission per recipient (profile default)
   static constexpr int cCOMMON  = 1;                // one emission heard by every recipient ("common RF delivery")
};

// The channel that carries participant frames.
struct Channel
{
   static constexpr int cIDEAL   = 0;                // one collision domain, perfect scheduling, a fixed PHY rate (Rev 1 to 5)
   static constexpr int cWIFI_AP = 1;                // 802.11 infrastructure: EDCA contention, ACKs and retries, the AP relays every frame
};

// How the access point sends a group-addressed frame on.
struct GroupMode
{
   static constexpr int cBASIC_RATE = 0;             // one transmission at the basic rate, no ACK, no retry; held for DTIM if any station dozes
   static constexpr int cTO_UNICAST = 1;             // converted to one acknowledged unicast per station (RFC 9119, 4.6.2)
};

// Which instant closes a response for scoring (trace_contract.evaluated_endpoint).
struct Endpoint
{
   static constexpr int cPUBLICATION    = 0;         // publication plus the declared P allowance
   static constexpr int cMODELED_PHOTON = 1;         // publication, then the recipient's next 90 Hz frame
};

// Every experiment knob. Defaults are the profile's starter workload and proposed
// allocations, taken from the generated profile_vectors.hpp. Numeric knobs are
// int64_t so one table can parse them; a flag is 0 or 1. Rates are millihertz so
// that 7.5 Hz and 0.5 Hz stay exact.
struct DoeConfig
{
   std::string mCase              = "Q0";            // case id printed in the banner

   // Run shape
   int64_t mNodes                 = Profile::cNODES;
   int64_t mDurationS             = Profile::cDURATION_S;
   int64_t mWarmupS               = Profile::cWARMUP_S;        // no required inputs before this
   int64_t mDrainS                = Profile::cDRAIN_S;         // no required inputs in the final part
   int64_t mSeed                  = 1;

   // Retained limits and proposed stage allocations
   int64_t mDeadlineUs            = Profile::cDEADLINE_US;         // NET-001
   int64_t mInputReadyUs          = Profile::cINPUT_READY_US;      // I: supplied allowance in this mode
   int64_t mOpportunityMilliHz    = Profile::cOPPORTUNITY_MILLIHZ; // urgent service rate
   int64_t mStageDeliveryUs       = Profile::cDELIVERY_US;         // D limit (checked, never added)
   int64_t mDisplayUs             = Profile::cDISPLAY_US;          // P allowance
   int64_t mUncertaintyUs         = Profile::cUNCERTAINTY_US;      // U
   int64_t mCopies                = Profile::cURGENT_COPIES;       // blind copies per urgent event
   int64_t mEndpoint              = Endpoint::cPUBLICATION;

   // Transport-queue model inputs (illustrative, not measured)
   int64_t mPhyBps                = Profile::cPHY_BPS;
   int64_t mEncodeUs              = Profile::cENCODE_US;
   int64_t mDecodeUs              = Profile::cDECODE_US;
   int64_t mPropagationUs         = Profile::cPROPAGATION_US;
   int64_t mJitterMaxUs           = 0;               // added delay, uniform on [0, max], per delivery

   // Offered workload (abstract complete-frame costs)
   int64_t mInputMilliHz          = Profile::cINPUT_MILLIHZ;
   int64_t mInputDitherUs         = 0;               // spread of each input interval, centered; 0: strictly periodic inputs
   int64_t mInputMinGapUs         = 0;               // human motor limit between one participant's inputs; > 0: inputs are a
                                                      // renewal process, minimum gap plus an exponential, at the mean input rate
   int64_t mInputTrain            = 1;               // inputs per engagement (trigger pulls in a string); > 1: engagements keep
                                                      // the mean input rate, each a string of inputs mInputTrainGapUs apart
   int64_t mInputTrainGapUs       = 0;               // gap between inputs inside an engagement (split time)
   int64_t mRepairRequestBits     = 0;               // charged size of a repair request; 0: the control frame size
   int64_t mBurst                 = 1;               // simultaneous inputs per node at each input instant
   int64_t mStateMilliHz          = Profile::cSTATE_MILLIHZ;
   int64_t mStateBits             = Profile::cSTATE_BITS;
   int64_t mUrgentBits            = Profile::cURGENT_BITS;
   int64_t mControlMilliHz        = Profile::cCONTROL_MILLIHZ;
   int64_t mControlBits           = Profile::cCONTROL_BITS;
   int64_t mRepairMilliHz         = Profile::cREPAIR_MILLIHZ;
   int64_t mRepairBits            = Profile::cREPAIR_BITS;

   // Topology
   int64_t mFanout                = Fanout::cUNICAST;
   int64_t mHops                  = 1;               // emissions per unicast copy; relays are other participants
   int64_t mFixturesPerRecipient  = 1;               // unicast: control and repair fixtures multiply per recipient
   int64_t mSharedEncode          = 0;               // 1: identical content for several recipients is encoded once

   // NET-002 ledger and reservations
   int64_t mLimitBits             = Profile::cLIMIT_BITS;
   int64_t mWindowUs              = Profile::cWINDOW_US;
   int64_t mUrgentShareBits       = Profile::cSHARE_URGENT_BITS;
   int64_t mRepairShareBits       = 0;               // repair share held back from control and state; 0: none (Rev 1 to 3); the profile's start is 8000
   int64_t mLendUrgentShare       = 1;               // 1: lend unused urgent share while pending copies stay feasible (the profile's rule)

   // Phases
   int64_t mSyncPhases            = 0;               // 1: every node shares one opportunity phase and one input instant;
                                                     // 2: one input instant, each node's own clock phases (Rev 6)
   double  mInputPhaseFrac        = -1.0;            // event-ready phase within the sender's opportunity period; -1 for seeded
   double  mRxPhaseFrac           = -1.0;            // recipient display-frame phase; -1 for seeded
   int64_t mPhaseOffsetUs         = 0;               // added to both phases: the 1 us boundary tests

   // Loss and faults
   double  mLossProb              = 0.0;             // independent, per transmission and recipient
   int64_t mLossDirection         = 0;               // 0 both ways; 1 only from node 0; 2 only toward node 0
   int64_t mCorrelatedLoss        = 0;               // common fan-out: one loss draw covers all recipients
   int64_t mEraseCopies           = 0;               // Q1/Q2: erase urgent copy indices below this
   int64_t mBlockPeriods          = 0;               // C0: block the origin for this many opportunity periods from each event's first copy
   int64_t mBlackoutUs            = 0;               // S0: every link down for this long ...
   int64_t mBlackoutEveryS        = 10;              // ... once per this many seconds
   double  mDupProb               = 0.0;             // P0: deliver a second copy of a frame
   double  mReorderProb           = 0.0;             // P0: hold a frame back ...
   int64_t mReorderUs             = 20000;           // ... by this long
   int64_t mPartitionUs           = 0;               // R1: node 0 cut from the rest for this long ...
   int64_t mPartitionAtS          = 30;              // ... starting here
   int64_t mReplayTest            = 0;               // R1: replay an old create after its delete; attempt an owner takeover
   int64_t mRepair                = 1;               // recipients request events they learn they missed

   // Declared capacities (NET-015; simulator defaults, not hardware limits)
   int64_t mUrgentEventCap        = Profile::cURGENT_EVENT_CAP;
   int64_t mUrgentByteCap         = Profile::cURGENT_BYTE_CAP;
   int64_t mDedupEntries          = Profile::cDEDUP_ENTRIES;
   int64_t mDedupRetentionMs      = Profile::cDEDUP_RETENTION_MS;
   int64_t mRelayBacklogCap       = 128;             // frames a relay may hold while it waits for budget

   // Encoded wire (NET-003, Rev 6). WireMode values are in cot_codec.hpp.
   int64_t mWire                  = 0;               // 0 abstract costs; 1 XML specified; 2 XML lean; 3 TAK v1 specified; 4 TAK v1 lean
   int64_t mUdpIpBytes            = 28;              // charged per datagram: UDP 8 + IPv4 20
   int64_t mAuthBytes             = 28;              // assumed authentication: 12 B nonce, 16 B tag (ruling A19)
   int64_t mDatagramLimitBytes    = 1200;            // profile resource default; an urgent event above it fails the profile

   // Motion and serverless alignment (Rev 6)
   int64_t mAlign                 = 0;               // 1: alignment from the peer positioning function, calibration epochs on the wire
   int64_t mArenaM                = 0;               // square arena side; 0: 10 m times the square root of the participants
   double  mFarSigmaM             = 1.5;             // own and peers' GPS-aided solution error (illustrative)
   double  mNearSigmaM            = 0.02;            // near relative solution error (illustrative)
   double  mDriftFraction         = 0.01;            // tracking drift per metre walked (illustrative)
   double  mFarGain               = 0.02;            // alignment gain per far solution
   double  mCalThresholdDeg       = 0.5;             // a new calibration epoch when the heading correction exceeds this
   double  mFarGainNear           = 0.002;           // the same while a near peer anchors the participant
   double  mNearGain              = 0.3;             // alignment gain per near update (Rev 6 sweep)
   double  mCalThresholdM         = 0.1;             // announce a new calibration epoch past this correction (Rev 6 sweep)
   double  mCheckNearM            = 0.5;             // consistency check: published pose against the near fix
   double  mCheckFarM             = 5.0;             // consistency check: published pose against the far fix

   // Channel (Rev 6)
   int64_t mChannel               = Channel::cIDEAL;
   int64_t mGroupMode             = GroupMode::cBASIC_RATE;
   double  mWifiDataMbps          = 65.0;            // station and AP unicast rate (802.11n MCS 7, 20 MHz, illustrative)
   double  mWifiBasicMbps         = 6.0;             // group-addressed rate (the lowest 5 GHz basic rate)
   double  mWifiAckMbps           = 24.0;            // control response rate
   int64_t mWifiMacBytes          = 38;              // MAC header 24, QoS 2, LLC/SNAP 8, FCS 4
   int64_t mWifiRetryLimit        = 7;               // attempts after the first
   int64_t mWifiBeaconUs          = 102400;          // 100 TU
   int64_t mWifiDtimPeriod        = 1;               // beacons per DTIM
   int64_t mWifiDozing            = 0;               // stations in power save; any one makes the AP hold group frames for DTIM
   int64_t mWifiVoice             = 1;               // 1: urgent and control at AC_VO, the rest at AC_BE; 0: all at AC_BE
   int64_t mChargeMac             = 1;               // 1: MAC framing and every retry count against NET-002, as the profile says

   std::string mTracePrefix;                         // write the four trace ledgers as <prefix>_*.csv when set
   std::string mCorpusFile;                          // Rust port stage 0: append encoded messages to this JSONL file when set
   int64_t     mCorpusPerKind = 300;                 // at most this many messages of each kind per run
};

// The profile's eleven cases as presets over the starter workload.
DoeConfig MakeCase(const std::string& aCase);
std::vector<std::string> CaseNames();

// Apply "key=value". Returns false for an unknown key or a bad value.
bool ApplyOverride(const std::string& aOverride, DoeConfig& aConfig);

// One line listing every knob, for the run banner.
std::string DescribeConfig(const DoeConfig& aConfig);

#endif
