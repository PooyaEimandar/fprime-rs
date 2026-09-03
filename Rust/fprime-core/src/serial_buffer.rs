//! Fixed-capacity serialized buffers (`Fw::LinearBufferBase` subclasses).
//!
//! `Fw::ComBuffer`, `Fw::CmdArgBuffer`, `Fw::LogBuffer`, `Fw::TlmBuffer`,
//! `Fw::ParamBuffer` and `Fw::StatementArgBuffer` are all the same thing: a
//! byte buffer of a configured maximum size that serializes as a
//! `FwSizeStoreType` length token followed by that many bytes. Each is an FPP
//! *abstract* type, so the model declares the name and says nothing about the
//! layout — but the layout is identical across all six, and each one's
//! capacity is an FPP constant the generator can resolve.
//!
//! Same wire shape as [`crate::fpp_string::FwString`], and deliberately a
//! separate type: these hold arbitrary bytes, not characters, and conflating
//! them would invite `as_str`-shaped mistakes on data that is not text.

use crate::{DeserReader, Endianness, LengthMode, SerBuffer, SerError, SerResult};
use fprime_config::FwSizeStoreType;

/// A serialized buffer holding at most `N` bytes.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct FwSerialBuffer<const N: usize> {
    length: usize,
    bytes: [u8; N],
}

impl<const N: usize> Default for FwSerialBuffer<N> {
    fn default() -> Self {
        Self {
            length: 0,
            bytes: [0; N],
        }
    }
}

impl<const N: usize> FwSerialBuffer<N> {
    /// Matches the C++ `SERIALIZED_SIZE`, which every one of these declares as
    /// `STATIC_SERIALIZED_SIZE(<its max size>)` — the length token plus a
    /// full-capacity payload. This is the maximum; a shorter buffer occupies
    /// less on the wire.
    pub const SERIALIZED_SIZE: usize = core::mem::size_of::<FwSizeStoreType>() + N;

    /// The most bytes this can hold.
    pub const CAPACITY: usize = N;

    /// Borrow the bytes held.
    pub fn as_bytes(&self) -> &[u8] {
        &self.bytes[..self.length]
    }

    /// How many bytes are held.
    pub fn len(&self) -> usize {
        self.length
    }

    /// Whether the buffer holds no bytes.
    pub fn is_empty(&self) -> bool {
        self.length == 0
    }

    /// Build from bytes, or `None` when they do not fit.
    ///
    /// Refuses rather than truncating, which is the opposite of
    /// [`crate::fpp_string::FwString::from_bytes`] and deliberate: a truncated
    /// string is still a string, whereas a truncated serialized buffer is a
    /// corrupt message. The C++ agrees — `LinearBufferBase::serializeFrom`
    /// answers `FW_SERIALIZE_NO_ROOM_LEFT` rather than writing a prefix.
    pub fn from_bytes(source: &[u8]) -> Option<Self> {
        if source.len() > N {
            return None;
        }
        let mut bytes = [0; N];
        bytes[..source.len()].copy_from_slice(source);
        Some(Self {
            length: source.len(),
            bytes,
        })
    }

    /// Serialize as a length token followed by the bytes.
    pub fn serialize_to(&self, buf: &mut SerBuffer<'_>, mode: Endianness) -> SerResult {
        buf.ser_bytes(self.as_bytes(), LengthMode::IncludeLength, mode)
    }

    /// Deserialize a length token and that many bytes.
    ///
    /// A stored length above `N` is a `SizeMismatch`, matching the C++, whose
    /// `deserializeTo` seeds its size estimate with the capacity and fails
    /// when the token exceeds it.
    pub fn deserialize_from(buf: &mut DeserReader<'_>, mode: Endianness) -> SerResult<Self> {
        let mut bytes = [0; N];
        let length = buf.deser_bytes(&mut bytes, LengthMode::IncludeLength, mode)?;
        if length > N {
            return Err(SerError::SizeMismatch);
        }
        Ok(Self { length, bytes })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_serialized_size_is_the_token_plus_the_capacity() {
        // Fw::LogBuffer's C++ SERIALIZED_SIZE is
        // STATIC_SERIALIZED_SIZE(FW_LOG_BUFFER_MAX_SIZE), i.e. 2 + that.
        assert_eq!(FwSerialBuffer::<506>::SERIALIZED_SIZE, 2 + 506);
    }

    #[test]
    fn a_round_trip_preserves_the_bytes() {
        let value = FwSerialBuffer::<16>::from_bytes(&[1, 2, 3, 0, 255]).unwrap();
        let mut storage = [0u8; 32];
        let written = {
            let mut out = SerBuffer::new(&mut storage);
            value.serialize_to(&mut out, Endianness::Big).unwrap();
            out.len()
        };
        assert_eq!(written, 2 + 5);

        let mut reader = DeserReader::new(&storage[..written]);
        let back = FwSerialBuffer::<16>::deserialize_from(&mut reader, Endianness::Big).unwrap();
        assert_eq!(back.as_bytes(), &[1, 2, 3, 0, 255]);
    }

    #[test]
    fn an_interior_zero_is_data_not_a_terminator() {
        // The distinction from FwString that matters: these hold bytes.
        let value = FwSerialBuffer::<8>::from_bytes(&[0, 0, 7]).unwrap();
        assert_eq!(value.len(), 3);
        assert_eq!(value.as_bytes(), &[0, 0, 7]);
    }

    #[test]
    fn oversized_input_is_refused_rather_than_truncated() {
        // Unlike FwString, which truncates: a truncated serialized buffer is a
        // corrupt message, not a shorter one.
        assert_eq!(FwSerialBuffer::<3>::from_bytes(&[1, 2, 3, 4]), None);
        assert!(FwSerialBuffer::<4>::from_bytes(&[1, 2, 3, 4]).is_some());
    }

    #[test]
    fn the_length_token_follows_the_requested_endianness() {
        // The opposite of FwString, deliberately. LinearBufferBase::
        // serializeFrom(const LinearBufferBase&, mode) passes the mode to the
        // token (Fw/Types/Serializable.cpp:288), where
        // ConstStringBase::serializeTo drops it. The two types look alike and
        // differ exactly here, so both halves are pinned.
        let value = FwSerialBuffer::<8>::from_bytes(&[1, 2]).unwrap();
        let mut big = [0u8; 16];
        let mut little = [0u8; 16];
        {
            let mut out = SerBuffer::new(&mut big);
            value.serialize_to(&mut out, Endianness::Big).unwrap();
        }
        {
            let mut out = SerBuffer::new(&mut little);
            value.serialize_to(&mut out, Endianness::Little).unwrap();
        }
        assert_eq!(&big[..2], &[0, 2]);
        assert_eq!(&little[..2], &[2, 0]);
        assert_ne!(big, little, "the token really does follow the mode here");
    }

    #[test]
    fn a_length_token_beyond_the_capacity_is_refused() {
        let wire = [0u8, 9, 1, 2, 3, 4, 5, 6, 7, 8, 9];
        let mut reader = DeserReader::new(&wire);
        assert_eq!(
            FwSerialBuffer::<4>::deserialize_from(&mut reader, Endianness::Big),
            Err(SerError::SizeMismatch)
        );
    }
}
