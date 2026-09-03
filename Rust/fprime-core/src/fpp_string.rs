//! Fixed-capacity FPP strings (`string size N`).
//!
//! An FPP `string size N` member is a buffer of at most `N` characters that
//! serializes as a `FwSizeStoreType` length token followed by that many bytes
//! — variable on the wire, bounded in memory. `Fw::StringBase` is the C++ side
//! of it; this is the same contract for generated Rust types.
//!
//! The capacity is a const generic rather than a runtime field so that
//! `SERIALIZED_SIZE` stays a compile-time constant, matching
//! `Fw::StringBase::STATIC_SERIALIZED_SIZE(N)` and letting a generated struct
//! sum its members' sizes the way the autocoded C++ does.
//!
//! **Not NUL-terminated.** The C++ keeps a terminator because its accessors
//! hand out a `const char*`; nothing here does, so the length is the length
//! and there is no reserved byte. `BUFFER_SIZE(N)` in the C++ is `N + 1` for
//! exactly that reason, and it is not this type's concern.

use crate::{DeserReader, Endianness, LengthMode, SerBuffer, SerError, SerResult};
use fprime_config::FwSizeStoreType;

/// A string of at most `N` bytes.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct FwString<const N: usize> {
    length: usize,
    bytes: [u8; N],
}

impl<const N: usize> Default for FwString<N> {
    fn default() -> Self {
        Self {
            length: 0,
            bytes: [0; N],
        }
    }
}

impl<const N: usize> FwString<N> {
    /// Matches `Fw::StringBase::STATIC_SERIALIZED_SIZE(N)`: the length token
    /// plus a full-capacity payload. This is the *maximum*; a shorter string
    /// occupies less on the wire.
    pub const SERIALIZED_SIZE: usize = core::mem::size_of::<FwSizeStoreType>() + N;

    /// The most characters this can hold.
    pub const CAPACITY: usize = N;

    /// Borrow the characters. Not NUL-terminated; the length is authoritative.
    pub fn as_bytes(&self) -> &[u8] {
        &self.bytes[..self.length]
    }

    /// How many characters are held.
    pub fn len(&self) -> usize {
        self.length
    }

    /// Whether the string holds no characters.
    pub fn is_empty(&self) -> bool {
        self.length == 0
    }

    /// Build from bytes, truncating at the capacity.
    ///
    /// Truncation rather than refusal, because that is what the C++ does:
    /// `ConstStringBase::serializeTo` writes `min(maxLength, length())` and
    /// `Fw::StringUtils::string_copy` truncates rather than overflowing.
    pub fn from_bytes(source: &[u8]) -> Self {
        let length = source.len().min(N);
        let mut bytes = [0; N];
        bytes[..length].copy_from_slice(&source[..length]);
        Self { length, bytes }
    }

    /// Serialize as a length token followed by the characters.
    ///
    /// **The `mode` is accepted and ignored, because the C++ ignores it.**
    /// `ConstStringBase::serializeTo(buffer, maxLength, mode)` takes an
    /// endianness and then calls `serializeFrom(ptr, len, INCLUDE_LENGTH)`
    /// without passing it on, so the length token falls back to the
    /// `Endianness::BIG` default no matter what the caller asked for
    /// (`Fw/Types/ConstStringBase.cpp:85-89`). The characters themselves are
    /// bytes and have no endianness.
    ///
    /// This is not the same as `FwSerialBuffer`, which passes the mode through
    /// — `LinearBufferBase::serializeFrom(const LinearBufferBase&, mode)`
    /// really does honour it (`Fw/Types/Serializable.cpp:288`). The two look
    /// alike and differ here, which is exactly why it is written down.
    pub fn serialize_to(&self, buf: &mut SerBuffer<'_>, _mode: Endianness) -> SerResult {
        buf.ser_bytes(self.as_bytes(), LengthMode::IncludeLength, Endianness::Big)
    }

    /// Deserialize a length token and that many characters.
    ///
    /// A stored length above `N` is a `SizeMismatch`, which is what the C++
    /// reports: its `deserializeTo` seeds the size estimate with `maxLength()`
    /// and fails when the token exceeds it. On failure the value is left
    /// untouched, mirroring "leave string unmodified" in `StringBase.cpp`.
    pub fn deserialize_from(buf: &mut DeserReader<'_>, _mode: Endianness) -> SerResult<Self> {
        let mut bytes = [0; N];
        // Big for the same reason as `serialize_to`: StringBase::deserializeFrom
        // drops the mode too, reaching deserializeTo's Endianness::BIG default.
        let length = buf.deser_bytes(&mut bytes, LengthMode::IncludeLength, Endianness::Big)?;
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
        // Matches Fw::StringBase::STATIC_SERIALIZED_SIZE(N), which is
        // sizeof(FwSizeStoreType) + N -- 2 + N here.
        assert_eq!(FwString::<80>::SERIALIZED_SIZE, 2 + 80);
        assert_eq!(FwString::<0>::SERIALIZED_SIZE, 2);
    }

    #[test]
    fn a_round_trip_preserves_the_characters() {
        let value = FwString::<16>::from_bytes(b"hello");
        let mut storage = [0u8; 32];
        let written = {
            let mut out = SerBuffer::new(&mut storage);
            value.serialize_to(&mut out, Endianness::Big).unwrap();
            out.len()
        };
        // Two bytes of length token, then the characters: shorter than the
        // capacity, so the wire form is shorter than SERIALIZED_SIZE.
        assert_eq!(written, 2 + 5);

        let mut reader = DeserReader::new(&storage[..written]);
        let back = FwString::<16>::deserialize_from(&mut reader, Endianness::Big).unwrap();
        assert_eq!(back.as_bytes(), b"hello");
        assert_eq!(back, value);
    }

    #[test]
    fn an_empty_string_is_just_the_token() {
        let value = FwString::<8>::default();
        assert!(value.is_empty());
        let mut storage = [0u8; 16];
        let written = {
            let mut out = SerBuffer::new(&mut storage);
            value.serialize_to(&mut out, Endianness::Big).unwrap();
            out.len()
        };
        assert_eq!(written, 2);
        assert_eq!(&storage[..2], &[0, 0]);
    }

    #[test]
    fn construction_truncates_at_the_capacity() {
        // The C++ truncates rather than refusing, in both string_copy and
        // serializeTo's min(maxLength, length()).
        let value = FwString::<3>::from_bytes(b"abcdef");
        assert_eq!(value.len(), 3);
        assert_eq!(value.as_bytes(), b"abc");
    }

    #[test]
    fn a_length_token_beyond_the_capacity_is_refused() {
        // Hand-built wire form claiming 9 characters for a capacity of 4.
        let wire = [0u8, 9, b'a', b'b', b'c', b'd', b'e', b'f', b'g', b'h', b'i'];
        let mut reader = DeserReader::new(&wire);
        assert_eq!(
            FwString::<4>::deserialize_from(&mut reader, Endianness::Big),
            Err(SerError::SizeMismatch)
        );
    }

    #[test]
    fn the_length_token_is_big_endian_whatever_the_caller_asks() {
        // Fw::ConstStringBase::serializeTo accepts an endianness and does not
        // pass it on, so the token is always big. Serializing the same string
        // both ways must produce identical bytes; if this ever fails, the C++
        // changed and fpp_string.rs must follow.
        let value = FwString::<8>::from_bytes(b"ab");
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
        assert_eq!(big, little);
        assert_eq!(&big[..2], &[0, 2], "big-endian length token");
    }

    #[test]
    fn a_truncated_payload_is_refused() {
        // Claims 5 characters but only 2 follow.
        let wire = [0u8, 5, b'a', b'b'];
        let mut reader = DeserReader::new(&wire);
        assert_eq!(
            FwString::<16>::deserialize_from(&mut reader, Endianness::Big),
            Err(SerError::SizeMismatch)
        );
    }
}
