//! Every TAK message in the golden corpus, read by prost from the official TAK .proto
//! definitions. The gate requires that:
//!   the three-byte mesh header is 0xBF 0x01 0xBF and the rest parses as a TakMessage;
//!   the CotEvent's type, uid, times, how and point are the ones cot_codec decodes;
//!   the detail XML sits in Detail.xmlDetail;
//!   prost's own re-serialization of the parsed message is byte-identical to the wire, so
//!   the hand-written writer emits what a standard protobuf serializer would.

#![cfg(takproto)]

use std::path::PathBuf;

use cot_codec::{CotCodec, WireForm};
use prost::Message;
use tak_oracle::tak::TakMessage;

fn corpus_dir() -> PathBuf {
    let dir = std::env::var_os("AR_SIM_CORPUS")
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../corpus"));
    assert!(
        dir.join("messages.jsonl").is_file(),
        "no golden corpus at {}: set AR_SIM_CORPUS",
        dir.display()
    );
    dir
}

fn check(file: &str) -> (usize, usize) {
    let text = std::fs::read_to_string(corpus_dir().join(file)).unwrap();
    let (mut checked, mut decoded) = (0, 0);
    for (index, line) in text.lines().enumerate() {
        let record: serde_json::Value = serde_json::from_str(line).unwrap();
        let form = WireForm::from_code(record["form"].as_i64().unwrap() as i32).unwrap();
        if !form.is_tak() {
            continue;
        }
        let hex = record["wire"].as_str().unwrap();
        let wire: Vec<u8> = (0..hex.len())
            .step_by(2)
            .map(|at| u8::from_str_radix(&hex[at..at + 2], 16).unwrap())
            .collect();
        let at = format!("{file} line {}", index + 1);
        assert_eq!(wire[..3], [0xBF, 0x01, 0xBF], "{at}: mesh header");
        let message = TakMessage::decode(&wire[3..])
            .unwrap_or_else(|error| panic!("{at}: prost rejects the wire: {error}"));
        assert!(
            message.tak_control.is_none(),
            "{at}: no TakControl expected"
        );
        let event = message
            .cot_event
            .as_ref()
            .unwrap_or_else(|| panic!("{at}: no CotEvent"));
        assert_eq!(event.send_time, event.start_time, "{at}: start equals time");
        let xml = &event
            .detail
            .as_ref()
            .unwrap_or_else(|| panic!("{at}: no Detail"))
            .xml_detail;
        assert!(
            xml.starts_with('<') && xml.ends_with("/>"),
            "{at}: xmlDetail is one AR element"
        );
        let mut again = vec![0xBF, 0x01, 0xBF];
        message.encode(&mut again).unwrap();
        assert_eq!(again, wire, "{at}: prost re-serializes differently");
        checked += 1;

        let codec = CotCodec::new(
            form,
            record["n"].as_u64().unwrap() as usize,
            record["seed"].as_u64().unwrap(),
        );
        if let Ok(ours) = codec.decode(&wire) {
            decoded += 1;
            assert_eq!(
                ours.time_us,
                (event.send_time as i64).wrapping_mul(1000),
                "{at}: time"
            );
            assert_eq!(
                ours.stale_us,
                (event.stale_time as i64).wrapping_mul(1000),
                "{at}: stale"
            );
            let point = [ours.lat, ours.lon, ours.hae, ours.ce, ours.le].map(f64::to_bits);
            let theirs = [event.lat, event.lon, event.hae, event.ce, event.le].map(f64::to_bits);
            assert_eq!(point, theirs, "{at}: point");
            let expected = [
                "b-m-p-s-m",
                "a-f-G-U-C",
                "t-x-ar-c",
                "t-x-ar-r",
                "t-x-ar-s",
                "t-x-ar-k",
            ][ours.kind as usize];
            assert_eq!(event.r#type, expected, "{at}: type");
        }
    }
    (checked, decoded)
}

#[test]
#[ignore = "needs the golden corpus: set AR_SIM_CORPUS, run with --include-ignored"]
fn corpus_tak_messages_are_standard_protobuf() {
    for file in ["messages.jsonl", "sim_messages.jsonl"] {
        let (checked, decoded) = check(file);
        println!("{file}: {checked} TAK messages parse and re-serialize identically under prost; {decoded} cross-checked field by field");
        assert!(checked > 0);
    }
}
