//! The golden-corpus gate, run on the device: the same checks as
//! `cot_codec/tests/corpus.rs`, so a phone either reproduces the reference's bytes, fields
//! and error text for every record, or names the first records that differ.
//!
//! Each file may be plain (`messages.jsonl`) or gzip-compressed (`messages.jsonl.gz`).

use std::collections::HashMap;
use std::fmt::Write as _;
use std::io::Read;
use std::path::Path;
use std::time::Instant;

use cot_codec::{CotCodec, CotMessage, MessageKind, Operation, WireForm};
use serde_json::Value;

/// A corpus file's text, from `<name>` or `<name>.gz`.
fn read_corpus(dir: &Path, name: &str) -> Result<String, String> {
    let plain = dir.join(name);
    if plain.is_file() {
        return std::fs::read_to_string(&plain)
            .map_err(|error| format!("{}: {error}", plain.display()));
    }
    let packed = dir.join(format!("{name}.gz"));
    let file =
        std::fs::File::open(&packed).map_err(|error| format!("{}: {error}", packed.display()))?;
    let mut text = String::new();
    flate2::read::GzDecoder::new(file)
        .read_to_string(&mut text)
        .map_err(|error| format!("{}: {error}", packed.display()))?;
    Ok(text)
}

fn hex_bytes(text: &str) -> Vec<u8> {
    (0..text.len())
        .step_by(2)
        .map(|index| u8::from_str_radix(&text[index..index + 2], 16).unwrap())
        .collect()
}

/// Readable text for a failure report: lossy UTF-8, cut to 240 characters.
fn shown(bytes: &[u8]) -> String {
    let text = String::from_utf8_lossy(bytes);
    let mut out: String = text.chars().take(240).collect();
    if text.chars().count() > 240 {
        let _ = write!(out, "... ({} bytes)", bytes.len());
    }
    out
}

fn bits(value: &Value) -> f64 {
    f64::from_bits(u64::from_str_radix(value.as_str().unwrap(), 16).unwrap())
}

fn bits_into(value: &Value, targets: &mut [f64]) {
    for (target, item) in targets.iter_mut().zip(value.as_array().unwrap()) {
        *target = bits(item);
    }
}

fn int(value: &Value, key: &str) -> i64 {
    value[key]
        .as_i64()
        .unwrap_or_else(|| panic!("{key} is not an integer"))
}

fn small(value: &Value, key: &str) -> i32 {
    i32::try_from(int(value, key)).unwrap()
}

/// A message as the reference's `WriteMessage` records it.
fn message(value: &Value) -> CotMessage {
    let mut message = CotMessage {
        kind: MessageKind::from_code(small(value, "kind")).unwrap(),
        time_us: int(value, "time_us"),
        stale_us: int(value, "stale_us"),
        lat: bits(&value["lat"]),
        lon: bits(&value["lon"]),
        hae: bits(&value["hae"]),
        ce: bits(&value["ce"]),
        le: bits(&value["le"]),
        owner: small(value, "owner"),
        sender: small(value, "sender"),
        entity: int(value, "entity"),
        sequence: int(value, "sequence"),
        authority_epoch: small(value, "authority_epoch"),
        revision: small(value, "revision"),
        sample_us: int(value, "sample_us"),
        onset_us: int(value, "onset_us"),
        deadline_us: int(value, "deadline_us"),
        clock_offset_us: int(value, "clock_offset_us"),
        clock_epoch: small(value, "clock_epoch"),
        clock_uncertainty_us: small(value, "clock_uncertainty_us"),
        frame_epoch: small(value, "frame_epoch"),
        calibration_epoch: small(value, "calibration_epoch"),
        operation: Operation::from_code(small(value, "operation")).unwrap(),
        ack_sequence: int(value, "ack_sequence"),
        ack_owner: small(value, "ack_owner"),
        expiry_us: int(value, "expiry_us"),
        value: value["value"].as_u64().unwrap(),
        membership_epoch: small(value, "membership_epoch"),
        highest_sequence: int(value, "highest_sequence"),
        request_owner: small(value, "request_owner"),
        missing: value["missing"]
            .as_array()
            .unwrap()
            .iter()
            .map(|item| item.as_i64().unwrap())
            .collect(),
        calibration_source: small(value, "calibration_source"),
        calibration_reference: small(value, "calibration_reference"),
        ..CotMessage::default()
    };
    bits_into(&value["pose"], &mut message.pose);
    bits_into(&value["velocity"], &mut message.velocity);
    bits_into(&value["calibration_delta"], &mut message.calibration_delta);
    message
}

/// Every field, doubles as bit patterns, so two messages compare bit for bit.
fn fingerprint(message: &CotMessage) -> String {
    let doubles = |values: &[f64]| {
        values
            .iter()
            .map(|value| format!("{:016x}", value.to_bits()))
            .collect::<Vec<_>>()
            .join(",")
    };
    format!(
        "{:?} {} {} [{}] {} {} {} {} {} {} {} {} {} {} {} {} {} {} [{}] [{}] {:?} {} {} {} {} {} {} {} {:?} [{}] {} {}",
        message.kind,
        message.time_us,
        message.stale_us,
        doubles(&[message.lat, message.lon, message.hae, message.ce, message.le]),
        message.owner,
        message.sender,
        message.entity,
        message.sequence,
        message.authority_epoch,
        message.revision,
        message.sample_us,
        message.onset_us,
        message.deadline_us,
        message.clock_offset_us,
        message.clock_epoch,
        message.clock_uncertainty_us,
        message.frame_epoch,
        message.calibration_epoch,
        doubles(&message.pose),
        doubles(&message.velocity),
        message.operation,
        message.ack_sequence,
        message.ack_owner,
        message.expiry_us,
        message.value,
        message.membership_epoch,
        message.highest_sequence,
        message.request_owner,
        message.missing,
        doubles(&message.calibration_delta),
        message.calibration_source,
        message.calibration_reference
    )
}

#[derive(Default)]
struct Codecs(HashMap<(i64, i64, u64), CotCodec>);

impl Codecs {
    fn get(&mut self, record: &Value) -> &CotCodec {
        let key = (
            int(record, "form"),
            int(record, "n"),
            record["seed"].as_u64().unwrap(),
        );
        self.0.entry(key).or_insert_with(|| {
            let form = WireForm::from_code(key.0 as i32).unwrap();
            CotCodec::new(form, key.1 as usize, key.2)
        })
    }
}

#[derive(Default)]
struct Tally {
    records: usize,
    decoded: usize,
    rejected: usize,
    failures: Vec<String>,
}

impl Tally {
    fn fail(&mut self, line: usize, what: String) {
        if self.failures.len() < 20 {
            self.failures.push(format!("line {line}: {what}"));
        } else if self.failures.len() == 20 {
            self.failures
                .push("(further failures not listed)".to_string());
        }
    }
}

fn check_messages(text: &str) -> Tally {
    let mut codecs = Codecs::default();
    let mut tally = Tally::default();
    for (index, line) in text.lines().enumerate() {
        let line_number = index + 1;
        let record: Value = serde_json::from_str(line).unwrap();
        let codec = codecs.get(&record).clone();
        let original = message(&record["msg"]);
        let wire = hex_bytes(record["wire"].as_str().unwrap());
        tally.records += 1;
        let encoded = codec.encode(&original);
        if encoded != wire {
            tally.fail(
                line_number,
                format!(
                    "encoding differs\n  rust: {}\n  c++:  {}",
                    shown(&encoded),
                    shown(&wire)
                ),
            );
            continue;
        }
        match (codec.decode(&wire), record["ok"].as_bool().unwrap()) {
            (Ok(decoded), true) => {
                tally.decoded += 1;
                let expected = message(&record["decoded"]);
                if fingerprint(&decoded) != fingerprint(&expected) {
                    tally.fail(
                        line_number,
                        format!(
                            "decoded fields differ\n  rust: {}\n  c++:  {}",
                            fingerprint(&decoded),
                            fingerprint(&expected)
                        ),
                    );
                }
                let same = codec.encode(&decoded) == wire;
                if same != record["reencoded_same"].as_bool().unwrap() {
                    tally.fail(
                        line_number,
                        format!("re-encoding sameness differs (rust {same})"),
                    );
                }
            }
            (Err(error), false) => {
                tally.rejected += 1;
                let expected = hex_bytes(record["error"].as_str().unwrap());
                if error.0 != expected {
                    tally.fail(
                        line_number,
                        format!(
                            "error differs\n  rust: {}\n  c++:  {}",
                            shown(&error.0),
                            shown(&expected)
                        ),
                    );
                }
            }
            (Ok(_), false) => tally.fail(
                line_number,
                "rust decodes what the reference rejects".to_string(),
            ),
            (Err(error), true) => tally.fail(
                line_number,
                format!("rust rejects what the reference decodes: {error}"),
            ),
        }
    }
    tally
}

fn check_flips(text: &str) -> (Tally, [usize; 3]) {
    let mut codecs = Codecs::default();
    let mut tally = Tally::default();
    let mut outcomes = [0usize; 3];
    for (index, line) in text.lines().enumerate() {
        let record: Value = serde_json::from_str(line).unwrap();
        let codec = codecs.get(&record).clone();
        let wire = hex_bytes(record["wire"].as_str().unwrap());
        for (position, flip) in record["flips"].as_array().unwrap().iter().enumerate() {
            tally.records += 1;
            let expected_code = flip[0].as_u64().unwrap() as usize;
            let expected_data = hex_bytes(flip[1].as_str().unwrap());
            let mut bad = wire.clone();
            bad[position] ^= 0x5A;
            let (code, data) = match codec.decode(&bad) {
                Err(error) => (0, error.0),
                Ok(back) => {
                    let again = codec.encode(&back);
                    if again == wire {
                        (1, Vec::new())
                    } else {
                        (2, again)
                    }
                }
            };
            outcomes[code] += 1;
            if code != expected_code || data != expected_data {
                tally.fail(
                    index + 1,
                    format!(
                        "byte {position}: rust {code} {:?}, c++ {expected_code} {:?}",
                        shown(&data),
                        shown(&expected_data)
                    ),
                );
            }
        }
    }
    (tally, outcomes)
}

pub fn run(dir: &Path) -> Result<(), String> {
    let mut failed = false;
    for name in ["messages.jsonl", "sim_messages.jsonl", "flips.jsonl"] {
        let text = read_corpus(dir, name)?;
        let start = Instant::now();
        let (tally, outcomes) = if name == "flips.jsonl" {
            check_flips(&text)
        } else {
            (check_messages(&text), [0; 3])
        };
        let seconds = start.elapsed().as_secs_f64();
        failed |= !tally.failures.is_empty();
        println!(
            "{}",
            serde_json::json!({
                "what": "gate",
                "file": name,
                "records": tally.records,
                "decoded": tally.decoded,
                "rejected": tally.rejected,
                "flip_outcomes": outcomes,
                "failures": tally.failures.len(),
                "first_failures": tally.failures,
                "seconds": seconds,
                "us_per_record": if tally.records > 0 { seconds * 1e6 / tally.records as f64 } else { 0.0 },
            })
        );
    }
    if failed {
        Err("the device differs from the reference: see first_failures".into())
    } else {
        Ok(())
    }
}
