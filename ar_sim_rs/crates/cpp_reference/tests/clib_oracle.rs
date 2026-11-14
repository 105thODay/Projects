//! The `cot_codec::clib` emulation against the C library the C++ reference calls, on
//! generated text and values. On glibc this is the oracle the corpus was made with; on
//! another C library (MSVC's UCRT) a failure here names a behavior that library does not
//! share with glibc, and therefore a wire difference in the C++ reference there.
//!
//! CLIB_ITERS sets the cases per routine (default 200,000).

mod support;

use cot_codec::clib;
use cpp_reference::libc;
use support::{interesting_double, number_text, Rng};

fn iterations() -> usize {
    std::env::var("CLIB_ITERS")
        .ok()
        .and_then(|text| text.parse().ok())
        .unwrap_or(200_000)
}

fn shown(text: &[u8]) -> String {
    format!("{:?}", String::from_utf8_lossy(text))
}

#[test]
fn strtoll_and_strtoull_match() {
    let mut rng = Rng(1);
    for _ in 0..iterations() {
        let text = number_text(&mut rng);
        for base in [10, 16] {
            assert_eq!(
                clib::strtoll(&text, base),
                libc::strtoll(&text, base),
                "strtoll({}, {base})",
                shown(&text)
            );
            assert_eq!(
                clib::strtoull(&text, base),
                libc::strtoull(&text, base),
                "strtoull({}, {base})",
                shown(&text)
            );
        }
    }
}

#[test]
fn strtod_matches() {
    let mut rng = Rng(2);
    for _ in 0..iterations() {
        let text = number_text(&mut rng);
        let (ours, our_end) = clib::strtod(&text);
        let (theirs, their_end) = libc::strtod(&text);
        assert!(
            ours.to_bits() == theirs.to_bits() && our_end == their_end,
            "strtod({}): rust {:016x} after {our_end}, libc {:016x} after {their_end}",
            shown(&text),
            ours.to_bits(),
            theirs.to_bits()
        );
    }
}

#[test]
fn strtod_reads_printf_output_back() {
    let mut rng = Rng(3);
    for _ in 0..iterations() {
        let value = interesting_double(&mut rng);
        let text = libc::printf_fixed(value, rng.below(9));
        assert_eq!(
            clib::strtod(text.as_bytes()).0.to_bits(),
            libc::strtod(text.as_bytes()).0.to_bits(),
            "strtod({text})"
        );
    }
}

#[test]
fn printf_fixed_matches() {
    let mut rng = Rng(4);
    for _ in 0..iterations() {
        let value = interesting_double(&mut rng);
        let digits = rng.below(9);
        assert_eq!(
            clib::printf_fixed(value, digits),
            libc::printf_fixed(value, digits),
            "%.{digits}f of {:016x}",
            value.to_bits()
        );
    }
}

#[test]
fn iso_scan_matches_sscanf() {
    let mut rng = Rng(5);
    let pieces: &[&[u8]] = &[
        b"2026", b"9999", b"0001", b"-999", b"+12", b"10000", b"-", b"+", b"T", b":", b".", b"Z",
        b" ", b"\t", b"\n", b"0", b"5", b"12", b"123", b"-1", b"+-", b"\0", b"x",
    ];
    for _ in 0..iterations() {
        let mut text = Vec::new();
        if rng.chance(1, 2) {
            // mostly well-formed, with one perturbation
            text.extend_from_slice(b"2026-11-14T08:30:59.123456Z");
            let at = rng.below(text.len() + 1);
            let piece = rng.pick(pieces);
            if rng.chance(1, 2) && at < text.len() {
                text.splice(at..at + 1, piece.iter().copied());
            } else {
                text.splice(at..at, piece.iter().copied());
            }
        } else {
            for _ in 0..1 + rng.below(14) {
                text.extend_from_slice(rng.pick(pieces));
            }
        }
        assert_eq!(
            clib::scan_iso_fields(&text),
            libc::scan_iso_fields(&text),
            "sscanf({})",
            shown(&text)
        );
    }
}
