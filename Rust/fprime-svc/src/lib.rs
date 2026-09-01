//! Rust implementations of F Prime service-component logic (fprime-rs
//! Phase 2). Each module holds the pure protocol logic of one component;
//! the C++ shims (`Svc/<Name>/<Name>Rust.cpp`) keep the autocoded component
//! base, the FW_ASSERT preconditions, event emission, and port plumbing.
//!
//! The oracle for equivalence is each component's existing unit-test suite,
//! which runs unchanged against the Rust-backed component.
#![cfg_attr(not(test), no_std)]

/// Types generated from FPP models by tools/fprime-rs/fpp_to_rust.py.
pub mod generated {
    pub mod com_cfg;
    // Checked in to keep the Phase 14 generator work from rotting: this is the
    // first real framework module the type generator could emit at all, and it
    // is the one that exercises the two capabilities that unblocked it -- an
    // FPP abstract type (`Fw.Buffer`, mirrored by hand in
    // fprime-core/src/fw_buffer.rs) and arrays sized by named constants
    // (`ComQueueComPorts`, `ComQueueBufferPorts`) rather than literals. CI
    // regenerates it and diffs.
    pub mod com_queue;
    pub mod fprime_protocol;
    pub mod rust_demo_glue;
}

pub mod accumulator;
pub mod comqueue;
pub mod deframer;
pub mod framer;
pub mod router_table;
pub mod rust_demo;

/// F Prime protocol constants, mirroring `Svc/FprimeProtocol/FprimeProtocol.fpp`
/// (`FrameHeader { startWord: U32 = 0xdeadbeef, lengthField: U32 }`,
/// `FrameTrailer { crcField: U32 }`). The shims static_assert the C++
/// SERIALIZED_SIZEs and FW_ASSERT the default start word against these.
pub mod protocol {
    use crate::generated::fprime_protocol::{
        SvcFprimeProtocolFrameHeader as FrameHeader, SvcFprimeProtocolFrameTrailer as FrameTrailer,
    };

    pub const START_WORD: u32 = 0xDEAD_BEEF;
    // Phase 4 retrofit: these were hand-written constants; they now come from
    // the fpp-to-rust generated types, derived from the same FPP model the C++
    // autocode uses. The shims still static_assert the C++ SERIALIZED_SIZEs.
    pub const HEADER_SIZE: usize = FrameHeader::SERIALIZED_SIZE;
    pub const TRAILER_SIZE: usize = FrameTrailer::SERIALIZED_SIZE;

    /// The frame CRC: conventional CRC-32 (init all-ones, final complement)
    /// over the given bytes, as `Utils::Hash` computes it.
    pub fn frame_crc(bytes: &[u8]) -> u32 {
        !fprime_utils::crc32::update(0xFFFF_FFFF, bytes)
    }
}
