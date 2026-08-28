//! The framing logic of `Svc/FprimeFramer/FprimeFramer.cpp`
//! (`dataIn_handler`): build an F Prime frame around a payload, obtaining the
//! frame storage through a caller-supplied allocator — across the FFI, that
//! allocator is the component's `bufferAllocate` output port (`Fw.BufferGet`,
//! a synchronous returning port call made from inside Rust logic; the
//! Phase 2 pattern this component exists to prove).
//!
//! The C++ shim keeps the FW_ASSERT preconditions, events, and port sends.

use crate::protocol::{frame_crc, HEADER_SIZE, START_WORD, TRAILER_SIZE};

/// The allocator returned no buffer, or one smaller than the frame
/// (`NoBufferAvailable` on the C++ side; the shim owns deallocation of a
/// too-small buffer).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct NoBuffer;

/// Frame `payload` into storage obtained from `alloc`, mirroring the C++:
/// header (start word + payload length), payload bytes, then a CRC32 trailer
/// over header + payload. Returns the number of frame bytes written.
///
/// `alloc` is called exactly once, with the full frame size; returning `None`
/// or a too-small slice yields `NoBuffer` (matching the C++ invalid /
/// smaller-than-requested checks). The C++ preconditions (payload length fits
/// the U32 length field) are FW_ASSERTed by the shim before entry.
pub fn frame<'a>(
    payload: &[u8],
    alloc: impl FnOnce(u64) -> Option<&'a mut [u8]>,
) -> Result<usize, NoBuffer> {
    let frame_size = HEADER_SIZE + payload.len() + TRAILER_SIZE;
    let storage = match alloc(frame_size as u64) {
        Some(storage) if storage.len() >= frame_size => storage,
        _ => return Err(NoBuffer),
    };

    storage[0..4].copy_from_slice(&START_WORD.to_be_bytes());
    storage[4..8].copy_from_slice(&(payload.len() as u32).to_be_bytes());
    storage[HEADER_SIZE..HEADER_SIZE + payload.len()].copy_from_slice(payload);
    let crc = frame_crc(&storage[..HEADER_SIZE + payload.len()]);
    storage[HEADER_SIZE + payload.len()..frame_size].copy_from_slice(&crc.to_be_bytes());
    Ok(frame_size)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::deframer::{deframe, Outcome};

    #[test]
    fn frame_then_deframe_roundtrip() {
        let payload = [0x00u8, 0x01, 0xCA, 0xFE];
        let mut storage = [0u8; 64];
        let written = frame(&payload, |size| {
            assert_eq!(size, (HEADER_SIZE + payload.len() + TRAILER_SIZE) as u64);
            Some(&mut storage[..])
        })
        .unwrap();
        assert_eq!(written, HEADER_SIZE + payload.len() + TRAILER_SIZE);

        let result = deframe(&storage[..written]);
        assert_eq!(result.outcome, Outcome::Ok);
        assert_eq!(result.descriptor, Some(1));
        assert_eq!(&storage[HEADER_SIZE..HEADER_SIZE + payload.len()], &payload);
    }

    #[test]
    fn no_buffer_from_allocator() {
        assert_eq!(frame(&[1, 2, 3], |_| None), Err(NoBuffer));
    }

    #[test]
    fn short_buffer_from_allocator() {
        let mut small = [0u8; 4];
        assert_eq!(frame(&[1, 2, 3], |_| Some(&mut small[..])), Err(NoBuffer));
    }

    #[test]
    fn oversized_allocation_is_used_and_trimmed_by_count() {
        let mut big = [0xEEu8; 128];
        let written = frame(&[7u8; 3], |_| Some(&mut big[..])).unwrap();
        assert_eq!(written, HEADER_SIZE + 3 + TRAILER_SIZE);
        // Bytes beyond the frame are untouched
        assert_eq!(big[written], 0xEE);
    }

    #[test]
    fn empty_payload_frames() {
        let mut storage = [0u8; 16];
        let written = frame(&[], |_| Some(&mut storage[..])).unwrap();
        assert_eq!(written, HEADER_SIZE + TRAILER_SIZE);
        assert_eq!(deframe(&storage[..written]).outcome, Outcome::Ok);
    }
}
