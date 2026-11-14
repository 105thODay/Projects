//! Protobuf types prost generates from the TAK Protocol v1 .proto files, used only to
//! check `cot_codec`'s hand-written TAK writer and reader (ruling R2).

#[cfg(takproto)]
pub mod tak {
    include!(concat!(
        env!("OUT_DIR"),
        "/atakmap.commoncommo.protobuf.v1.rs"
    ));
}

/// Whether the oracle was built (the .proto files were found).
pub const AVAILABLE: bool = cfg!(takproto);
