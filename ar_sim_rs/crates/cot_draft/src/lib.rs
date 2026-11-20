//! Draft CoT message kinds that the reference codec does not carry (Network ICD sections 5 and 6,
//! open item N-05), in TAK Protocol v1 with the detail as XML text, like `tak_lean`.
//!
//! | Kind | CoT type | Detail element | Layer |
//! |---|---|---|---|
//! | Session record | `t-x-ar-m` | `arm` | L5 |
//! | Join request | `t-x-ar-j` | `arj`, plus binary contact and takv presence | L5 |
//! | Snapshot barrier | `t-x-ar-b` | `arb` | L5 |
//! | Catch-up request | `t-x-ar-q` | `arq` | L5 |
//! | Marker delete | `t-x-d-d` | `link`, `__forcedelete`, `ard` | L7 |
//!
//! They live outside `cot_codec` so that crate stays byte-identical to the C++ reference: these
//! kinds exist only in the draft schema (ruling A18), and the C++ simulator does not model them.
//! A receiver tries [`decode`] first; [`DecodeError::NotDraft`] means "hand the bytes to
//! `cot_codec`". Every type here is a draft and needs registration before release (ICD N-03).

#![forbid(unsafe_code)]

use std::collections::BTreeMap;
use std::fmt;

const TAK_HEADER: [u8; 3] = [0xBF, 0x01, 0xBF];
/// The AR detail extension's identifier, advertised in join requests (ruling R13).
pub const AR_EXTENSION_ID: u32 = 8206;
const CONTROL_ERROR_M: f64 = 9_999_999.0;

/// A retained item: a marker, named by its creator's roster index and the event sequence that created it.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct ItemId {
    pub owner: i32,
    pub sequence: i64,
}

impl fmt::Display for ItemId {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}:{}", self.owner, self.sequence)
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Phase {
    Lobby,
    Match,
    Closed,
}

impl Phase {
    fn code(self) -> &'static str {
        match self {
            Phase::Lobby => "l",
            Phase::Match => "m",
            Phase::Closed => "c",
        }
    }
}

/// The leader's session record (HICD section 4): every leader control is a new revision of it.
#[derive(Clone, Debug, PartialEq)]
pub struct SessionRecord {
    pub session: u32,
    /// The leader's participant identifier (8 hex digits).
    pub leader: String,
    pub revision: u64,
    /// Roster epoch; rises with every membership change.
    pub epoch: u32,
    pub phase: Phase,
    pub end_us: i64,
    pub capacity: u32,
    pub marker_cap: u32,
    /// Participant identifiers in roster-index order; an index is never reused.
    pub roster: Vec<String>,
    /// Indices of members who have left; they stay listed (R16).
    pub departed: Vec<u32>,
    pub time_us: i64,
}

/// A candidate's request to join (HICD 5.1).
#[derive(Clone, Debug, PartialEq)]
pub struct JoinRequest {
    pub session: u32,
    pub candidate: String,
    pub tak_version: u32,
    pub extensions: Vec<u32>,
    pub time_us: i64,
}

/// The end of a catch-up: every retained item the leader holds, so a newcomer can tell what it missed.
#[derive(Clone, Debug, PartialEq)]
pub struct Barrier {
    pub session: u32,
    pub sender: String,
    /// The session record revision this snapshot belongs to.
    pub revision: u64,
    pub markers: Vec<ItemId>,
    pub tombstones: Vec<ItemId>,
    pub time_us: i64,
}

/// A newcomer's request for retained items it lacks; an empty list asks for everything.
#[derive(Clone, Debug, PartialEq)]
pub struct CatchupRequest {
    pub session: u32,
    pub requester: String,
    pub missing: Vec<ItemId>,
    pub time_us: i64,
}

/// A marker delete (HICD 5.3), after ATAK's `t-x-d-d` task with `<link>` and `<__forcedelete/>`.
#[derive(Clone, Debug, PartialEq)]
pub struct MarkerDelete {
    pub session: u32,
    /// Roster index of the member deleting.
    pub deleter: i32,
    /// The deleter's own delete counter, so each delete has its own envelope uid.
    pub sequence: i64,
    pub target: ItemId,
    pub time_us: i64,
}

#[derive(Clone, Debug, PartialEq)]
pub enum Draft {
    Record(SessionRecord),
    Join(JoinRequest),
    Barrier(Barrier),
    CatchupRequest(CatchupRequest),
    Delete(MarkerDelete),
}

impl Draft {
    /// The log code the harness uses for this kind (the reference codec's kinds are 0 to 5).
    pub fn kind_code(&self) -> i32 {
        match self {
            Draft::Record(_) => 10,
            Draft::Join(_) => 11,
            Draft::Barrier(_) => 12,
            Draft::CatchupRequest(_) => 13,
            Draft::Delete(_) => 14,
        }
    }

    pub fn session(&self) -> u32 {
        match self {
            Draft::Record(m) => m.session,
            Draft::Join(m) => m.session,
            Draft::Barrier(m) => m.session,
            Draft::CatchupRequest(m) => m.session,
            Draft::Delete(m) => m.session,
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum DecodeError {
    /// Not a draft kind (or not TAK at all): give the bytes to the reference codec.
    NotDraft,
    Malformed(String),
}

impl fmt::Display for DecodeError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            DecodeError::NotDraft => write!(f, "not a draft message kind"),
            DecodeError::Malformed(why) => write!(f, "malformed draft message: {why}"),
        }
    }
}

impl std::error::Error for DecodeError {}

// ----------------------------------------------------------------------------- protobuf

fn put_varint(out: &mut Vec<u8>, mut value: u64) {
    while value >= 0x80 {
        out.push((value as u8) | 0x80);
        value >>= 7;
    }
    out.push(value as u8);
}

fn put_bytes(out: &mut Vec<u8>, field: u32, bytes: &[u8]) {
    if bytes.is_empty() {
        return; // proto3 omits defaults
    }
    put_varint(out, u64::from(field << 3 | 2));
    put_varint(out, bytes.len() as u64);
    out.extend_from_slice(bytes);
}

fn put_unsigned(out: &mut Vec<u8>, field: u32, value: u64) {
    if value != 0 {
        put_varint(out, u64::from(field << 3));
        put_varint(out, value);
    }
}

fn put_double(out: &mut Vec<u8>, field: u32, value: f64) {
    if value.to_bits() != 0 {
        put_varint(out, u64::from(field << 3 | 1));
        out.extend_from_slice(&value.to_le_bytes());
    }
}

struct Field<'a> {
    number: u32,
    varint: u64,
    bytes: &'a [u8],
}

/// Reads the fields of one protobuf message; None on any malformed field.
fn fields(mut data: &[u8]) -> Option<Vec<Field<'_>>> {
    fn varint(data: &mut &[u8]) -> Option<u64> {
        let mut value = 0u64;
        for shift in (0..64).step_by(7) {
            let (&byte, rest) = data.split_first()?;
            *data = rest;
            value |= u64::from(byte & 0x7F) << shift;
            if byte < 0x80 {
                return Some(value);
            }
        }
        None
    }
    let mut out = Vec::new();
    while !data.is_empty() {
        let key = varint(&mut data)?;
        let number = u32::try_from(key >> 3).ok()?;
        match key & 7 {
            0 => out.push(Field {
                number,
                varint: varint(&mut data)?,
                bytes: &[],
            }),
            1 => {
                let (bytes, rest) = data.split_at_checked(8)?;
                data = rest;
                out.push(Field {
                    number,
                    varint: 0,
                    bytes,
                });
            }
            2 => {
                let length = usize::try_from(varint(&mut data)?).ok()?;
                let (bytes, rest) = data.split_at_checked(length)?;
                data = rest;
                out.push(Field {
                    number,
                    varint: 0,
                    bytes,
                });
            }
            5 => {
                let (bytes, rest) = data.split_at_checked(4)?;
                data = rest;
                out.push(Field {
                    number,
                    varint: 0,
                    bytes,
                });
            }
            _ => return None,
        }
    }
    Some(out)
}

// ----------------------------------------------------------------------------- XML detail

fn escape(text: &str) -> String {
    text.replace('&', "&amp;")
        .replace('<', "&lt;")
        .replace('>', "&gt;")
        .replace('"', "&quot;")
}

fn unescape(text: &str) -> String {
    text.replace("&quot;", "\"")
        .replace("&lt;", "<")
        .replace("&gt;", ">")
        .replace("&amp;", "&")
}

fn element(name: &str, attributes: &[(&str, String)]) -> String {
    let mut out = format!("<{name}");
    for (key, value) in attributes {
        out.push_str(&format!(" {key}=\"{}\"", escape(value)));
    }
    out.push_str("/>");
    out
}

/// Every element in a flat detail, by name, with its attributes; nested content is not expected.
fn elements(detail: &str) -> Result<BTreeMap<String, BTreeMap<String, String>>, DecodeError> {
    let bad = |why: &str| DecodeError::Malformed(why.to_string());
    let mut out = BTreeMap::new();
    let mut rest = detail.trim();
    while !rest.is_empty() {
        let body = rest
            .strip_prefix('<')
            .ok_or_else(|| bad("detail text outside an element"))?;
        let end = body
            .find("/>")
            .ok_or_else(|| bad("element not self-closed"))?;
        let inner = &body[..end];
        rest = body[end + 2..].trim_start();
        let name_end = inner.find(char::is_whitespace).unwrap_or(inner.len());
        let name = &inner[..name_end];
        if name.is_empty() {
            return Err(bad("element without a name"));
        }
        let mut attributes = BTreeMap::new();
        let mut text = inner[name_end..].trim();
        while !text.is_empty() {
            let eq = text
                .find("=\"")
                .ok_or_else(|| bad("attribute without a quoted value"))?;
            let key = text[..eq].trim();
            let value_start = eq + 2;
            let close = text[value_start..]
                .find('"')
                .ok_or_else(|| bad("unterminated attribute value"))?;
            attributes.insert(
                key.to_string(),
                unescape(&text[value_start..value_start + close]),
            );
            text = text[value_start + close + 1..].trim_start();
        }
        out.insert(name.to_string(), attributes);
    }
    Ok(out)
}

fn items(list: &[ItemId]) -> String {
    list.iter()
        .map(ItemId::to_string)
        .collect::<Vec<_>>()
        .join(",")
}

fn parse_items(text: &str) -> Result<Vec<ItemId>, DecodeError> {
    if text.is_empty() {
        return Ok(Vec::new());
    }
    text.split(',')
        .map(|pair| {
            let (owner, sequence) = pair.split_once(':').ok_or_else(|| {
                DecodeError::Malformed(format!("item {pair:?} is not owner:sequence"))
            })?;
            Ok(ItemId {
                owner: number(owner, "item owner")?,
                sequence: number(sequence, "item sequence")?,
            })
        })
        .collect()
}

fn number<T: std::str::FromStr>(text: &str, what: &str) -> Result<T, DecodeError> {
    text.parse()
        .map_err(|_| DecodeError::Malformed(format!("{what} {text:?} is not a number")))
}

fn hex8(text: &str, what: &str) -> Result<u32, DecodeError> {
    if text.len() != 8 {
        return Err(DecodeError::Malformed(format!(
            "{what} {text:?} is not 8 hex digits"
        )));
    }
    u32::from_str_radix(text, 16)
        .map_err(|_| DecodeError::Malformed(format!("{what} {text:?} is not hex")))
}

fn participant(text: &str, what: &str) -> Result<String, DecodeError> {
    hex8(text, what)?;
    Ok(text.to_string())
}

// ----------------------------------------------------------------------------- encode

struct Envelope<'a> {
    kind: &'a str,
    uid: String,
    time_us: i64,
    stale_us: i64,
    how: &'a str,
    xml: String,
    callsign: Option<String>,
    platform: Option<(&'a str, &'a str)>,
}

fn tak(envelope: Envelope<'_>) -> Vec<u8> {
    let mut detail = Vec::new();
    put_bytes(&mut detail, 1, envelope.xml.as_bytes());
    if let Some(callsign) = &envelope.callsign {
        let mut contact = Vec::new();
        put_bytes(&mut contact, 2, callsign.as_bytes());
        put_bytes(&mut detail, 2, &contact);
    }
    if let Some((platform, version)) = envelope.platform {
        let mut takv = Vec::new();
        put_bytes(&mut takv, 2, platform.as_bytes());
        put_bytes(&mut takv, 4, version.as_bytes());
        put_bytes(&mut detail, 6, &takv);
    }
    let time_ms = (envelope.time_us.max(0) / 1000) as u64;
    let mut event = Vec::new();
    put_bytes(&mut event, 1, envelope.kind.as_bytes());
    put_bytes(&mut event, 5, envelope.uid.as_bytes());
    put_unsigned(&mut event, 6, time_ms);
    put_unsigned(&mut event, 7, time_ms);
    put_unsigned(&mut event, 8, (envelope.stale_us.max(0) / 1000) as u64);
    put_bytes(&mut event, 9, envelope.how.as_bytes());
    put_double(&mut event, 13, CONTROL_ERROR_M);
    put_double(&mut event, 14, CONTROL_ERROR_M);
    put_bytes(&mut event, 15, &detail);
    let mut out = TAK_HEADER.to_vec();
    put_bytes(&mut out, 2, &event);
    out
}

/// The message on the wire: TAK Protocol v1 mesh framing, detail as XML text.
pub fn encode(draft: &Draft) -> Vec<u8> {
    match draft {
        Draft::Record(m) => tak(Envelope {
            kind: "t-x-ar-m",
            uid: format!("{:08x}.m", m.session),
            time_us: m.time_us,
            stale_us: m.end_us.max(m.time_us),
            how: "m-g",
            xml: element(
                "arm",
                &[
                    ("s", format!("{:08x}", m.session)),
                    ("o", m.leader.clone()),
                    ("sd", m.leader.clone()),
                    ("r", m.revision.to_string()),
                    ("me", m.epoch.to_string()),
                    ("ph", m.phase.code().to_string()),
                    ("et", m.end_us.to_string()),
                    ("cap", m.capacity.to_string()),
                    ("mk", m.marker_cap.to_string()),
                    ("ro", m.roster.join(",")),
                    (
                        "dp",
                        m.departed
                            .iter()
                            .map(u32::to_string)
                            .collect::<Vec<_>>()
                            .join(","),
                    ),
                ],
            ),
            callsign: None,
            platform: None,
        }),
        Draft::Join(m) => tak(Envelope {
            kind: "t-x-ar-j",
            uid: format!("{}.j", m.candidate),
            time_us: m.time_us,
            stale_us: m.time_us + 10_000_000,
            how: "m-g",
            xml: element(
                "arj",
                &[
                    ("s", format!("{:08x}", m.session)),
                    ("o", m.candidate.clone()),
                    ("sd", m.candidate.clone()),
                    ("tv", m.tak_version.to_string()),
                    (
                        "xt",
                        m.extensions
                            .iter()
                            .map(u32::to_string)
                            .collect::<Vec<_>>()
                            .join(","),
                    ),
                ],
            ),
            callsign: Some(format!("P-{}", m.candidate)),
            platform: Some(("ar_device", env!("CARGO_PKG_VERSION"))),
        }),
        Draft::Barrier(m) => tak(Envelope {
            kind: "t-x-ar-b",
            uid: format!("{}.b", m.sender),
            time_us: m.time_us,
            stale_us: m.time_us + 10_000_000,
            how: "m-g",
            xml: element(
                "arb",
                &[
                    ("s", format!("{:08x}", m.session)),
                    ("sd", m.sender.clone()),
                    ("r", m.revision.to_string()),
                    ("mi", items(&m.markers)),
                    ("ti", items(&m.tombstones)),
                ],
            ),
            callsign: None,
            platform: None,
        }),
        Draft::CatchupRequest(m) => tak(Envelope {
            kind: "t-x-ar-q",
            uid: format!("{}.q", m.requester),
            time_us: m.time_us,
            stale_us: m.time_us + 10_000_000,
            how: "m-g",
            xml: element(
                "arq",
                &[
                    ("s", format!("{:08x}", m.session)),
                    ("sd", m.requester.clone()),
                    ("mi", items(&m.missing)),
                ],
            ),
            callsign: None,
            platform: None,
        }),
        Draft::Delete(m) => {
            let mut xml = element(
                "link",
                &[
                    ("uid", m.target.to_string()),
                    ("relation", "none".to_string()),
                    ("type", "b-m-p-s-m".to_string()),
                ],
            );
            xml.push_str("<__forcedelete/>");
            xml.push_str(&element(
                "ard",
                &[
                    ("s", format!("{:08x}", m.session)),
                    ("o", m.deleter.to_string()),
                    ("sd", m.deleter.to_string()),
                    ("q", m.sequence.to_string()),
                    ("ti", m.target.to_string()),
                ],
            ));
            tak(Envelope {
                kind: "t-x-d-d",
                uid: format!("{}.d{}", m.deleter, m.sequence),
                time_us: m.time_us,
                stale_us: m.time_us + 10_000_000,
                how: "h-e",
                xml,
                callsign: None,
                platform: None,
            })
        }
    }
}

// ----------------------------------------------------------------------------- decode

const DRAFT_TYPES: [&str; 5] = ["t-x-ar-m", "t-x-ar-j", "t-x-ar-b", "t-x-ar-q", "t-x-d-d"];

/// Decodes a draft kind, or returns [`DecodeError::NotDraft`] for anything else.
pub fn decode(wire: &[u8]) -> Result<Draft, DecodeError> {
    let body = wire
        .strip_prefix(&TAK_HEADER[..])
        .ok_or(DecodeError::NotDraft)?;
    let top = fields(body).ok_or(DecodeError::NotDraft)?;
    let event = top
        .iter()
        .find(|f| f.number == 2)
        .ok_or(DecodeError::NotDraft)?;
    let event = fields(event.bytes).ok_or(DecodeError::NotDraft)?;
    let text = |number: u32| {
        event
            .iter()
            .find(|f| f.number == number)
            .map(|f| String::from_utf8_lossy(f.bytes).into_owned())
            .unwrap_or_default()
    };
    let kind = text(1);
    if !DRAFT_TYPES.contains(&kind.as_str()) {
        return Err(DecodeError::NotDraft);
    }
    let time_us = event
        .iter()
        .find(|f| f.number == 6)
        .map(|f| f.varint as i64 * 1000)
        .unwrap_or(0);
    let detail = event
        .iter()
        .find(|f| f.number == 15)
        .and_then(|f| fields(f.bytes))
        .ok_or_else(|| DecodeError::Malformed("no detail".into()))?;
    let xml = detail
        .iter()
        .find(|f| f.number == 1)
        .map(|f| String::from_utf8_lossy(f.bytes).into_owned())
        .ok_or_else(|| DecodeError::Malformed("no XML detail".into()))?;
    let elements = elements(&xml)?;
    let get = |element: &str, key: &str| -> Result<String, DecodeError> {
        elements
            .get(element)
            .and_then(|attributes| attributes.get(key))
            .cloned()
            .ok_or_else(|| DecodeError::Malformed(format!("{element} lacks {key}")))
    };
    let list = |text: String| -> Result<Vec<u32>, DecodeError> {
        if text.is_empty() {
            return Ok(Vec::new());
        }
        text.split(',').map(|n| number(n, "list entry")).collect()
    };
    match kind.as_str() {
        "t-x-ar-m" => {
            let roster: Vec<String> = match get("arm", "ro")?.as_str() {
                "" => Vec::new(),
                text => text
                    .split(',')
                    .map(|uid| participant(uid, "roster entry"))
                    .collect::<Result<_, _>>()?,
            };
            let phase = match get("arm", "ph")?.as_str() {
                "l" => Phase::Lobby,
                "m" => Phase::Match,
                "c" => Phase::Closed,
                other => return Err(DecodeError::Malformed(format!("phase {other:?}"))),
            };
            Ok(Draft::Record(SessionRecord {
                session: hex8(&get("arm", "s")?, "session")?,
                leader: participant(&get("arm", "o")?, "leader")?,
                revision: number(&get("arm", "r")?, "revision")?,
                epoch: number(&get("arm", "me")?, "epoch")?,
                phase,
                end_us: number(&get("arm", "et")?, "end time")?,
                capacity: number(&get("arm", "cap")?, "capacity")?,
                marker_cap: number(&get("arm", "mk")?, "marker cap")?,
                roster,
                departed: list(get("arm", "dp")?)?,
                time_us,
            }))
        }
        "t-x-ar-j" => Ok(Draft::Join(JoinRequest {
            session: hex8(&get("arj", "s")?, "session")?,
            candidate: participant(&get("arj", "o")?, "candidate")?,
            tak_version: number(&get("arj", "tv")?, "TAK version")?,
            extensions: list(get("arj", "xt")?)?,
            time_us,
        })),
        "t-x-ar-b" => Ok(Draft::Barrier(Barrier {
            session: hex8(&get("arb", "s")?, "session")?,
            sender: participant(&get("arb", "sd")?, "sender")?,
            revision: number(&get("arb", "r")?, "revision")?,
            markers: parse_items(&get("arb", "mi")?)?,
            tombstones: parse_items(&get("arb", "ti")?)?,
            time_us,
        })),
        "t-x-ar-q" => Ok(Draft::CatchupRequest(CatchupRequest {
            session: hex8(&get("arq", "s")?, "session")?,
            requester: participant(&get("arq", "sd")?, "requester")?,
            missing: parse_items(&get("arq", "mi")?)?,
            time_us,
        })),
        _ => {
            if !elements.contains_key("__forcedelete") {
                return Err(DecodeError::Malformed(
                    "delete without __forcedelete".into(),
                ));
            }
            let target = parse_items(&get("ard", "ti")?)?;
            let [target] = target[..] else {
                return Err(DecodeError::Malformed("delete names one target".into()));
            };
            if get("link", "uid")? != target.to_string() {
                return Err(DecodeError::Malformed(
                    "delete link and target differ".into(),
                ));
            }
            Ok(Draft::Delete(MarkerDelete {
                session: hex8(&get("ard", "s")?, "session")?,
                deleter: number(&get("ard", "o")?, "deleter")?,
                sequence: number(&get("ard", "q")?, "delete sequence")?,
                target,
                time_us,
            }))
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn roster(n: usize) -> Vec<String> {
        (0..n)
            .map(|i| format!("{:08x}", 0x9805_1d63u32.wrapping_add(i as u32 * 0x111_1111)))
            .collect()
    }

    fn samples() -> Vec<Draft> {
        let item = |owner, sequence| ItemId { owner, sequence };
        vec![
            Draft::Record(SessionRecord {
                session: 0x8902_5cc1,
                leader: roster(1)[0].clone(),
                revision: 12,
                epoch: 3,
                phase: Phase::Match,
                end_us: 1_794_579_432_000_000,
                capacity: 8,
                marker_cap: 50,
                roster: roster(8),
                departed: vec![5],
                time_us: 1_794_577_632_000_000,
            }),
            Draft::Join(JoinRequest {
                session: 0x8902_5cc1,
                candidate: "a9162e74".into(),
                tak_version: 1,
                extensions: vec![AR_EXTENSION_ID],
                time_us: 1_794_577_632_000_000,
            }),
            Draft::Barrier(Barrier {
                session: 0x8902_5cc1,
                sender: roster(1)[0].clone(),
                revision: 12,
                markers: (0..40).map(|i| item(i % 8, 1000 + i as i64)).collect(),
                tombstones: (0..10).map(|i| item(i % 8, 2000 + i as i64)).collect(),
                time_us: 1_794_577_632_000_000,
            }),
            Draft::CatchupRequest(CatchupRequest {
                session: 0x8902_5cc1,
                requester: "a9162e74".into(),
                missing: vec![item(3, 17), item(0, 4)],
                time_us: 1_794_577_632_000_000,
            }),
            Draft::CatchupRequest(CatchupRequest {
                session: 0x8902_5cc1,
                requester: "a9162e74".into(),
                missing: Vec::new(),
                time_us: 1_794_577_632_000_000,
            }),
            Draft::Delete(MarkerDelete {
                session: 0x8902_5cc1,
                deleter: 2,
                sequence: 7,
                target: item(3, 17),
                time_us: 1_794_577_632_000_000,
            }),
        ]
    }

    #[test]
    fn every_kind_round_trips_and_fits_a_datagram() {
        for draft in samples() {
            let wire = encode(&draft);
            assert!(wire.len() <= 1200, "{} B", wire.len());
            assert_eq!(decode(&wire).unwrap(), draft);
            assert_eq!(encode(&decode(&wire).unwrap()), wire);
        }
    }

    #[test]
    fn reference_codec_messages_are_not_drafts() {
        use cot_codec::{CotCodec, CotMessage, MessageKind, WireForm};
        let codec = CotCodec::new(WireForm::TakLean, 8, 1);
        for kind in MessageKind::ALL {
            let message = CotMessage {
                kind,
                time_us: 1_794_577_632_000_000,
                stale_us: 1_794_577_642_000_000,
                ..CotMessage::default()
            };
            assert_eq!(decode(&codec.encode(&message)), Err(DecodeError::NotDraft));
        }
        assert_eq!(decode(b"<event/>"), Err(DecodeError::NotDraft));
        assert_eq!(
            decode(&[0xBF, 0x01, 0xBF, 0xFF]),
            Err(DecodeError::NotDraft)
        );
    }

    #[test]
    fn corruption_never_panics() {
        for draft in samples() {
            let wire = encode(&draft);
            for index in 0..wire.len() {
                for byte in [0x00, 0x22, 0x2C, 0x3A, 0x7F, 0xFF] {
                    let mut bad = wire.clone();
                    bad[index] = byte;
                    let _ = decode(&bad);
                }
            }
            for cut in 0..wire.len() {
                let _ = decode(&wire[..cut]);
            }
        }
    }

    #[test]
    fn sizes() {
        for draft in samples() {
            println!("kind {}: {} B", draft.kind_code(), encode(&draft).len());
        }
    }
}
