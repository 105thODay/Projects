// ref_shim.cpp -- a C ABI over the ar_sim C++ reference codec, for differential tests only.
// RefMessage mirrors CotMessage field for field; the Rust side declares it #[repr(C)].

#include <cstdint>
#include <exception>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "cot_codec.hpp"

extern "C"
{
   struct RefMessage
   {
      int32_t        kind;
      int64_t        time_us;
      int64_t        stale_us;
      double         lat;
      double         lon;
      double         hae;
      double         ce;
      double         le;
      int32_t        owner;
      int32_t        sender;
      int64_t        entity;
      int64_t        sequence;
      int32_t        authority_epoch;
      int32_t        revision;
      int64_t        sample_us;
      int64_t        onset_us;
      int64_t        deadline_us;
      int64_t        clock_offset_us;
      int32_t        clock_epoch;
      int32_t        clock_uncertainty_us;
      int32_t        frame_epoch;
      int32_t        calibration_epoch;
      double         pose[7];
      double         velocity[3];
      int32_t        operation;
      int64_t        ack_sequence;
      int32_t        ack_owner;
      int64_t        expiry_us;
      uint64_t       value;
      int32_t        membership_epoch;
      int64_t        highest_sequence;
      int32_t        request_owner;
      const int64_t* missing;
      uint64_t       missing_count;
      double         calibration_delta[4];
      int32_t        calibration_source;
      int32_t        calibration_reference;
   };
}

namespace
{
   struct Handle
   {
      CotCodec    mCodec;
      CotMessage  mLast;
      std::string mText;
   };

   void ToRef(const CotMessage& aIn, RefMessage& aOut)
   {
      aOut.kind                 = aIn.mKind;
      aOut.time_us              = aIn.mTimeUs;
      aOut.stale_us             = aIn.mStaleUs;
      aOut.lat                  = aIn.mLat;
      aOut.lon                  = aIn.mLon;
      aOut.hae                  = aIn.mHae;
      aOut.ce                   = aIn.mCe;
      aOut.le                   = aIn.mLe;
      aOut.owner                = aIn.mOwner;
      aOut.sender               = aIn.mSender;
      aOut.entity               = aIn.mEntity;
      aOut.sequence             = aIn.mSequence;
      aOut.authority_epoch      = aIn.mAuthorityEpoch;
      aOut.revision             = aIn.mRevision;
      aOut.sample_us            = aIn.mSampleUs;
      aOut.onset_us             = aIn.mOnsetUs;
      aOut.deadline_us          = aIn.mDeadlineUs;
      aOut.clock_offset_us      = aIn.mClockOffsetUs;
      aOut.clock_epoch          = aIn.mClockEpoch;
      aOut.clock_uncertainty_us = aIn.mClockUncertaintyUs;
      aOut.frame_epoch          = aIn.mFrameEpoch;
      aOut.calibration_epoch    = aIn.mCalibrationEpoch;
      std::memcpy(aOut.pose, aIn.mPose, sizeof(aOut.pose));
      std::memcpy(aOut.velocity, aIn.mVelocity, sizeof(aOut.velocity));
      aOut.operation            = aIn.mOperation;
      aOut.ack_sequence         = aIn.mAckSequence;
      aOut.ack_owner            = aIn.mAckOwner;
      aOut.expiry_us            = aIn.mExpiryUs;
      aOut.value                = aIn.mValue;
      aOut.membership_epoch     = aIn.mMembershipEpoch;
      aOut.highest_sequence     = aIn.mHighestSequence;
      aOut.request_owner        = aIn.mRequestOwner;
      aOut.missing              = aIn.mMissing.data();
      aOut.missing_count        = aIn.mMissing.size();
      std::memcpy(aOut.calibration_delta, aIn.mCalibrationDelta, sizeof(aOut.calibration_delta));
      aOut.calibration_source    = aIn.mCalibrationSource;
      aOut.calibration_reference = aIn.mCalibrationReference;
   }

   CotMessage FromRef(const RefMessage& aIn)
   {
      CotMessage out;
      out.mKind               = aIn.kind;
      out.mTimeUs             = aIn.time_us;
      out.mStaleUs            = aIn.stale_us;
      out.mLat                = aIn.lat;
      out.mLon                = aIn.lon;
      out.mHae                = aIn.hae;
      out.mCe                 = aIn.ce;
      out.mLe                 = aIn.le;
      out.mOwner              = aIn.owner;
      out.mSender             = aIn.sender;
      out.mEntity             = aIn.entity;
      out.mSequence           = aIn.sequence;
      out.mAuthorityEpoch     = aIn.authority_epoch;
      out.mRevision           = aIn.revision;
      out.mSampleUs           = aIn.sample_us;
      out.mOnsetUs            = aIn.onset_us;
      out.mDeadlineUs         = aIn.deadline_us;
      out.mClockOffsetUs      = aIn.clock_offset_us;
      out.mClockEpoch         = aIn.clock_epoch;
      out.mClockUncertaintyUs = aIn.clock_uncertainty_us;
      out.mFrameEpoch         = aIn.frame_epoch;
      out.mCalibrationEpoch   = aIn.calibration_epoch;
      std::memcpy(out.mPose, aIn.pose, sizeof(out.mPose));
      std::memcpy(out.mVelocity, aIn.velocity, sizeof(out.mVelocity));
      out.mOperation          = aIn.operation;
      out.mAckSequence        = aIn.ack_sequence;
      out.mAckOwner           = aIn.ack_owner;
      out.mExpiryUs           = aIn.expiry_us;
      out.mValue              = aIn.value;
      out.mMembershipEpoch    = aIn.membership_epoch;
      out.mHighestSequence    = aIn.highest_sequence;
      out.mRequestOwner       = aIn.request_owner;
      out.mMissing.assign(aIn.missing, aIn.missing + aIn.missing_count);
      std::memcpy(out.mCalibrationDelta, aIn.calibration_delta, sizeof(out.mCalibrationDelta));
      out.mCalibrationSource    = aIn.calibration_source;
      out.mCalibrationReference = aIn.calibration_reference;
      return out;
   }
}

extern "C"
{
   void* ref_codec_new(int32_t aMode, int32_t aParticipants, uint64_t aSeed)
   {
      return new Handle{ CotCodec(aMode, aParticipants, aSeed), CotMessage(), std::string() };
   }

   void ref_codec_free(void* aHandle)
   {
      delete static_cast<Handle*>(aHandle);
   }

   // 1 and the message on success; 0 and the error text (ref_text) on failure; 2 and the
   // exception text when the reference throws. The message's
   // missing list stays valid until the next call on this handle.
   int32_t ref_decode(void* aHandle, const uint8_t* aWire, uint64_t aLength, RefMessage* aOut)
   {
      Handle& handle = *static_cast<Handle*>(aHandle);
      handle.mText.clear();
      const std::string wire(reinterpret_cast<const char*>(aWire), static_cast<size_t>(aLength));
      try
      {
         if (!handle.mCodec.Decode(wire, handle.mLast, handle.mText))
         {
            return 0;
         }
      }
      catch (const std::exception& aException)
      {
         handle.mText = std::string("C++ EXCEPTION: ") + aException.what();
         return 2;
      }
      ToRef(handle.mLast, *aOut);
      return 1;
   }

   // The wire form of a message; the bytes stay valid until the next call on this handle.
   const uint8_t* ref_encode(void* aHandle, const RefMessage* aMessage, uint64_t* aLength)
   {
      Handle& handle = *static_cast<Handle*>(aHandle);
      handle.mText   = handle.mCodec.Encode(FromRef(*aMessage));
      *aLength       = handle.mText.size();
      return reinterpret_cast<const uint8_t*>(handle.mText.data());
   }

   const uint8_t* ref_text(void* aHandle, uint64_t* aLength)
   {
      Handle& handle = *static_cast<Handle*>(aHandle);
      *aLength       = handle.mText.size();
      return reinterpret_cast<const uint8_t*>(handle.mText.data());
   }

   // The C library routines the codec calls, through the same C++ toolchain.
   int64_t ref_strtoll(const char* aText, int32_t aBase, uint64_t* aUsed)
   {
      char* end = nullptr;
      const long long value = std::strtoll(aText, &end, aBase);
      *aUsed = static_cast<uint64_t>(end - aText);
      return value;
   }

   uint64_t ref_strtoull(const char* aText, int32_t aBase, uint64_t* aUsed)
   {
      char* end = nullptr;
      const unsigned long long value = std::strtoull(aText, &end, aBase);
      *aUsed = static_cast<uint64_t>(end - aText);
      return value;
   }

   double ref_strtod(const char* aText, uint64_t* aUsed)
   {
      char* end = nullptr;
      const double value = std::strtod(aText, &end);
      *aUsed = static_cast<uint64_t>(end - aText);
      return value;
   }

   int32_t ref_scan_iso(const char* aText, int32_t* aFields, int32_t* aConsumed)
   {
      *aConsumed = 0;
      return std::sscanf(aText, "%4d-%2d-%2dT%2d:%2d:%2d%n", &aFields[0], &aFields[1], &aFields[2], &aFields[3], &aFields[4],
                         &aFields[5], aConsumed);
   }

   int32_t ref_fixed(double aValue, int32_t aDigits, char* aText)
   {
      return std::snprintf(aText, 64, "%.*f", aDigits, aValue);
   }
}
