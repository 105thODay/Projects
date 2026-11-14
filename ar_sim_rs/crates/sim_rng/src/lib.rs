//! The ar_sim random streams. Each stream is an MT19937-64 engine (`std::mt19937_64` in
//! the C++ reference) with its own seed derived from the run seed, so a draw on one stream
//! moves no other. Draws use the raw 64-bit output only: the standard library
//! distributions differ by compiler, so the reference derives every variate itself.

#![forbid(unsafe_code)]

use rand_mt::Mt64;

/// The streams the simulator and the alignment model seed.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Stream {
    Setup,
    Loss,
    Jitter,
    Fault,
    /// Input-interval dither.
    Input,
    /// Alignment drift and marker ranges.
    Align,
    Wifi,
    Motion,
    Positioning,
}

impl Stream {
    pub const ALL: [Stream; 9] = [
        Stream::Setup,
        Stream::Loss,
        Stream::Jitter,
        Stream::Fault,
        Stream::Input,
        Stream::Align,
        Stream::Wifi,
        Stream::Motion,
        Stream::Positioning,
    ];

    /// The name the corpus records.
    pub fn name(self) -> &'static str {
        match self {
            Stream::Setup => "setup",
            Stream::Loss => "loss",
            Stream::Jitter => "jitter",
            Stream::Fault => "fault",
            Stream::Input => "input",
            Stream::Align => "align",
            Stream::Wifi => "wifi",
            Stream::Motion => "motion",
            Stream::Positioning => "positioning",
        }
    }

    /// The engine seed for a run seed, as the reference derives it.
    pub fn seed(self, run_seed: u64) -> u64 {
        match self {
            Stream::Setup => run_seed.wrapping_mul(4).wrapping_add(1),
            Stream::Loss => run_seed.wrapping_mul(4).wrapping_add(2),
            Stream::Jitter => run_seed.wrapping_mul(4).wrapping_add(3),
            Stream::Fault => run_seed.wrapping_mul(4).wrapping_add(4),
            Stream::Input => run_seed ^ 0x9E37_79B9_7F4A_7C15,
            Stream::Align => run_seed ^ 0xA11C_0FFE_E0DD_F00D,
            Stream::Wifi => run_seed ^ 0x8021_1E0C_A0F1_C0DE,
            Stream::Motion => run_seed.wrapping_mul(7919).wrapping_add(17),
            Stream::Positioning => run_seed.wrapping_mul(104_729).wrapping_add(3),
        }
    }
}

/// One stream's engine.
#[derive(Clone)]
pub struct Rng(Mt64);

const TWO_TO_53: f64 = 9_007_199_254_740_992.0;

impl Rng {
    pub fn new(seed: u64) -> Self {
        Self(Mt64::new(seed))
    }

    pub fn for_stream(stream: Stream, run_seed: u64) -> Self {
        Self::new(stream.seed(run_seed))
    }

    /// The engine's next raw output.
    pub fn raw(&mut self) -> u64 {
        self.0.next_u64()
    }

    /// The simulator's `Unit`: 53 bits, in [0, 1).
    pub fn unit(&mut self) -> f64 {
        (self.raw() >> 11) as f64 * (1.0 / TWO_TO_53)
    }

    /// The simulator's `Below`: `unit() * bound`, truncated toward zero.
    pub fn below(&mut self, bound: i64) -> i64 {
        (self.unit() * bound as f64) as i64
    }

    /// The alignment model's `Unit`: 53 bits at bin centers, in (0, 1), so its logarithm is
    /// finite.
    pub fn unit_open(&mut self) -> f64 {
        ((self.raw() >> 11) as f64 + 0.5) * (1.0 / TWO_TO_53)
    }
}
