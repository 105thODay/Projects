//! Test-only binding to the ar_sim C++ reference codec and to the C library it calls, for
//! differential testing of `cot_codec`. This crate holds the workspace's only `unsafe`
//! code: the foreign calls. Nothing in the product depends on it.

use std::ffi::{c_char, c_void, CString};

use cot_codec::{CotMessage, MessageKind, Operation, WireForm};

#[repr(C)]
struct RefMessage {
    kind: i32,
    time_us: i64,
    stale_us: i64,
    lat: f64,
    lon: f64,
    hae: f64,
    ce: f64,
    le: f64,
    owner: i32,
    sender: i32,
    entity: i64,
    sequence: i64,
    authority_epoch: i32,
    revision: i32,
    sample_us: i64,
    onset_us: i64,
    deadline_us: i64,
    clock_offset_us: i64,
    clock_epoch: i32,
    clock_uncertainty_us: i32,
    frame_epoch: i32,
    calibration_epoch: i32,
    pose: [f64; 7],
    velocity: [f64; 3],
    operation: i32,
    ack_sequence: i64,
    ack_owner: i32,
    expiry_us: i64,
    value: u64,
    membership_epoch: i32,
    highest_sequence: i64,
    request_owner: i32,
    missing: *const i64,
    missing_count: u64,
    calibration_delta: [f64; 4],
    calibration_source: i32,
    calibration_reference: i32,
}

extern "C" {
    fn ref_codec_new(mode: i32, participants: i32, seed: u64) -> *mut c_void;
    fn ref_codec_free(handle: *mut c_void);
    fn ref_decode(handle: *mut c_void, wire: *const u8, length: u64, out: *mut RefMessage) -> i32;
    fn ref_encode(handle: *mut c_void, message: *const RefMessage, length: *mut u64) -> *const u8;
    fn ref_text(handle: *mut c_void, length: *mut u64) -> *const u8;
    fn ref_strtoll(text: *const c_char, base: i32, used: *mut u64) -> i64;
    fn ref_strtoull(text: *const c_char, base: i32, used: *mut u64) -> u64;
    fn ref_strtod(text: *const c_char, used: *mut u64) -> f64;
    fn ref_scan_iso(text: *const c_char, fields: *mut i32, consumed: *mut i32) -> i32;
    fn ref_fixed(value: f64, digits: i32, text: *mut c_char) -> i32;
}

/// The C++ `CotCodec`.
pub struct RefCodec {
    handle: *mut c_void,
}

impl RefCodec {
    pub fn new(form: WireForm, participants: usize, session_seed: u64) -> Self {
        let participants = i32::try_from(participants).expect("participant count fits an int");
        // SAFETY: the constructor takes plain values and returns an owned handle.
        let handle = unsafe { ref_codec_new(form as i32, participants, session_seed) };
        Self { handle }
    }

    fn bytes(pointer: *const u8, length: u64) -> Vec<u8> {
        if length == 0 {
            return Vec::new();
        }
        // SAFETY: the shim returns a pointer to `length` bytes owned by the handle, valid
        // until the next call on it; they are copied out at once.
        unsafe { std::slice::from_raw_parts(pointer, length as usize) }.to_vec()
    }

    pub fn encode(&self, message: &CotMessage) -> Vec<u8> {
        let raw = to_ref(message);
        let mut length = 0;
        // SAFETY: `raw` and the missing list it points into outlive the call.
        let pointer = unsafe { ref_encode(self.handle, &raw, &mut length) };
        Self::bytes(pointer, length)
    }

    /// The reference's decode: the message, or its error text.
    pub fn decode(&self, wire: &[u8]) -> Result<CotMessage, Vec<u8>> {
        let mut raw = to_ref(&CotMessage::default());
        // SAFETY: the wire slice is valid for its length; `raw` is a valid out-parameter.
        let ok = unsafe { ref_decode(self.handle, wire.as_ptr(), wire.len() as u64, &mut raw) };
        if ok == 2 {
            let mut length = 0;
            // SAFETY: as in `bytes`.
            let pointer = unsafe { ref_text(self.handle, &mut length) };
            panic!(
                "the C++ reference threw: {}",
                String::from_utf8_lossy(&Self::bytes(pointer, length))
            );
        }
        if ok == 0 {
            let mut length = 0;
            // SAFETY: as in `bytes`.
            let pointer = unsafe { ref_text(self.handle, &mut length) };
            return Err(Self::bytes(pointer, length));
        }
        Ok(from_ref(&raw))
    }
}

impl Drop for RefCodec {
    fn drop(&mut self) {
        // SAFETY: the handle came from `ref_codec_new` and is freed once.
        unsafe { ref_codec_free(self.handle) };
    }
}

fn to_ref(message: &CotMessage) -> RefMessage {
    RefMessage {
        kind: message.kind as i32,
        time_us: message.time_us,
        stale_us: message.stale_us,
        lat: message.lat,
        lon: message.lon,
        hae: message.hae,
        ce: message.ce,
        le: message.le,
        owner: message.owner,
        sender: message.sender,
        entity: message.entity,
        sequence: message.sequence,
        authority_epoch: message.authority_epoch,
        revision: message.revision,
        sample_us: message.sample_us,
        onset_us: message.onset_us,
        deadline_us: message.deadline_us,
        clock_offset_us: message.clock_offset_us,
        clock_epoch: message.clock_epoch,
        clock_uncertainty_us: message.clock_uncertainty_us,
        frame_epoch: message.frame_epoch,
        calibration_epoch: message.calibration_epoch,
        pose: message.pose,
        velocity: message.velocity,
        operation: message.operation as i32,
        ack_sequence: message.ack_sequence,
        ack_owner: message.ack_owner,
        expiry_us: message.expiry_us,
        value: message.value,
        membership_epoch: message.membership_epoch,
        highest_sequence: message.highest_sequence,
        request_owner: message.request_owner,
        missing: message.missing.as_ptr(),
        missing_count: message.missing.len() as u64,
        calibration_delta: message.calibration_delta,
        calibration_source: message.calibration_source,
        calibration_reference: message.calibration_reference,
    }
}

fn from_ref(raw: &RefMessage) -> CotMessage {
    let missing = if raw.missing_count == 0 {
        Vec::new()
    } else {
        // SAFETY: the shim points into the handle's last decoded message, still alive.
        unsafe { std::slice::from_raw_parts(raw.missing, raw.missing_count as usize) }.to_vec()
    };
    CotMessage {
        kind: MessageKind::from_code(raw.kind).expect("a decoded kind is valid"),
        time_us: raw.time_us,
        stale_us: raw.stale_us,
        lat: raw.lat,
        lon: raw.lon,
        hae: raw.hae,
        ce: raw.ce,
        le: raw.le,
        owner: raw.owner,
        sender: raw.sender,
        entity: raw.entity,
        sequence: raw.sequence,
        authority_epoch: raw.authority_epoch,
        revision: raw.revision,
        sample_us: raw.sample_us,
        onset_us: raw.onset_us,
        deadline_us: raw.deadline_us,
        clock_offset_us: raw.clock_offset_us,
        clock_epoch: raw.clock_epoch,
        clock_uncertainty_us: raw.clock_uncertainty_us,
        frame_epoch: raw.frame_epoch,
        calibration_epoch: raw.calibration_epoch,
        pose: raw.pose,
        velocity: raw.velocity,
        operation: Operation::from_code(raw.operation).expect("a decoded operation is valid"),
        ack_sequence: raw.ack_sequence,
        ack_owner: raw.ack_owner,
        expiry_us: raw.expiry_us,
        value: raw.value,
        membership_epoch: raw.membership_epoch,
        highest_sequence: raw.highest_sequence,
        request_owner: raw.request_owner,
        missing,
        calibration_delta: raw.calibration_delta,
        calibration_source: raw.calibration_source,
        calibration_reference: raw.calibration_reference,
    }
}

/// The platform C library, as the C++ reference sees it.
pub mod libc {
    use super::*;

    /// The text as a C string: everything before the first NUL.
    fn c_string(text: &[u8]) -> CString {
        CString::new(cot_codec::clib::c_str(text)).expect("no interior NUL")
    }

    pub fn strtoll(text: &[u8], base: u32) -> (i64, usize) {
        let text = c_string(text);
        let mut used = 0;
        // SAFETY: a valid C string and out-parameter.
        let value = unsafe { ref_strtoll(text.as_ptr(), base as i32, &mut used) };
        (value, used as usize)
    }

    pub fn strtoull(text: &[u8], base: u32) -> (u64, usize) {
        let text = c_string(text);
        let mut used = 0;
        // SAFETY: as above.
        let value = unsafe { ref_strtoull(text.as_ptr(), base as i32, &mut used) };
        (value, used as usize)
    }

    pub fn strtod(text: &[u8]) -> (f64, usize) {
        let text = c_string(text);
        let mut used = 0;
        // SAFETY: as above.
        let value = unsafe { ref_strtod(text.as_ptr(), &mut used) };
        (value, used as usize)
    }

    /// The six fields and the bytes consumed when `sscanf` converts all six.
    pub fn scan_iso_fields(text: &[u8]) -> Option<([i32; 6], usize)> {
        let text = c_string(text);
        let mut fields = [0i32; 6];
        let mut consumed = 0;
        // SAFETY: a valid C string, six ints, one int.
        let count = unsafe { ref_scan_iso(text.as_ptr(), fields.as_mut_ptr(), &mut consumed) };
        (count == 6).then_some((fields, consumed as usize))
    }

    /// `snprintf(buffer, 64, "%.*f", digits, value)`.
    pub fn printf_fixed(value: f64, digits: usize) -> String {
        let mut buffer = [0 as c_char; 64];
        // SAFETY: the buffer holds the 64 bytes snprintf is told it has.
        unsafe { ref_fixed(value, digits as i32, buffer.as_mut_ptr()) };
        let bytes: Vec<u8> = buffer
            .iter()
            .take_while(|&&byte| byte != 0)
            .map(|&byte| byte as u8)
            .collect();
        String::from_utf8(bytes).expect("printf writes ASCII")
    }
}
