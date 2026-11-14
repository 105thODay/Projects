//! Differential fuzzing: the Rust codec against the C++ reference, linked in-process.
//!
//! Seeds are wire messages both codecs agree on: random messages over wide value ranges
//! in all four forms, plus the golden corpus when AR_SIM_CORPUS names it. Each case
//! applies one to four mutations (byte edits, grammar tokens, range deletes and
//! duplicates, splices, and attribute values replaced by generated numeric text; for TAK
//! the detail XML is mutated inside the protobuf and the lengths rebuilt). Both decoders
//! must succeed or fail together, with the same error text, the same fields bit for bit,
//! and the same re-encoding.
//!
//! This is mutation fuzzing on stable Rust: deterministic, seeded, repeatable. DIFF_ITERS
//! sets the cases per form (default 100,000); DIFF_SEED changes the stream.

mod support;

use std::path::PathBuf;

use cot_codec::{CotCodec, CotMessage, MessageKind, Operation, WireForm};
use cpp_reference::RefCodec;
use support::{interesting_double, number_text, Rng};

const FORMS: [WireForm; 4] = [
    WireForm::XmlSpecified,
    WireForm::XmlLean,
    WireForm::TakSpecified,
    WireForm::TakLean,
];
const SETUPS: [(usize, u64); 3] = [(2, 1), (16, 7), (160, 123_456_789)];

fn iterations() -> usize {
    std::env::var("DIFF_ITERS")
        .ok()
        .and_then(|text| text.parse().ok())
        .unwrap_or(100_000)
}

fn random_message(rng: &mut Rng, kind: MessageKind, participants: usize) -> CotMessage {
    let index = |rng: &mut Rng| rng.below(participants) as i32;
    let time = |rng: &mut Rng| match rng.below(4) {
        0 => rng.next() as i64,
        1 => (rng.next() % 4_102_444_800_000_000) as i64, // 1970 to 2100
        2 => -((rng.next() % 62_135_596_800_000_000) as i64), // back to year 1
        _ => 1_794_577_632_000_000 + (rng.next() % 1_000_000_000) as i64,
    };
    let owner = index(rng);
    let mut message = CotMessage {
        kind,
        time_us: time(rng),
        stale_us: time(rng),
        lat: interesting_double(rng),
        lon: interesting_double(rng),
        hae: interesting_double(rng),
        ce: interesting_double(rng),
        le: interesting_double(rng),
        owner,
        sender: index(rng),
        entity: i64::from(owner) * cot_codec::ENTITY_STRIDE + (rng.next() % 1_000_000_000) as i64,
        sequence: (rng.next() >> rng.below(64)) as i64 & 0xFFFF_FFFF_FFFF,
        authority_epoch: rng.next() as i32,
        revision: rng.next() as i32,
        sample_us: time(rng),
        onset_us: time(rng),
        deadline_us: time(rng),
        clock_offset_us: rng.next() as i64 >> rng.below(64),
        clock_epoch: rng.next() as i32,
        clock_uncertainty_us: rng.next() as i32,
        frame_epoch: rng.next() as i32,
        calibration_epoch: rng.next() as i32,
        operation: *rng.pick(&Operation::ALL),
        ack_sequence: if rng.chance(1, 4) {
            -1
        } else {
            (rng.next() >> rng.below(64)) as i64 & 0xFFFF_FFFF_FFFF
        },
        ack_owner: index(rng),
        expiry_us: time(rng),
        value: rng.next(),
        membership_epoch: rng.next() as i32,
        highest_sequence: rng.next() as i64 >> rng.below(64),
        request_owner: index(rng),
        missing: (0..rng.below(8))
            .map(|_| rng.next() as i64 >> rng.below(64))
            .collect(),
        calibration_source: rng.below(3) as i32,
        calibration_reference: if rng.chance(1, 3) { -1 } else { index(rng) },
        ..CotMessage::default()
    };
    for value in message
        .pose
        .iter_mut()
        .chain(&mut message.velocity)
        .chain(&mut message.calibration_delta)
    {
        *value = interesting_double(rng);
    }
    if message.ack_sequence < 0 {
        message.ack_owner = -1;
    }
    message
}

fn corpus_seeds(form: WireForm) -> Vec<(usize, u64, Vec<u8>)> {
    let Some(dir) = std::env::var_os("AR_SIM_CORPUS").map(PathBuf::from) else {
        return Vec::new();
    };
    let mut seeds = Vec::new();
    for file in ["messages.jsonl", "sim_messages.jsonl"] {
        let Ok(text) = std::fs::read_to_string(dir.join(file)) else {
            continue;
        };
        for (index, line) in text.lines().enumerate() {
            if file == "sim_messages.jsonl" && index % 7 != 0 {
                continue;
            }
            let record: serde_json::Value = serde_json::from_str(line).unwrap();
            if record["form"].as_i64() != Some(form as i64) {
                continue;
            }
            let hex = record["wire"].as_str().unwrap();
            let wire = (0..hex.len())
                .step_by(2)
                .map(|at| u8::from_str_radix(&hex[at..at + 2], 16).unwrap())
                .collect();
            seeds.push((
                record["n"].as_u64().unwrap() as usize,
                record["seed"].as_u64().unwrap(),
                wire,
            ));
        }
    }
    seeds
}

// ---------------------------------------------------------------- TAK framing for mutation

fn varint(data: &[u8], pos: &mut usize) -> Option<u64> {
    let mut value = 0u64;
    for shift in (0..64).step_by(7) {
        let byte = *data.get(*pos)?;
        *pos += 1;
        value |= u64::from(byte & 0x7F) << shift;
        if byte & 0x80 == 0 {
            return Some(value);
        }
    }
    None
}

fn put_varint(out: &mut Vec<u8>, mut value: u64) {
    while value >= 0x80 {
        out.push((value as u8 & 0x7F) | 0x80);
        value >>= 7;
    }
    out.push(value as u8);
}

/// A well-formed TAK message split into the CotEvent bytes before and after field 15 and
/// the detail XML inside it.
struct TakParts {
    before: Vec<u8>,
    xml: Vec<u8>,
    after: Vec<u8>,
}

fn split_tak(wire: &[u8]) -> Option<TakParts> {
    let mut pos = 3;
    if varint(wire, &mut pos)? != 0x12 {
        return None;
    }
    let length = varint(wire, &mut pos)? as usize;
    let event = wire.get(pos..pos + length)?;
    let mut at = 0;
    while at < event.len() {
        let start = at;
        let key = varint(event, &mut at)?;
        match key & 7 {
            0 => {
                varint(event, &mut at)?;
            }
            1 => at += 8,
            2 => {
                let size = varint(event, &mut at)? as usize;
                if key >> 3 == 15 {
                    let holder = event.get(at..at + size)?;
                    let mut inner = 0;
                    if varint(holder, &mut inner)? != 0x0A {
                        return None;
                    }
                    let xml_size = varint(holder, &mut inner)? as usize;
                    return Some(TakParts {
                        before: event[..start].to_vec(),
                        xml: holder.get(inner..inner + xml_size)?.to_vec(),
                        after: event[at + size..].to_vec(),
                    });
                }
                at += size;
            }
            _ => return None,
        }
    }
    None
}

fn join_tak(parts: &TakParts) -> Vec<u8> {
    let mut holder = vec![0x0A];
    put_varint(&mut holder, parts.xml.len() as u64);
    holder.extend_from_slice(&parts.xml);
    let mut event = parts.before.clone();
    event.push(0x7A);
    put_varint(&mut event, holder.len() as u64);
    event.extend_from_slice(&holder);
    event.extend_from_slice(&parts.after);
    let mut out = vec![0xBF, 0x01, 0xBF, 0x12];
    put_varint(&mut out, event.len() as u64);
    out.extend_from_slice(&event);
    out
}

// ---------------------------------------------------------------- mutations

const TOKENS: &[&str] = &[
    "&amp;",
    "&lt;",
    "&#65;",
    "&#x41;",
    "&#0;",
    "&#128;",
    "&# 65;",
    "&#x0x41;",
    "&#65\0;",
    "&bogus;",
    "&",
    ";",
    ",",
    ",,",
    "-",
    "+",
    ".",
    " ",
    "\t",
    "\0",
    "\"",
    "'",
    "<",
    ">",
    "/>",
    "</",
    "<!--",
    "-->",
    "<!-- x -->",
    "<x/>",
    "<?xml?>",
    "=",
    "Z",
    "T",
    ":",
    "nan",
    "-nan",
    "inf",
    "0x",
    "e999",
    "e-999",
    "1e5",
    "0x1p3",
    "ANDROID-",
    "clk-",
    "a",
    "b",
    ".c",
    ":0",
    "ffffffffffffffff",
    "18446744073709551616",
    "9223372036854775808",
    "-0",
    "00",
    " 1",
    "1 ",
];

fn mutate_bytes(rng: &mut Rng, data: &mut Vec<u8>, donor: &[u8]) {
    let choice = rng.below(6);
    mutate_with(rng, data, donor, choice);
}

/// Mutation `choice`: 0 byte edit, 1 token insert, 2 range delete, 3 range duplicate,
/// 4 splice from a donor, otherwise an attribute value replaced by generated numeric text.
fn mutate_with(rng: &mut Rng, data: &mut Vec<u8>, donor: &[u8], choice: usize) {
    match choice {
        0 if !data.is_empty() => {
            let at = rng.below(data.len());
            data[at] = rng.next() as u8;
        }
        1 => {
            let at = rng.below(data.len() + 1);
            let token = rng.pick(TOKENS).as_bytes();
            data.splice(at..at, token.iter().copied());
        }
        2 if !data.is_empty() => {
            let at = rng.below(data.len());
            let end = (at + 1 + rng.below(8)).min(data.len());
            data.drain(at..end);
        }
        3 if !data.is_empty() => {
            let at = rng.below(data.len());
            let end = (at + 1 + rng.below(16)).min(data.len());
            let copy = data[at..end].to_vec();
            let to = rng.below(data.len() + 1);
            data.splice(to..to, copy);
        }
        4 if !donor.is_empty() => {
            let at = rng.below(data.len() + 1);
            let from = rng.below(donor.len());
            data.truncate(at);
            data.extend_from_slice(&donor[from..]);
        }
        _ => {
            // replace one attribute value with generated numeric text
            let quotes: Vec<usize> = data
                .iter()
                .enumerate()
                .filter(|(_, &byte)| byte == b'"')
                .map(|(at, _)| at)
                .collect();
            if quotes.len() >= 2 {
                let pair = rng.below(quotes.len() / 2) * 2;
                let (open, close) = (quotes[pair], quotes[pair + 1]);
                let mut text = number_text(rng);
                text.retain(|&byte| byte != b'"');
                if rng.chance(1, 3) {
                    let keep = data[open + 1..close].to_vec();
                    let cut = rng.below(keep.len() + 1);
                    text = [&keep[..cut], &text[..]].concat();
                }
                data.splice(open + 1..close, text);
            }
        }
    }
}

fn mutate(rng: &mut Rng, form: WireForm, seed: &[u8], donor: &[u8]) -> Vec<u8> {
    // A third of the cases change one attribute value only, so most still decode and the
    // field parsers' accepting paths are compared, not only the rejections.
    if rng.chance(1, 3) {
        if form.is_tak() {
            if let Some(mut parts) = split_tak(seed) {
                mutate_with(rng, &mut parts.xml, &[], 5);
                return join_tak(&parts);
            }
        }
        let mut data = seed.to_vec();
        mutate_with(rng, &mut data, &[], 5);
        return data;
    }
    let rounds = 1 + rng.below(4);
    if form.is_tak() && rng.chance(3, 4) {
        if let Some(mut parts) = split_tak(seed) {
            let donor_xml = split_tak(donor).map(|other| other.xml).unwrap_or_default();
            for _ in 0..rounds {
                mutate_bytes(rng, &mut parts.xml, &donor_xml);
            }
            if rng.chance(1, 8) {
                mutate_bytes(rng, &mut parts.before, &[]);
            }
            return join_tak(&parts);
        }
    }
    let mut data = seed.to_vec();
    for _ in 0..rounds {
        mutate_bytes(rng, &mut data, donor);
    }
    data
}

// ---------------------------------------------------------------- comparison

fn fingerprint(message: &CotMessage) -> String {
    let doubles = |values: &[f64]| {
        values
            .iter()
            .map(|value| format!("{:016x}", value.to_bits()))
            .collect::<Vec<_>>()
            .join(",")
    };
    let mut text = format!("{message:?}");
    // Debug prints NaNs alike; append the exact bits of every double.
    text.push_str(&doubles(&[
        message.lat,
        message.lon,
        message.hae,
        message.ce,
        message.le,
    ]));
    text.push_str(&doubles(&message.pose));
    text.push_str(&doubles(&message.velocity));
    text.push_str(&doubles(&message.calibration_delta));
    text
}

struct Outcome {
    accepted: usize,
    rejected: usize,
    reencoded_same: usize,
}

fn compare(rust: &CotCodec, reference: &RefCodec, wire: &[u8]) -> Result<Option<bool>, String> {
    let shown = |bytes: &[u8]| format!("{:?}", String::from_utf8_lossy(bytes));
    match (rust.decode(wire), reference.decode(wire)) {
        (Err(ours), Err(theirs)) if ours.0 == theirs => Ok(None),
        (Ok(ours), Ok(theirs)) => {
            if fingerprint(&ours) != fingerprint(&theirs) {
                return Err(format!(
                    "fields differ\n  rust: {}\n  c++:  {}",
                    fingerprint(&ours),
                    fingerprint(&theirs)
                ));
            }
            let (again, their_again) = (rust.encode(&ours), reference.encode(&theirs));
            if again != their_again {
                return Err(format!(
                    "re-encodings differ\n  rust: {}\n  c++:  {}",
                    shown(&again),
                    shown(&their_again)
                ));
            }
            Ok(Some(again == wire))
        }
        (ours, theirs) => Err(format!(
            "outcomes differ\n  rust: {}\n  c++:  {}",
            ours.map_or_else(|error| format!("error {error}"), |_| "decoded".to_string()),
            theirs.map_or_else(
                |error| format!("error {}", shown(&error)),
                |_| "decoded".to_string()
            )
        )),
    }
}

fn run_form(form: WireForm) {
    let base_seed = std::env::var("DIFF_SEED")
        .ok()
        .and_then(|text| text.parse().ok())
        .unwrap_or(20_261_114u64);
    let mut rng = Rng(base_seed ^ (form as u64).wrapping_mul(0x51_7CC1_B727_220A));
    let codecs: Vec<(CotCodec, RefCodec)> = SETUPS
        .iter()
        .map(|&(participants, seed)| {
            (
                CotCodec::new(form, participants, seed),
                RefCodec::new(form, participants, seed),
            )
        })
        .collect();

    // Seeds: generated messages (encodings must agree first), then the corpus.
    let mut seeds: Vec<(usize, Vec<u8>)> = Vec::new();
    for (setup, (rust, reference)) in codecs.iter().enumerate() {
        for kind in MessageKind::ALL {
            for _ in 0..40 {
                let message = random_message(&mut rng, kind, SETUPS[setup].0);
                let wire = rust.encode(&message);
                assert_eq!(
                    wire,
                    reference.encode(&message),
                    "{form:?} encodings differ for {message:?}"
                );
                compare(rust, reference, &wire)
                    .unwrap_or_else(|why| panic!("{form:?} unmutated seed: {why}"));
                seeds.push((setup, wire));
            }
        }
    }
    for (participants, session, wire) in corpus_seeds(form) {
        if let Some(setup) = SETUPS
            .iter()
            .position(|&setup| setup == (participants, session))
        {
            seeds.push((setup, wire));
        }
    }

    let mut outcome = Outcome {
        accepted: 0,
        rejected: 0,
        reencoded_same: 0,
    };
    let mut failures = Vec::new();
    for case in 0..iterations() {
        let (setup, seed) = &seeds[rng.below(seeds.len())];
        let donor = &seeds[rng.below(seeds.len())].1;
        let wire = mutate(&mut rng, form, seed, donor);
        let (rust, reference) = &codecs[*setup];
        let checked = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
            compare(rust, reference, &wire)
        }));
        let Ok(result) = checked else {
            panic!(
                "{form:?} case {case}: the reference threw on {:?}\n  rust says {:?}",
                String::from_utf8_lossy(&wire),
                rust.decode(&wire).map(|_| ())
            );
        };
        match result {
            Ok(None) => outcome.rejected += 1,
            Ok(Some(same)) => {
                outcome.accepted += 1;
                outcome.reencoded_same += usize::from(same);
            }
            Err(why) => {
                if failures.len() < 10 {
                    failures.push(format!(
                        "case {case}: {why}\n  wire: {:?}",
                        String::from_utf8_lossy(&wire)
                    ));
                }
            }
        }
    }
    println!(
        "{form:?}: {} seeds, {} mutated cases: {} rejected by both, {} decoded alike ({} re-encode to the same bytes), {} disagreements",
        seeds.len(),
        iterations(),
        outcome.rejected,
        outcome.accepted,
        outcome.reencoded_same,
        failures.len()
    );
    assert!(failures.is_empty(), "{form:?}:\n{}", failures.join("\n"));
}

#[test]
fn xml_specified_agrees() {
    run_form(FORMS[0]);
}

#[test]
fn xml_lean_agrees() {
    run_form(FORMS[1]);
}

#[test]
fn tak_specified_agrees() {
    run_form(FORMS[2]);
}

#[test]
fn tak_lean_agrees() {
    run_form(FORMS[3]);
}
