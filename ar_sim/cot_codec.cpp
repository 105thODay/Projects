#include "cot_codec.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
   // Field identifiers, in the order a detail element lists them.
   struct Field
   {
      static constexpr int cSESSION            = 0;
      static constexpr int cOWNER              = 1;
      static constexpr int cSENDER             = 2;
      static constexpr int cENTITY             = 3;
      static constexpr int cEVENT              = 4;
      static constexpr int cAUTHORITY          = 5;
      static constexpr int cREVISION           = 6;
      static constexpr int cSEQUENCE           = 7;
      static constexpr int cSAMPLE             = 8;
      static constexpr int cONSET              = 9;
      static constexpr int cCLOCK              = 10;
      static constexpr int cCLOCK_EPOCH        = 11;
      static constexpr int cCLOCK_UNCERTAINTY  = 12;
      static constexpr int cDEADLINE           = 13;
      static constexpr int cFRAME              = 14;
      static constexpr int cFRAME_EPOCH        = 15;
      static constexpr int cCALIBRATION_EPOCH  = 16;
      static constexpr int cPOSE_DIRECTION     = 17;
      static constexpr int cPOSE               = 18;
      static constexpr int cVELOCITY           = 19;
      static constexpr int cUNITS              = 20;
      static constexpr int cQUATERNION_ORDER   = 21;
      static constexpr int cOPERATION          = 22;
      static constexpr int cTARGET             = 23;
      static constexpr int cACK                = 24;
      static constexpr int cEXPIRY             = 25;
      static constexpr int cPROVENANCE         = 26;
      static constexpr int cVALUE              = 27;
      static constexpr int cCLOCK_OFFSET       = 28;
      static constexpr int cMEMBERSHIP         = 29;
      static constexpr int cHIGHEST            = 30;
      static constexpr int cREQUEST_OWNER      = 31;
      static constexpr int cMISSING            = 32;
      static constexpr int cCAL_DELTA          = 33;
      static constexpr int cCAL_SOURCE         = 34;
      static constexpr int cCAL_REFERENCE      = 35;
      static constexpr int cCOUNT              = 36;
   };

   const char* const cSPECIFIED_NAME[Field::cCOUNT] =
   {
      "session_id", "origin_owner_id", "sender_id", "entity_uid", "event_uid", "authority_epoch", "revision", "sequence",
      "source_sample_time", "input_onset_time", "clock_id", "clock_mapping_epoch", "clock_uncertainty", "response_deadline",
      "mission_frame_id", "frame_epoch", "calibration_epoch", "pose_direction", "pose", "velocity", "units", "quaternion_order",
      "operation", "target_entity_uid", "ack_event_uid", "expiry", "observation_provenance", "value", "clock_offset",
      "membership_epoch", "highest_sequence", "owner_id", "missing", "calibration_delta", "calibration_source",
      "calibration_reference"
   };
   const char* const cLEAN_NAME[Field::cCOUNT] =
   {
      "s", "o", "sd", "e", "ev", "ae", "r", "q", "st", "it", "ck", "cm", "cu", "dl", "mf", "fe", "ce", "pd", "p", "v", "u", "qo",
      "op", "te", "ak", "x", "pv", "val", "co", "me", "hs", "ow", "ms", "cd", "cs", "cr"
   };

   // The fields each message kind carries, in order.
   const std::vector<int> cKIND_FIELDS[MessageKind::cCOUNT] =
   {
      { Field::cSESSION, Field::cOWNER, Field::cSENDER, Field::cENTITY, Field::cEVENT, Field::cAUTHORITY, Field::cREVISION,
        Field::cSEQUENCE, Field::cSAMPLE, Field::cONSET, Field::cCLOCK, Field::cCLOCK_EPOCH, Field::cCLOCK_UNCERTAINTY,
        Field::cDEADLINE, Field::cFRAME, Field::cFRAME_EPOCH, Field::cCALIBRATION_EPOCH, Field::cPOSE_DIRECTION, Field::cUNITS,
        Field::cQUATERNION_ORDER, Field::cOPERATION, Field::cTARGET, Field::cACK, Field::cEXPIRY, Field::cPROVENANCE,
        Field::cVALUE },
      { Field::cSESSION, Field::cOWNER, Field::cSENDER, Field::cENTITY, Field::cAUTHORITY, Field::cREVISION, Field::cSEQUENCE,
        Field::cSAMPLE, Field::cCLOCK, Field::cCLOCK_EPOCH, Field::cCLOCK_UNCERTAINTY, Field::cFRAME, Field::cFRAME_EPOCH,
        Field::cCALIBRATION_EPOCH, Field::cPOSE, Field::cVELOCITY, Field::cUNITS, Field::cQUATERNION_ORDER },
      { Field::cSESSION, Field::cOWNER, Field::cSENDER, Field::cCLOCK, Field::cCLOCK_EPOCH, Field::cCLOCK_OFFSET,
        Field::cCLOCK_UNCERTAINTY, Field::cMEMBERSHIP, Field::cHIGHEST, Field::cACK, Field::cFRAME, Field::cFRAME_EPOCH,
        Field::cCALIBRATION_EPOCH, Field::cCAL_DELTA },
      { Field::cSESSION, Field::cOWNER, Field::cSENDER, Field::cREQUEST_OWNER, Field::cMISSING },
      { Field::cSESSION, Field::cOWNER, Field::cSENDER, Field::cHIGHEST },
      { Field::cSESSION, Field::cOWNER, Field::cSENDER, Field::cSAMPLE, Field::cFRAME, Field::cFRAME_EPOCH,
        Field::cCALIBRATION_EPOCH, Field::cCAL_DELTA, Field::cCAL_SOURCE, Field::cCAL_REFERENCE }
   };

   // Draft CoT type catalog (ruling A18), detail element names and envelope uid suffixes.
   const char* const cTYPE[MessageKind::cCOUNT]            = { "b-m-p-s-m", "a-f-G-U-C", "t-x-ar-c", "t-x-ar-r", "t-x-ar-s", "t-x-ar-k" };
   const char* const cHOW[MessageKind::cCOUNT]             = { "h-e", "m-g", "m-g", "m-g", "m-g", "m-g" };
   const char* const cSPECIFIED_ELEMENT[MessageKind::cCOUNT] = { "ar_event", "ar_state", "ar_control", "ar_repair", "ar_repair_status", "ar_calibration" };
   const char* const cLEAN_ELEMENT[MessageKind::cCOUNT]      = { "are", "ars", "arc", "arr", "arq", "ark" };
   const char* const cSPECIFIED_SUFFIX[MessageKind::cCOUNT]  = { "", "", "-control", "-repair", "-repair-status", "-calibration" };
   const char* const cLEAN_SUFFIX[MessageKind::cCOUNT]       = { "", "", ".c", ".r", ".s", ".k" };
   const char* const cSPECIFIED_OPERATION[3]                 = { "create", "delete", "takeover" };
   const char* const cLEAN_OPERATION[3]                      = { "c", "d", "t" };

   // Fixed session constants the profile requires on every spatial message.
   struct Constant
   {
      static constexpr const char* cSPECIFIED_FRAME = "synthetic-mission-frame-1";   // explicitly synthetic surveyed origin
      static constexpr const char* cLEAN_FRAME      = "1";
      static constexpr const char* cUNITS           = "m";
      static constexpr const char* cQUATERNION      = "wxyz";
      static constexpr const char* cSPECIFIED_PROV  = "local_input";
      static constexpr const char* cLEAN_PROV       = "l";
      static constexpr const char* cXML_DECLARATION = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
   };

   struct Digits
   {
      // Decimal places per quantity: specified schema, then lean schema.
      static constexpr int cPOSITION[2]   = { 4, 3 };   // mission frame metres
      static constexpr int cQUATERNION[2] = { 7, 5 };
      static constexpr int cVELOCITY[2]   = { 3, 2 };
      static constexpr int cDELTA_M[2]    = { 4, 3 };
      static constexpr int cDELTA_RAD[2]  = { 6, 5 };
      static constexpr int cDEGREES       = 7;          // latitude and longitude
      static constexpr int cMETRES        = 1;          // hae, ce, le
   };

   struct Tak
   {
      static constexpr unsigned char cMAGIC   = 0xBF;
      static constexpr unsigned char cVERSION = 0x01;
      static constexpr int cWIRE_VARINT  = 0;
      static constexpr int cWIRE_FIXED64 = 1;
      static constexpr int cWIRE_BYTES   = 2;
      static constexpr int cWIRE_FIXED32 = 5;
   };

   uint64_t Mix(uint64_t aValue)
   {
      // splitmix64 finalizer: stable identifiers from the session seed
      aValue += 0x9E3779B97F4A7C15ULL;
      aValue = (aValue ^ (aValue >> 30)) * 0xBF58476D1CE4E5B9ULL;
      aValue = (aValue ^ (aValue >> 27)) * 0x94D049BB133111EBULL;
      return aValue ^ (aValue >> 31);
   }

   std::string Hex(uint64_t aValue, int aDigits)
   {
      char text[32];
      std::snprintf(text, sizeof(text), "%0*llx", aDigits, static_cast<unsigned long long>(aValue));
      return text;
   }

   std::string Fixed(double aValue, int aDigits)
   {
      char text[64];
      std::snprintf(text, sizeof(text), "%.*f", aDigits, aValue);
      if (std::strcmp(text, "-0") == 0 || (text[0] == '-' && std::strspn(text + 1, "0.") == std::strlen(text + 1)))
      {
         return std::string(text + 1);                   // no negative zero on the wire
      }
      return text;
   }

   std::string List(const double* aValues, int aCount, int aDigits)
   {
      std::string text;
      for (int index = 0; index < aCount; ++index)
      {
         if (index > 0)
         {
            text += ",";
         }
         text += Fixed(aValues[index], aDigits);
      }
      return text;
   }

   bool ParseInteger(const std::string& aText, int64_t& aValue, int aBase = 10)
   {
      if (aText.empty())
      {
         return false;
      }
      char* end = nullptr;
      aValue = static_cast<int64_t>(std::strtoll(aText.c_str(), &end, aBase));
      return end != nullptr && *end == '\0';
   }

   bool ParseUnsigned(const std::string& aText, uint64_t& aValue, int aBase)
   {
      if (aText.empty())
      {
         return false;
      }
      char* end = nullptr;
      aValue = static_cast<uint64_t>(std::strtoull(aText.c_str(), &end, aBase));
      return end != nullptr && *end == '\0';
   }

   bool ParseList(const std::string& aText, double* aValues, int aCount)
   {
      const char* cursor = aText.c_str();
      for (int index = 0; index < aCount; ++index)
      {
         char* end = nullptr;
         aValues[index] = std::strtod(cursor, &end);
         if (end == cursor)
         {
            return false;
         }
         cursor = end;
         if (index + 1 < aCount)
         {
            if (*cursor != ',')
            {
               return false;
            }
            ++cursor;
         }
      }
      return *cursor == '\0';
   }

   std::string Escape(const std::string& aText)
   {
      std::string out;
      for (char c : aText)
      {
         switch (c)
         {
         case '&':  out += "&amp;";  break;
         case '<':  out += "&lt;";   break;
         case '>':  out += "&gt;";   break;
         case '"':  out += "&quot;"; break;
         case '\'': out += "&apos;"; break;
         default:   out += c;        break;
         }
      }
      return out;
   }

   // ---------------------------------------------------------------- protobuf wire helpers

   void PutVarint(std::string& aOut, uint64_t aValue)
   {
      while (aValue >= 0x80)
      {
         aOut += static_cast<char>((aValue & 0x7F) | 0x80);
         aValue >>= 7;
      }
      aOut += static_cast<char>(aValue);
   }

   void PutKey(std::string& aOut, int aField, int aWire)
   {
      PutVarint(aOut, (static_cast<uint64_t>(aField) << 3) | static_cast<uint64_t>(aWire));
   }

   void PutBytes(std::string& aOut, int aField, const std::string& aBytes)
   {
      if (aBytes.empty())
      {
         return;                                       // proto3: default values are not sent
      }
      PutKey(aOut, aField, Tak::cWIRE_BYTES);
      PutVarint(aOut, aBytes.size());
      aOut += aBytes;
   }

   void PutUnsigned(std::string& aOut, int aField, uint64_t aValue)
   {
      if (aValue == 0)
      {
         return;
      }
      PutKey(aOut, aField, Tak::cWIRE_VARINT);
      PutVarint(aOut, aValue);
   }

   void PutDouble(std::string& aOut, int aField, double aValue)
   {
      if (aValue == 0.0 && !std::signbit(aValue))
      {
         return;
      }
      uint64_t bits = 0;
      std::memcpy(&bits, &aValue, sizeof(bits));
      PutKey(aOut, aField, Tak::cWIRE_FIXED64);
      for (int shift = 0; shift < 64; shift += 8)
      {
         aOut += static_cast<char>((bits >> shift) & 0xFF);   // little-endian whatever the host
      }
   }

   struct ProtoReader
   {
      const unsigned char* mCursor;
      const unsigned char* mEnd;

      bool Varint(uint64_t& aValue)
      {
         aValue = 0;
         for (int shift = 0; shift < 64; shift += 7)
         {
            if (mCursor >= mEnd)
            {
               return false;
            }
            const unsigned char byte = *mCursor++;
            aValue |= static_cast<uint64_t>(byte & 0x7F) << shift;
            if ((byte & 0x80) == 0)
            {
               return true;
            }
         }
         return false;
      }

      bool Field(int& aField, int& aWire, uint64_t& aNumber, std::string& aBytes)
      {
         uint64_t key = 0;
         if (!Varint(key))
         {
            return false;
         }
         aField = static_cast<int>(key >> 3);
         aWire  = static_cast<int>(key & 7);
         if (aWire == Tak::cWIRE_VARINT)
         {
            return Varint(aNumber);
         }
         if (aWire == Tak::cWIRE_FIXED64 || aWire == Tak::cWIRE_FIXED32)
         {
            const int size = (aWire == Tak::cWIRE_FIXED64) ? 8 : 4;
            if (mEnd - mCursor < size)
            {
               return false;
            }
            aNumber = 0;
            for (int index = 0; index < size; ++index)
            {
               aNumber |= static_cast<uint64_t>(mCursor[index]) << (8 * index);
            }
            mCursor += size;
            return true;
         }
         if (aWire == Tak::cWIRE_BYTES)
         {
            uint64_t length = 0;
            if (!Varint(length) || static_cast<uint64_t>(mEnd - mCursor) < length)
            {
               return false;
            }
            aBytes.assign(reinterpret_cast<const char*>(mCursor), static_cast<size_t>(length));
            mCursor += length;
            return true;
         }
         return false;                                 // groups are not used by TAK
      }
   };

   double AsDouble(uint64_t aBits)
   {
      double value = 0.0;
      std::memcpy(&value, &aBits, sizeof(value));
      return value;
   }

   // ---------------------------------------------------------------- calendar (UTC, proleptic Gregorian)

   int64_t DaysFromCivil(int64_t aYear, int64_t aMonth, int64_t aDay)
   {
      aYear -= (aMonth <= 2) ? 1 : 0;
      const int64_t era = (aYear >= 0 ? aYear : aYear - 399) / 400;
      const int64_t yoe = aYear - era * 400;
      const int64_t doy = (153 * (aMonth + (aMonth > 2 ? -3 : 9)) + 2) / 5 + aDay - 1;
      const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
      return era * 146097 + doe - 719468;
   }

   void CivilFromDays(int64_t aDays, int64_t& aYear, int64_t& aMonth, int64_t& aDay)
   {
      aDays += 719468;
      const int64_t era = (aDays >= 0 ? aDays : aDays - 146096) / 146097;
      const int64_t doe = aDays - era * 146097;
      const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
      const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
      const int64_t mp  = (5 * doy + 2) / 153;
      aDay   = doy - (153 * mp + 2) / 5 + 1;
      aMonth = mp + (mp < 10 ? 3 : -9);
      aYear  = yoe + era * 400 + (aMonth <= 2 ? 1 : 0);
   }

   const int64_t cUS_PER_S   = 1000000;
   const int64_t cUS_PER_DAY = 86400 * cUS_PER_S;
}

int64_t CotCodec::cEntityStride = 1000000000LL;

const std::string* XmlElement::Attribute(const std::string& aName) const
{
   for (const auto& attribute : mAttributes)
   {
      if (attribute.first == aName)
      {
         return &attribute.second;
      }
   }
   return nullptr;
}

// ---------------------------------------------------------------- XML parser

namespace
{
   struct XmlCursor
   {
      const std::string& mText;
      size_t             mPos;
      std::string&       mError;

      bool Fail(const char* aWhat)
      {
         mError = std::string(aWhat) + " at byte " + std::to_string(mPos);
         return false;
      }
      void SkipSpace()
      {
         while (mPos < mText.size() && (mText[mPos] == ' ' || mText[mPos] == '\t' || mText[mPos] == '\r' || mText[mPos] == '\n'))
         {
            ++mPos;
         }
      }
      bool StartsWith(const char* aPrefix) const
      {
         return mText.compare(mPos, std::strlen(aPrefix), aPrefix) == 0;
      }
      bool SkipMisc()
      {
         for (;;)
         {
            SkipSpace();
            if (StartsWith("<!--"))
            {
               const size_t end = mText.find("-->", mPos + 4);
               if (end == std::string::npos)
               {
                  return Fail("unterminated comment");
               }
               mPos = end + 3;
            }
            else
            {
               return true;
            }
         }
      }
      bool Name(std::string& aName)
      {
         const size_t start = mPos;
         while (mPos < mText.size())
         {
            const char c = mText[mPos];
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == ':' || c == '-' || c == '.')
            {
               ++mPos;
            }
            else
            {
               break;
            }
         }
         if (mPos == start)
         {
            return Fail("expected a name");
         }
         aName = mText.substr(start, mPos - start);
         return true;
      }
      bool Unescape(const std::string& aRaw, std::string& aOut)
      {
         aOut.clear();
         for (size_t index = 0; index < aRaw.size(); ++index)
         {
            if (aRaw[index] != '&')
            {
               aOut += aRaw[index];
               continue;
            }
            const size_t semi = aRaw.find(';', index);
            if (semi == std::string::npos)
            {
               return Fail("unterminated entity");
            }
            const std::string entity = aRaw.substr(index + 1, semi - index - 1);
            if (entity == "amp")       aOut += '&';
            else if (entity == "lt")   aOut += '<';
            else if (entity == "gt")   aOut += '>';
            else if (entity == "quot") aOut += '"';
            else if (entity == "apos") aOut += '\'';
            else if (!entity.empty() && entity[0] == '#')
            {
               int64_t code = 0;
               const bool hex = entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X');
               if (!ParseInteger(entity.substr(hex ? 2 : 1), code, hex ? 16 : 10) || code <= 0 || code > 0x7F)
               {
                  return Fail("unsupported character reference");
               }
               aOut += static_cast<char>(code);
            }
            else
            {
               return Fail("unknown entity");
            }
            index = semi;
         }
         return true;
      }
      bool Element(XmlElement& aElement)
      {
         if (mPos >= mText.size() || mText[mPos] != '<')
         {
            return Fail("expected '<'");
         }
         ++mPos;
         if (!Name(aElement.mName))
         {
            return false;
         }
         for (;;)
         {
            SkipSpace();
            if (StartsWith("/>"))
            {
               mPos += 2;
               return true;
            }
            if (StartsWith(">"))
            {
               ++mPos;
               break;
            }
            std::string name;
            if (!Name(name))
            {
               return false;
            }
            SkipSpace();
            if (!StartsWith("="))
            {
               return Fail("expected '='");
            }
            ++mPos;
            SkipSpace();
            if (mPos >= mText.size() || (mText[mPos] != '"' && mText[mPos] != '\''))
            {
               return Fail("expected a quoted value");
            }
            const char quote = mText[mPos++];
            const size_t end = mText.find(quote, mPos);
            if (end == std::string::npos)
            {
               return Fail("unterminated value");
            }
            std::string value;
            if (!Unescape(mText.substr(mPos, end - mPos), value))
            {
               return false;
            }
            for (const auto& existing : aElement.mAttributes)
            {
               if (existing.first == name)
               {
                  return Fail("duplicate attribute");
               }
            }
            aElement.mAttributes.emplace_back(name, value);
            mPos = end + 1;
         }
         for (;;)
         {
            const size_t next = mText.find('<', mPos);
            if (next == std::string::npos)
            {
               return Fail("unterminated element");
            }
            mPos = next;                               // character data is ignored
            if (StartsWith("<!--"))
            {
               if (!SkipMisc())
               {
                  return false;
               }
               continue;
            }
            if (StartsWith("</"))
            {
               mPos += 2;
               std::string name;
               if (!Name(name))
               {
                  return false;
               }
               if (name != aElement.mName)
               {
                  return Fail("mismatched end tag");
               }
               SkipSpace();
               if (!StartsWith(">"))
               {
                  return Fail("expected '>'");
               }
               ++mPos;
               return true;
            }
            aElement.mChildren.emplace_back();
            if (!Element(aElement.mChildren.back()))
            {
               return false;
            }
         }
      }
   };
}

bool ParseXml(const std::string& aText, XmlElement& aRoot, std::string& aError)
{
   XmlCursor cursor{ aText, 0, aError };
   aRoot = XmlElement();
   if (cursor.StartsWith("<?xml"))
   {
      const size_t end = aText.find("?>");
      if (end == std::string::npos)
      {
         return cursor.Fail("unterminated declaration");
      }
      cursor.mPos = end + 2;
   }
   if (!cursor.SkipMisc() || !cursor.Element(aRoot) || !cursor.SkipMisc())
   {
      return false;
   }
   if (cursor.mPos != aText.size())
   {
      return cursor.Fail("content after the root element");
   }
   return true;
}

// ---------------------------------------------------------------- codec

CotCodec::CotCodec(int aWireMode, int aParticipants, uint64_t aSessionSeed)
   : mMode(aWireMode),
     mUuidPrefix(Mix(aSessionSeed ^ 0xA5A5A5A5ULL))
{
   const uint64_t session = Mix(aSessionSeed);
   if (Lean())
   {
      mSession = Hex(session & 0xFFFFFFFFULL, 8);
   }
   else
   {
      const uint64_t low = Mix(session);
      mSession = Hex(session >> 32, 8) + "-" + Hex((session >> 16) & 0xFFFF, 4) + "-4" + Hex(session & 0xFFF, 3) + "-" +
                 Hex(0x8000 | (low >> 48 & 0x3FFF), 4) + "-" + Hex(low & 0xFFFFFFFFFFFFULL, 12);
   }
   for (int index = 0; index < aParticipants; ++index)
   {
      const uint64_t id = Mix(session + static_cast<uint64_t>(index) + 1);
      mRoster.push_back(Lean() ? Hex(id & 0xFFFFFFFFULL, 8) : "ANDROID-" + Hex(id, 16));
   }
}

int CotCodec::Mode() const
{
   return mMode;
}

bool CotCodec::Lean() const
{
   return mMode == WireMode::cXML_LEAN || mMode == WireMode::cTAK_LEAN;
}

const char* CotCodec::Name(int aField) const
{
   return Lean() ? cLEAN_NAME[aField] : cSPECIFIED_NAME[aField];
}

// An index outside the roster yields empty text, which matches no identifier. A decoded
// entity can name one: a lean local number at or above the stride carries into the owner.
std::string CotCodec::Participant(int aIndex) const
{
   if (aIndex < 0 || static_cast<size_t>(aIndex) >= mRoster.size())
   {
      return std::string();
   }
   return mRoster[static_cast<size_t>(aIndex)];
}

bool CotCodec::ParticipantIndex(const std::string& aText, int& aIndex) const
{
   for (size_t index = 0; index < mRoster.size(); ++index)
   {
      if (mRoster[index] == aText)
      {
         aIndex = static_cast<int>(index);
         return true;
      }
   }
   return false;
}

// Specified schema: UUIDs whose last 48 bits carry the number and whose prefix is the
// session's, so a receiver recovers the number. Lean: owner identifier and a hex count.
std::string CotCodec::Entity(int64_t aEntity) const
{
   const int     owner = static_cast<int>(aEntity / cEntityStride);
   const int64_t local = aEntity % cEntityStride;
   if (Lean())
   {
      return Participant(owner) + "." + Hex(static_cast<uint64_t>(local), 1);
   }
   const uint64_t prefix = mUuidPrefix;
   return Hex(prefix >> 32, 8) + "-" + Hex((prefix >> 16) & 0xFFFF, 4) + "-4" + Hex(prefix & 0xFFF, 3) + "-a" +
          Hex(static_cast<uint64_t>(owner) & 0xFFF, 3) + "-" + Hex(static_cast<uint64_t>(local), 12);
}

bool CotCodec::EntityNumber(const std::string& aText, int64_t& aEntity) const
{
   if (Lean())
   {
      const size_t dot = aText.find('.');
      int          owner = 0;
      uint64_t     local = 0;
      if (dot == std::string::npos || !ParticipantIndex(aText.substr(0, dot), owner) || !ParseUnsigned(aText.substr(dot + 1), local, 16))
      {
         return false;
      }
      aEntity = owner * cEntityStride + static_cast<int64_t>(local);
      return true;
   }
   if (aText.size() != 36 || aText.compare(0, 19, Entity(0).substr(0, 19)) != 0 || aText[19] != 'a')
   {
      return false;
   }
   uint64_t owner = 0;
   uint64_t local = 0;
   if (!ParseUnsigned(aText.substr(20, 3), owner, 16) || !ParseUnsigned(aText.substr(24, 12), local, 16))
   {
      return false;
   }
   aEntity = static_cast<int64_t>(owner) * cEntityStride + static_cast<int64_t>(local);
   return true;
}

// Event identifiers derive from owner and sequence, so they cannot collide (ruling A18).
std::string CotCodec::EventId(int aOwner, int64_t aSequence) const
{
   if (Lean())
   {
      return Participant(aOwner) + ":" + Hex(static_cast<uint64_t>(aSequence), 1);
   }
   const uint64_t prefix = mUuidPrefix;
   return Hex(prefix >> 32, 8) + "-" + Hex((prefix >> 16) & 0xFFFF, 4) + "-4" + Hex(prefix & 0xFFF, 3) + "-b" +
          Hex(static_cast<uint64_t>(aOwner) & 0xFFF, 3) + "-" + Hex(static_cast<uint64_t>(aSequence), 12);
}

bool CotCodec::EventNumber(const std::string& aText, int& aOwner, int64_t& aSequence) const
{
   if (Lean())
   {
      const size_t colon = aText.find(':');
      uint64_t     sequence = 0;
      if (colon == std::string::npos || !ParticipantIndex(aText.substr(0, colon), aOwner) || !ParseUnsigned(aText.substr(colon + 1), sequence, 16))
      {
         return false;
      }
      aSequence = static_cast<int64_t>(sequence);
      return true;
   }
   if (aText.size() != 36 || aText.compare(0, 19, EventId(0, 0).substr(0, 19)) != 0 || aText[19] != 'b')
   {
      return false;
   }
   uint64_t owner = 0;
   uint64_t sequence = 0;
   if (!ParseUnsigned(aText.substr(20, 3), owner, 16) || !ParseUnsigned(aText.substr(24, 12), sequence, 16))
   {
      return false;
   }
   aOwner    = static_cast<int>(owner);
   aSequence = static_cast<int64_t>(sequence);
   return aOwner < static_cast<int>(mRoster.size());
}

std::string CotCodec::IsoTime(int64_t aUtcUs, int aDigits)
{
   const int64_t days = (aUtcUs >= 0 ? aUtcUs : aUtcUs - cUS_PER_DAY + 1) / cUS_PER_DAY;
   const int64_t rest = aUtcUs - days * cUS_PER_DAY;
   int64_t year = 0, month = 0, day = 0;
   CivilFromDays(days, year, month, day);
   const int64_t seconds = rest / cUS_PER_S;
   const int64_t micros  = rest % cUS_PER_S;
   char text[160];   // room for any int64 field, so the compiler can prove no truncation
   if (aDigits == 6)
   {
      std::snprintf(text, sizeof(text), "%04lld-%02lld-%02lldT%02lld:%02lld:%02lld.%06lldZ", static_cast<long long>(year),
                    static_cast<long long>(month), static_cast<long long>(day), static_cast<long long>(seconds / 3600),
                    static_cast<long long>((seconds / 60) % 60), static_cast<long long>(seconds % 60), static_cast<long long>(micros));
   }
   else
   {
      std::snprintf(text, sizeof(text), "%04lld-%02lld-%02lldT%02lld:%02lld:%02lld.%03lldZ", static_cast<long long>(year),
                    static_cast<long long>(month), static_cast<long long>(day), static_cast<long long>(seconds / 3600),
                    static_cast<long long>((seconds / 60) % 60), static_cast<long long>(seconds % 60), static_cast<long long>(micros / 1000));
   }
   return text;
}

bool CotCodec::ParseIsoTime(const std::string& aText, int64_t& aUtcUs)
{
   int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0, consumed = 0;
   if (std::sscanf(aText.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d%n", &year, &month, &day, &hour, &minute, &second, &consumed) != 6)
   {
      return false;
   }
   int64_t micros = 0;
   size_t  pos = static_cast<size_t>(consumed);
   if (pos < aText.size() && aText[pos] == '.')
   {
      ++pos;
      int digits = 0;
      while (pos < aText.size() && aText[pos] >= '0' && aText[pos] <= '9')
      {
         if (digits < 6)
         {
            micros = micros * 10 + (aText[pos] - '0');
         }
         ++digits;
         ++pos;
      }
      for (; digits < 6; ++digits)
      {
         micros *= 10;
      }
   }
   if (pos + 1 != aText.size() || aText[pos] != 'Z')
   {
      return false;
   }
   aUtcUs = DaysFromCivil(year, month, day) * cUS_PER_DAY + ((hour * 60 + minute) * 60 + second) * cUS_PER_S + micros;
   return true;
}

std::string CotCodec::Time(int64_t aUtcUs) const
{
   return Lean() ? std::to_string(aUtcUs) : IsoTime(aUtcUs, 6);
}

bool CotCodec::TimeValue(const std::string& aText, int64_t& aUtcUs) const
{
   return Lean() ? ParseInteger(aText, aUtcUs) : ParseIsoTime(aText, aUtcUs);
}

std::string CotCodec::Detail(const CotMessage& aMessage) const
{
   const int s = Lean() ? 1 : 0;
   std::string out = std::string("<") + (Lean() ? cLEAN_ELEMENT[aMessage.mKind] : cSPECIFIED_ELEMENT[aMessage.mKind]);
   for (int field : cKIND_FIELDS[aMessage.mKind])
   {
      std::string value;
      switch (field)
      {
      case Field::cSESSION:           value = mSession; break;
      case Field::cOWNER:             value = Participant(aMessage.mOwner); break;
      case Field::cSENDER:            value = Participant(aMessage.mSender); break;
      case Field::cENTITY:            value = Entity(aMessage.mEntity); break;
      case Field::cEVENT:             value = EventId(aMessage.mOwner, aMessage.mSequence); break;
      case Field::cAUTHORITY:         value = std::to_string(aMessage.mAuthorityEpoch); break;
      case Field::cREVISION:          value = std::to_string(aMessage.mRevision); break;
      case Field::cSEQUENCE:          value = std::to_string(aMessage.mSequence); break;
      case Field::cSAMPLE:            value = Time(aMessage.mSampleUs); break;
      case Field::cONSET:             value = Time(aMessage.mOnsetUs); break;
      case Field::cCLOCK:             value = Lean() ? "1" : "clk-" + mSession.substr(0, 8); break;
      case Field::cCLOCK_EPOCH:       value = std::to_string(aMessage.mClockEpoch); break;
      case Field::cCLOCK_UNCERTAINTY: value = std::to_string(aMessage.mClockUncertaintyUs); break;
      case Field::cDEADLINE:          value = Time(aMessage.mDeadlineUs); break;
      case Field::cFRAME:             value = Lean() ? Constant::cLEAN_FRAME : Constant::cSPECIFIED_FRAME; break;
      case Field::cFRAME_EPOCH:       value = std::to_string(aMessage.mFrameEpoch); break;
      case Field::cCALIBRATION_EPOCH: value = std::to_string(aMessage.mCalibrationEpoch); break;
      case Field::cPOSE_DIRECTION:    value = List(aMessage.mPose + 3, 4, Digits::cQUATERNION[s]); break;
      case Field::cPOSE:              value = List(aMessage.mPose, 3, Digits::cPOSITION[s]) + "," + List(aMessage.mPose + 3, 4, Digits::cQUATERNION[s]); break;
      case Field::cVELOCITY:          value = List(aMessage.mVelocity, 3, Digits::cVELOCITY[s]); break;
      case Field::cUNITS:             value = Constant::cUNITS; break;
      case Field::cQUATERNION_ORDER:  value = Constant::cQUATERNION; break;
      case Field::cOPERATION:         value = Lean() ? cLEAN_OPERATION[aMessage.mOperation] : cSPECIFIED_OPERATION[aMessage.mOperation]; break;
      case Field::cTARGET:            value = Entity(aMessage.mEntity); break;
      case Field::cACK:               value = (aMessage.mAckSequence < 0) ? "" : EventId(aMessage.mAckOwner, aMessage.mAckSequence); break;
      case Field::cEXPIRY:            value = Time(aMessage.mExpiryUs); break;
      case Field::cPROVENANCE:        value = Lean() ? Constant::cLEAN_PROV : Constant::cSPECIFIED_PROV; break;
      case Field::cVALUE:             value = Hex(aMessage.mValue, 16); break;
      case Field::cCLOCK_OFFSET:      value = std::to_string(aMessage.mClockOffsetUs); break;
      case Field::cMEMBERSHIP:        value = std::to_string(aMessage.mMembershipEpoch); break;
      case Field::cHIGHEST:           value = std::to_string(aMessage.mHighestSequence); break;
      case Field::cREQUEST_OWNER:     value = Participant(aMessage.mRequestOwner); break;
      case Field::cMISSING:
         for (size_t index = 0; index < aMessage.mMissing.size(); ++index)
         {
            value += (index > 0 ? "," : "") + std::to_string(aMessage.mMissing[index]);
         }
         break;
      case Field::cCAL_DELTA:         value = List(aMessage.mCalibrationDelta, 3, Digits::cDELTA_M[s]) + "," +
                                              Fixed(aMessage.mCalibrationDelta[3], Digits::cDELTA_RAD[s]); break;
      case Field::cCAL_SOURCE:        value = std::to_string(aMessage.mCalibrationSource); break;
      case Field::cCAL_REFERENCE:     value = (aMessage.mCalibrationReference < 0) ? "" : Participant(aMessage.mCalibrationReference); break;
      default: break;
      }
      if (value.empty() && Lean())
      {
         continue;                                     // lean schema omits empty optional fields
      }
      out += std::string(" ") + Name(field) + "=\"" + Escape(value) + "\"";
   }
   return out + "/>";
}

std::string CotCodec::EnvelopeXml(const CotMessage& aMessage, const std::string& aDetail) const
{
   const std::string uid = (aMessage.mKind == MessageKind::cEVENT || aMessage.mKind == MessageKind::cSTATE)
                         ? Entity(aMessage.mEntity)
                         : Participant(aMessage.mSender) + (Lean() ? cLEAN_SUFFIX[aMessage.mKind] : cSPECIFIED_SUFFIX[aMessage.mKind]);
   const std::string time = IsoTime(aMessage.mTimeUs, 3);
   std::string out = Lean() ? "" : Constant::cXML_DECLARATION;
   out += "<event version=\"2.0\" uid=\"" + Escape(uid) + "\" type=\"" + cTYPE[aMessage.mKind] + "\" how=\"" + cHOW[aMessage.mKind] +
          "\" time=\"" + time + "\" start=\"" + time + "\" stale=\"" + IsoTime(aMessage.mStaleUs, 3) + "\">";
   out += "<point lat=\"" + Fixed(aMessage.mLat, Digits::cDEGREES) + "\" lon=\"" + Fixed(aMessage.mLon, Digits::cDEGREES) +
          "\" hae=\"" + Fixed(aMessage.mHae, Digits::cMETRES) + "\" ce=\"" + Fixed(aMessage.mCe, Digits::cMETRES) +
          "\" le=\"" + Fixed(aMessage.mLe, Digits::cMETRES) + "\"/>";
   return out + "<detail>" + aDetail + "</detail></event>";
}

std::string CotCodec::TakMessage(const CotMessage& aMessage, const std::string& aDetail) const
{
   const std::string uid = (aMessage.mKind == MessageKind::cEVENT || aMessage.mKind == MessageKind::cSTATE)
                         ? Entity(aMessage.mEntity)
                         : Participant(aMessage.mSender) + (Lean() ? cLEAN_SUFFIX[aMessage.mKind] : cSPECIFIED_SUFFIX[aMessage.mKind]);
   // CotEvent fields in number order, as protobuf serializers emit them.
   std::string detail;
   PutBytes(detail, 1, aDetail);                     // Detail.xmlDetail
   std::string event;
   PutBytes(event, 1, cTYPE[aMessage.mKind]);
   PutBytes(event, 5, uid);
   const uint64_t timeMs = static_cast<uint64_t>(aMessage.mTimeUs / 1000);
   PutUnsigned(event, 6, timeMs);
   PutUnsigned(event, 7, timeMs);
   PutUnsigned(event, 8, static_cast<uint64_t>(aMessage.mStaleUs / 1000));
   PutBytes(event, 9, cHOW[aMessage.mKind]);
   // The XML point carries fixed decimals; the binary point carries the same rounded values.
   PutDouble(event, 10, std::atof(Fixed(aMessage.mLat, Digits::cDEGREES).c_str()));
   PutDouble(event, 11, std::atof(Fixed(aMessage.mLon, Digits::cDEGREES).c_str()));
   PutDouble(event, 12, std::atof(Fixed(aMessage.mHae, Digits::cMETRES).c_str()));
   PutDouble(event, 13, std::atof(Fixed(aMessage.mCe, Digits::cMETRES).c_str()));
   PutDouble(event, 14, std::atof(Fixed(aMessage.mLe, Digits::cMETRES).c_str()));
   PutBytes(event, 15, detail);
   std::string out;
   out += static_cast<char>(Tak::cMAGIC);
   out += static_cast<char>(Tak::cVERSION);
   out += static_cast<char>(Tak::cMAGIC);
   PutBytes(out, 2, event);                          // TakMessage.cotEvent
   return out;
}

std::string CotCodec::Encode(const CotMessage& aMessage) const
{
   const std::string detail = Detail(aMessage);
   if (mMode == WireMode::cTAK_SPECIFIED || mMode == WireMode::cTAK_LEAN)
   {
      return TakMessage(aMessage, detail);
   }
   return EnvelopeXml(aMessage, detail);
}

bool CotCodec::DecodeDetail(const XmlElement& aDetail, CotMessage& aMessage, std::string& aError) const
{
   int kind = -1;
   for (int index = 0; index < MessageKind::cCOUNT; ++index)
   {
      if (aDetail.mName == (Lean() ? cLEAN_ELEMENT[index] : cSPECIFIED_ELEMENT[index]))
      {
         kind = index;
      }
   }
   if (kind < 0)
   {
      aError = "unknown detail element " + aDetail.mName;
      return false;
   }
   if (kind != aMessage.mKind)
   {
      aError = "detail element does not match the CoT type";
      return false;
   }
   if (aDetail.mAttributes.size() > cKIND_FIELDS[kind].size())
   {
      aError = "unexpected attributes";
      return false;
   }
   for (int field : cKIND_FIELDS[kind])
   {
      const std::string* raw = aDetail.Attribute(Name(field));
      const bool optional = (field == Field::cACK || field == Field::cCAL_REFERENCE || field == Field::cMISSING);
      if (raw == nullptr)
      {
         if (Lean() && optional)
         {
            continue;
         }
         aError = std::string("missing field ") + Name(field);
         return false;
      }
      const std::string& text = *raw;
      bool ok = true;
      int64_t number = 0;
      switch (field)
      {
      case Field::cSESSION:           ok = (text == mSession); break;
      case Field::cOWNER:             ok = ParticipantIndex(text, aMessage.mOwner); break;
      case Field::cSENDER:            ok = ParticipantIndex(text, aMessage.mSender); break;
      case Field::cENTITY:            ok = EntityNumber(text, aMessage.mEntity); break;
      case Field::cEVENT:
      {
         int owner = -1;
         ok = EventNumber(text, owner, aMessage.mSequence) && owner == aMessage.mOwner;
         break;
      }
      case Field::cAUTHORITY:         ok = ParseInteger(text, number); aMessage.mAuthorityEpoch = static_cast<int>(number); break;
      case Field::cREVISION:          ok = ParseInteger(text, number); aMessage.mRevision = static_cast<int>(number); break;
      case Field::cSEQUENCE:
         ok = ParseInteger(text, number) && (kind != MessageKind::cEVENT || number == aMessage.mSequence);
         aMessage.mSequence = number;
         break;
      case Field::cSAMPLE:            ok = TimeValue(text, aMessage.mSampleUs); break;
      case Field::cONSET:             ok = TimeValue(text, aMessage.mOnsetUs); break;
      case Field::cCLOCK:             ok = (text == (Lean() ? std::string("1") : "clk-" + mSession.substr(0, 8))); break;
      case Field::cCLOCK_EPOCH:       ok = ParseInteger(text, number); aMessage.mClockEpoch = static_cast<int>(number); break;
      case Field::cCLOCK_UNCERTAINTY: ok = ParseInteger(text, number); aMessage.mClockUncertaintyUs = static_cast<int>(number); break;
      case Field::cDEADLINE:          ok = TimeValue(text, aMessage.mDeadlineUs); break;
      case Field::cFRAME:             ok = (text == (Lean() ? Constant::cLEAN_FRAME : Constant::cSPECIFIED_FRAME)); break;
      case Field::cFRAME_EPOCH:       ok = ParseInteger(text, number); aMessage.mFrameEpoch = static_cast<int>(number); break;
      case Field::cCALIBRATION_EPOCH: ok = ParseInteger(text, number); aMessage.mCalibrationEpoch = static_cast<int>(number); break;
      case Field::cPOSE_DIRECTION:    ok = ParseList(text, aMessage.mPose + 3, 4); break;
      case Field::cPOSE:              ok = ParseList(text, aMessage.mPose, 7); break;
      case Field::cVELOCITY:          ok = ParseList(text, aMessage.mVelocity, 3); break;
      case Field::cUNITS:             ok = (text == Constant::cUNITS); break;
      case Field::cQUATERNION_ORDER:  ok = (text == Constant::cQUATERNION); break;
      case Field::cOPERATION:
         ok = false;
         for (int op = 0; op < 3; ++op)
         {
            if (text == (Lean() ? cLEAN_OPERATION[op] : cSPECIFIED_OPERATION[op]))
            {
               aMessage.mOperation = op;
               ok = true;
            }
         }
         break;
      case Field::cTARGET:
      {
         int64_t target = -1;
         ok = EntityNumber(text, target) && target == aMessage.mEntity;
         break;
      }
      case Field::cACK:
         if (text.empty())
         {
            aMessage.mAckSequence = -1;
            aMessage.mAckOwner    = -1;
         }
         else
         {
            ok = EventNumber(text, aMessage.mAckOwner, aMessage.mAckSequence);
         }
         break;
      case Field::cEXPIRY:            ok = TimeValue(text, aMessage.mExpiryUs); break;
      case Field::cPROVENANCE:        ok = (text == (Lean() ? Constant::cLEAN_PROV : Constant::cSPECIFIED_PROV)); break;
      case Field::cVALUE:             ok = ParseUnsigned(text, aMessage.mValue, 16) && text.size() == 16; break;
      case Field::cCLOCK_OFFSET:      ok = ParseInteger(text, aMessage.mClockOffsetUs); break;
      case Field::cMEMBERSHIP:        ok = ParseInteger(text, number); aMessage.mMembershipEpoch = static_cast<int>(number); break;
      case Field::cHIGHEST:           ok = ParseInteger(text, aMessage.mHighestSequence); break;
      case Field::cREQUEST_OWNER:     ok = ParticipantIndex(text, aMessage.mRequestOwner); break;
      case Field::cMISSING:
      {
         aMessage.mMissing.clear();
         size_t start = 0;
         while (ok && start < text.size())
         {
            size_t comma = text.find(',', start);
            if (comma == std::string::npos)
            {
               comma = text.size();
            }
            ok = ParseInteger(text.substr(start, comma - start), number);
            aMessage.mMissing.push_back(number);
            start = comma + 1;
         }
         break;
      }
      case Field::cCAL_DELTA:         ok = ParseList(text, aMessage.mCalibrationDelta, 4); break;
      case Field::cCAL_SOURCE:        ok = ParseInteger(text, number); aMessage.mCalibrationSource = static_cast<int>(number); break;
      case Field::cCAL_REFERENCE:
         aMessage.mCalibrationReference = -1;
         ok = text.empty() || ParticipantIndex(text, aMessage.mCalibrationReference);
         break;
      default: break;
      }
      if (!ok)
      {
         aError = std::string("bad field ") + Name(field) + "=\"" + text + "\"";
         return false;
      }
   }
   return true;
}

bool CotCodec::Decode(const std::string& aWire, CotMessage& aMessage, std::string& aError) const
{
   aMessage = CotMessage();
   std::string type, uid, how, detailText;
   XmlElement  detail;
   if (mMode == WireMode::cTAK_SPECIFIED || mMode == WireMode::cTAK_LEAN)
   {
      if (aWire.size() < 3 || static_cast<unsigned char>(aWire[0]) != Tak::cMAGIC || static_cast<unsigned char>(aWire[1]) != Tak::cVERSION ||
          static_cast<unsigned char>(aWire[2]) != Tak::cMAGIC)
      {
         aError = "missing TAK mesh header";
         return false;
      }
      ProtoReader outer{ reinterpret_cast<const unsigned char*>(aWire.data()) + 3, reinterpret_cast<const unsigned char*>(aWire.data()) + aWire.size() };
      std::string eventBytes;
      while (outer.mCursor < outer.mEnd)
      {
         int field = 0, wire = 0;
         uint64_t number = 0;
         std::string bytes;
         if (!outer.Field(field, wire, number, bytes))
         {
            aError = "malformed TakMessage";
            return false;
         }
         if (field == 2 && wire == Tak::cWIRE_BYTES)
         {
            eventBytes = bytes;
         }
      }
      ProtoReader inner{ reinterpret_cast<const unsigned char*>(eventBytes.data()), reinterpret_cast<const unsigned char*>(eventBytes.data()) + eventBytes.size() };
      uint64_t sendMs = 0, startMs = 0, staleMs = 0;
      while (inner.mCursor < inner.mEnd)
      {
         int field = 0, wire = 0;
         uint64_t number = 0;
         std::string bytes;
         if (!inner.Field(field, wire, number, bytes))
         {
            aError = "malformed CotEvent";
            return false;
         }
         switch (field)
         {
         case 1:  type = bytes; break;
         case 5:  uid = bytes; break;
         case 6:  sendMs = number; break;
         case 7:  startMs = number; break;
         case 8:  staleMs = number; break;
         case 9:  how = bytes; break;
         case 10: aMessage.mLat = AsDouble(number); break;
         case 11: aMessage.mLon = AsDouble(number); break;
         case 12: aMessage.mHae = AsDouble(number); break;
         case 13: aMessage.mCe = AsDouble(number); break;
         case 14: aMessage.mLe = AsDouble(number); break;
         case 15:
         {
            ProtoReader holder{ reinterpret_cast<const unsigned char*>(bytes.data()), reinterpret_cast<const unsigned char*>(bytes.data()) + bytes.size() };
            while (holder.mCursor < holder.mEnd)
            {
               int f = 0, w = 0;
               uint64_t n = 0;
               std::string b;
               if (!holder.Field(f, w, n, b))
               {
                  aError = "malformed Detail";
                  return false;
               }
               if (f == 1)
               {
                  detailText = b;
               }
            }
            break;
         }
         default: break;                               // unknown fields are ignored, as version 1 requires
         }
      }
      if (startMs != sendMs)
      {
         aError = "start differs from time";
         return false;
      }
      aMessage.mTimeUs  = static_cast<int64_t>(sendMs) * 1000;
      aMessage.mStaleUs = static_cast<int64_t>(staleMs) * 1000;
      if (!ParseXml(detailText, detail, aError))
      {
         return false;
      }
   }
   else
   {
      if (!Lean() && aWire.compare(0, std::strlen(Constant::cXML_DECLARATION), Constant::cXML_DECLARATION) != 0)
      {
         aError = "missing XML declaration";
         return false;
      }
      XmlElement root;
      if (!ParseXml(aWire, root, aError))
      {
         return false;
      }
      const std::string* version = root.Attribute("version");
      if (root.mName != "event" || version == nullptr || *version != "2.0" || root.mChildren.size() != 2 ||
          root.mChildren[0].mName != "point" || root.mChildren[1].mName != "detail" || root.mChildren[1].mChildren.size() != 1)
      {
         aError = "not a CoT 2.0 event with point and one detail element";
         return false;
      }
      const std::string* fields[6] = { root.Attribute("type"), root.Attribute("uid"), root.Attribute("how"), root.Attribute("time"),
                                       root.Attribute("start"), root.Attribute("stale") };
      for (const std::string* value : fields)
      {
         if (value == nullptr)
         {
            aError = "missing event attribute";
            return false;
         }
      }
      type = *fields[0];
      uid  = *fields[1];
      how  = *fields[2];
      int64_t start = 0;
      if (!ParseIsoTime(*fields[3], aMessage.mTimeUs) || !ParseIsoTime(*fields[4], start) || !ParseIsoTime(*fields[5], aMessage.mStaleUs) ||
          start != aMessage.mTimeUs)
      {
         aError = "bad event time";
         return false;
      }
      const XmlElement& point = root.mChildren[0];
      double* targets[5] = { &aMessage.mLat, &aMessage.mLon, &aMessage.mHae, &aMessage.mCe, &aMessage.mLe };
      const char* names[5] = { "lat", "lon", "hae", "ce", "le" };
      for (int index = 0; index < 5; ++index)
      {
         const std::string* value = point.Attribute(names[index]);
         if (value == nullptr || !ParseList(*value, targets[index], 1))
         {
            aError = std::string("bad point ") + names[index];
            return false;
         }
      }
      detail = root.mChildren[1].mChildren[0];
   }
   aMessage.mKind = -1;
   for (int index = 0; index < MessageKind::cCOUNT; ++index)
   {
      if (type == cTYPE[index])
      {
         aMessage.mKind = index;
      }
   }
   if (aMessage.mKind < 0 || how != cHOW[aMessage.mKind])
   {
      aError = "unknown CoT type or how";
      return false;
   }
   if (!DecodeDetail(detail, aMessage, aError))
   {
      return false;
   }
   const std::string expected = (aMessage.mKind == MessageKind::cEVENT || aMessage.mKind == MessageKind::cSTATE)
                              ? Entity(aMessage.mEntity)
                              : Participant(aMessage.mSender) + (Lean() ? cLEAN_SUFFIX[aMessage.mKind] : cSPECIFIED_SUFFIX[aMessage.mKind]);
   if (uid != expected)
   {
      aError = "envelope uid does not match the detail";
      return false;
   }
   return true;
}
