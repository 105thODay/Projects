//! The message model: every field a message can carry, in semantic form.

/// The five wire forms. `Abstract` builds no message; the simulator charges configured
/// frame costs instead.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
#[repr(i32)]
pub enum WireForm {
    Abstract = 0,
    XmlSpecified = 1,
    XmlLean = 2,
    TakSpecified = 3,
    TakLean = 4,
}

impl WireForm {
    pub const ALL: [WireForm; 5] = [
        WireForm::Abstract,
        WireForm::XmlSpecified,
        WireForm::XmlLean,
        WireForm::TakSpecified,
        WireForm::TakLean,
    ];

    /// The form with the reference's integer code.
    pub fn from_code(code: i32) -> Option<WireForm> {
        WireForm::ALL.into_iter().find(|form| *form as i32 == code)
    }

    /// The compact schema rather than the profile's field names.
    pub fn is_lean(self) -> bool {
        matches!(self, WireForm::XmlLean | WireForm::TakLean)
    }

    /// TAK Protocol version 1 rather than CoT XML.
    pub fn is_tak(self) -> bool {
        matches!(self, WireForm::TakSpecified | WireForm::TakLean)
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash)]
#[repr(i32)]
pub enum MessageKind {
    /// A discrete event: marker create, delete or takeover claim.
    #[default]
    Event = 0,
    /// Replaceable participant pose state.
    State = 1,
    /// Clock, membership, receipt and event summary; current calibration.
    Control = 2,
    /// A recipient lists the events it missed.
    RepairRequest = 3,
    /// Background repair fixture.
    RepairStatus = 4,
    /// A new calibration epoch and the change from the last one.
    Calibration = 5,
}

impl MessageKind {
    pub const ALL: [MessageKind; 6] = [
        MessageKind::Event,
        MessageKind::State,
        MessageKind::Control,
        MessageKind::RepairRequest,
        MessageKind::RepairStatus,
        MessageKind::Calibration,
    ];

    pub fn from_code(code: i32) -> Option<MessageKind> {
        MessageKind::ALL
            .into_iter()
            .find(|kind| *kind as i32 == code)
    }

    pub fn index(self) -> usize {
        self as usize
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash)]
#[repr(i32)]
pub enum Operation {
    #[default]
    Create = 0,
    Delete = 1,
    /// R1 stimulus: a non-owner claims an entity.
    Takeover = 2,
}

impl Operation {
    pub const ALL: [Operation; 3] = [Operation::Create, Operation::Delete, Operation::Takeover];

    pub fn from_code(code: i32) -> Option<Operation> {
        Operation::ALL
            .into_iter()
            .find(|operation| *operation as i32 == code)
    }
}

/// Every field a message can carry. The codec formats them per schema and recovers them
/// exactly at the formatted precision. Times are UTC microseconds since 1970; the mission
/// frame is in meters with the quaternion in w x y z order.
#[derive(Clone, Debug, PartialEq)]
pub struct CotMessage {
    pub kind: MessageKind,
    // envelope
    pub time_us: i64,
    pub stale_us: i64,
    pub lat: f64,
    pub lon: f64,
    pub hae: f64,
    pub ce: f64,
    pub le: f64,
    // identity
    /// Origin owner (participant index).
    pub owner: i32,
    pub sender: i32,
    /// owner * ENTITY_STRIDE + local number; -1 none.
    pub entity: i64,
    /// The owner's event or state sequence.
    pub sequence: i64,
    pub authority_epoch: i32,
    pub revision: i32,
    // timing
    pub sample_us: i64,
    pub onset_us: i64,
    pub deadline_us: i64,
    pub clock_offset_us: i64,
    pub clock_epoch: i32,
    pub clock_uncertainty_us: i32,
    // spatial
    pub frame_epoch: i32,
    pub calibration_epoch: i32,
    pub pose: [f64; 7],
    pub velocity: [f64; 3],
    // transaction
    pub operation: Operation,
    /// Receipt: the newest event applied from `ack_owner`; -1 none.
    pub ack_sequence: i64,
    pub ack_owner: i32,
    pub expiry_us: i64,
    pub value: u64,
    // control and repair
    pub membership_epoch: i32,
    pub highest_sequence: i64,
    /// Repair request: whose events are missing; -1 none.
    pub request_owner: i32,
    pub missing: Vec<i64>,
    // calibration: the mission-frame change since the previous epoch
    /// dx dy dz (m), dyaw (rad).
    pub calibration_delta: [f64; 4],
    /// 0 own far solution, 1 near peer.
    pub calibration_source: i32,
    /// The near peer used; -1 none.
    pub calibration_reference: i32,
}

impl Default for CotMessage {
    fn default() -> Self {
        Self {
            kind: MessageKind::Event,
            time_us: 0,
            stale_us: 0,
            lat: 0.0,
            lon: 0.0,
            hae: 0.0,
            ce: 0.0,
            le: 0.0,
            owner: 0,
            sender: 0,
            entity: -1,
            sequence: 0,
            authority_epoch: 0,
            revision: 0,
            sample_us: 0,
            onset_us: 0,
            deadline_us: 0,
            clock_offset_us: 0,
            clock_epoch: 0,
            clock_uncertainty_us: 0,
            frame_epoch: 0,
            calibration_epoch: 0,
            pose: [0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0],
            velocity: [0.0; 3],
            operation: Operation::Create,
            ack_sequence: -1,
            ack_owner: -1,
            expiry_us: 0,
            value: 0,
            membership_epoch: 0,
            highest_sequence: 0,
            request_owner: -1,
            missing: Vec::new(),
            calibration_delta: [0.0; 4],
            calibration_source: 0,
            calibration_reference: -1,
        }
    }
}

/// Why a wire message did not decode. The bytes are the C++ reference's error text, which
/// can quote raw bytes from the message.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DecodeError(pub Vec<u8>);

impl std::fmt::Display for DecodeError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter.write_str(&String::from_utf8_lossy(&self.0))
    }
}

impl std::error::Error for DecodeError {}
