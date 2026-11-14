//! Exact emulation of the C library routines the C++ reference codec calls.
//!
//! The reference is glibc 2.39 in the C locale, the library that produced the golden corpus.
//! Every routine works on bytes and stops at the first NUL, as the C routine does when it is
//! handed `std::string::c_str()`. Each function documents the glibc behavior it reproduces;
//! the `clib_oracle` test compares them against the live glibc routines.

/// The bytes a C string routine sees: everything before the first NUL.
pub fn c_str(bytes: &[u8]) -> &[u8] {
    match bytes.iter().position(|&byte| byte == 0) {
        Some(end) => &bytes[..end],
        None => bytes,
    }
}

/// `isspace` in the C locale.
pub fn is_c_space(byte: u8) -> bool {
    matches!(byte, b' ' | b'\t' | b'\n' | 0x0b | 0x0c | b'\r')
}

fn at(text: &[u8], index: usize) -> u8 {
    text.get(index).copied().unwrap_or(0)
}

fn digit_value(byte: u8) -> u32 {
    match byte {
        b'0'..=b'9' => u32::from(byte - b'0'),
        b'a'..=b'z' => u32::from(byte - b'a') + 10,
        b'A'..=b'Z' => u32::from(byte - b'A') + 10,
        _ => u32::MAX,
    }
}

/// The shared core of `strtoll` and `strtoull` (glibc `____strtol_l_internal`).
struct Integer {
    negative: bool,
    magnitude: u64,
    overflow: bool,
    /// Bytes consumed; 0 when no conversion was performed.
    end: usize,
}

fn strto_integer(text: &[u8], base: u32) -> Integer {
    let s = c_str(text);
    let mut pos = 0;
    let mut result = Integer {
        negative: false,
        magnitude: 0,
        overflow: false,
        end: 0,
    };
    while pos < s.len() && is_c_space(s[pos]) {
        pos += 1;
    }
    if pos >= s.len() {
        return result;
    }
    if s[pos] == b'-' {
        result.negative = true;
        pos += 1;
    } else if s[pos] == b'+' {
        pos += 1;
    }
    let mut base = base;
    if at(s, pos) == b'0' {
        if (base == 0 || base == 16) && at(s, pos + 1).eq_ignore_ascii_case(&b'X') {
            pos += 2;
            base = 16;
        } else if base == 0 {
            base = 8;
        }
    } else if base == 0 {
        base = 10;
    }
    let save = pos;
    let cutoff = u64::MAX / u64::from(base);
    let cutlim = u64::MAX % u64::from(base);
    while pos < s.len() {
        let digit = digit_value(s[pos]);
        if digit >= base {
            break;
        }
        let digit = u64::from(digit);
        if result.magnitude > cutoff || (result.magnitude == cutoff && digit > cutlim) {
            result.overflow = true;
        } else {
            result.magnitude = result.magnitude * u64::from(base) + digit;
        }
        pos += 1;
    }
    if pos == save {
        // "0x" with no hex digit after it converts the 0 and ends at the x.
        result.negative = false;
        result.magnitude = 0;
        result.overflow = false;
        if save >= 2 && s[save - 1].eq_ignore_ascii_case(&b'X') && s[save - 2] == b'0' {
            result.end = save - 1;
        }
        return result;
    }
    result.end = pos;
    result
}

/// `strtoll(text, &end, base)`: the value and the number of bytes consumed (0 for no
/// conversion). Out-of-range values saturate to `i64::MIN` or `i64::MAX`.
pub fn strtoll(text: &[u8], base: u32) -> (i64, usize) {
    let parsed = strto_integer(text, base);
    let limit = if parsed.negative {
        1u64 << 63
    } else {
        i64::MAX as u64
    };
    if parsed.overflow || parsed.magnitude > limit {
        let value = if parsed.negative { i64::MIN } else { i64::MAX };
        return (value, parsed.end);
    }
    let value = if parsed.negative {
        (parsed.magnitude as i64).wrapping_neg()
    } else {
        parsed.magnitude as i64
    };
    (value, parsed.end)
}

/// `strtoull(text, &end, base)`: a leading '-' negates modulo 2^64; overflow gives `u64::MAX`.
pub fn strtoull(text: &[u8], base: u32) -> (u64, usize) {
    let parsed = strto_integer(text, base);
    if parsed.overflow {
        return (u64::MAX, parsed.end);
    }
    let value = if parsed.negative {
        parsed.magnitude.wrapping_neg()
    } else {
        parsed.magnitude
    };
    (value, parsed.end)
}

/// glibc's quiet NaN, `NAN`.
const QUIET_NAN_BITS: u64 = 0x7ff8_0000_0000_0000;

/// More significant decimal digits than any double needs for correct rounding (767), so
/// keeping this many plus a sticky digit rounds exactly as the full string would.
const KEEP_DECIMAL_DIGITS: usize = 800;

/// `strtod(text, &end)` (glibc `____strtod_l_internal`): the value and the bytes consumed
/// (0 for no conversion). Decimal and hexadecimal forms round correctly to nearest-even;
/// `inf`, `infinity`, `nan` and `nan(n-char-sequence)` are accepted without regard to case.
pub fn strtod(text: &[u8]) -> (f64, usize) {
    let s = c_str(text);
    let mut pos = 0;
    while pos < s.len() && is_c_space(s[pos]) {
        pos += 1;
    }
    let mut negative = false;
    if at(s, pos) == b'-' {
        negative = true;
        pos += 1;
    } else if at(s, pos) == b'+' {
        pos += 1;
    }
    let signed = |value: f64| if negative { -value } else { value };
    let c = at(s, pos);
    let point_then_digit = c == b'.' && at(s, pos + 1).is_ascii_digit();
    if !point_then_digit && !c.is_ascii_digit() {
        if s[pos..].len() >= 3 && s[pos..pos + 3].eq_ignore_ascii_case(b"inf") {
            let end =
                if s[pos + 3..].len() >= 5 && s[pos + 3..pos + 8].eq_ignore_ascii_case(b"inity") {
                    pos + 8
                } else {
                    pos + 3
                };
            return (signed(f64::INFINITY), end);
        }
        if s[pos..].len() >= 3 && s[pos..pos + 3].eq_ignore_ascii_case(b"nan") {
            let (value, end) = nan_tail(s, pos + 3);
            return (signed(value), end);
        }
        return (0.0, 0);
    }
    let hex = c == b'0' && at(s, pos + 1).eq_ignore_ascii_case(&b'x');
    if hex {
        pos += 2;
    }
    let is_digit = |byte: u8| {
        byte.is_ascii_digit() || (hex && matches!(byte.to_ascii_lowercase(), b'a'..=b'f'))
    };
    let start_of_digits = pos;
    while at(s, pos) == b'0' {
        pos += 1;
    }
    let c = at(s, pos);
    let lower = c.to_ascii_lowercase();
    let accepted = is_digit(c)
        || (c == b'.' && (!hex || pos != start_of_digits || is_digit(at(s, pos + 1))))
        || (hex && pos != start_of_digits && lower == b'p')
        || (!hex && lower == b'e');
    if !accepted {
        let end = if pos != start_of_digits {
            pos
        } else if hex {
            pos - 1
        } else {
            0
        };
        return (signed(0.0), end);
    }

    // Significant digits (leading zeros dropped) and how many of them precede the point.
    let mut digits: Vec<u8> = Vec::new();
    let mut sticky = false;
    let mut int_digits: i64 = 0;
    let mut leading_fraction_zeros: i64 = 0;
    let push = |digit: u8, digits: &mut Vec<u8>, sticky: &mut bool| {
        if digits.len() < KEEP_DECIMAL_DIGITS.max(64) {
            digits.push(digit);
        } else if digit != 0 {
            *sticky = true;
        }
    };
    while is_digit(at(s, pos)) {
        push(digit_value(at(s, pos)) as u8, &mut digits, &mut sticky);
        int_digits += 1;
        pos += 1;
    }
    if at(s, pos) == b'.' {
        pos += 1;
        while is_digit(at(s, pos)) {
            let digit = digit_value(at(s, pos)) as u8;
            if digits.is_empty() && digit == 0 {
                leading_fraction_zeros += 1;
            } else {
                push(digit, &mut digits, &mut sticky);
            }
            pos += 1;
        }
    }
    let mut exponent: i64 = 0;
    let marker = if hex { b'p' } else { b'e' };
    if at(s, pos).to_ascii_lowercase() == marker {
        let exponent_start = pos;
        pos += 1;
        let mut exponent_negative = false;
        if at(s, pos) == b'-' {
            exponent_negative = true;
            pos += 1;
        } else if at(s, pos) == b'+' {
            pos += 1;
        }
        if at(s, pos).is_ascii_digit() {
            while at(s, pos).is_ascii_digit() {
                let digit = i64::from(at(s, pos) - b'0');
                exponent = exponent
                    .saturating_mul(10)
                    .saturating_add(digit)
                    .min(1 << 40);
                pos += 1;
            }
            if exponent_negative {
                exponent = -exponent;
            }
        } else {
            pos = exponent_start;
        }
    }
    let end = pos;
    if digits.iter().all(|&digit| digit == 0) && !sticky {
        return (signed(0.0), end);
    }
    let value = if hex {
        hex_value(
            &digits,
            sticky,
            int_digits,
            leading_fraction_zeros,
            exponent,
        )
    } else {
        decimal_value(
            &digits,
            sticky,
            int_digits,
            leading_fraction_zeros,
            exponent,
        )
    };
    (signed(value), end)
}

/// The text after "nan": an optional "(n-char-sequence)" whose value, read as `strtoull`
/// base 0 reads it, becomes the payload (glibc `__strtod_nan` and `SET_NAN_PAYLOAD`).
fn nan_tail(s: &[u8], pos: usize) -> (f64, usize) {
    if at(s, pos) != b'(' {
        return (f64::from_bits(QUIET_NAN_BITS), pos);
    }
    let start = pos + 1;
    let mut cursor = start;
    while matches!(at(s, cursor), b'0'..=b'9' | b'A'..=b'Z' | b'a'..=b'z' | b'_') {
        cursor += 1;
    }
    if at(s, cursor) != b')' {
        return (f64::from_bits(QUIET_NAN_BITS), pos);
    }
    let mut bits = QUIET_NAN_BITS;
    let (payload, used) = strtoull(&s[start..cursor], 0);
    if start + used == cursor {
        let mantissa = payload & ((1u64 << 51) - 1);
        if mantissa != 0 {
            bits |= mantissa;
        }
    }
    (f64::from_bits(bits), cursor + 1)
}

/// A decimal significand, correctly rounded by the standard library's parser once the
/// exponent is reduced to a range it represents exactly.
fn decimal_value(
    digits: &[u8],
    sticky: bool,
    int_digits: i64,
    leading_fraction_zeros: i64,
    exponent: i64,
) -> f64 {
    // value = 0.d1 d2 d3 ... * 10^point
    let point = int_digits - leading_fraction_zeros + exponent;
    if point > 400 {
        return f64::INFINITY;
    }
    if point < -400 {
        return 0.0;
    }
    let mut text = String::with_capacity(digits.len() + 16);
    text.push_str("0.");
    for &digit in digits {
        text.push(char::from(b'0' + digit));
    }
    if sticky {
        text.push('1');
    }
    text.push('e');
    text.push_str(&point.to_string());
    text.parse::<f64>().unwrap_or(0.0)
}

/// A hexadecimal significand rounded to nearest-even, through subnormals and overflow.
fn hex_value(
    digits: &[u8],
    sticky: bool,
    int_digits: i64,
    leading_fraction_zeros: i64,
    exponent: i64,
) -> f64 {
    // Up to 16 hex digits (64 bits) into the mantissa; the rest only stick.
    let mut mantissa: u64 = 0;
    let mut taken: i64 = 0;
    let mut sticky = sticky;
    for &digit in digits {
        if taken < 16 {
            mantissa = (mantissa << 4) | u64::from(digit);
            taken += 1;
        } else if digit != 0 {
            sticky = true;
        }
    }
    // value = mantissa * 2^(4 * (int_digits - leading_fraction_zeros - taken) + exponent)
    let scale = 4 * (int_digits - leading_fraction_zeros - taken) + exponent;
    let lead = i64::from(mantissa.leading_zeros());
    let mantissa = mantissa << lead;
    // value = mantissa * 2^(scale - lead), mantissa in [2^63, 2^64): unbiased exponent below.
    let unbiased = scale - lead + 63;
    if unbiased > 1023 {
        return f64::INFINITY;
    }
    let wide = u128::from(mantissa) << 64;
    let (shift, base) = if unbiased >= -1022 {
        (11 + 64, ((unbiased + 1022) as u64) << 52)
    } else {
        (11 + 64 + (-1022 - unbiased), 0)
    };
    if shift > 128 {
        return 0.0;
    }
    let shift = shift as u32;
    let quotient = if shift == 128 { 0 } else { wide >> shift };
    let round_bit = (wide >> (shift - 1)) & 1 == 1;
    let below = (wide & ((1u128 << (shift - 1)) - 1)) != 0 || sticky;
    let mut quotient = quotient as u64;
    if round_bit && (below || quotient & 1 == 1) {
        quotient += 1;
    }
    f64::from_bits(base + quotient)
}

/// `atof(text)`: `strtod` without the end pointer.
pub fn atof(text: &[u8]) -> f64 {
    strtod(text).0
}

/// `snprintf(buffer, 64, "%.*f", digits, value)`: glibc spells NaN "nan" or "-nan" and
/// infinity "inf" or "-inf", and the 64-byte buffer keeps at most 63 characters.
pub fn printf_fixed(value: f64, digits: usize) -> String {
    let mut text = if value.is_nan() {
        if value.is_sign_negative() {
            "-nan".to_string()
        } else {
            "nan".to_string()
        }
    } else if value.is_infinite() {
        if value < 0.0 {
            "-inf".to_string()
        } else {
            "inf".to_string()
        }
    } else {
        format!("{value:.digits$}")
    };
    text.truncate(63);
    text
}

/// `sscanf(text, "%4d-%2d-%2dT%2d:%2d:%2d%n", ...)` on the NUL-truncated text: the six
/// fields and the bytes consumed when all six convert, `None` otherwise. Each `%d` skips
/// leading white space and accepts a sign, which counts toward its width.
pub fn scan_iso_fields(text: &[u8]) -> Option<([i32; 6], usize)> {
    const WIDTHS: [u32; 6] = [4, 2, 2, 2, 2, 2];
    const SEPARATORS: [u8; 5] = *b"--T::";
    let s = c_str(text);
    let mut fields = [0i32; 6];
    let mut pos = 0;
    for index in 0..6 {
        while pos < s.len() && is_c_space(s[pos]) {
            pos += 1;
        }
        if pos >= s.len() {
            return None;
        }
        let mut width = WIDTHS[index];
        let mut negative = false;
        if s[pos] == b'-' || s[pos] == b'+' {
            negative = s[pos] == b'-';
            pos += 1;
            width -= 1;
        }
        let digits_start = pos;
        let mut value: i32 = 0;
        while width > 0 && pos < s.len() && s[pos].is_ascii_digit() {
            value = value * 10 + i32::from(s[pos] - b'0');
            pos += 1;
            width -= 1;
        }
        if pos == digits_start {
            return None;
        }
        fields[index] = if negative { -value } else { value };
        if index < 5 {
            if pos >= s.len() || s[pos] != SEPARATORS[index] {
                return None;
            }
            pos += 1;
        }
    }
    Some((fields, pos))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn integers_follow_glibc() {
        assert_eq!(strtoull(b"-1", 16), (u64::MAX, 2));
        assert_eq!(strtoll(b"0x7fffffffffffffffff", 16), (i64::MAX, 20));
        assert_eq!(strtoll(b" -0x10", 16), (-16, 6));
        assert_eq!(strtoll(b"0x", 16), (0, 1));
        assert_eq!(strtoll(b"0xg", 16), (0, 1));
        assert_eq!(strtoll(b"-9223372036854775808", 10), (i64::MIN, 20));
        assert_eq!(strtoll(b"-9223372036854775809", 10), (i64::MIN, 20));
        assert_eq!(strtoll(b"+", 10), (0, 0));
        assert_eq!(strtoll(b"\0abc", 10), (0, 0));
        assert_eq!(strtoull(b"18446744073709551616", 10), (u64::MAX, 20));
    }

    #[test]
    fn doubles_follow_glibc() {
        assert_eq!(strtod(b"0x1.fffffffffffff8p0").0, 2.0);
        assert_eq!(strtod(b"0x"), (0.0, 1));
        assert_eq!(strtod(b"nan").0.to_bits(), 0x7ff8_0000_0000_0000);
        assert_eq!(strtod(b"-nan").0.to_bits(), 0xfff8_0000_0000_0000);
        assert_eq!(strtod(b"infinityx"), (f64::INFINITY, 8));
        assert_eq!(strtod(b"infinit"), (f64::INFINITY, 3));
        assert_eq!(strtod(b"5e"), (5.0, 1));
        assert_eq!(strtod(b"5e+"), (5.0, 1));
        assert_eq!(strtod(b"16."), (16.0, 3));
        assert_eq!(strtod(b".e1"), (0.0, 0));
        assert_eq!(strtod(b"0x.p1"), (0.0, 1));
        assert_eq!(strtod(b"0x1p-1074").0.to_bits(), 1);
        assert_eq!(strtod(b"0x1p-1075").0.to_bits(), 0);
        assert_eq!(strtod(b"0x1.8p-1075").0.to_bits(), 1);
        assert_eq!(strtod(b"1e-400").0, 0.0);
        assert_eq!(strtod(b"1e400").0, f64::INFINITY);
    }

    #[test]
    fn fixed_matches_glibc_spelling() {
        assert_eq!(printf_fixed(f64::NAN, 3), "nan");
        assert_eq!(printf_fixed(-f64::NAN, 3), "-nan");
        assert_eq!(printf_fixed(f64::NEG_INFINITY, 3), "-inf");
        assert_eq!(printf_fixed(-0.0, 1), "-0.0");
        assert_eq!(printf_fixed(0.125, 2), "0.12");
        assert_eq!(printf_fixed(1e300, 7).len(), 63);
    }

    #[test]
    fn iso_scan_follows_sscanf() {
        assert_eq!(
            scan_iso_fields(b"2026-11-14T08:00:00.0Z"),
            Some(([2026, 11, 14, 8, 0, 0], 19))
        );
        assert_eq!(
            scan_iso_fields(b" -999-+1-\t2T 3:-4:+5Z"),
            Some(([-999, 1, 2, 3, -4, 5], 20))
        );
        assert_eq!(scan_iso_fields(b"10000-01-01T00:00:00Z"), None);
    }
}
