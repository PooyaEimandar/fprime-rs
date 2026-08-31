//! The wire form of `Fw::Buffer` (fprime-rs Phase 14).
//!
//! `Fw::Buffer` is an FPP *abstract* type: `Fw/Buffer/Buffer.fpp` declares
//! `type Buffer` and the implementation is hand-written C++. The model
//! therefore says nothing about how it serializes, which is why the type
//! generator cannot derive this and why it lives here instead.
//!
//! **This is an address, not a buffer.** `Fw::Buffer::serializeTo` writes the
//! wrapped pointer as a `PlatformPointerCastType` — a host memory address —
//! so a serialized Buffer is meaningful only inside the process that wrote
//! it. That is not a flaw: its use is queue messages between components in
//! one address space (`Svc::ComQueue` sizes its entries from
//! `Fw::Buffer::SERIALIZED_SIZE`). It does mean this type must never be
//! treated as something that survives a downlink, and that `data` is
//! deliberately a bare integer rather than a pointer or slice.
//!
//! The field *order* is not the declaration order in `Buffer.hpp`, and was
//! pinned by nothing until `Fw/Buffer/test/ut/TestBuffer.cpp`'s `WireFormat`
//! test; `Rust/RustAbstractTypeCheck.cpp` pins the size against the C++
//! constant so a change on either side fails the build.

use crate::{DeserReader, Endianness, SerBuffer, SerResult};
use fprime_config::FwSizeType;

/// A serialized `Fw::Buffer`.
///
/// Mirrors the five fields `Fw::Buffer::serializeTo` writes, in that order.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct FwBuffer {
    /// The wrapped allocation's address, as `PlatformPointerCastType`.
    ///
    /// An opaque token here. Rust never dereferences it, and it is only
    /// meaningful in the process that serialized it.
    pub data: u64,
    /// Bytes currently represented, starting at `offset`.
    pub size: FwSizeType,
    /// Caller-defined disposal context.
    pub context: u32,
    /// Offset of the represented data within the allocation.
    pub offset: FwSizeType,
    /// Size of the original allocation.
    pub capacity: FwSizeType,
}

impl FwBuffer {
    /// Matches the C++ `Fw::Buffer::SERIALIZED_SIZE`.
    ///
    /// `3 * sizeof(FwSizeType) + sizeof(U32) + sizeof(PlatformPointerCastType)`.
    /// The last term is why this constant is not derivable from the FPP model:
    /// `PlatformPointerCastType` is declared in C++ only, and is 8 bytes only
    /// on a 64-bit target. `Rust/RustAbstractTypeCheck.cpp` refuses to compile
    /// where that is untrue.
    pub const SERIALIZED_SIZE: usize = 3 * 8 + 4 + 8;

    /// Serialize in the order `Fw::Buffer::serializeTo` writes.
    ///
    /// Note this is NOT the declaration order in `Buffer.hpp`, which is
    /// data, offset, size, capacity, context. Getting the two confused
    /// produces a buffer that round-trips against itself and against nothing
    /// else, which is why `TestBuffer.cpp`'s `WireFormat` test reads the
    /// fields back individually rather than through `deserializeFrom`.
    pub fn serialize_to(&self, buf: &mut SerBuffer<'_>, mode: Endianness) -> SerResult {
        buf.ser_u64(self.data, mode)?;
        buf.ser_u64(self.size, mode)?;
        buf.ser_u32(self.context, mode)?;
        buf.ser_u64(self.offset, mode)?;
        buf.ser_u64(self.capacity, mode)?;
        Ok(())
    }

    pub fn deserialize_from(buf: &mut DeserReader<'_>, mode: Endianness) -> SerResult<Self> {
        // Read into named locals first, in wire order. Reading straight into a
        // struct literal would work -- Rust evaluates field initializers in
        // the order written -- but it would make a correct wire format depend
        // on the order of lines inside a literal, where reordering looks
        // harmless. Here the sequence is the statements, and the literal below
        // just names them.
        let data = buf.deser_u64(mode)?;
        let size = buf.deser_u64(mode)?;
        let context = buf.deser_u32(mode)?;
        let offset = buf.deser_u64(mode)?;
        let capacity = buf.deser_u64(mode)?;
        Ok(Self {
            data,
            size,
            context,
            offset,
            capacity,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_wire_order_is_not_the_declaration_order() {
        let value = FwBuffer {
            data: 0x1122_3344_5566_7788,
            size: 0x11,
            context: 0xAABB_CCDD,
            offset: 0x22,
            capacity: 0x33,
        };
        let mut bytes = [0u8; FwBuffer::SERIALIZED_SIZE];
        let mut out = SerBuffer::new(&mut bytes);
        value.serialize_to(&mut out, Endianness::Big).unwrap();
        assert_eq!(out.len(), FwBuffer::SERIALIZED_SIZE);

        // context sits third, between size and offset -- the fact the C++
        // WireFormat test pins on the other side.
        assert_eq!(&bytes[16..20], &0xAABB_CCDDu32.to_be_bytes());

        let mut reader = DeserReader::new(&bytes);
        assert_eq!(
            FwBuffer::deserialize_from(&mut reader, Endianness::Big).unwrap(),
            value
        );
    }
}
