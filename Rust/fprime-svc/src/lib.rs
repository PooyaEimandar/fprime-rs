//! Rust implementations of F Prime service-component logic (fprime-rs
//! Phase 2). Each module holds the pure protocol logic of one component;
//! the C++ shims (`Svc/<Name>/<Name>Rust.cpp`) keep the autocoded component
//! base, the FW_ASSERT preconditions, event emission, and port plumbing.
//!
//! The oracle for equivalence is each component's existing unit-test suite,
//! which runs unchanged against the Rust-backed component.
#![cfg_attr(not(test), no_std)]

pub mod deframer;
pub mod framer;

/// F Prime protocol constants, mirroring `Svc/FprimeProtocol/FprimeProtocol.fpp`
/// (`FrameHeader { startWord: U32 = 0xdeadbeef, lengthField: U32 }`,
/// `FrameTrailer { crcField: U32 }`). The shims static_assert the C++
/// SERIALIZED_SIZEs and FW_ASSERT the default start word against these.
pub mod protocol {
    pub const START_WORD: u32 = 0xDEAD_BEEF;
    pub const HEADER_SIZE: usize = 8;
    pub const TRAILER_SIZE: usize = 4;

    /// The frame CRC: conventional CRC-32 (init all-ones, final complement)
    /// over the given bytes, as `Utils::Hash` computes it.
    pub fn frame_crc(bytes: &[u8]) -> u32 {
        !fprime_utils::crc32::update(0xFFFF_FFFF, bytes)
    }
}
