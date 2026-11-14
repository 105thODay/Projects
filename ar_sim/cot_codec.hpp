#ifndef COT_CODEC_HPP
#define COT_CODEC_HPP

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Encoded-wire CoT (NET-003). Builds and parses every message the participant
// network carries, in four wire forms:
//   XML 2.0, field names as the profile gives them ("specified") or a compact schema ("lean");
//   TAK Protocol version 1 mesh messages of either schema: a protobuf envelope with the AR
//   detail carried as the xmlDetail string, because version 1 lets a client add only fields a
//   receiver may ignore.
// The AR detail elements and CoT types are a DRAFT schema (ruling A18): the profile names the
// fields but not the schema or type catalog.

struct WireMode
{
   static constexpr int cABSTRACT      = 0;          // configured frame costs; no message is built
   static constexpr int cXML_SPECIFIED = 1;
   static constexpr int cXML_LEAN      = 2;
   static constexpr int cTAK_SPECIFIED = 3;
   static constexpr int cTAK_LEAN      = 4;
   static constexpr int cCOUNT         = 5;
};

struct MessageKind
{
   static constexpr int cEVENT          = 0;         // a discrete event (marker create, delete or takeover claim)
   static constexpr int cSTATE          = 1;         // replaceable participant pose state
   static constexpr int cCONTROL        = 2;         // clock, membership, receipt and event summary; current calibration
   static constexpr int cREPAIR_REQUEST = 3;         // a recipient lists the events it missed
   static constexpr int cREPAIR_STATUS  = 4;         // background repair fixture
   static constexpr int cCALIBRATION    = 5;         // a new calibration epoch and the change from the last one
   static constexpr int cCOUNT          = 6;
};

struct Operation
{
   static constexpr int cCREATE   = 0;
   static constexpr int cDELETE   = 1;
   static constexpr int cTAKEOVER = 2;               // R1 stimulus: a non-owner claims an entity
};

// Every field a message can carry, in semantic form. The codec formats them per
// schema and recovers them exactly at the formatted precision.
struct CotMessage
{
   int                  mKind = MessageKind::cEVENT;
   // envelope
   int64_t              mTimeUs = 0;                 // UTC microseconds since 1970; CoT time and start
   int64_t              mStaleUs = 0;
   double               mLat = 0.0, mLon = 0.0, mHae = 0.0, mCe = 0.0, mLe = 0.0;
   // identity
   int                  mOwner = 0;                  // origin owner (participant index)
   int                  mSender = 0;
   int64_t              mEntity = -1;                // owner * stride + local number
   int64_t              mSequence = 0;               // owner's event or state sequence
   int                  mAuthorityEpoch = 0;
   int                  mRevision = 0;
   // timing
   int64_t              mSampleUs = 0;
   int64_t              mOnsetUs = 0;
   int64_t              mDeadlineUs = 0;
   int64_t              mClockOffsetUs = 0;
   int                  mClockEpoch = 0;
   int                  mClockUncertaintyUs = 0;
   // spatial (mission frame, metres, quaternion w x y z)
   int                  mFrameEpoch = 0;
   int                  mCalibrationEpoch = 0;
   double               mPose[7] = { 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0 };
   double               mVelocity[3] = { 0.0, 0.0, 0.0 };
   // transaction
   int                  mOperation = Operation::cCREATE;
   int64_t              mAckSequence = -1;           // receipt: newest event applied from mAckOwner; -1 none
   int                  mAckOwner = -1;
   int64_t              mExpiryUs = 0;
   uint64_t             mValue = 0;
   // control and repair
   int                  mMembershipEpoch = 0;
   int64_t              mHighestSequence = 0;
   int                  mRequestOwner = -1;          // REPAIR_REQUEST: whose events are missing
   std::vector<int64_t> mMissing;
   // calibration (mission-frame change since the previous epoch)
   double               mCalibrationDelta[4] = { 0.0, 0.0, 0.0, 0.0 };   // dx dy dz (m), dyaw (rad)
   int                  mCalibrationSource = 0;      // 0 own far solution, 1 near peer
   int                  mCalibrationReference = -1;  // near peer used, -1 none
};

struct XmlElement
{
   std::string                                      mName;
   std::vector<std::pair<std::string, std::string>> mAttributes;
   std::vector<XmlElement>                          mChildren;

   const std::string* Attribute(const std::string& aName) const;
};

// Strict parser for the XML subset CoT uses: one declaration, elements, attributes,
// the five predefined entities, comments; character data is ignored.
bool ParseXml(const std::string& aText, XmlElement& aRoot, std::string& aError);

class CotCodec
{
public:
   // The session's roster maps participant index to its on-wire identifier; both ends
   // learn it at session join (membership), so identifiers decode to indices.
   CotCodec(int aWireMode, int aParticipants, uint64_t aSessionSeed);

   int         Mode() const;
   bool        Lean() const;
   std::string Encode(const CotMessage& aMessage) const;
   bool        Decode(const std::string& aWire, CotMessage& aMessage, std::string& aError) const;

   static int64_t     cEntityStride;                 // entity and event numbering: owner * stride + local number
   static std::string IsoTime(int64_t aUtcUs, int aDigits);
   static bool        ParseIsoTime(const std::string& aText, int64_t& aUtcUs);

private:
   std::string Detail(const CotMessage& aMessage) const;
   std::string EnvelopeXml(const CotMessage& aMessage, const std::string& aDetail) const;
   std::string TakMessage(const CotMessage& aMessage, const std::string& aDetail) const;
   bool        DecodeDetail(const XmlElement& aDetail, CotMessage& aMessage, std::string& aError) const;
   std::string Participant(int aIndex) const;
   bool        ParticipantIndex(const std::string& aText, int& aIndex) const;
   std::string Entity(int64_t aEntity) const;
   bool        EntityNumber(const std::string& aText, int64_t& aEntity) const;
   std::string EventId(int aOwner, int64_t aSequence) const;
   bool        EventNumber(const std::string& aText, int& aOwner, int64_t& aSequence) const;
   std::string Time(int64_t aUtcUs) const;
   bool        TimeValue(const std::string& aText, int64_t& aUtcUs) const;
   const char* Name(int aField) const;

   int                      mMode;
   std::string              mSession;
   std::vector<std::string> mRoster;
   uint64_t                 mUuidPrefix;
};

#endif
