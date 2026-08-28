//! The frame-validation logic of `Svc/FprimeDeframer/FprimeDeframer.cpp`
//! (`dataIn_handler`), as a pure function over the frame bytes.
//!
//! The C++ shim maps the outcome to the component's events and port calls:
//! every drop outcome triggers one WARNING_HI event and returns the buffer;
//! `payload_too_short` maps to the WARNING_LO event the C++ emits BEFORE
//! trailer validation (so it can accompany a later `BadCrc`); APID validity
//! (`ComCfg::Apid::isValid`, config-defined) stays on the C++ side, which is
//! why the raw descriptor is returned rather than a validated APID.

use fprime_config::FwPacketDescriptorType;

use crate::protocol::{frame_crc, HEADER_SIZE, START_WORD, TRAILER_SIZE};

/// Mirrors the dataIn_handler decision, in C++ evaluation order.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum Outcome {
    /// Frame is valid: strip header/trailer and emit the payload
    Ok = 0,
    /// Buffer cannot hold header + trailer (`InvalidBufferReceived`)
    BufferTooShort = 1,
    /// Start word mismatch (`InvalidStartWord`)
    BadStartWord = 2,
    /// Buffer size does not match header length field (`InvalidLengthReceived`)
    BadLength = 3,
    /// Trailer CRC mismatch (`InvalidChecksum`)
    BadCrc = 4,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Deframed {
    pub outcome: Outcome,
    /// Payload too short to carry a packet descriptor (`PayloadTooShort`,
    /// WARNING_LO). Meaningful for `Ok` and `BadCrc` — the outcomes that get
    /// past the length check, matching where the C++ emits the event.
    pub payload_too_short: bool,
    /// The raw packet descriptor read from the payload head, when present.
    /// The shim applies `ComCfg::Apid::isValid` and sets the frame context.
    pub descriptor: Option<FwPacketDescriptorType>,
}

fn be_u32(bytes: &[u8]) -> u32 {
    let mut value: u32 = 0;
    for &byte in &bytes[..4] {
        value = (value << 8) | byte as u32;
    }
    value
}

pub fn deframe(data: &[u8]) -> Deframed {
    let drop = |outcome| Deframed {
        outcome,
        payload_too_short: false,
        descriptor: None,
    };

    if data.len() < HEADER_SIZE + TRAILER_SIZE {
        return drop(Outcome::BufferTooShort);
    }
    if be_u32(&data[0..4]) != START_WORD {
        return drop(Outcome::BadStartWord);
    }
    let length_field = be_u32(&data[4..8]) as u64;
    // Mirrors: expected = SERIALIZED_SIZE(header) + lengthField + SERIALIZED_SIZE(trailer)
    let expected = (HEADER_SIZE as u64) + length_field + (TRAILER_SIZE as u64);
    if data.len() as u64 != expected {
        return drop(Outcome::BadLength);
    }

    // APID extraction, mirroring: deserializeSizeLeft < trailer + sizeof(descriptor)
    const DESC_SIZE: usize = core::mem::size_of::<FwPacketDescriptorType>();
    let payload_too_short = data.len() - HEADER_SIZE < TRAILER_SIZE + DESC_SIZE;
    let descriptor = if payload_too_short {
        None
    } else {
        let mut value: FwPacketDescriptorType = 0;
        for &byte in &data[HEADER_SIZE..HEADER_SIZE + DESC_SIZE] {
            value = (value << 8) | byte as FwPacketDescriptorType;
        }
        Some(value)
    };

    // Trailer CRC over header + body (validated above: hashed + TRAILER == len)
    let hashed = HEADER_SIZE + length_field as usize;
    let computed = frame_crc(&data[..hashed]);
    let transmitted = be_u32(&data[hashed..hashed + 4]);
    let outcome = if transmitted == computed {
        Outcome::Ok
    } else {
        Outcome::BadCrc
    };
    Deframed {
        outcome,
        payload_too_short,
        descriptor,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn build_frame(payload: &[u8]) -> std::vec::Vec<u8> {
        let mut frame = std::vec::Vec::new();
        frame.extend_from_slice(&START_WORD.to_be_bytes());
        frame.extend_from_slice(&(payload.len() as u32).to_be_bytes());
        frame.extend_from_slice(payload);
        let crc = frame_crc(&frame);
        frame.extend_from_slice(&crc.to_be_bytes());
        frame
    }

    #[test]
    fn valid_frame_with_descriptor() {
        let frame = build_frame(&[0x00, 0x05, 0xAA, 0xBB]);
        let result = deframe(&frame);
        assert_eq!(result.outcome, Outcome::Ok);
        assert!(!result.payload_too_short);
        assert_eq!(result.descriptor, Some(5));
    }

    #[test]
    fn too_short_buffer() {
        assert_eq!(deframe(&[0u8; 11]).outcome, Outcome::BufferTooShort);
        // Exactly header+trailer passes the size gate (empty payload)
        let frame = build_frame(&[]);
        let result = deframe(&frame);
        assert_eq!(result.outcome, Outcome::Ok);
        assert!(result.payload_too_short);
        assert_eq!(result.descriptor, None);
    }

    #[test]
    fn bad_start_word() {
        let mut frame = build_frame(&[1, 2, 3]);
        frame[0] = 0x00;
        assert_eq!(deframe(&frame).outcome, Outcome::BadStartWord);
    }

    #[test]
    fn bad_length() {
        let mut frame = build_frame(&[1, 2, 3]);
        frame.push(0xFF); // one extra byte
        assert_eq!(deframe(&frame).outcome, Outcome::BadLength);
    }

    #[test]
    fn bad_crc_still_reports_short_payload() {
        // 1-byte payload: too short for a 2-byte descriptor; corrupt the CRC
        let mut frame = build_frame(&[0x42]);
        let last = frame.len() - 1;
        frame[last] ^= 0xFF;
        let result = deframe(&frame);
        assert_eq!(result.outcome, Outcome::BadCrc);
        assert!(result.payload_too_short);
        assert_eq!(result.descriptor, None);
    }

    #[test]
    fn corrupted_body_fails_crc() {
        let mut frame = build_frame(&[9, 9, 9, 9]);
        frame[10] ^= 0x01;
        assert_eq!(deframe(&frame).outcome, Outcome::BadCrc);
    }
}
