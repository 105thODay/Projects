//! Encode and decode speed on the device, per wire form and message kind.
//!
//! The same messages and method as `cpp_reference/examples/compare_speed.rs` (Rust side):
//! 2,000 variants per form and kind, mean microseconds per message, best of 25 passes. The
//! median pass is reported too, since a phone's governor and big.LITTLE scheduling make the
//! spread worth seeing. A sustained phase then decodes TAK lean state messages for a fixed
//! time and reports the rate in each second, to expose thermal throttling.

use std::hint::black_box;
use std::time::{Duration, Instant};

use cot_codec::{CotCodec, CotMessage, MessageKind, WireForm};

use crate::samples::sample;
use crate::Args;

const PARTICIPANTS: usize = 16;

struct Timing {
    best: f64,
    median: f64,
}

/// Mean microseconds per message for each pass; best and median.
fn time_passes<F: FnMut()>(passes: usize, rounds: usize, mut pass: F) -> Timing {
    pass(); // warm caches and the CPU governor
    let mut means: Vec<f64> = (0..passes)
        .map(|_| {
            let start = Instant::now();
            pass();
            start.elapsed().as_secs_f64() * 1e6 / rounds as f64
        })
        .collect();
    means.sort_by(f64::total_cmp);
    Timing {
        best: means[0],
        median: means[means.len() / 2],
    }
}

pub fn run(args: &Args) -> Result<(), String> {
    let rounds: usize = args.number("rounds", 2000)?;
    let passes: usize = args.number("passes", 25)?;
    let sustain_s: u64 = args.number("sustain_s", 20)?;
    args.finish()?;
    if rounds == 0 || passes == 0 {
        return Err("rounds and passes must be positive".into());
    }

    let forms = [
        WireForm::XmlSpecified,
        WireForm::XmlLean,
        WireForm::TakSpecified,
        WireForm::TakLean,
    ];
    let mut totals = [0.0f64; 2];
    for form in forms {
        let codec = CotCodec::new(form, PARTICIPANTS, 1);
        for kind in MessageKind::ALL {
            let messages: Vec<CotMessage> = (0..rounds as i64)
                .map(|variant| sample(kind, variant))
                .collect();
            let wires: Vec<Vec<u8>> = messages
                .iter()
                .map(|message| codec.encode(message))
                .collect();
            // Every sample must round-trip before its timing means anything.
            for wire in &wires {
                let decoded = codec.decode(wire).map_err(|error| {
                    format!("{form:?} {kind:?} sample does not decode: {error}")
                })?;
                if &codec.encode(&decoded) != wire {
                    return Err(format!("{form:?} {kind:?} sample does not round-trip"));
                }
            }
            let encode = time_passes(passes, rounds, || {
                messages
                    .iter()
                    .for_each(|message| drop(black_box(codec.encode(black_box(message)))))
            });
            let decode = time_passes(passes, rounds, || {
                wires
                    .iter()
                    .for_each(|wire| drop(black_box(codec.decode(black_box(wire)))))
            });
            totals[0] += encode.best;
            totals[1] += decode.best;
            let max_bytes = wires.iter().map(Vec::len).max().unwrap_or(0);
            println!(
                "{}",
                serde_json::json!({
                    "what": "bench",
                    "form": format!("{form:?}"),
                    "kind": format!("{kind:?}"),
                    "bytes": max_bytes,
                    "encode_us_best": encode.best,
                    "encode_us_median": encode.median,
                    "decode_us_best": decode.best,
                    "decode_us_median": decode.median,
                })
            );
        }
    }
    println!(
        "{}",
        serde_json::json!({
            "what": "bench_total",
            "rounds": rounds,
            "passes": passes,
            "encode_us_sum_best": totals[0],
            "decode_us_sum_best": totals[1],
        })
    );

    if sustain_s > 0 {
        let codec = CotCodec::new(WireForm::TakLean, PARTICIPANTS, 1);
        let wires: Vec<Vec<u8>> = (0..rounds as i64)
            .map(|variant| codec.encode(&sample(MessageKind::State, variant)))
            .collect();
        let mut per_second = Vec::new();
        let start = Instant::now();
        for second in 1..=sustain_s {
            let end = start + Duration::from_secs(second);
            let mut count: u64 = 0;
            while Instant::now() < end {
                for wire in &wires[..64.min(wires.len())] {
                    drop(black_box(codec.decode(black_box(wire))));
                }
                count += 64.min(wires.len()) as u64;
            }
            per_second.push(count);
        }
        println!(
            "{}",
            serde_json::json!({
                "what": "sustain",
                "form": "TakLean",
                "kind": "State",
                "decodes_per_second": per_second,
            })
        );
    }
    Ok(())
}
