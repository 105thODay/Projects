#include "alignment.hpp"

#include <algorithm>
#include <cmath>

namespace
{
   const double cPI = 3.14159265358979323846;

   struct Walk
   {
      static constexpr double cWALK_MPS       = 1.4;
      static constexpr double cSPRINT_MPS     = 4.0;
      static constexpr double cSPRINT_SHARE   = 0.2;
      static constexpr double cPAUSE_SHARE    = 0.25;
      static constexpr double cPAUSE_MIN_S    = 0.5;
      static constexpr double cPAUSE_MAX_S    = 3.0;
      static constexpr double cEYE_HEIGHT_M   = 1.6;
      static constexpr double cBOB_M          = 0.02;
      static constexpr double cBOB_HZ         = 1.8;
      static constexpr double cSCAN_RAD       = 0.35;  // head scan about the walking heading
      static constexpr double cSCAN_PERIOD_S  = 6.0;
      static constexpr double cPITCH_RAD      = 0.08;
      static constexpr double cPITCH_PERIOD_S = 4.3;
      static constexpr double cROLL_RAD       = 0.03;
      static constexpr double cROLL_PERIOD_S  = 5.1;
   };

   // Portable draws from the engine's raw output (the standard distributions differ by compiler).
   double Unit(std::mt19937_64& aRng)
   {
      return (static_cast<double>(aRng() >> 11) + 0.5) * (1.0 / 9007199254740992.0);
   }

   double Gauss(std::mt19937_64& aRng)
   {
      const double u1 = Unit(aRng);
      const double u2 = Unit(aRng);
      return std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * cPI * u2);
   }

   double Seconds(SimTime aTime)
   {
      return static_cast<double>(aTime) / static_cast<double>(TimeUnit::cPER_S);
   }
}

// ---------------------------------------------------------------- motion

Motion::Motion(int aParticipants, double aArenaM, uint64_t aSeed)
   : mArena(aArenaM),
     mSegments(static_cast<size_t>(aParticipants)),
     mRng(aSeed)
{
   for (int node = 0; node < aParticipants; ++node)
   {
      const double x = Unit(mRng) * mArena;
      const double y = Unit(mRng) * mArena;
      mSegments[static_cast<size_t>(node)].push_back(Segment{ 0.0, 0.0, x, y, x, y, Unit(mRng) * 2.0 * cPI });
      mPhase.push_back(Unit(mRng) * 2.0 * cPI);
   }
}

double Motion::ArenaM() const
{
   return mArena;
}

void Motion::Extend(int aNode, double aUntil)
{
   std::vector<Segment>& segments = mSegments[static_cast<size_t>(aNode)];
   while (segments.back().mT1 <= aUntil)
   {
      const Segment& last = segments.back();
      Segment next{ last.mT1, 0.0, last.mX1, last.mY1, last.mX1, last.mY1, last.mHeading };
      if (Unit(mRng) < Walk::cPAUSE_SHARE)
      {
         next.mT1 = next.mT0 + Walk::cPAUSE_MIN_S + Unit(mRng) * (Walk::cPAUSE_MAX_S - Walk::cPAUSE_MIN_S);
      }
      else
      {
         next.mX1 = Unit(mRng) * mArena;
         next.mY1 = Unit(mRng) * mArena;
         const double speed = (Unit(mRng) < Walk::cSPRINT_SHARE) ? Walk::cSPRINT_MPS : Walk::cWALK_MPS;
         const double length = std::hypot(next.mX1 - next.mX0, next.mY1 - next.mY0);
         next.mT1 = next.mT0 + std::max(length / speed, 0.1);
         if (length > 0.0)
         {
            next.mHeading = std::atan2(next.mY1 - next.mY0, next.mX1 - next.mX0);
         }
      }
      segments.push_back(next);
   }
}

TruthPose Motion::At(int aNode, SimTime aTime)
{
   const double t = Seconds(aTime);
   Extend(aNode, t);
   const std::vector<Segment>& segments = mSegments[static_cast<size_t>(aNode)];
   auto it = std::upper_bound(segments.begin(), segments.end(), t, [](double aT, const Segment& aSegment) { return aT < aSegment.mT1; });
   const Segment& segment = (it == segments.end()) ? segments.back() : *it;
   const double span = segment.mT1 - segment.mT0;
   const double f    = (span > 0.0) ? std::min(std::max((t - segment.mT0) / span, 0.0), 1.0) : 1.0;
   const double phase = mPhase[static_cast<size_t>(aNode)];
   TruthPose pose;
   pose.mX  = segment.mX0 + f * (segment.mX1 - segment.mX0);
   pose.mY  = segment.mY0 + f * (segment.mY1 - segment.mY0);
   pose.mVx = (span > 0.0) ? (segment.mX1 - segment.mX0) / span : 0.0;
   pose.mVy = (span > 0.0) ? (segment.mY1 - segment.mY0) / span : 0.0;
   const bool moving = (pose.mVx != 0.0 || pose.mVy != 0.0);
   pose.mZ   = Walk::cEYE_HEIGHT_M + (moving ? Walk::cBOB_M * std::sin(2.0 * cPI * Walk::cBOB_HZ * t + phase) : 0.0);
   pose.mVz  = moving ? Walk::cBOB_M * 2.0 * cPI * Walk::cBOB_HZ * std::cos(2.0 * cPI * Walk::cBOB_HZ * t + phase) : 0.0;
   pose.mYaw   = segment.mHeading + Walk::cSCAN_RAD * std::sin(2.0 * cPI * t / Walk::cSCAN_PERIOD_S + phase);
   pose.mPitch = Walk::cPITCH_RAD * std::sin(2.0 * cPI * t / Walk::cPITCH_PERIOD_S + 2.0 * phase);
   pose.mRoll  = Walk::cROLL_RAD * std::sin(2.0 * cPI * t / Walk::cROLL_PERIOD_S + 3.0 * phase);
   return pose;
}

// ---------------------------------------------------------------- the positioning function (stub)

PeerPositioning::PeerPositioning(Motion& aMotion, const AlignmentConfig& aConfig, int aParticipants, uint64_t aSeed)
   : mMotion(aMotion),
     mConfig(aConfig),
     mFarState(static_cast<size_t>(aParticipants), std::vector<double>(4, 0.0)),
     mFarTick(static_cast<size_t>(aParticipants), 0),
     mRng(aSeed)
{
   for (std::vector<double>& state : mFarState)
   {
      state[0] = mConfig.mFarSigmaM * Gauss(mRng);
      state[1] = mConfig.mFarSigmaM * Gauss(mRng);
      state[2] = mConfig.mFarSigmaVertM * Gauss(mRng);
      state[3] = mConfig.mFarSigmaYawRad * Gauss(mRng);
   }
}

void PeerPositioning::FarError(int aNode, SimTime aTick, double aError[4])
{
   std::vector<double>& state = mFarState[static_cast<size_t>(aNode)];
   SimTime& tick = mFarTick[static_cast<size_t>(aNode)];
   const SimTime period = static_cast<SimTime>(static_cast<double>(TimeUnit::cPER_S) / mConfig.mFarRateHz + 0.5);
   const double  phi    = std::exp(-1.0 / (mConfig.mFarRateHz * mConfig.mFarCorrelationS));
   const double  sigma[4] = { mConfig.mFarSigmaM, mConfig.mFarSigmaM, mConfig.mFarSigmaVertM, mConfig.mFarSigmaYawRad };
   while (tick + period <= aTick)
   {
      for (int axis = 0; axis < 4; ++axis)
      {
         state[static_cast<size_t>(axis)] = phi * state[static_cast<size_t>(axis)] + std::sqrt(1.0 - phi * phi) * sigma[axis] * Gauss(mRng);
      }
      tick += period;
   }
   for (int axis = 0; axis < 4; ++axis)
   {
      aError[axis] = state[static_cast<size_t>(axis)];
   }
}

bool PeerPositioning::Near(int aCaller, int aPeer, SimTime aNow)
{
   const TruthPose a = mMotion.At(aCaller, aNow);
   const TruthPose b = mMotion.At(aPeer, aNow);
   return std::hypot(a.mX - b.mX, a.mY - b.mY) <= mConfig.mNearRangeM;
}

PeerFix PeerPositioning::OwnFar(int aNode, SimTime aNow)
{
   const SimTime period = static_cast<SimTime>(static_cast<double>(TimeUnit::cPER_S) / mConfig.mFarRateHz + 0.5);
   PeerFix fix;
   fix.mTimestamp = (aNow / period) * period;
   const TruthPose truth = mMotion.At(aNode, fix.mTimestamp);
   double error[4];
   FarError(aNode, fix.mTimestamp, error);
   fix.mPose[0] = truth.mX + error[0];
   fix.mPose[1] = truth.mY + error[1];
   fix.mPose[2] = truth.mZ + error[2];
   fix.mPose[3] = truth.mYaw + error[3];
   return fix;
}

PeerFix PeerPositioning::Query(int aCaller, int aPeer, SimTime aNow)
{
   if (!Near(aCaller, aPeer, aNow))
   {
      return OwnFar(aPeer, aNow);                    // the peer's own GPS-aided solution, mission frame, 5 Hz
   }
   const SimTime period = static_cast<SimTime>(static_cast<double>(TimeUnit::cPER_S) / mConfig.mNearRateHz + 0.5);
   PeerFix fix;
   fix.mNear      = true;
   fix.mTimestamp = (aNow / period) * period;
   const TruthPose me   = mMotion.At(aCaller, fix.mTimestamp);
   const TruthPose peer = mMotion.At(aPeer, fix.mTimestamp);
   const double dx = peer.mX - me.mX;
   const double dy = peer.mY - me.mY;
   const double c  = std::cos(me.mYaw);
   const double s  = std::sin(me.mYaw);
   fix.mPose[0] = c * dx + s * dy + mConfig.mNearSigmaM * Gauss(mRng);    // caller body axes
   fix.mPose[1] = -s * dx + c * dy + mConfig.mNearSigmaM * Gauss(mRng);
   fix.mPose[2] = peer.mZ - me.mZ + mConfig.mNearSigmaM * Gauss(mRng);
   fix.mPose[3] = peer.mYaw - me.mYaw + mConfig.mNearSigmaYawRad * Gauss(mRng);
   return fix;
}
