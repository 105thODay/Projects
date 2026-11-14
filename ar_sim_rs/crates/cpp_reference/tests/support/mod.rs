//! Shared test support: a deterministic generator and grammar-biased text.

#![allow(dead_code)]

/// splitmix64: small, deterministic, no dependency.
pub struct Rng(pub u64);

impl Rng {
    pub fn next(&mut self) -> u64 {
        self.0 = self.0.wrapping_add(0x9E37_79B9_7F4A_7C15);
        let mut z = self.0;
        z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
        z ^ (z >> 31)
    }

    /// Uniform in 0..bound.
    pub fn below(&mut self, bound: usize) -> usize {
        (self.next() % bound as u64) as usize
    }

    pub fn chance(&mut self, numerator: u64, denominator: u64) -> bool {
        self.next() % denominator < numerator
    }

    pub fn pick<'a, T>(&mut self, items: &'a [T]) -> &'a T {
        &items[self.below(items.len())]
    }
}

/// Pieces of numeric text that exercise the C parsers' corners.
pub const NUMBER_PIECES: &[&str] = &[
    " ",
    "\t",
    "\n",
    "\x0b",
    "\x0c",
    "\r",
    "+",
    "-",
    "0",
    "00",
    "0x",
    "0X",
    "1",
    "9",
    "12345",
    "18446744073709551615",
    "18446744073709551616",
    "9223372036854775807",
    "9223372036854775808",
    "ffffffffffffffff",
    "fFfF",
    "a",
    "g",
    "z",
    ".",
    "..",
    "e",
    "E",
    "e+",
    "e-",
    "e5",
    "e-5",
    "e308",
    "e309",
    "e-324",
    "e-400",
    "e99999999999999999999",
    "p",
    "P",
    "p+",
    "p-",
    "p1",
    "p-1074",
    "p-1075",
    "p1023",
    "p1024",
    "inf",
    "INF",
    "infinity",
    "InFiNiTy",
    "infin",
    "nan",
    "NaN",
    "nan(",
    "nan()",
    "nan(123)",
    "nan(0x1f)",
    "nan(_a9)",
    "nan(08)",
    "nan(0x)",
    "nan(0b101)",
    "nan(0B1)",
    ")",
    "(",
    ",",
    "\0",
    "Z",
    ":",
    "T",
    "_",
    "1.5",
    "0.5",
    "2.5",
    "0.125",
    "1.7976931348623157",
    "4.9406564584124654",
    "2.4703282292062327",
    "0x1.fffffffffffff8",
    "0x1.000000000000008",
    "0x.8",
    "0x1p-1074",
    "000000000000000000000001",
    "7",
    "5",
    "3",
];

pub fn digits(rng: &mut Rng, alphabet: &[u8], count: usize) -> Vec<u8> {
    (0..count).map(|_| *rng.pick(alphabet)).collect()
}

/// Numeric-looking text: grammar pieces, digit runs and the odd stray byte.
pub fn number_text(rng: &mut Rng) -> Vec<u8> {
    let mut out = Vec::new();
    for _ in 0..1 + rng.below(7) {
        match rng.below(10) {
            0..=5 => out.extend_from_slice(rng.pick(NUMBER_PIECES).as_bytes()),
            6 => {
                let count = rng.below(30);
                out.extend(digits(rng, b"0123456789", count));
            }
            7 => {
                let count = rng.below(24);
                out.extend(digits(rng, b"0123456789abcdefABCDEF", count));
            }
            8 => {
                let count = if rng.chance(1, 8) {
                    300 + rng.below(900)
                } else {
                    rng.below(60)
                };
                out.extend(digits(rng, b"0123456789", count));
            }
            _ => out.push(rng.next() as u8),
        }
    }
    out
}

/// A double from a distribution heavy in the formatter's and parser's corners.
pub fn interesting_double(rng: &mut Rng) -> f64 {
    match rng.below(9) {
        0 => f64::from_bits(rng.next()),
        1 => *rng.pick(&[
            0.0,
            -0.0,
            f64::INFINITY,
            f64::NEG_INFINITY,
            f64::NAN,
            -f64::NAN,
            f64::MIN_POSITIVE,
            5e-324,
            f64::MAX,
        ]),
        // exact binary fractions: decimal ties at every precision
        2 => (rng.next() as i64 >> rng.below(60)) as f64 / (1u64 << rng.below(30)) as f64,
        3 => ((rng.next() % 2_000_001) as f64 - 1_000_000.0) / 10f64.powi(rng.below(9) as i32),
        4 => (rng.next() % 1000) as f64 * 0.0005 - 0.25,
        5 => f64::from_bits(rng.next() >> 1) * if rng.chance(1, 2) { 1.0 } else { -1.0 },
        6 => 10f64.powi(rng.below(40) as i32 - 20) * (rng.below(20) as f64 - 10.0),
        7 => (rng.next() as f64) / 7.0,
        _ => (rng.below(2000) as f64 - 1000.0) / 8.0,
    }
}
