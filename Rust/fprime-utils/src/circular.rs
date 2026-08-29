//! The byte engine of `Utils/Types/CircularBuffer.cpp`, operating on the
//! C++ object's own fields through mutable references. The C++ shim
//! (`Utils/Types/CircularBufferRust.cpp`) keeps the class layout, the
//! `FW_ASSERT` preconditions, and the `Fw::Serializable` template paths; every
//! byte moved through the ring moves through this module.
//!
//! Status codes mirror the C++ original: `FW_SERIALIZE_NO_ROOM_LEFT` on a
//! full ring, `FW_DESERIALIZE_BUFFER_EMPTY` on insufficient data. Size
//! comparisons use wrapping arithmetic exactly where the C++ uses unchecked
//! unsigned addition.

use fprime_config::FwSizeType;
use fprime_core::{SerError, SerResult};

/// Mutable view of a circular buffer's state fields, as held by the C++
/// object. The shim constructs one per call from `m_store`/`m_head_idx`/
/// `m_allocated_size`/`m_high_water_mark`.
pub struct RingMut<'a> {
    pub store: &'a mut [u8],
    pub head_idx: &'a mut FwSizeType,
    pub allocated_size: &'a mut FwSizeType,
    pub high_water_mark: &'a mut FwSizeType,
}

/// Read-only view for peek operations.
pub struct RingRef<'a> {
    pub store: &'a [u8],
    pub head_idx: FwSizeType,
    pub allocated_size: FwSizeType,
}

fn advance(store_size: FwSizeType, idx: FwSizeType, amount: FwSizeType) -> FwSizeType {
    // Real assert (not debug): the C++ original FW_ASSERTs this in every build,
    // and a panic here routes to the FW_ASSERT fault path via the panic hook.
    // wrapping_add mirrors the C++ unchecked unsigned addition in all profiles.
    assert!(idx < store_size);
    idx.wrapping_add(amount) % store_size
}

impl<'a> RingMut<'a> {
    fn store_size(&self) -> FwSizeType {
        self.store.len() as FwSizeType
    }

    /// Mirrors `CircularBuffer::serialize(const U8*, FwSizeType)`. Borrow
    /// rules make overlap impossible through this safe API; the shared
    /// implementation is the raw [`serialize_overlapping`].
    pub fn serialize(&mut self, src: &[u8]) -> SerResult {
        let store_size = self.store_size();
        // SAFETY: slices from safe borrows; disjoint by construction
        unsafe {
            serialize_overlapping(
                self.store.as_mut_ptr(),
                store_size,
                *self.head_idx,
                self.allocated_size,
                self.high_water_mark,
                src.as_ptr(),
                src.len() as FwSizeType,
            )
        }
    }

    /// Mirrors the bookkeeping tail of the C++ serialize paths: grow the
    /// allocation and track the high-water mark. Used directly by the C++
    /// shim's linear `Fw::Serializable` path, which writes the bytes itself.
    pub fn commit(&mut self, size: FwSizeType) {
        *self.allocated_size += size;
        if *self.allocated_size > *self.high_water_mark {
            *self.high_water_mark = *self.allocated_size;
        }
    }

    /// Mirrors `CircularBuffer::rotate`.
    pub fn rotate(&mut self, amount: FwSizeType) -> SerResult {
        if amount > *self.allocated_size {
            return Err(SerError::BufferEmpty);
        }
        *self.head_idx = advance(self.store_size(), *self.head_idx, amount);
        *self.allocated_size -= amount;
        Ok(())
    }

    /// Mirrors `CircularBuffer::trim`.
    pub fn trim(&mut self, amount: FwSizeType) -> SerResult {
        if amount > *self.allocated_size {
            return Err(SerError::BufferEmpty);
        }
        *self.allocated_size -= amount;
        Ok(())
    }
}

/// Overlap-tolerant serialize used by the FFI: the C++ per-byte copy loop is
/// well-defined when `src` overlaps the ring store (in-tree code serializes a
/// ring's own storage into itself), so this variant mirrors that loop with
/// raw pointer reads/writes — identical order, hence identical bytes — and
/// never materializes a `src` slice.
///
/// # Safety
/// `store` must point to `store_size` writable bytes; `src` to `len` readable
/// bytes; the head/alloc/high references must not alias the byte ranges.
#[allow(clippy::too_many_arguments)]
pub unsafe fn serialize_overlapping(
    store: *mut u8,
    store_size: FwSizeType,
    head_idx: FwSizeType,
    allocated_size: &mut FwSizeType,
    high_water_mark: &mut FwSizeType,
    src: *const u8,
    len: FwSizeType,
) -> SerResult {
    assert!(*allocated_size <= store_size);
    if len > store_size - *allocated_size {
        return Err(SerError::NoRoomLeft);
    }
    let mut idx = advance(store_size, head_idx, *allocated_size);
    for i in 0..len {
        core::ptr::write(
            store.add(idx as usize),
            core::ptr::read(src.add(i as usize)),
        );
        idx = advance(store_size, idx, 1);
    }
    *allocated_size += len;
    if *allocated_size > *high_water_mark {
        *high_water_mark = *allocated_size;
    }
    Ok(())
}

/// Overlap-tolerant peek used by the FFI: the C++ per-byte loop permits `out`
/// overlapping the store; mirror it with raw pointers, identical order.
///
/// # Safety
/// `store` must point to `store_size` readable bytes; `out` to `size`
/// writable bytes.
pub unsafe fn peek_bytes_overlapping(
    store: *const u8,
    store_size: FwSizeType,
    head_idx: FwSizeType,
    allocated_size: FwSizeType,
    offset: FwSizeType,
    out: *mut u8,
    size: FwSizeType,
) -> SerResult {
    if size.wrapping_add(offset) > allocated_size {
        return Err(SerError::BufferEmpty);
    }
    let mut idx = advance(store_size, head_idx, offset);
    for i in 0..size {
        core::ptr::write(
            out.add(i as usize),
            core::ptr::read(store.add(idx as usize)),
        );
        idx = advance(store_size, idx, 1);
    }
    Ok(())
}

impl<'a> RingRef<'a> {
    fn store_size(&self) -> FwSizeType {
        self.store.len() as FwSizeType
    }

    /// Mirrors `CircularBuffer::peek(U8*, FwSizeType, FwSizeType)`. The
    /// availability check uses wrapping addition, as the C++ unsigned
    /// arithmetic does.
    pub fn peek_bytes(&self, out: &mut [u8], offset: FwSizeType) -> SerResult {
        let size = out.len() as FwSizeType;
        if size.wrapping_add(offset) > self.allocated_size {
            return Err(SerError::BufferEmpty);
        }
        let mut idx = advance(self.store_size(), self.head_idx, offset);
        for slot in out.iter_mut() {
            *slot = self.store[idx as usize];
            idx = advance(self.store_size(), idx, 1);
        }
        Ok(())
    }

    /// Mirrors `CircularBuffer::peek(U8&, FwSizeType)`.
    pub fn peek_u8(&self, offset: FwSizeType) -> SerResult<u8> {
        let mut byte = [0u8; 1];
        self.peek_bytes(&mut byte, offset)?;
        Ok(byte[0])
    }

    /// Mirrors `CircularBuffer::peek(U32&, FwSizeType)`: four bytes composed
    /// big-endian ("network format").
    pub fn peek_u32(&self, offset: FwSizeType) -> SerResult<u32> {
        let mut bytes = [0u8; 4];
        self.peek_bytes(&mut bytes, offset)?;
        Ok(u32::from_be_bytes(bytes))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    struct Ring {
        store: [u8; 8],
        head: FwSizeType,
        alloc: FwSizeType,
        high: FwSizeType,
    }

    impl Ring {
        fn new() -> Self {
            Self {
                store: [0; 8],
                head: 0,
                alloc: 0,
                high: 0,
            }
        }
        fn as_mut(&mut self) -> RingMut<'_> {
            RingMut {
                store: &mut self.store,
                head_idx: &mut self.head,
                allocated_size: &mut self.alloc,
                high_water_mark: &mut self.high,
            }
        }
        fn as_ref(&self) -> RingRef<'_> {
            RingRef {
                store: &self.store,
                head_idx: self.head,
                allocated_size: self.alloc,
            }
        }
    }

    #[test]
    fn serialize_peek_rotate_roundtrip() {
        let mut ring = Ring::new();
        ring.as_mut().serialize(&[1, 2, 3, 4, 5]).unwrap();
        assert_eq!(ring.alloc, 5);
        assert_eq!(ring.high, 5);

        let mut out = [0u8; 3];
        ring.as_ref().peek_bytes(&mut out, 1).unwrap();
        assert_eq!(out, [2, 3, 4]);

        ring.as_mut().rotate(2).unwrap();
        assert_eq!(ring.as_ref().peek_u8(0).unwrap(), 3);
        assert_eq!(ring.alloc, 3);
    }

    #[test]
    fn wraparound_write_and_read() {
        let mut ring = Ring::new();
        ring.as_mut().serialize(&[0; 6]).unwrap();
        ring.as_mut().rotate(6).unwrap(); // head now at 6
        ring.as_mut().serialize(&[0xA, 0xB, 0xC, 0xD]).unwrap(); // wraps
        let mut out = [0u8; 4];
        ring.as_ref().peek_bytes(&mut out, 0).unwrap();
        assert_eq!(out, [0xA, 0xB, 0xC, 0xD]);
        // Physical layout wrapped: bytes at 6,7,0,1
        assert_eq!(ring.store[6], 0xA);
        assert_eq!(ring.store[0], 0xC);
    }

    #[test]
    fn full_ring_and_empty_statuses() {
        let mut ring = Ring::new();
        ring.as_mut().serialize(&[0; 8]).unwrap();
        assert_eq!(ring.as_mut().serialize(&[1]), Err(SerError::NoRoomLeft));
        assert_eq!(ring.as_mut().rotate(9), Err(SerError::BufferEmpty));
        assert_eq!(ring.as_ref().peek_u8(8), Err(SerError::BufferEmpty));
        ring.as_mut().trim(8).unwrap();
        assert_eq!(ring.as_ref().peek_u8(0), Err(SerError::BufferEmpty));
    }

    #[test]
    fn peek_u32_is_big_endian() {
        let mut ring = Ring::new();
        ring.as_mut().serialize(&[0x01, 0x02, 0x03, 0x04]).unwrap();
        assert_eq!(ring.as_ref().peek_u32(0).unwrap(), 0x0102_0304);
    }

    #[test]
    fn overlapping_source_matches_cpp_loop() {
        // Serialize a ring's own store into itself (the in-tree detector
        // tests do this): result must match the C++ per-byte loop order.
        let mut store = [9u8, 8, 7, 6, 0, 0, 0, 0];
        let mut head: FwSizeType = 0;
        let mut alloc: FwSizeType = 0;
        let mut high: FwSizeType = 0;
        let src = store.as_ptr();
        unsafe {
            serialize_overlapping(store.as_mut_ptr(), 8, head, &mut alloc, &mut high, src, 3)
                .unwrap();
        }
        let _ = &mut head;
        // C++ loop: writes store[0]=src[0](9), store[1]=src[1](8), store[2]=src[2](7)
        assert_eq!(&store[..4], &[9, 8, 7, 6]);
        assert_eq!(alloc, 3);
    }

    #[test]
    fn high_water_survives_rotate() {
        let mut ring = Ring::new();
        ring.as_mut().serialize(&[0; 7]).unwrap();
        ring.as_mut().rotate(7).unwrap();
        assert_eq!(ring.high, 7);
        ring.as_mut().serialize(&[0; 2]).unwrap();
        assert_eq!(ring.high, 7);
    }
}
