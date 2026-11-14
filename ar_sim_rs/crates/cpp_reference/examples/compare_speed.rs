//! Encode and decode speed, Rust against the C++ reference, in one process on the same
//! messages: the codec self-test's representative messages (`ar_sim codec`), 2,000
//! variants per wire form and kind, best of 25 passes. Decode is timed alone (the C++
//! self-test subtracts a mean encode instead). The C++ figures include the shim's copy of
//! each message and wire across the C boundary, a few tens of nanoseconds.
//!
//! cargo run --release -p cpp_reference --example compare_speed

use std::hint::black_box;
use std::time::Instant;

use cot_codec::{CotCodec, CotMessage, MessageKind, Operation, WireForm, ENTITY_STRIDE};
use cpp_reference::RefCodec;

const PARTICIPANTS: usize = 16;
const ROUNDS: usize = 2000;
const PASSES: usize = 25;

/// The self-test's `Sample` (codec_selftest.cpp).
fn sample(kind: MessageKind, variant: i64) -> CotMessage {
    let base = 1_794_577_632_000_000 + variant * 11_111;
    let wiggle = 0.001 * (variant % 97) as f64;
    let mut m = CotMessage {
        kind,
        time_us: base,
        stale_us: base + 10_000_000,
        owner: 3,
        sender: 3,
        clock_epoch: 1,
        clock_uncertainty_us: 1000,
        frame_epoch: 1,
        ce: 9_999_999.0,
        le: 9_999_999.0,
        ..CotMessage::default()
    };
    match kind {
        MessageKind::Event => {
            m.entity = 3 * ENTITY_STRIDE + 41 + variant;
            m.sequence = 41 + variant;
            m.authority_epoch = 1;
            m.revision = 1;
            m.sample_us = base - 1200;
            m.onset_us = base - 11_100;
            m.deadline_us = m.onset_us + 100_000;
            m.calibration_epoch = 4;
            m.pose[3] = 0.923_879_5;
            m.pose[4] = 0.0;
            m.pose[5] = 0.0;
            m.pose[6] = 0.382_683_4 + wiggle * 0.01;
            m.operation = Operation::Create;
            m.expiry_us = m.onset_us + 30_000_000;
            m.value = 734_981 + variant as u64;
            m.stale_us = m.expiry_us;
            m.lat = 35.012_481_1 + wiggle * 1e-4;
            m.lon = -116.456_610_2;
            m.hae = 713.3;
            m.ce = 1.5;
            m.le = 2.0;
        }
        MessageKind::State => {
            m.entity = 3 * ENTITY_STRIDE;
            m.sequence = 900 + variant;
            m.authority_epoch = 1;
            m.revision = (900 + variant) as i32;
            m.sample_us = base;
            m.calibration_epoch = 4;
            m.pose = [
                12.3456 + wiggle,
                -7.6543,
                1.6123,
                0.965_925_8,
                0.012_345_7,
                -0.003_456_7,
                0.258_819_0,
            ];
            m.velocity = [1.234, -0.567, 0.012];
            m.stale_us = base + 2_000_000;
            m.lat = 35.012_276_6;
            m.lon = -116.456_653_5 + wiggle * 1e-4;
            m.hae = 713.9;
            m.ce = 1.5;
            m.le = 2.0;
        }
        MessageKind::Control => {
            m.highest_sequence = 41 + variant;
            m.clock_offset_us = -213;
            m.membership_epoch = 1;
            m.ack_owner = 7;
            m.ack_sequence = 18 + variant;
            m.calibration_epoch = 4;
            m.calibration_delta = [0.0712, -0.0333 + wiggle, 0.0041, 0.001_234];
        }
        MessageKind::RepairRequest => {
            m.request_owner = 9;
            m.missing = vec![17 + variant, 18 + variant, 21 + variant];
        }
        MessageKind::RepairStatus => {
            m.highest_sequence = 41 + variant;
        }
        MessageKind::Calibration => {
            m.sample_us = base;
            m.calibration_epoch = 5;
            m.calibration_source = 1;
            m.calibration_reference = 11;
            m.calibration_delta = [0.0812 + wiggle, -0.0333, 0.0041, 0.001_834];
        }
    }
    m
}

/// Best mean microseconds per call over the passes.
fn best<F: FnMut()>(mut pass: F) -> f64 {
    (0..PASSES)
        .map(|_| {
            let start = Instant::now();
            pass();
            start.elapsed().as_secs_f64() * 1e6 / ROUNDS as f64
        })
        .fold(f64::INFINITY, f64::min)
}

fn main() {
    println!("{PARTICIPANTS} participants, {ROUNDS} messages per form and kind, best of {PASSES} passes; microseconds per message");
    println!(
        "{:<14} {:<15} {:>7} {:>9} {:>9} {:>7} {:>9} {:>9} {:>7}",
        "form", "kind", "bytes", "enc c++", "enc rust", "ratio", "dec c++", "dec rust", "ratio"
    );
    let forms = [
        WireForm::XmlSpecified,
        WireForm::XmlLean,
        WireForm::TakSpecified,
        WireForm::TakLean,
    ];
    let mut totals = [0.0f64; 4];
    for form in forms {
        let rust = CotCodec::new(form, PARTICIPANTS, 1);
        let reference = RefCodec::new(form, PARTICIPANTS, 1);
        for kind in MessageKind::ALL {
            let messages: Vec<CotMessage> = (0..ROUNDS as i64)
                .map(|variant| sample(kind, variant))
                .collect();
            let wires: Vec<Vec<u8>> = messages
                .iter()
                .map(|message| rust.encode(message))
                .collect();
            for (message, wire) in messages.iter().zip(&wires) {
                assert_eq!(&reference.encode(message), wire, "encodings differ");
                let decoded = rust.decode(wire).expect("the sample decodes");
                assert_eq!(&rust.encode(&decoded), wire, "the sample round-trips");
            }
            let encode_cpp = best(|| {
                messages
                    .iter()
                    .for_each(|message| drop(black_box(reference.encode(black_box(message)))))
            });
            let encode_rust = best(|| {
                messages
                    .iter()
                    .for_each(|message| drop(black_box(rust.encode(black_box(message)))))
            });
            let decode_cpp = best(|| {
                wires
                    .iter()
                    .for_each(|wire| drop(black_box(reference.decode(black_box(wire)))))
            });
            let decode_rust = best(|| {
                wires
                    .iter()
                    .for_each(|wire| drop(black_box(rust.decode(black_box(wire)))))
            });
            totals[0] += encode_cpp;
            totals[1] += encode_rust;
            totals[2] += decode_cpp;
            totals[3] += decode_rust;
            println!(
                "{:<14} {:<15} {:>7} {:>9.2} {:>9.2} {:>7.2} {:>9.2} {:>9.2} {:>7.2}",
                format!("{form:?}"),
                format!("{kind:?}"),
                wires[0].len(),
                encode_cpp,
                encode_rust,
                encode_rust / encode_cpp,
                decode_cpp,
                decode_rust,
                decode_rust / decode_cpp
            );
        }
    }
    println!(
        "all 24: encode rust/c++ = {:.2}, decode rust/c++ = {:.2} (ratio of summed means; below 1 is faster)",
        totals[1] / totals[0],
        totals[3] / totals[2]
    );
}
