#ifndef ALIGNMENT_HPP
#define ALIGNMENT_HPP

#include <cstdint>
#include <random>
#include <vector>

#include "sim_time.hpp"

// Participant motion, the peer positioning function, and serverless frame alignment.
//
// The positioning function is the user's specified subsystem, reached as a call. Its own
// radio and network are outside this model: not in the Wi-Fi airtime, not charged to NET-002.
//   far peers (beyond 5 m): the peer's GPS-aided fusion solution, mission frame, 5 Hz
//   near peers (within 5 m): the peer's pose relative to the caller, 960 Hz
// Each call returns a pose and its solution timestamp; no uncertainty.

// Truth pose in the mission frame (synthetic local ENU at the surveyed origin).
struct TruthPose
{
   double mX = 0.0, mY = 0.0, mZ = 0.0;              // metres
   double mYaw = 0.0, mPitch = 0.0, mRoll = 0.0;     // radians; yaw counter-clockwise from east
   double mVx = 0.0, mVy = 0.0, mVz = 0.0;           // metres per second
};

// Random-waypoint walking and sprinting inside a square arena, generated lazily.
class Motion
{
public:
   Motion(int aParticipants, double aArenaM, uint64_t aSeed);

   TruthPose At(int aNode, SimTime aTime);
   double    ArenaM() const;

private:
   struct Segment
   {
      double mT0, mT1;                               // seconds
      double mX0, mY0, mX1, mY1;
      double mHeading;                               // radians
   };
   void Extend(int aNode, double aUntil);

   double                            mArena;
   std::vector<std::vector<Segment>> mSegments;
   std::vector<double>               mPhase;          // head-scan phase per participant
   std::mt19937_64                   mRng;
};

struct PeerFix
{
   bool    mNear = false;
   SimTime mTimestamp = 0;                           // solution time
   double  mPose[4] = { 0.0, 0.0, 0.0, 0.0 };        // far: mission x y z yaw; near: peer relative to caller, caller body axes
};

struct AlignmentConfig
{
   double mNearRangeM        = 5.0;                  // user's spec
   double mFarRateHz         = 5.0;                  // user's spec
   double mNearRateHz        = 960.0;                // user's spec
   // Illustrative error and filter values (no qualified numbers supplied); all are knobs.
   double mFarSigmaM         = 1.5;                  // GPS-aided horizontal error, Gauss-Markov
   double mFarSigmaVertM     = 3.0;
   double mFarSigmaYawRad    = 0.0349;               // 2 degrees
   double mFarCorrelationS   = 30.0;
   double mNearSigmaM        = 0.02;
   double mNearSigmaYawRad   = 0.0087;               // 0.5 degree
   double mDriftFraction     = 0.01;                 // tracking drift per metre walked
   double mDriftYawRadPerS   = 0.000145;             // 0.5 degree per minute
   double mFarGain           = 0.02;                 // per 5 Hz solution: 10 s time constant
   double mFarGainNear       = 0.002;                // per 5 Hz solution while a near peer anchors: 100 s
   double mNearGain          = 0.3;                  // per near update
   double mThresholdM        = 0.1;                  // a new calibration epoch when the correction exceeds this
   double mThresholdYawRad   = 0.0087;
};

// The user's positioning function, stubbed from truth with the configured errors.
class PeerPositioning
{
public:
   PeerPositioning(Motion& aMotion, const AlignmentConfig& aConfig, int aParticipants, uint64_t aSeed);

   PeerFix Query(int aCaller, int aPeer, SimTime aNow);
   PeerFix OwnFar(int aNode, SimTime aNow);          // a participant's own GPS-aided solution (ruling A21)
   bool    Near(int aCaller, int aPeer, SimTime aNow);

private:
   void FarError(int aNode, SimTime aTick, double aError[4]);

   Motion&                          mMotion;
   AlignmentConfig                  mConfig;
   std::vector<std::vector<double>> mFarState;       // per node: Gauss-Markov error x y z yaw at mFarTick
   std::vector<SimTime>             mFarTick;
   std::mt19937_64                  mRng;
};

// One participant's alignment of its tracking frame to the mission frame. The error is
// what its published mission poses are off by; corrections come from its own far
// solution and from near peers' published poses joined to the near relative fix.
struct Alignment
{
   double  mError[4] = { 0.0, 0.0, 0.0, 0.0 };       // x y z (m), yaw (rad)
   double  mCorrection[4] = { 0.0, 0.0, 0.0, 0.0 };  // cumulative applied correction since the session started
   double  mAnnounced[4] = { 0.0, 0.0, 0.0, 0.0 };   // cumulative correction carried by the newest epoch
   int     mEpoch = 0;
   double  mDriftDirection = 0.0;                    // radians
   double  mDriftYawSign = 1.0;
   SimTime mLastUpdate = -1;
   SimTime mLastFar = -1;
   int     mLastSource = 0;
   int     mLastReference = -1;
};

#endif
