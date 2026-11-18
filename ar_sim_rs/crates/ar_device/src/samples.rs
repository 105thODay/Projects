//! Representative messages: the codec self-test's `Sample` (codec_selftest.cpp), exactly as
//! `cpp_reference/examples/compare_speed.rs` builds them, so phone timings compare directly
//! with the desktop figures.

use cot_codec::{CotMessage, MessageKind, Operation, ENTITY_STRIDE};

/// The self-test's `Sample` (codec_selftest.cpp).
pub fn sample(kind: MessageKind, variant: i64) -> CotMessage {
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
