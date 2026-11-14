//! Encoded-wire CoT (NET-003): builds and parses every message the participant network
//! carries, in four wire forms. XML 2.0 carries field names as the profile gives them
//! ("specified") or a compact schema ("lean"). TAK Protocol version 1 mesh messages of
//! either schema carry a protobuf envelope with the AR detail as the xmlDetail string,
//! because version 1 lets a client add only fields a receiver may ignore. The AR detail
//! elements and CoT types are a DRAFT schema (ruling A18).

use crate::clib;
use crate::message::{CotMessage, DecodeError, MessageKind, Operation, WireForm};
use crate::proto::{self, ProtoReader};
use crate::xml::{self, parse_integer, parse_unsigned, XmlElement};

/// Entity and event numbering: owner * stride + local number.
pub const ENTITY_STRIDE: i64 = 1_000_000_000;

/// Field identifiers, in the order a detail element lists them.
#[derive(Clone, Copy, PartialEq, Eq)]
enum Field {
    Session,
    Owner,
    Sender,
    Entity,
    Event,
    Authority,
    Revision,
    Sequence,
    Sample,
    Onset,
    Clock,
    ClockEpoch,
    ClockUncertainty,
    Deadline,
    Frame,
    FrameEpoch,
    CalibrationEpoch,
    PoseDirection,
    Pose,
    Velocity,
    Units,
    QuaternionOrder,
    Operation,
    Target,
    Ack,
    Expiry,
    Provenance,
    Value,
    ClockOffset,
    Membership,
    Highest,
    RequestOwner,
    Missing,
    CalDelta,
    CalSource,
    CalReference,
}

const SPECIFIED_NAME: [&str; 36] = [
    "session_id",
    "origin_owner_id",
    "sender_id",
    "entity_uid",
    "event_uid",
    "authority_epoch",
    "revision",
    "sequence",
    "source_sample_time",
    "input_onset_time",
    "clock_id",
    "clock_mapping_epoch",
    "clock_uncertainty",
    "response_deadline",
    "mission_frame_id",
    "frame_epoch",
    "calibration_epoch",
    "pose_direction",
    "pose",
    "velocity",
    "units",
    "quaternion_order",
    "operation",
    "target_entity_uid",
    "ack_event_uid",
    "expiry",
    "observation_provenance",
    "value",
    "clock_offset",
    "membership_epoch",
    "highest_sequence",
    "owner_id",
    "missing",
    "calibration_delta",
    "calibration_source",
    "calibration_reference",
];

const LEAN_NAME: [&str; 36] = [
    "s", "o", "sd", "e", "ev", "ae", "r", "q", "st", "it", "ck", "cm", "cu", "dl", "mf", "fe",
    "ce", "pd", "p", "v", "u", "qo", "op", "te", "ak", "x", "pv", "val", "co", "me", "hs", "ow",
    "ms", "cd", "cs", "cr",
];

/// The fields each message kind carries, in order.
const KIND_FIELDS: [&[Field]; 6] = {
    use Field::*;
    [
        &[
            Session,
            Owner,
            Sender,
            Entity,
            Event,
            Authority,
            Revision,
            Sequence,
            Sample,
            Onset,
            Clock,
            ClockEpoch,
            ClockUncertainty,
            Deadline,
            Frame,
            FrameEpoch,
            CalibrationEpoch,
            PoseDirection,
            Units,
            QuaternionOrder,
            Operation,
            Target,
            Ack,
            Expiry,
            Provenance,
            Value,
        ],
        &[
            Session,
            Owner,
            Sender,
            Entity,
            Authority,
            Revision,
            Sequence,
            Sample,
            Clock,
            ClockEpoch,
            ClockUncertainty,
            Frame,
            FrameEpoch,
            CalibrationEpoch,
            Pose,
            Velocity,
            Units,
            QuaternionOrder,
        ],
        &[
            Session,
            Owner,
            Sender,
            Clock,
            ClockEpoch,
            ClockOffset,
            ClockUncertainty,
            Membership,
            Highest,
            Ack,
            Frame,
            FrameEpoch,
            CalibrationEpoch,
            CalDelta,
        ],
        &[Session, Owner, Sender, RequestOwner, Missing],
        &[Session, Owner, Sender, Highest],
        &[
            Session,
            Owner,
            Sender,
            Sample,
            Frame,
            FrameEpoch,
            CalibrationEpoch,
            CalDelta,
            CalSource,
            CalReference,
        ],
    ]
};

// Draft CoT type catalog (ruling A18), detail element names and envelope uid suffixes.
const TYPE: [&str; 6] = [
    "b-m-p-s-m",
    "a-f-G-U-C",
    "t-x-ar-c",
    "t-x-ar-r",
    "t-x-ar-s",
    "t-x-ar-k",
];
const HOW: [&str; 6] = ["h-e", "m-g", "m-g", "m-g", "m-g", "m-g"];
const SPECIFIED_ELEMENT: [&str; 6] = [
    "ar_event",
    "ar_state",
    "ar_control",
    "ar_repair",
    "ar_repair_status",
    "ar_calibration",
];
const LEAN_ELEMENT: [&str; 6] = ["are", "ars", "arc", "arr", "arq", "ark"];
const SPECIFIED_SUFFIX: [&str; 6] = [
    "",
    "",
    "-control",
    "-repair",
    "-repair-status",
    "-calibration",
];
const LEAN_SUFFIX: [&str; 6] = ["", "", ".c", ".r", ".s", ".k"];
const SPECIFIED_OPERATION: [&str; 3] = ["create", "delete", "takeover"];
const LEAN_OPERATION: [&str; 3] = ["c", "d", "t"];

// Fixed session constants the profile requires on every spatial message.
/// An explicitly synthetic surveyed origin.
const SPECIFIED_FRAME: &str = "synthetic-mission-frame-1";
const LEAN_FRAME: &str = "1";
const UNITS: &str = "m";
const QUATERNION: &str = "wxyz";
const SPECIFIED_PROVENANCE: &str = "local_input";
const LEAN_PROVENANCE: &str = "l";
const XML_DECLARATION: &str = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";

// Decimal places per quantity: specified schema, then lean schema.
/// Mission-frame meters.
const POSITION_DIGITS: [usize; 2] = [4, 3];
const QUATERNION_DIGITS: [usize; 2] = [7, 5];
const VELOCITY_DIGITS: [usize; 2] = [3, 2];
const DELTA_M_DIGITS: [usize; 2] = [4, 3];
const DELTA_RAD_DIGITS: [usize; 2] = [6, 5];
/// Latitude and longitude.
const DEGREE_DIGITS: usize = 7;
/// Height above ellipsoid, circular and linear error.
const METER_DIGITS: usize = 1;

const TAK_MAGIC: u8 = 0xBF;
const TAK_VERSION: u8 = 0x01;

const US_PER_S: i64 = 1_000_000;
const US_PER_DAY: i64 = 86_400 * US_PER_S;

/// splitmix64 finalizer: stable identifiers from the session seed.
fn mix(value: u64) -> u64 {
    let mut value = value.wrapping_add(0x9E37_79B9_7F4A_7C15);
    value = (value ^ (value >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
    value = (value ^ (value >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
    value ^ (value >> 31)
}

/// `"%0*llx"`: lowercase hex, zero-padded to at least `digits`.
fn hex(value: u64, digits: usize) -> String {
    format!("{value:0digits$x}")
}

/// Fixed decimals, with no negative zero on the wire.
fn fixed(value: f64, digits: usize) -> String {
    let text = clib::printf_fixed(value, digits);
    if let Some(rest) = text.strip_prefix('-') {
        if rest.bytes().all(|byte| byte == b'0' || byte == b'.') {
            return rest.to_string();
        }
    }
    text
}

fn list(values: &[f64], digits: usize) -> String {
    values
        .iter()
        .map(|&value| fixed(value, digits))
        .collect::<Vec<_>>()
        .join(",")
}

/// The C++ `ParseList`: exactly `targets.len()` comma-separated `strtod` values filling the
/// C string. Targets before a failure keep the values parsed so far.
fn parse_list(text: &[u8], targets: &mut [f64]) -> bool {
    let s = clib::c_str(text);
    let mut cursor = 0;
    let count = targets.len();
    for (index, target) in targets.iter_mut().enumerate() {
        let (value, used) = clib::strtod(&s[cursor..]);
        *target = value;
        if used == 0 {
            return false;
        }
        cursor += used;
        if index + 1 < count {
            if s.get(cursor) != Some(&b',') {
                return false;
            }
            cursor += 1;
        }
    }
    cursor == s.len()
}

fn escape(text: &[u8], out: &mut Vec<u8>) {
    for &byte in text {
        match byte {
            b'&' => out.extend_from_slice(b"&amp;"),
            b'<' => out.extend_from_slice(b"&lt;"),
            b'>' => out.extend_from_slice(b"&gt;"),
            b'"' => out.extend_from_slice(b"&quot;"),
            b'\'' => out.extend_from_slice(b"&apos;"),
            _ => out.push(byte),
        }
    }
}

// ---------------------------------------------------------------- calendar (UTC, proleptic Gregorian)

fn days_from_civil(year: i64, month: i64, day: i64) -> i64 {
    let year = year - i64::from(month <= 2);
    let era = (if year >= 0 { year } else { year - 399 }) / 400;
    let yoe = year - era * 400;
    let doy = (153 * (month + if month > 2 { -3 } else { 9 }) + 2) / 5 + day - 1;
    let doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    era * 146_097 + doe - 719_468
}

fn civil_from_days(days: i64) -> (i64, i64, i64) {
    let days = days.wrapping_add(719_468);
    let era = (if days >= 0 {
        days
    } else {
        days.wrapping_sub(146_096)
    }) / 146_097;
    let doe = days.wrapping_sub(era.wrapping_mul(146_097));
    let yoe = (doe - doe / 1460 + doe / 36_524 - doe / 146_096) / 365;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let day = doy - (153 * mp + 2) / 5 + 1;
    let month = mp + if mp < 10 { 3 } else { -9 };
    let year = yoe.wrapping_add(era.wrapping_mul(400)) + i64::from(month <= 2);
    (year, month, day)
}

/// ISO 8601 UTC with 3 or 6 fractional digits. Arithmetic wraps as the reference's does
/// at the ends of the `i64` range.
pub fn iso_time(utc_us: i64, digits: u32) -> String {
    let days = (if utc_us >= 0 {
        utc_us
    } else {
        utc_us.wrapping_sub(US_PER_DAY).wrapping_add(1)
    }) / US_PER_DAY;
    let rest = utc_us.wrapping_sub(days.wrapping_mul(US_PER_DAY));
    let (year, month, day) = civil_from_days(days);
    let seconds = rest / US_PER_S;
    let micros = rest % US_PER_S;
    let (hour, minute, second) = (seconds / 3600, (seconds / 60) % 60, seconds % 60);
    if digits == 6 {
        format!("{year:04}-{month:02}-{day:02}T{hour:02}:{minute:02}:{second:02}.{micros:06}Z")
    } else {
        let millis = micros / 1000;
        format!("{year:04}-{month:02}-{day:02}T{hour:02}:{minute:02}:{second:02}.{millis:03}Z")
    }
}

/// Reads what [`iso_time`] writes, with the reference's `sscanf` leniency: fields are not
/// range-checked, and each may carry leading white space and a sign. Any number of
/// fractional digits is accepted and the first six are kept; the text must end in 'Z'.
pub fn parse_iso_time(text: &[u8]) -> Option<i64> {
    let ([year, month, day, hour, minute, second], consumed) = clib::scan_iso_fields(text)?;
    let mut micros: i64 = 0;
    let mut pos = consumed;
    if text.get(pos) == Some(&b'.') {
        pos += 1;
        let mut digits = 0;
        while let Some(byte) = text.get(pos).filter(|byte| byte.is_ascii_digit()) {
            if digits < 6 {
                micros = micros * 10 + i64::from(byte - b'0');
            }
            digits += 1;
            pos += 1;
        }
        while digits < 6 {
            micros *= 10;
            digits += 1;
        }
    }
    if pos + 1 != text.len() || text[pos] != b'Z' {
        return None;
    }
    let clock = i64::from((hour * 60 + minute) * 60 + second);
    let days = days_from_civil(i64::from(year), i64::from(month), i64::from(day));
    Some(days * US_PER_DAY + clock * US_PER_S + micros)
}

/// Builds and parses one session's messages in one wire form. The session's roster maps
/// participant index to its on-wire identifier; both ends learn it at session join
/// (membership), so identifiers decode to indices.
#[derive(Clone, Debug)]
pub struct CotCodec {
    form: WireForm,
    session: String,
    roster: Vec<String>,
    uuid_prefix: u64,
}

impl CotCodec {
    pub fn new(form: WireForm, participants: usize, session_seed: u64) -> Self {
        let lean = form.is_lean();
        let session_mix = mix(session_seed);
        let session = if lean {
            hex(session_mix & 0xFFFF_FFFF, 8)
        } else {
            let low = mix(session_mix);
            format!(
                "{}-{}-4{}-{}-{}",
                hex(session_mix >> 32, 8),
                hex((session_mix >> 16) & 0xFFFF, 4),
                hex(session_mix & 0xFFF, 3),
                hex(0x8000 | ((low >> 48) & 0x3FFF), 4),
                hex(low & 0xFFFF_FFFF_FFFF, 12)
            )
        };
        let roster = (0..participants)
            .map(|index| {
                let id = mix(session_mix.wrapping_add(index as u64).wrapping_add(1));
                if lean {
                    hex(id & 0xFFFF_FFFF, 8)
                } else {
                    format!("ANDROID-{}", hex(id, 16))
                }
            })
            .collect();
        Self {
            form,
            session,
            roster,
            uuid_prefix: mix(session_seed ^ 0xA5A5_A5A5),
        }
    }

    pub fn form(&self) -> WireForm {
        self.form
    }

    pub fn is_lean(&self) -> bool {
        self.form.is_lean()
    }

    fn schema(&self) -> usize {
        usize::from(self.is_lean())
    }

    fn name(&self, field: Field) -> &'static str {
        if self.is_lean() {
            LEAN_NAME[field as usize]
        } else {
            SPECIFIED_NAME[field as usize]
        }
    }

    /// A participant's on-wire identifier. An index outside the roster yields empty text,
    /// where the reference would index out of bounds (undefined behavior).
    fn participant(&self, index: i32) -> &str {
        usize::try_from(index)
            .ok()
            .and_then(|index| self.roster.get(index))
            .map_or("", String::as_str)
    }

    fn participant_index(&self, text: &[u8]) -> Option<i32> {
        self.roster
            .iter()
            .position(|id| id.as_bytes() == text)
            .map(|index| index as i32)
    }

    /// The session's UUID prefix: everything before the variant nibble.
    fn uuid_head(&self) -> String {
        let prefix = self.uuid_prefix;
        format!(
            "{}-{}-4{}-",
            hex(prefix >> 32, 8),
            hex((prefix >> 16) & 0xFFFF, 4),
            hex(prefix & 0xFFF, 3)
        )
    }

    /// Specified schema: UUIDs whose last 48 bits carry the number and whose prefix is the
    /// session's, so a receiver recovers the number. Lean: owner identifier and a hex count.
    fn entity(&self, entity: i64) -> String {
        let owner = (entity / ENTITY_STRIDE) as i32;
        let local = entity % ENTITY_STRIDE;
        if self.is_lean() {
            return format!("{}.{}", self.participant(owner), hex(local as u64, 1));
        }
        format!(
            "{}a{}-{}",
            self.uuid_head(),
            hex(owner as u64 & 0xFFF, 3),
            hex(local as u64, 12)
        )
    }

    fn entity_number(&self, text: &[u8]) -> Option<i64> {
        if self.is_lean() {
            let dot = text.iter().position(|&byte| byte == b'.')?;
            let owner = self.participant_index(&text[..dot])?;
            let local = parse_unsigned(&text[dot + 1..], 16)?;
            return Some(
                i64::from(owner)
                    .wrapping_mul(ENTITY_STRIDE)
                    .wrapping_add(local as i64),
            );
        }
        if text.len() != 36 || text[..19] != self.uuid_head().as_bytes()[..19] || text[19] != b'a' {
            return None;
        }
        let owner = parse_unsigned(&text[20..23], 16)?;
        let local = parse_unsigned(&text[24..36], 16)?;
        Some(
            (owner as i64)
                .wrapping_mul(ENTITY_STRIDE)
                .wrapping_add(local as i64),
        )
    }

    /// Event identifiers derive from owner and sequence, so they cannot collide (ruling A18).
    fn event_id(&self, owner: i32, sequence: i64) -> String {
        if self.is_lean() {
            return format!("{}:{}", self.participant(owner), hex(sequence as u64, 1));
        }
        format!(
            "{}b{}-{}",
            self.uuid_head(),
            hex(owner as u64 & 0xFFF, 3),
            hex(sequence as u64, 12)
        )
    }

    fn event_number(&self, text: &[u8]) -> Option<(i32, i64)> {
        if self.is_lean() {
            let colon = text.iter().position(|&byte| byte == b':')?;
            let owner = self.participant_index(&text[..colon])?;
            let sequence = parse_unsigned(&text[colon + 1..], 16)?;
            return Some((owner, sequence as i64));
        }
        if text.len() != 36 || text[..19] != self.uuid_head().as_bytes()[..19] || text[19] != b'b' {
            return None;
        }
        let owner = parse_unsigned(&text[20..23], 16)? as i32;
        let sequence = parse_unsigned(&text[24..36], 16)? as i64;
        // An owner that wrapped negative passes, as in the reference.
        (i64::from(owner) < self.roster.len() as i64).then_some((owner, sequence))
    }

    fn time(&self, utc_us: i64) -> String {
        if self.is_lean() {
            utc_us.to_string()
        } else {
            iso_time(utc_us, 6)
        }
    }

    fn time_value(&self, text: &[u8]) -> Option<i64> {
        if self.is_lean() {
            parse_integer(text, 10)
        } else {
            parse_iso_time(text)
        }
    }

    fn clock_id(&self) -> String {
        if self.is_lean() {
            "1".to_string()
        } else {
            format!("clk-{}", &self.session[..8])
        }
    }

    fn envelope_uid(&self, message: &CotMessage) -> String {
        match message.kind {
            MessageKind::Event | MessageKind::State => self.entity(message.entity),
            kind => {
                let suffix = if self.is_lean() {
                    LEAN_SUFFIX[kind.index()]
                } else {
                    SPECIFIED_SUFFIX[kind.index()]
                };
                format!("{}{}", self.participant(message.sender), suffix)
            }
        }
    }

    fn detail(&self, message: &CotMessage) -> Vec<u8> {
        let s = self.schema();
        let lean = self.is_lean();
        let kind = message.kind.index();
        let mut out = Vec::with_capacity(512);
        out.push(b'<');
        out.extend_from_slice(
            if lean {
                LEAN_ELEMENT[kind]
            } else {
                SPECIFIED_ELEMENT[kind]
            }
            .as_bytes(),
        );
        for &field in KIND_FIELDS[kind] {
            let value: String = match field {
                Field::Session => self.session.clone(),
                Field::Owner => self.participant(message.owner).to_string(),
                Field::Sender => self.participant(message.sender).to_string(),
                Field::Entity | Field::Target => self.entity(message.entity),
                Field::Event => self.event_id(message.owner, message.sequence),
                Field::Authority => message.authority_epoch.to_string(),
                Field::Revision => message.revision.to_string(),
                Field::Sequence => message.sequence.to_string(),
                Field::Sample => self.time(message.sample_us),
                Field::Onset => self.time(message.onset_us),
                Field::Clock => self.clock_id(),
                Field::ClockEpoch => message.clock_epoch.to_string(),
                Field::ClockUncertainty => message.clock_uncertainty_us.to_string(),
                Field::Deadline => self.time(message.deadline_us),
                Field::Frame => if lean { LEAN_FRAME } else { SPECIFIED_FRAME }.to_string(),
                Field::FrameEpoch => message.frame_epoch.to_string(),
                Field::CalibrationEpoch => message.calibration_epoch.to_string(),
                Field::PoseDirection => list(&message.pose[3..], QUATERNION_DIGITS[s]),
                Field::Pose => {
                    format!(
                        "{},{}",
                        list(&message.pose[..3], POSITION_DIGITS[s]),
                        list(&message.pose[3..], QUATERNION_DIGITS[s])
                    )
                }
                Field::Velocity => list(&message.velocity, VELOCITY_DIGITS[s]),
                Field::Units => UNITS.to_string(),
                Field::QuaternionOrder => QUATERNION.to_string(),
                Field::Operation => {
                    let index = message.operation as usize;
                    if lean {
                        LEAN_OPERATION[index]
                    } else {
                        SPECIFIED_OPERATION[index]
                    }
                    .to_string()
                }
                Field::Ack => {
                    if message.ack_sequence < 0 {
                        String::new()
                    } else {
                        self.event_id(message.ack_owner, message.ack_sequence)
                    }
                }
                Field::Expiry => self.time(message.expiry_us),
                Field::Provenance => if lean {
                    LEAN_PROVENANCE
                } else {
                    SPECIFIED_PROVENANCE
                }
                .to_string(),
                Field::Value => hex(message.value, 16),
                Field::ClockOffset => message.clock_offset_us.to_string(),
                Field::Membership => message.membership_epoch.to_string(),
                Field::Highest => message.highest_sequence.to_string(),
                Field::RequestOwner => self.participant(message.request_owner).to_string(),
                Field::Missing => message
                    .missing
                    .iter()
                    .map(i64::to_string)
                    .collect::<Vec<_>>()
                    .join(","),
                Field::CalDelta => format!(
                    "{},{}",
                    list(&message.calibration_delta[..3], DELTA_M_DIGITS[s]),
                    fixed(message.calibration_delta[3], DELTA_RAD_DIGITS[s])
                ),
                Field::CalSource => message.calibration_source.to_string(),
                Field::CalReference => {
                    if message.calibration_reference < 0 {
                        String::new()
                    } else {
                        self.participant(message.calibration_reference).to_string()
                    }
                }
            };
            if value.is_empty() && lean {
                continue; // the lean schema omits empty optional fields
            }
            out.push(b' ');
            out.extend_from_slice(self.name(field).as_bytes());
            out.extend_from_slice(b"=\"");
            escape(value.as_bytes(), &mut out);
            out.push(b'"');
        }
        out.extend_from_slice(b"/>");
        out
    }

    fn envelope_xml(&self, message: &CotMessage, detail: &[u8]) -> Vec<u8> {
        let kind = message.kind.index();
        let time = iso_time(message.time_us, 3);
        let mut out = Vec::with_capacity(detail.len() + 512);
        if !self.is_lean() {
            out.extend_from_slice(XML_DECLARATION.as_bytes());
        }
        out.extend_from_slice(b"<event version=\"2.0\" uid=\"");
        escape(self.envelope_uid(message).as_bytes(), &mut out);
        let head = format!(
            "\" type=\"{}\" how=\"{}\" time=\"{time}\" start=\"{time}\" stale=\"{}\"><point lat=\"{}\" lon=\"{}\" hae=\"{}\" ce=\"{}\" le=\"{}\"/><detail>",
            TYPE[kind],
            HOW[kind],
            iso_time(message.stale_us, 3),
            fixed(message.lat, DEGREE_DIGITS),
            fixed(message.lon, DEGREE_DIGITS),
            fixed(message.hae, METER_DIGITS),
            fixed(message.ce, METER_DIGITS),
            fixed(message.le, METER_DIGITS)
        );
        out.extend_from_slice(head.as_bytes());
        out.extend_from_slice(detail);
        out.extend_from_slice(b"</detail></event>");
        out
    }

    fn tak_message(&self, message: &CotMessage, detail: &[u8]) -> Vec<u8> {
        let kind = message.kind.index();
        // CotEvent fields in number order, as protobuf serializers emit them.
        let mut detail_holder = Vec::with_capacity(detail.len() + 4);
        proto::put_bytes(&mut detail_holder, 1, detail); // Detail.xmlDetail
        let mut event = Vec::with_capacity(detail.len() + 160);
        proto::put_bytes(&mut event, 1, TYPE[kind].as_bytes());
        proto::put_bytes(&mut event, 5, self.envelope_uid(message).as_bytes());
        let time_ms = (message.time_us / 1000) as u64;
        proto::put_unsigned(&mut event, 6, time_ms);
        proto::put_unsigned(&mut event, 7, time_ms);
        proto::put_unsigned(&mut event, 8, (message.stale_us / 1000) as u64);
        proto::put_bytes(&mut event, 9, HOW[kind].as_bytes());
        // The XML point carries fixed decimals; the binary point carries the same rounded values.
        let rounded = |value: f64, digits: usize| clib::atof(fixed(value, digits).as_bytes());
        proto::put_double(&mut event, 10, rounded(message.lat, DEGREE_DIGITS));
        proto::put_double(&mut event, 11, rounded(message.lon, DEGREE_DIGITS));
        proto::put_double(&mut event, 12, rounded(message.hae, METER_DIGITS));
        proto::put_double(&mut event, 13, rounded(message.ce, METER_DIGITS));
        proto::put_double(&mut event, 14, rounded(message.le, METER_DIGITS));
        proto::put_bytes(&mut event, 15, &detail_holder);
        let mut out = Vec::with_capacity(event.len() + 8);
        out.extend_from_slice(&[TAK_MAGIC, TAK_VERSION, TAK_MAGIC]);
        proto::put_bytes(&mut out, 2, &event); // TakMessage.cotEvent
        out
    }

    /// The message on the wire.
    pub fn encode(&self, message: &CotMessage) -> Vec<u8> {
        let detail = self.detail(message);
        if self.form.is_tak() {
            self.tak_message(message, &detail)
        } else {
            self.envelope_xml(message, &detail)
        }
    }

    /// The message a wire form carries, validated against the session.
    pub fn decode(&self, wire: &[u8]) -> Result<CotMessage, DecodeError> {
        let fail = |text: &str| Err(DecodeError(text.as_bytes().to_vec()));
        let mut message = CotMessage::default();
        let kind_type: Vec<u8>;
        let uid: Vec<u8>;
        let how: Vec<u8>;
        let detail: XmlElement;
        if self.form.is_tak() {
            if wire.len() < 3 || wire[..3] != [TAK_MAGIC, TAK_VERSION, TAK_MAGIC] {
                return fail("missing TAK mesh header");
            }
            let mut outer = ProtoReader::new(&wire[3..]);
            let mut event_bytes: &[u8] = &[];
            while !outer.at_end() {
                let Some(field) = outer.field() else {
                    return fail("malformed TakMessage");
                };
                if field.field == 2 && field.wire == proto::WIRE_BYTES {
                    event_bytes = field.bytes;
                }
            }
            let mut inner = ProtoReader::new(event_bytes);
            let (mut send_ms, mut start_ms, mut stale_ms) = (0u64, 0u64, 0u64);
            let mut found_type: &[u8] = &[];
            let mut found_uid: &[u8] = &[];
            let mut found_how: &[u8] = &[];
            let mut detail_text: &[u8] = &[];
            while !inner.at_end() {
                let Some(field) = inner.field() else {
                    return fail("malformed CotEvent");
                };
                let as_double = f64::from_bits(field.number);
                match field.field {
                    1 => found_type = field.bytes,
                    5 => found_uid = field.bytes,
                    6 => send_ms = field.number,
                    7 => start_ms = field.number,
                    8 => stale_ms = field.number,
                    9 => found_how = field.bytes,
                    10 => message.lat = as_double,
                    11 => message.lon = as_double,
                    12 => message.hae = as_double,
                    13 => message.ce = as_double,
                    14 => message.le = as_double,
                    15 => {
                        let mut holder = ProtoReader::new(field.bytes);
                        while !holder.at_end() {
                            let Some(inside) = holder.field() else {
                                return fail("malformed Detail");
                            };
                            if inside.field == 1 {
                                detail_text = inside.bytes;
                            }
                        }
                    }
                    _ => {} // unknown fields are ignored, as version 1 requires
                }
            }
            if start_ms != send_ms {
                return fail("start differs from time");
            }
            message.time_us = (send_ms as i64).wrapping_mul(1000);
            message.stale_us = (stale_ms as i64).wrapping_mul(1000);
            detail = xml::parse_xml(detail_text).map_err(DecodeError)?;
            kind_type = found_type.to_vec();
            uid = found_uid.to_vec();
            how = found_how.to_vec();
        } else {
            if !self.is_lean() && !wire.starts_with(XML_DECLARATION.as_bytes()) {
                return fail("missing XML declaration");
            }
            let mut root = xml::parse_xml(wire).map_err(DecodeError)?;
            let shaped = root.name == b"event"
                && root.attribute(b"version") == Some(b"2.0")
                && root.children.len() == 2
                && root.children[0].name == b"point"
                && root.children[1].name == b"detail"
                && root.children[1].children.len() == 1;
            if !shaped {
                return fail("not a CoT 2.0 event with point and one detail element");
            }
            let names: [&[u8]; 6] = [b"type", b"uid", b"how", b"time", b"start", b"stale"];
            let mut fields: [&[u8]; 6] = [&[]; 6];
            for (slot, name) in fields.iter_mut().zip(names) {
                match root.attribute(name) {
                    Some(value) => *slot = value,
                    None => return fail("missing event attribute"),
                }
            }
            let times = (
                parse_iso_time(fields[3]),
                parse_iso_time(fields[4]),
                parse_iso_time(fields[5]),
            );
            let (Some(time), Some(start), Some(stale)) = times else {
                return fail("bad event time");
            };
            if start != time {
                return fail("bad event time");
            }
            message.time_us = time;
            message.stale_us = stale;
            let point = &root.children[0];
            let mut targets = [0.0; 5];
            for (index, name) in ["lat", "lon", "hae", "ce", "le"].into_iter().enumerate() {
                let parsed = point
                    .attribute(name.as_bytes())
                    .is_some_and(|value| parse_list(value, &mut targets[index..=index]));
                if !parsed {
                    return Err(DecodeError(format!("bad point {name}").into_bytes()));
                }
            }
            [
                message.lat,
                message.lon,
                message.hae,
                message.ce,
                message.le,
            ] = targets;
            kind_type = fields[0].to_vec();
            uid = fields[1].to_vec();
            how = fields[2].to_vec();
            detail = root.children.swap_remove(1).children.swap_remove(0);
        }
        let Some(kind) = MessageKind::ALL
            .into_iter()
            .find(|kind| kind_type == TYPE[kind.index()].as_bytes())
        else {
            return fail("unknown CoT type or how");
        };
        if how != HOW[kind.index()].as_bytes() {
            return fail("unknown CoT type or how");
        }
        message.kind = kind;
        self.decode_detail(&detail, &mut message)?;
        if uid != self.envelope_uid(&message).as_bytes() {
            return fail("envelope uid does not match the detail");
        }
        Ok(message)
    }

    fn decode_detail(
        &self,
        detail: &XmlElement,
        message: &mut CotMessage,
    ) -> Result<(), DecodeError> {
        let lean = self.is_lean();
        let found = MessageKind::ALL.into_iter().rev().find(|kind| {
            let name = if lean {
                LEAN_ELEMENT[kind.index()]
            } else {
                SPECIFIED_ELEMENT[kind.index()]
            };
            detail.name == name.as_bytes()
        });
        let Some(kind) = found else {
            let mut error = b"unknown detail element ".to_vec();
            error.extend_from_slice(&detail.name);
            return Err(DecodeError(error));
        };
        if kind != message.kind {
            return Err(DecodeError(
                b"detail element does not match the CoT type".to_vec(),
            ));
        }
        let fields = KIND_FIELDS[kind.index()];
        if detail.attributes.len() > fields.len() {
            return Err(DecodeError(b"unexpected attributes".to_vec()));
        }
        for &field in fields {
            let name = self.name(field);
            let Some(text) = detail.attribute(name.as_bytes()) else {
                let optional = matches!(field, Field::Ack | Field::CalReference | Field::Missing);
                if lean && optional {
                    continue;
                }
                return Err(DecodeError(format!("missing field {name}").into_bytes()));
            };
            if !self.decode_field(field, kind, text, message) {
                let mut error = format!("bad field {name}=\"").into_bytes();
                error.extend_from_slice(text);
                error.push(b'"');
                return Err(DecodeError(error));
            }
        }
        Ok(())
    }

    /// One detail field into the message; false when the text is not valid for it.
    fn decode_field(
        &self,
        field: Field,
        kind: MessageKind,
        text: &[u8],
        message: &mut CotMessage,
    ) -> bool {
        let lean = self.is_lean();
        let integer = |target: &mut i32| match parse_integer(text, 10) {
            Some(number) => {
                *target = number as i32;
                true
            }
            None => false,
        };
        let store = |target: &mut i64, value: Option<i64>| match value {
            Some(value) => {
                *target = value;
                true
            }
            None => false,
        };
        let index = |target: &mut i32, value: Option<i32>| match value {
            Some(value) => {
                *target = value;
                true
            }
            None => false,
        };
        match field {
            Field::Session => text == self.session.as_bytes(),
            Field::Owner => index(&mut message.owner, self.participant_index(text)),
            Field::Sender => index(&mut message.sender, self.participant_index(text)),
            Field::Entity => store(&mut message.entity, self.entity_number(text)),
            Field::Event => match self.event_number(text) {
                Some((owner, sequence)) => {
                    message.sequence = sequence;
                    owner == message.owner
                }
                None => false,
            },
            Field::Authority => integer(&mut message.authority_epoch),
            Field::Revision => integer(&mut message.revision),
            Field::Sequence => match parse_integer(text, 10) {
                Some(number) => {
                    let matches = kind != MessageKind::Event || number == message.sequence;
                    message.sequence = number;
                    matches
                }
                None => false,
            },
            Field::Sample => store(&mut message.sample_us, self.time_value(text)),
            Field::Onset => store(&mut message.onset_us, self.time_value(text)),
            Field::Clock => text == self.clock_id().as_bytes(),
            Field::ClockEpoch => integer(&mut message.clock_epoch),
            Field::ClockUncertainty => integer(&mut message.clock_uncertainty_us),
            Field::Deadline => store(&mut message.deadline_us, self.time_value(text)),
            Field::Frame => text == if lean { LEAN_FRAME } else { SPECIFIED_FRAME }.as_bytes(),
            Field::FrameEpoch => integer(&mut message.frame_epoch),
            Field::CalibrationEpoch => integer(&mut message.calibration_epoch),
            Field::PoseDirection => parse_list(text, &mut message.pose[3..]),
            Field::Pose => parse_list(text, &mut message.pose),
            Field::Velocity => parse_list(text, &mut message.velocity),
            Field::Units => text == UNITS.as_bytes(),
            Field::QuaternionOrder => text == QUATERNION.as_bytes(),
            Field::Operation => {
                let names = if lean {
                    LEAN_OPERATION
                } else {
                    SPECIFIED_OPERATION
                };
                match Operation::ALL
                    .into_iter()
                    .find(|operation| text == names[*operation as usize].as_bytes())
                {
                    Some(operation) => {
                        message.operation = operation;
                        true
                    }
                    None => false,
                }
            }
            Field::Target => self.entity_number(text) == Some(message.entity),
            Field::Ack => {
                if text.is_empty() {
                    message.ack_sequence = -1;
                    message.ack_owner = -1;
                    return true;
                }
                match self.event_number(text) {
                    Some((owner, sequence)) => {
                        message.ack_owner = owner;
                        message.ack_sequence = sequence;
                        true
                    }
                    None => false,
                }
            }
            Field::Expiry => store(&mut message.expiry_us, self.time_value(text)),
            Field::Provenance => {
                text == if lean {
                    LEAN_PROVENANCE
                } else {
                    SPECIFIED_PROVENANCE
                }
                .as_bytes()
            }
            Field::Value => match parse_unsigned(text, 16) {
                Some(value) if text.len() == 16 => {
                    message.value = value;
                    true
                }
                _ => false,
            },
            Field::ClockOffset => store(&mut message.clock_offset_us, parse_integer(text, 10)),
            Field::Membership => integer(&mut message.membership_epoch),
            Field::Highest => store(&mut message.highest_sequence, parse_integer(text, 10)),
            Field::RequestOwner => index(&mut message.request_owner, self.participant_index(text)),
            Field::Missing => {
                // Comma-separated integers; a trailing comma is accepted, as in the reference.
                message.missing.clear();
                let mut start = 0;
                while start < text.len() {
                    let comma = text[start..]
                        .iter()
                        .position(|&byte| byte == b',')
                        .map_or(text.len(), |offset| start + offset);
                    let Some(number) = parse_integer(&text[start..comma], 10) else {
                        return false;
                    };
                    message.missing.push(number);
                    start = comma + 1;
                }
                true
            }
            Field::CalDelta => parse_list(text, &mut message.calibration_delta),
            Field::CalSource => integer(&mut message.calibration_source),
            Field::CalReference => {
                message.calibration_reference = -1;
                text.is_empty()
                    || index(
                        &mut message.calibration_reference,
                        self.participant_index(text),
                    )
            }
        }
    }
}
