//! F Prime serialization codec, byte- and status-compatible with the C++
//! `Fw::LinearBufferBase` implementation in `Fw/Types/Serializable.cpp`.
//!
//! Compatibility is the contract: every encoding decision here mirrors the C++
//! implementation, including its quirks (cursor semantics on error paths,
//! unchecked length-token narrowing), and is enforced by the differential unit
//! test in `Rust/test/ut/` which runs both codecs over the same values.
#![cfg_attr(not(test), no_std)]

pub mod fpp_string;
pub mod fw_buffer;
pub mod raw_time;
pub mod serial_buffer;
pub mod strings;
pub mod time;

pub use fprime_config::{
    FwSizeStoreType, FwSizeType, FW_SERIALIZE_FALSE_VALUE, FW_SERIALIZE_TRUE_VALUE,
};

/// Status code returned to C++ across the FFI for a successful operation.
/// Mirrors `Fw::FW_SERIALIZE_OK`.
pub const STATUS_OK: i32 = 0;

/// The non-OK values of `Fw::SerializeStatus`, in declaration order.
/// `Result<T, SerError>` is the Rust-side shape; only the i32 code crosses FFI.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum SerError {
    /// FW_SERIALIZE_FORMAT_ERROR
    FormatError = 1,
    /// FW_SERIALIZE_NO_ROOM_LEFT
    NoRoomLeft = 2,
    /// FW_DESERIALIZE_BUFFER_EMPTY
    BufferEmpty = 3,
    /// FW_DESERIALIZE_FORMAT_ERROR
    DeserFormatError = 4,
    /// FW_DESERIALIZE_SIZE_MISMATCH
    SizeMismatch = 5,
    /// FW_DESERIALIZE_TYPE_MISMATCH
    TypeMismatch = 6,
    /// FW_DESERIALIZE_IMMUTABLE
    Immutable = 7,
    /// FW_DESERIALIZE_INVALID_DATA
    InvalidData = 8,
    /// FW_SERIALIZE_DISCARDED_EXISTING
    DiscardedExisting = 9,
}

impl SerError {
    pub const fn code(self) -> i32 {
        self as i32
    }
}

pub type SerResult<T = ()> = Result<T, SerError>;

/// Convert a codec result to the C++ `Fw::SerializeStatus` integer value.
pub fn status_code<T>(result: &SerResult<T>) -> i32 {
    match result {
        Ok(_) => STATUS_OK,
        Err(e) => e.code(),
    }
}

/// Mirrors `Fw::Endianness`. BIG is the F Prime default wire order.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
#[repr(i32)]
pub enum Endianness {
    #[default]
    Big = 0,
    Little = 1,
}

impl Endianness {
    /// FFI decode; unknown values are a caller contract violation.
    pub const fn from_code(code: i32) -> Option<Self> {
        match code {
            0 => Some(Self::Big),
            1 => Some(Self::Little),
            _ => None,
        }
    }
}

/// Mirrors `Fw::Serialization::t` for byte-buffer fields.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum LengthMode {
    IncludeLength,
    OmitLength,
}

/// A read-only deserialization cursor over existing wire bytes. This is the
/// primary deserializer: it never mutates or copies the source, so it can run
/// directly over caller-provided memory of any length (the C++ side has no
/// source-length ceiling either). `SerBuffer` delegates its deserialize
/// methods here.
///
/// Cursor semantics mirror C++ exactly, error paths included: primitive reads
/// advance only on success; a length-prefixed byte read that fails after the
/// length token leaves the token consumed, as `LinearBufferBase` does.
pub struct DeserReader<'a> {
    data: &'a [u8],
    loc: usize,
}

impl<'a> DeserReader<'a> {
    pub fn new(data: &'a [u8]) -> Self {
        Self { data, loc: 0 }
    }

    /// Bytes consumed so far.
    pub fn consumed(&self) -> usize {
        self.loc
    }

    /// Bytes remaining; mirrors `getDeserializeSizeLeft`.
    pub fn remaining(&self) -> usize {
        self.data.len() - self.loc
    }

    fn raw<const N: usize>(&mut self) -> SerResult<[u8; N]> {
        // C++ distinguishes a fully drained buffer from a short one.
        if self.remaining() == 0 {
            return Err(SerError::BufferEmpty);
        }
        if self.remaining() < N {
            return Err(SerError::SizeMismatch);
        }
        let mut out = [0u8; N];
        out.copy_from_slice(&self.data[self.loc..self.loc + N]);
        self.loc += N;
        Ok(out)
    }

    pub fn deser_u8(&mut self, _mode: Endianness) -> SerResult<u8> {
        Ok(self.raw::<1>()?[0])
    }

    pub fn deser_i8(&mut self, mode: Endianness) -> SerResult<i8> {
        Ok(self.deser_u8(mode)? as i8)
    }

    /// Mirrors the C++ bool path: the byte is validated BEFORE the cursor
    /// advances, so a non-canonical byte leaves the cursor untouched
    /// (`Serializable.cpp` returns FORMAT_ERROR before `m_deserLoc +=`).
    pub fn deser_bool(&mut self, _mode: Endianness) -> SerResult<bool> {
        if self.remaining() == 0 {
            return Err(SerError::BufferEmpty);
        }
        let value = match self.data[self.loc] {
            FW_SERIALIZE_TRUE_VALUE => true,
            FW_SERIALIZE_FALSE_VALUE => false,
            _ => return Err(SerError::DeserFormatError),
        };
        self.loc += 1;
        Ok(value)
    }

    /// Deserialize a byte buffer into `out`. With `IncludeLength`, reads the
    /// `FwSizeStoreType` token first and returns the stored length; with
    /// `OmitLength`, fills all of `out`. Statuses mirror the C++ overload —
    /// including that a SIZE_MISMATCH after the length token leaves the token
    /// consumed.
    pub fn deser_bytes(
        &mut self,
        out: &mut [u8],
        length: LengthMode,
        mode: Endianness,
    ) -> SerResult<usize> {
        let count = match length {
            LengthMode::IncludeLength => {
                let stored = self.deser_size_store(mode)? as usize;
                if stored > self.remaining() || stored > out.len() {
                    return Err(SerError::SizeMismatch);
                }
                stored
            }
            LengthMode::OmitLength => {
                if out.len() > self.remaining() {
                    return Err(SerError::SizeMismatch);
                }
                out.len()
            }
        };
        out[..count].copy_from_slice(&self.data[self.loc..self.loc + count]);
        self.loc += count;
        Ok(count)
    }

    /// Mirrors `deserializeSize`: reads a `FwSizeStoreType` token.
    pub fn deser_size(&mut self, mode: Endianness) -> SerResult<FwSizeType> {
        Ok(self.deser_size_store(mode)? as FwSizeType)
    }

    fn deser_size_store(&mut self, mode: Endianness) -> SerResult<FwSizeStoreType> {
        let raw = self.raw::<2>()?;
        Ok(match mode {
            Endianness::Big => FwSizeStoreType::from_be_bytes(raw),
            Endianness::Little => FwSizeStoreType::from_le_bytes(raw),
        })
    }
}

/// A serialize/deserialize cursor over caller-provided storage, mirroring the
/// semantics of `Fw::LinearBufferBase`: `ser_loc` is the fill level, `deser_loc`
/// the read position, and — as in C++ — every successful serialize resets the
/// read position to zero. Deserialization delegates to [`DeserReader`].
pub struct SerBuffer<'a> {
    data: &'a mut [u8],
    ser_loc: usize,
    deser_loc: usize,
}

impl<'a> SerBuffer<'a> {
    /// An empty buffer over `data` for serialization.
    pub fn new(data: &'a mut [u8]) -> Self {
        Self {
            data,
            ser_loc: 0,
            deser_loc: 0,
        }
    }

    /// A buffer whose first `fill` bytes are existing wire data to deserialize.
    /// Mirrors `setBuffLen`; `fill` must not exceed the storage size.
    pub fn with_fill(data: &'a mut [u8], fill: usize) -> SerResult<Self> {
        if fill > data.len() {
            return Err(SerError::NoRoomLeft);
        }
        Ok(Self {
            data,
            ser_loc: fill,
            deser_loc: 0,
        })
    }

    pub fn len(&self) -> usize {
        self.ser_loc
    }

    pub fn is_empty(&self) -> bool {
        self.ser_loc == 0
    }

    pub fn as_bytes(&self) -> &[u8] {
        &self.data[..self.ser_loc]
    }

    /// Bytes remaining to deserialize; mirrors `getDeserializeSizeLeft`.
    pub fn deser_remaining(&self) -> usize {
        self.ser_loc - self.deser_loc
    }

    /// Run one deserialize operation through a [`DeserReader`] positioned at
    /// the current read cursor, then adopt its cursor. Per-operation error
    /// cursor semantics live in the reader, so adopting unconditionally is
    /// exactly the C++ behavior.
    fn with_reader<T>(
        &mut self,
        f: impl FnOnce(&mut DeserReader<'_>) -> SerResult<T>,
    ) -> SerResult<T> {
        let mut reader = DeserReader {
            data: &self.data[..self.ser_loc],
            loc: self.deser_loc,
        };
        let out = f(&mut reader);
        self.deser_loc = reader.loc;
        out
    }

    // -- serialization ----------------------------------------------------

    fn ser_raw(&mut self, bytes: &[u8]) -> SerResult {
        // C++: `m_serLoc + sizeof(val) - 1 >= m_capacity` -> NO_ROOM_LEFT
        if self.ser_loc + bytes.len() > self.data.len() {
            return Err(SerError::NoRoomLeft);
        }
        self.data[self.ser_loc..self.ser_loc + bytes.len()].copy_from_slice(bytes);
        self.ser_loc += bytes.len();
        // C++ resets the read cursor on every write.
        self.deser_loc = 0;
        Ok(())
    }

    pub fn ser_u8(&mut self, val: u8, _mode: Endianness) -> SerResult {
        self.ser_raw(&[val])
    }

    pub fn ser_i8(&mut self, val: i8, mode: Endianness) -> SerResult {
        self.ser_u8(val as u8, mode)
    }

    pub fn ser_bool(&mut self, val: bool, _mode: Endianness) -> SerResult {
        self.ser_raw(&[if val {
            FW_SERIALIZE_TRUE_VALUE
        } else {
            FW_SERIALIZE_FALSE_VALUE
        }])
    }

    /// Serialize a byte buffer, optionally preceded by a `FwSizeStoreType`
    /// length token in the same endianness. Mirrors the C++ overload exactly,
    /// including the unchecked narrowing of `len` into the length token.
    pub fn ser_bytes(&mut self, bytes: &[u8], length: LengthMode, mode: Endianness) -> SerResult {
        if length == LengthMode::IncludeLength {
            self.ser_size_store(bytes.len() as FwSizeStoreType, mode)?;
        }
        if self.ser_loc + bytes.len() > self.data.len() {
            return Err(SerError::NoRoomLeft);
        }
        self.data[self.ser_loc..self.ser_loc + bytes.len()].copy_from_slice(bytes);
        self.ser_loc += bytes.len();
        self.deser_loc = 0;
        Ok(())
    }

    /// Mirrors `serializeSize`: range-checks against `FwSizeStoreType` before
    /// emitting the token (`FW_SERIALIZE_FORMAT_ERROR` when out of range).
    pub fn ser_size(&mut self, size: FwSizeType, mode: Endianness) -> SerResult {
        if size > FwSizeStoreType::MAX as FwSizeType {
            return Err(SerError::FormatError);
        }
        self.ser_size_store(size as FwSizeStoreType, mode)
    }

    fn ser_size_store(&mut self, val: FwSizeStoreType, mode: Endianness) -> SerResult {
        match mode {
            Endianness::Big => self.ser_raw(&val.to_be_bytes()),
            Endianness::Little => self.ser_raw(&val.to_le_bytes()),
        }
    }

    // -- deserialization (delegates to DeserReader) -----------------------

    pub fn deser_u8(&mut self, mode: Endianness) -> SerResult<u8> {
        self.with_reader(|r| r.deser_u8(mode))
    }

    pub fn deser_i8(&mut self, mode: Endianness) -> SerResult<i8> {
        self.with_reader(|r| r.deser_i8(mode))
    }

    pub fn deser_bool(&mut self, mode: Endianness) -> SerResult<bool> {
        self.with_reader(|r| r.deser_bool(mode))
    }

    pub fn deser_bytes(
        &mut self,
        out: &mut [u8],
        length: LengthMode,
        mode: Endianness,
    ) -> SerResult<usize> {
        self.with_reader(|r| r.deser_bytes(out, length, mode))
    }

    pub fn deser_size(&mut self, mode: Endianness) -> SerResult<FwSizeType> {
        self.with_reader(|r| r.deser_size(mode))
    }

    pub fn reset_ser(&mut self) {
        self.ser_loc = 0;
        self.deser_loc = 0;
    }

    pub fn reset_deser(&mut self) {
        self.deser_loc = 0;
    }
}

/// Generate the multi-byte integer and float methods on both cursor types.
/// Integers are emitted byte-wise per `Endianness`; signed types serialize as
/// their unsigned bit-pattern; floats bit-copy to the same-width integer
/// first — all mirroring `Fw/Types/Serializable.cpp`.
macro_rules! codec_int {
    ($ser:ident, $deser:ident, $ty:ty) => {
        impl<'a> SerBuffer<'a> {
            pub fn $ser(&mut self, val: $ty, mode: Endianness) -> SerResult {
                match mode {
                    Endianness::Big => self.ser_raw(&val.to_be_bytes()),
                    Endianness::Little => self.ser_raw(&val.to_le_bytes()),
                }
            }
            pub fn $deser(&mut self, mode: Endianness) -> SerResult<$ty> {
                self.with_reader(|r| r.$deser(mode))
            }
        }
        impl<'a> DeserReader<'a> {
            pub fn $deser(&mut self, mode: Endianness) -> SerResult<$ty> {
                let raw = self.raw::<{ core::mem::size_of::<$ty>() }>()?;
                Ok(match mode {
                    Endianness::Big => <$ty>::from_be_bytes(raw),
                    Endianness::Little => <$ty>::from_le_bytes(raw),
                })
            }
        }
    };
}

codec_int!(ser_u16, deser_u16, u16);
codec_int!(ser_i16, deser_i16, i16);
codec_int!(ser_u32, deser_u32, u32);
codec_int!(ser_i32, deser_i32, i32);
codec_int!(ser_u64, deser_u64, u64);
codec_int!(ser_i64, deser_i64, i64);

macro_rules! codec_float {
    ($ser:ident, $deser:ident, $ty:ty, $bits:ty, $ser_int:ident, $deser_int:ident) => {
        impl<'a> SerBuffer<'a> {
            pub fn $ser(&mut self, val: $ty, mode: Endianness) -> SerResult {
                self.$ser_int(val.to_bits() as $bits, mode)
            }
            pub fn $deser(&mut self, mode: Endianness) -> SerResult<$ty> {
                Ok(<$ty>::from_bits(self.$deser_int(mode)? as _))
            }
        }
        impl<'a> DeserReader<'a> {
            pub fn $deser(&mut self, mode: Endianness) -> SerResult<$ty> {
                Ok(<$ty>::from_bits(self.$deser_int(mode)? as _))
            }
        }
    };
}

codec_float!(ser_f32, deser_f32, f32, u32, ser_u32, deser_u32);
codec_float!(ser_f64, deser_f64, f64, u64, ser_u64, deser_u64);

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn u32_big_endian_bytes() {
        let mut storage = [0u8; 8];
        let mut buf = SerBuffer::new(&mut storage);
        buf.ser_u32(0x0102_0304, Endianness::Big).unwrap();
        assert_eq!(buf.as_bytes(), &[1, 2, 3, 4]);
    }

    #[test]
    fn u32_little_endian_bytes() {
        let mut storage = [0u8; 8];
        let mut buf = SerBuffer::new(&mut storage);
        buf.ser_u32(0x0102_0304, Endianness::Little).unwrap();
        assert_eq!(buf.as_bytes(), &[4, 3, 2, 1]);
    }

    #[test]
    fn bool_wire_values_and_validation() {
        let mut storage = [0u8; 2];
        let mut buf = SerBuffer::new(&mut storage);
        buf.ser_bool(true, Endianness::Big).unwrap();
        buf.ser_bool(false, Endianness::Big).unwrap();
        assert_eq!(buf.as_bytes(), &[0xFF, 0x00]);

        let mut rd = DeserReader::new(&[0x42]);
        assert_eq!(
            rd.deser_bool(Endianness::Big),
            Err(SerError::DeserFormatError)
        );
    }

    #[test]
    fn bool_format_error_leaves_cursor_untouched() {
        // Mirrors C++: [0x42, 0xFF] must FORMAT_ERROR on every attempt, never
        // skip the bad byte and read the 0xFF behind it.
        let mut rd = DeserReader::new(&[0x42, 0xFF]);
        assert_eq!(
            rd.deser_bool(Endianness::Big),
            Err(SerError::DeserFormatError)
        );
        assert_eq!(
            rd.deser_bool(Endianness::Big),
            Err(SerError::DeserFormatError)
        );
        assert_eq!(rd.consumed(), 0);

        let mut storage = [0x42, 0xFF];
        let mut buf = SerBuffer::with_fill(&mut storage, 2).unwrap();
        assert_eq!(
            buf.deser_bool(Endianness::Big),
            Err(SerError::DeserFormatError)
        );
        assert_eq!(
            buf.deser_bool(Endianness::Big),
            Err(SerError::DeserFormatError)
        );
    }

    #[test]
    fn bytes_include_length_prefix() {
        let mut storage = [0u8; 16];
        let mut buf = SerBuffer::new(&mut storage);
        buf.ser_bytes(
            &[0xAA, 0xBB, 0xCC],
            LengthMode::IncludeLength,
            Endianness::Big,
        )
        .unwrap();
        assert_eq!(buf.as_bytes(), &[0x00, 0x03, 0xAA, 0xBB, 0xCC]);

        let mut out = [0u8; 8];
        buf.reset_deser();
        let n = buf
            .deser_bytes(&mut out, LengthMode::IncludeLength, Endianness::Big)
            .unwrap();
        assert_eq!(&out[..n], &[0xAA, 0xBB, 0xCC]);
    }

    #[test]
    fn bytes_size_mismatch_consumes_length_token() {
        // Mirrors C++: the failed body read leaves the token consumed.
        let mut rd = DeserReader::new(&[0x00, 0x09, 0xAA]);
        let mut out = [0u8; 16];
        assert_eq!(
            rd.deser_bytes(&mut out, LengthMode::IncludeLength, Endianness::Big),
            Err(SerError::SizeMismatch)
        );
        assert_eq!(rd.consumed(), 2);
    }

    #[test]
    fn empty_vs_short_statuses() {
        let mut storage = [0u8; 4];
        let mut buf = SerBuffer::new(&mut storage);
        assert_eq!(buf.deser_u32(Endianness::Big), Err(SerError::BufferEmpty));
        buf.ser_u16(7, Endianness::Big).unwrap();
        assert_eq!(buf.deser_u32(Endianness::Big), Err(SerError::SizeMismatch));
    }

    #[test]
    fn reader_has_no_length_ceiling() {
        // A large source buffer deserializes fine from the front, as in C++.
        let big = [0u8; 1024];
        let mut rd = DeserReader::new(&big);
        assert_eq!(rd.deser_u32(Endianness::Big).unwrap(), 0);
        assert_eq!(rd.consumed(), 4);
    }

    #[test]
    fn no_room_left() {
        let mut storage = [0u8; 3];
        let mut buf = SerBuffer::new(&mut storage);
        assert_eq!(buf.ser_u32(1, Endianness::Big), Err(SerError::NoRoomLeft));
    }

    #[test]
    fn serialize_resets_read_cursor() {
        // Mirrors the C++ behavior: any write rewinds the read position.
        let mut storage = [0u8; 8];
        let mut buf = SerBuffer::new(&mut storage);
        buf.ser_u16(0x0102, Endianness::Big).unwrap();
        assert_eq!(buf.deser_u8(Endianness::Big).unwrap(), 1);
        buf.ser_u8(9, Endianness::Big).unwrap();
        assert_eq!(buf.deser_u8(Endianness::Big).unwrap(), 1);
    }

    #[test]
    fn size_token_range_check() {
        let mut storage = [0u8; 4];
        let mut buf = SerBuffer::new(&mut storage);
        assert_eq!(
            buf.ser_size(0x1_0000, Endianness::Big),
            Err(SerError::FormatError)
        );
        buf.ser_size(0xFFFF, Endianness::Big).unwrap();
        assert_eq!(buf.as_bytes(), &[0xFF, 0xFF]);
    }
}

// ---------------------------------------------------------------------------
// Cursor API for the C++ Fw kernel (Phase 8)
// ---------------------------------------------------------------------------
//
// Phases 0-7 had Rust mirror the C++ wire format. This inverts it: the C++
// `Fw::LinearBufferBase` keeps its class, its storage and its cursors, but
// hands them here for every primitive read and write. The cursors stay in the
// C++ object because every other method in that class touches them; only the
// byte-level work moves.
//
// The semantics reproduced here are `Fw::LinearBufferBase`'s, quirks included:
// a successful write resets the read cursor, a fully-drained buffer reports
// BUFFER_EMPTY while a short one reports SIZE_MISMATCH.

/// A `LinearBufferBase`'s state, borrowed for one operation.
pub struct Cursor<'a> {
    /// The object's storage
    pub data: &'a mut [u8],
    /// Bytes written so far (`m_serLoc`)
    pub ser_loc: &'a mut FwSizeType,
    /// Read position (`m_deserLoc`)
    pub deser_loc: &'a mut FwSizeType,
}

impl Cursor<'_> {
    fn write(&mut self, bytes: &[u8]) -> SerResult {
        let at = *self.ser_loc as usize;
        if at + bytes.len() > self.data.len() {
            return Err(SerError::NoRoomLeft);
        }
        self.data[at..at + bytes.len()].copy_from_slice(bytes);
        *self.ser_loc += bytes.len() as FwSizeType;
        // A write rewinds the read cursor, as the C++ does.
        *self.deser_loc = 0;
        Ok(())
    }

    fn reader(&mut self) -> Reader<'_> {
        let written = (*self.ser_loc as usize).min(self.data.len());
        Reader {
            data: &self.data[..written],
            deser_loc: self.deser_loc,
        }
    }

    /// Mirrors `serializeFrom(U8)`.
    pub fn ser_u8(&mut self, value: u8) -> SerResult {
        self.write(&[value])
    }

    /// Mirrors `deserializeTo(U8&)`.
    pub fn deser_u8(&mut self) -> SerResult<u8> {
        self.reader().deser_u8()
    }

    /// Mirrors `serializeFrom(bool)`: the configured wire values, not 0/1.
    pub fn ser_bool(&mut self, value: bool) -> SerResult {
        self.write(&[if value {
            FW_SERIALIZE_TRUE_VALUE
        } else {
            FW_SERIALIZE_FALSE_VALUE
        }])
    }

    /// Mirrors `deserializeTo(bool&)`, which rejects any other byte.
    pub fn deser_bool(&mut self) -> SerResult<bool> {
        self.reader().deser_bool()
    }
}

/// The read half of a [`Cursor`]: a shared borrow of exactly the bytes that
/// have been written (`m_serLoc`), never the whole capacity.
///
/// Deserialization cannot touch a byte past the write high-water mark, so it
/// must not *borrow* one either — a mutable borrow of the full capacity would
/// impose a stronger precondition on the C++ caller than the code it replaced.
pub struct Reader<'a> {
    /// The written region: `data.len()` is the C++ `m_serLoc`
    pub data: &'a [u8],
    /// Read position (`m_deserLoc`)
    pub deser_loc: &'a mut FwSizeType,
}

impl Reader<'_> {
    /// Bytes left to read, or `None` if the cursors are inconsistent.
    fn available(&self) -> Option<usize> {
        self.data.len().checked_sub(*self.deser_loc as usize)
    }

    fn read<const N: usize>(&mut self) -> SerResult<[u8; N]> {
        // C++ lets `m_deserLoc > m_serLoc` wrap and then reads out of bounds;
        // report a size mismatch instead. The framework asserts the invariant
        // elsewhere, so this state is unreachable in a well-formed object.
        let available = self.available().ok_or(SerError::SizeMismatch)?;
        if available == 0 {
            return Err(SerError::BufferEmpty);
        }
        if available < N {
            return Err(SerError::SizeMismatch);
        }
        let at = *self.deser_loc as usize;
        let mut out = [0u8; N];
        out.copy_from_slice(&self.data[at..at + N]);
        *self.deser_loc += N as FwSizeType;
        Ok(out)
    }

    /// Mirrors `deserializeTo(U8&)`.
    pub fn deser_u8(&mut self) -> SerResult<u8> {
        Ok(self.read::<1>()?[0])
    }

    /// Mirrors `deserializeTo(bool&)`, which rejects any other byte.
    pub fn deser_bool(&mut self) -> SerResult<bool> {
        // Validate before advancing, as the C++ does: a bad byte leaves the
        // read cursor where it was.
        let available = self.available().ok_or(SerError::SizeMismatch)?;
        if available == 0 {
            return Err(SerError::BufferEmpty);
        }
        let value = match self.data[*self.deser_loc as usize] {
            FW_SERIALIZE_TRUE_VALUE => true,
            FW_SERIALIZE_FALSE_VALUE => false,
            _ => return Err(SerError::DeserFormatError),
        };
        *self.deser_loc += 1;
        Ok(value)
    }
}

macro_rules! cursor_int {
    ($ser:ident, $deser:ident, $ty:ty) => {
        impl Cursor<'_> {
            #[doc = concat!("Mirrors `serializeFrom(", stringify!($ty), ")`.")]
            pub fn $ser(&mut self, value: $ty, mode: Endianness) -> SerResult {
                match mode {
                    Endianness::Big => self.write(&value.to_be_bytes()),
                    Endianness::Little => self.write(&value.to_le_bytes()),
                }
            }
            #[doc = concat!("Mirrors `deserializeTo(", stringify!($ty), "&)`.")]
            pub fn $deser(&mut self, mode: Endianness) -> SerResult<$ty> {
                self.reader().$deser(mode)
            }
        }
        impl Reader<'_> {
            #[doc = concat!("Mirrors `deserializeTo(", stringify!($ty), "&)`.")]
            pub fn $deser(&mut self, mode: Endianness) -> SerResult<$ty> {
                let raw = self.read::<{ core::mem::size_of::<$ty>() }>()?;
                Ok(match mode {
                    Endianness::Big => <$ty>::from_be_bytes(raw),
                    Endianness::Little => <$ty>::from_le_bytes(raw),
                })
            }
        }
    };
}

cursor_int!(ser_u16, deser_u16, u16);
cursor_int!(ser_u32, deser_u32, u32);
cursor_int!(ser_u64, deser_u64, u64);

#[cfg(test)]
mod cursor_tests {
    use super::*;

    fn with<R>(
        store: &mut [u8],
        ser: &mut FwSizeType,
        deser: &mut FwSizeType,
        f: impl FnOnce(Cursor<'_>) -> R,
    ) -> R {
        f(Cursor {
            data: store,
            ser_loc: ser,
            deser_loc: deser,
        })
    }

    #[test]
    fn writes_big_endian_and_advances() {
        let mut store = [0u8; 8];
        let (mut s, mut d) = (0, 0);
        with(&mut store, &mut s, &mut d, |mut c| {
            c.ser_u32(0x0102_0304, Endianness::Big).unwrap()
        });
        assert_eq!(&store[..4], &[1, 2, 3, 4]);
        assert_eq!(s, 4);
    }

    #[test]
    fn write_rewinds_the_read_cursor() {
        let mut store = [0u8; 8];
        let (mut s, mut d) = (4, 2);
        with(&mut store, &mut s, &mut d, |mut c| c.ser_u8(9).unwrap());
        assert_eq!(d, 0, "a write must rewind the read cursor");
    }

    #[test]
    fn distinguishes_empty_from_short() {
        let mut store = [0u8; 8];
        let (mut s, mut d) = (0, 0);
        assert_eq!(
            with(&mut store, &mut s, &mut d, |mut c| c
                .deser_u32(Endianness::Big)),
            Err(SerError::BufferEmpty)
        );
        s = 2;
        assert_eq!(
            with(&mut store, &mut s, &mut d, |mut c| c
                .deser_u32(Endianness::Big)),
            Err(SerError::SizeMismatch)
        );
    }

    #[test]
    fn bad_bool_leaves_the_cursor_alone() {
        let mut store = [0x42u8; 1];
        let (mut s, mut d) = (1, 0);
        assert_eq!(
            with(&mut store, &mut s, &mut d, |mut c| c.deser_bool()),
            Err(SerError::DeserFormatError)
        );
        assert_eq!(d, 0);
    }

    #[test]
    fn no_room_left_leaves_state_untouched() {
        let mut store = [0u8; 3];
        let (mut s, mut d) = (0, 0);
        assert_eq!(
            with(&mut store, &mut s, &mut d, |mut c| c
                .ser_u32(1, Endianness::Big)),
            Err(SerError::NoRoomLeft)
        );
        assert_eq!(s, 0);
    }
}

// ---------------------------------------------------------------------------
// Bulk transfer planning (Phase 8)
// ---------------------------------------------------------------------------
//
// The bulk byte paths (`serializeFrom(const U8*, ...)`, `deserializeTo(U8*,
// ...)`, `setBuff`, `copyFrom`) move a caller-supplied count of bytes into or
// out of the object's storage. The *copy* has to happen behind raw pointers
// in `fprime-ffi`, because the C++ contract explicitly allows the source to
// overlap the destination and two aliasing Rust slices are UB. What can live
// here — safe, and unit-tested — is the arithmetic that decides whether the
// transfer is legal at all, which is where the exposure actually is: these
// lengths come off the wire.

/// Bytes the object can still accept at `ser_loc`, or the status the C++
/// returns when it cannot accept `length`.
///
/// Mirrors `m_serLoc + length > m_capacity` without the overflow that
/// expression has for a wire-supplied `length` near the type's maximum.
pub fn plan_bulk_write(ser_loc: FwSizeType, length: FwSizeType, capacity: FwSizeType) -> SerResult {
    match ser_loc.checked_add(length) {
        Some(end) if end <= capacity => Ok(()),
        _ => Err(SerError::NoRoomLeft),
    }
}

/// Checks a read of `length` bytes at `deser_loc` against the write
/// high-water mark, mirroring the C++ `getDeserializeSizeLeft()` comparison.
///
/// `deser_loc` past `ser_loc` is an inconsistent object. The C++
/// `getDeserializeSizeLeft()` **asserts** on it (`FW_ASSERT(m_serLoc >=
/// m_deserLoc)`) rather than wrapping, and the shims still call it, so that
/// fault is preserved on the paths that go through them; this `checked_sub`
/// is a second line of defense for callers that do not, and reports a size
/// mismatch rather than reading out of bounds.
pub fn plan_bulk_read(ser_loc: FwSizeType, deser_loc: FwSizeType, length: FwSizeType) -> SerResult {
    match ser_loc.checked_sub(deser_loc) {
        Some(left) if length <= left => Ok(()),
        _ => Err(SerError::SizeMismatch),
    }
}

#[cfg(test)]
mod bulk_tests {
    use super::*;

    #[test]
    fn write_fits_exactly_to_capacity() {
        assert_eq!(plan_bulk_write(4, 4, 8), Ok(()));
        assert_eq!(plan_bulk_write(0, 8, 8), Ok(()));
        assert_eq!(plan_bulk_write(8, 0, 8), Ok(()));
    }

    #[test]
    fn write_one_past_capacity_is_refused() {
        assert_eq!(plan_bulk_write(4, 5, 8), Err(SerError::NoRoomLeft));
        assert_eq!(plan_bulk_write(0, 9, 8), Err(SerError::NoRoomLeft));
    }

    #[test]
    fn write_length_near_the_maximum_does_not_wrap() {
        // The C++ `m_serLoc + length > m_capacity` wraps here and would admit
        // the transfer; the checked add refuses it.
        assert_eq!(
            plan_bulk_write(8, FwSizeType::MAX, 16),
            Err(SerError::NoRoomLeft)
        );
        assert_eq!(
            plan_bulk_write(FwSizeType::MAX, 1, FwSizeType::MAX),
            Err(SerError::NoRoomLeft)
        );
    }

    #[test]
    fn read_is_bounded_by_the_write_high_water_mark() {
        assert_eq!(plan_bulk_read(8, 2, 6), Ok(()));
        assert_eq!(plan_bulk_read(8, 2, 7), Err(SerError::SizeMismatch));
        assert_eq!(plan_bulk_read(8, 8, 0), Ok(()));
    }

    #[test]
    fn read_with_cursors_inverted_is_refused_not_wrapped() {
        // deser_loc > ser_loc: the C++ subtraction wraps to a huge "left".
        assert_eq!(plan_bulk_read(2, 8, 1), Err(SerError::SizeMismatch));
        assert_eq!(plan_bulk_read(2, 8, 0), Err(SerError::SizeMismatch));
    }
}
