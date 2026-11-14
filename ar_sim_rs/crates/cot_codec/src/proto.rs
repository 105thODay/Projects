//! The hand-written protobuf writer and reader for TAK Protocol version 1 (ruling R2: a
//! port of the C++ reference, with `prost` used only as a test oracle).

pub const WIRE_VARINT: u32 = 0;
pub const WIRE_FIXED64: u32 = 1;
pub const WIRE_BYTES: u32 = 2;
pub const WIRE_FIXED32: u32 = 5;

pub fn put_varint(out: &mut Vec<u8>, mut value: u64) {
    while value >= 0x80 {
        out.push(((value & 0x7F) | 0x80) as u8);
        value >>= 7;
    }
    out.push(value as u8);
}

pub fn put_key(out: &mut Vec<u8>, field: u32, wire: u32) {
    put_varint(out, (u64::from(field) << 3) | u64::from(wire));
}

/// A length-delimited field; empty values are not sent (proto3 defaults).
pub fn put_bytes(out: &mut Vec<u8>, field: u32, bytes: &[u8]) {
    if bytes.is_empty() {
        return;
    }
    put_key(out, field, WIRE_BYTES);
    put_varint(out, bytes.len() as u64);
    out.extend_from_slice(bytes);
}

/// A varint field; zero is not sent.
pub fn put_unsigned(out: &mut Vec<u8>, field: u32, value: u64) {
    if value == 0 {
        return;
    }
    put_key(out, field, WIRE_VARINT);
    put_varint(out, value);
}

/// A fixed64 double field; positive zero is not sent, negative zero is.
pub fn put_double(out: &mut Vec<u8>, field: u32, value: f64) {
    if value.to_bits() == 0 {
        return;
    }
    put_key(out, field, WIRE_FIXED64);
    out.extend_from_slice(&value.to_bits().to_le_bytes());
}

/// One decoded field. As in the reference, `number` and `bytes` are reset for every field,
/// so a field read with an unexpected wire type yields 0 or empty bytes.
#[derive(Default)]
pub struct ProtoField<'a> {
    /// The key's field number, truncated to 32 bits as the reference's `int` cast does.
    pub field: i32,
    pub wire: u32,
    pub number: u64,
    pub bytes: &'a [u8],
}

pub struct ProtoReader<'a> {
    data: &'a [u8],
    pos: usize,
}

impl<'a> ProtoReader<'a> {
    pub fn new(data: &'a [u8]) -> Self {
        Self { data, pos: 0 }
    }

    pub fn at_end(&self) -> bool {
        self.pos >= self.data.len()
    }

    /// Up to ten bytes; a continuation bit on the tenth is malformed.
    fn varint(&mut self) -> Option<u64> {
        let mut value: u64 = 0;
        let mut shift = 0;
        while shift < 64 {
            let byte = *self.data.get(self.pos)?;
            self.pos += 1;
            value |= u64::from(byte & 0x7F).wrapping_shl(shift);
            if byte & 0x80 == 0 {
                return Some(value);
            }
            shift += 7;
        }
        None
    }

    /// The next field, or `None` when the bytes are malformed. Groups are not used by TAK.
    pub fn field(&mut self) -> Option<ProtoField<'a>> {
        let key = self.varint()?;
        let mut field = ProtoField {
            field: (key >> 3) as i32,
            wire: (key & 7) as u32,
            ..ProtoField::default()
        };
        match field.wire {
            WIRE_VARINT => field.number = self.varint()?,
            WIRE_FIXED64 | WIRE_FIXED32 => {
                let size = if field.wire == WIRE_FIXED64 { 8 } else { 4 };
                let bytes = self.data.get(self.pos..self.pos + size)?;
                field.number = bytes
                    .iter()
                    .rev()
                    .fold(0u64, |value, &byte| (value << 8) | u64::from(byte));
                self.pos += size;
            }
            WIRE_BYTES => {
                let length = self.varint()?;
                let remaining = (self.data.len() - self.pos) as u64;
                if remaining < length {
                    return None;
                }
                let length = length as usize;
                field.bytes = &self.data[self.pos..self.pos + length];
                self.pos += length;
            }
            _ => return None,
        }
        Some(field)
    }
}
