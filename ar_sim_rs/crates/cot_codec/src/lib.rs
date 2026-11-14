//! Encoded-wire CoT for the Networked AR Renderer: CoT XML 2.0 and TAK Protocol version 1,
//! in the profile's field names or a compact schema.
//!
//! This crate is the Rust port of the ar_sim C++ codec (`cot_codec.cpp`). The port changes
//! no result: for every message it produces the same bytes, and for every wire message it
//! decodes the same fields or reports the same error text. The golden corpus the C++
//! reference writes (`ar_sim corpus <dir>`) gates that claim; see `tests/corpus.rs`.
//!
//! The reference calls C library routines whose exact behavior shows on the wire (how
//! `strtod` reads a number, how `printf` spells NaN). The [`clib`] module reproduces the
//! glibc 2.39 behavior the corpus was made with, so this crate behaves the same on every
//! platform.

#![forbid(unsafe_code)]

pub mod clib;
mod codec;
mod message;
mod proto;
pub mod xml;

pub use codec::{iso_time, parse_iso_time, CotCodec, ENTITY_STRIDE};
pub use message::{CotMessage, DecodeError, MessageKind, Operation, WireForm};
