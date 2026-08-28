//! Rust implementations of F Prime utility libraries, behaviorally identical
//! to their C++ originals (which remain in-tree behind a CMake switch):
//!
//! - [`circular`] — the byte engine of `Utils/Types/CircularBuffer.cpp`
//! - [`crc32`]    — `Utils/Hash/Crc32/Crc32.cpp` (`crc32_ieee802_3_update`)
//! - [`checksum`] — `CFDP/Checksum/Checksum.cpp` (CFDP 32-bit checksum)
//!
//! The oracle for equivalence is the existing C++ unit-test suites, which run
//! unchanged against these implementations through thin C++ shims.
#![cfg_attr(not(test), no_std)]

pub mod checksum;
pub mod circular;
pub mod crc32;
