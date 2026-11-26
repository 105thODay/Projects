//! A stand-in for a frame-paced workload (a renderer, or a tracker), to study CPU contention.
//!
//! Each frame is due at k / hz. The thread sleeps until then, then burns a fixed amount of CPU
//! time, measured on the thread's own CPU clock, so preemption stretches the frame instead of
//! hiding inside it. Reported per run: how late frames started, how long they took on the wall
//! clock, and how many finished after the next frame was due.

use std::time::{Duration, Instant};

use cpu_time::ThreadTime;

use crate::Args;

fn percentile(sorted: &[f64], q: f64) -> f64 {
    if sorted.is_empty() {
        return f64::NAN;
    }
    sorted[((sorted.len() - 1) as f64 * q).round() as usize]
}

pub fn run(args: &Args) -> Result<(), String> {
    let hz: f64 = args.number("hz", 90.0)?;
    let work_ms: f64 = args.number("work_ms", 8.0)?;
    let duration_s: f64 = args.number("duration_s", 30.0)?;
    let label = args.text("label", "render");
    args.finish()?;
    if hz <= 0.0 || work_ms <= 0.0 || work_ms >= 1000.0 / hz {
        return Err("need hz > 0 and 0 < work_ms < the frame period".into());
    }
    let period = Duration::from_secs_f64(1.0 / hz);
    let work = Duration::from_secs_f64(work_ms / 1000.0);
    let start = Instant::now();
    let frames = (duration_s * hz) as u32;
    let mut late = Vec::with_capacity(frames as usize);
    let mut took = Vec::with_capacity(frames as usize);
    let mut missed = 0u32;
    let mut sink = 0u64;
    for frame in 0..frames {
        let due = start + period * frame;
        let now = Instant::now();
        if due > now {
            std::thread::sleep(due - now);
        }
        let begun = Instant::now();
        let cpu = ThreadTime::now();
        while cpu.elapsed() < work {
            for i in 0..2000u64 {
                sink = sink.wrapping_mul(6_364_136_223_846_793_005).wrapping_add(i);
            }
        }
        let done = Instant::now();
        late.push((begun - due.min(begun)).as_secs_f64() * 1e3);
        took.push((done - begun).as_secs_f64() * 1e3);
        if done > due + period {
            missed += 1;
        }
    }
    std::hint::black_box(sink);
    late.sort_by(f64::total_cmp);
    took.sort_by(f64::total_cmp);
    println!(
        "{}",
        serde_json::json!({
            "what": "render",
            "label": label,
            "hz": hz,
            "work_ms": work_ms,
            "frames": frames,
            "missed": missed,
            "start_late_ms": { "p50": percentile(&late, 0.5), "p99": percentile(&late, 0.99), "max": percentile(&late, 1.0) },
            "frame_ms": { "p50": percentile(&took, 0.5), "p99": percentile(&took, 0.99), "max": percentile(&took, 1.0) },
        })
    );
    Ok(())
}
