//! Message-slot bookkeeping for the OSAL priority queue.
//!
//! Mirrors the `PriorityQueueHandle` methods in
//! `Os/Generic/PriorityQueue.cpp` — the queue selected by
//! `cmake/platform/unix/Platform/CMakeLists.txt`, so every message a
//! deployment sends to an active component passes through this code.
//!
//! The queue has two data structures. [`crate::max_heap`] projects priority
//! order onto the messages; this module owns the other one: a circular list
//! of free slot indices, and the flat slab those slots address. Together they
//! are the queue's *allocator* — which slot a message lands in, and where in
//! the slab that slot is.
//!
//! With both in Rust, every non-OS decision in the queue is Rust. What stays
//! in C++ is what should: the allocation, the mutex, the two condition
//! variables, and the `FW_ASSERT` guards. This module allocates nothing and
//! blocks on nothing.
//!
//! ## The ring cannot tell full from empty, and neither can the C++
//!
//! `start == stop` means every slot is free (the initial state) *or* every
//! slot is handed out; the ring carries no count that would separate them.
//! The C++ has the same gap and closes it the same way: `PriorityQueue::send`
//! checks `m_heap.isFull()` before taking an index and `receive` checks
//! `isEmpty()` before returning one, so the heap's size is the count. That is
//! preserved rather than fixed here — adding a second count would create two
//! sources of truth for one fact, and the one that already exists is the one
//! `getMessagesAvailable()` reports.

use fprime_config::FwSizeType;

/// A borrowed view of the free-index ring for one operation.
///
/// `indices.len()` is the C++ `m_depth`. `start` and `stop` are the handle's
/// own cursors, advanced in place.
pub struct SlotRing<'a> {
    /// The circular list of free slot indices, exactly `depth` long
    pub indices: &'a mut [FwSizeType],
    /// Cursor messages are taken from (`m_startIndex`)
    pub start: &'a mut FwSizeType,
    /// Cursor returned messages are written to (`m_stopIndex`)
    pub stop: &'a mut FwSizeType,
}

impl SlotRing<'_> {
    fn depth(&self) -> usize {
        self.indices.len()
    }

    /// Fill the ring with every slot, free, and rewind both cursors.
    ///
    /// The C++ does this inline at the end of `PriorityQueue::create`.
    pub fn reset(&mut self) {
        for (slot, entry) in self.indices.iter_mut().enumerate() {
            *entry = slot as FwSizeType;
        }
        *self.start = 0;
        *self.stop = 0;
    }

    /// Take the next free slot (`find_index`).
    ///
    /// The caller must already know a slot is free; see the module note.
    ///
    /// Both this and [`give`](Self::give) take the cursor modulo the depth on
    /// read *and* on write, as the C++ does. The second one is redundant --
    /// `cursor + 1` is at most `depth`, and the read would fold it back
    /// anyway -- and it is kept because dropping it makes this a
    /// near-copy that no longer reads like its original, for no gain. A
    /// mutation that removes it changes nothing, which is the proof.
    pub fn take(&mut self) -> FwSizeType {
        let depth = self.depth();
        assert!(depth > 0, "queue slot ring has no slots");
        let cursor = (*self.start as usize) % depth;
        let index = self.indices[cursor];
        *self.start = ((cursor + 1) % depth) as FwSizeType;
        index
    }

    /// Return a slot to the free list (`return_index`).
    ///
    /// The C++ checks the index only later, when `store_data` next hands it
    /// out; checking it here names the operation that corrupted the ring
    /// rather than the innocent send that trips over it afterwards.
    pub fn give(&mut self, index: FwSizeType) {
        let depth = self.depth();
        assert!(depth > 0, "queue slot ring has no slots");
        assert!(
            (index as usize) < depth,
            "returned slot index is past the queue depth"
        );
        let cursor = (*self.stop as usize) % depth;
        self.indices[cursor] = index;
        *self.stop = ((cursor + 1) % depth) as FwSizeType;
    }
}

/// A borrowed view of the message slab for one operation.
///
/// `data` is `depth * max_size` bytes and `sizes` is `depth` long; slot `i`
/// occupies `data[i * max_size ..][.. max_size]`.
pub struct SlotStore<'a> {
    /// The flat message storage (`m_data`)
    pub data: &'a mut [u8],
    /// Bytes actually stored in each slot (`m_sizes`)
    pub sizes: &'a mut [FwSizeType],
    /// Capacity of one slot (`m_maxSize`)
    pub max_size: FwSizeType,
}

impl SlotStore<'_> {
    /// The byte range slot `index` occupies, checked.
    ///
    /// The C++ computes `m_maxSize * index` as raw pointer arithmetic after
    /// asserting the index and the size separately; doing it as a slice range
    /// makes the product itself the thing that is checked.
    fn span(&self, index: FwSizeType) -> (usize, usize) {
        let slot = index as usize;
        assert!(
            slot < self.sizes.len(),
            "queue slot index is past the depth"
        );
        let width = self.max_size as usize;
        let start = slot
            .checked_mul(width)
            .expect("queue slab offset overflows a pointer");
        let end = start
            .checked_add(width)
            .expect("queue slab offset overflows a pointer");
        assert!(
            end <= self.data.len(),
            "queue slab is smaller than its depth"
        );
        (start, end)
    }

    /// Copy a message into a slot and record its length (`store_data`).
    pub fn store(&mut self, index: FwSizeType, source: &[u8]) {
        assert!(
            source.len() <= self.max_size as usize,
            "message is larger than the queue's message size"
        );
        let (start, _) = self.span(index);
        self.data[start..start + source.len()].copy_from_slice(source);
        self.sizes[index as usize] = source.len() as FwSizeType;
    }

    /// Copy a stored message out (`load_data`).
    ///
    /// `destination.len()` is the number of bytes to read, which the C++
    /// caller sets from `m_sizes[index]` — not the caller's capacity. Reading
    /// a slot's full width instead would hand the receiver whatever the
    /// previous message left behind.
    pub fn load(&self, index: FwSizeType, destination: &mut [u8]) {
        assert!(
            destination.len() <= self.max_size as usize,
            "read is larger than the queue's message size"
        );
        let (start, _) = self.span(index);
        destination.copy_from_slice(&self.data[start..start + destination.len()]);
    }

    /// Bytes stored in a slot (`m_sizes[index]`).
    pub fn stored_size(&self, index: FwSizeType) -> FwSizeType {
        assert!(
            (index as usize) < self.sizes.len(),
            "queue slot index is past the depth"
        );
        self.sizes[index as usize]
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    struct Ring {
        indices: Vec<FwSizeType>,
        start: FwSizeType,
        stop: FwSizeType,
    }

    impl Ring {
        fn new(depth: usize) -> Self {
            let mut ring = Self {
                indices: vec![0; depth],
                start: 0,
                stop: 0,
            };
            ring.view().reset();
            ring
        }

        fn view(&mut self) -> SlotRing<'_> {
            SlotRing {
                indices: &mut self.indices,
                start: &mut self.start,
                stop: &mut self.stop,
            }
        }
    }

    #[test]
    fn reset_hands_out_every_slot_once() {
        let mut ring = Ring::new(4);
        let mut seen: Vec<FwSizeType> = (0..4).map(|_| ring.view().take()).collect();
        seen.sort_unstable();
        assert_eq!(seen, vec![0, 1, 2, 3], "a slot was duplicated or skipped");
    }

    #[test]
    fn returned_slots_come_back_out_in_the_order_they_were_returned() {
        let mut ring = Ring::new(3);
        let first = ring.view().take();
        let second = ring.view().take();
        // Return the first two while the third is still free, then drain.
        ring.view().give(first);
        ring.view().give(second);
        let third = ring.view().take();
        assert_ne!(third, first);
        assert_ne!(third, second);
        assert_eq!(ring.view().take(), first, "the ring is not FIFO");
        assert_eq!(ring.view().take(), second, "the ring is not FIFO");
    }

    #[test]
    fn cursors_wrap_without_drifting() {
        // The C++ takes both cursors modulo depth on every step. Run many more
        // cycles than the depth: any drift shows up as a repeated or lost slot.
        let mut ring = Ring::new(3);
        for _ in 0..100 {
            let index = ring.view().take();
            assert!(index < 3, "handed out a slot past the depth");
            ring.view().give(index);
        }
        let mut seen: Vec<FwSizeType> = (0..3).map(|_| ring.view().take()).collect();
        seen.sort_unstable();
        assert_eq!(seen, vec![0, 1, 2], "the ring lost or duplicated a slot");
    }

    #[test]
    #[should_panic(expected = "past the queue depth")]
    fn a_slot_index_past_the_depth_is_refused() {
        let mut ring = Ring::new(2);
        ring.view().give(2);
    }

    fn store(depth: usize, max_size: FwSizeType) -> (Vec<u8>, Vec<FwSizeType>) {
        (vec![0u8; depth * max_size as usize], vec![0; depth])
    }

    #[test]
    fn a_message_round_trips_through_a_slot() {
        let (mut data, mut sizes) = store(3, 8);
        let mut slab = SlotStore {
            data: &mut data,
            sizes: &mut sizes,
            max_size: 8,
        };
        slab.store(1, b"hello");
        assert_eq!(slab.stored_size(1), 5);
        let mut out = [0u8; 5];
        slab.load(1, &mut out);
        assert_eq!(&out, b"hello");
    }

    #[test]
    fn slots_do_not_overlap() {
        // The bug this catches is an offset computed with the message size
        // rather than the slot width: slot 1 would then start inside slot 0.
        let (mut data, mut sizes) = store(3, 4);
        let mut slab = SlotStore {
            data: &mut data,
            sizes: &mut sizes,
            max_size: 4,
        };
        slab.store(0, b"aaaa");
        slab.store(1, b"bb");
        slab.store(2, b"cccc");
        let mut first = [0u8; 4];
        slab.load(0, &mut first);
        assert_eq!(&first, b"aaaa", "slot 1 wrote into slot 0");
        let mut third = [0u8; 4];
        slab.load(2, &mut third);
        assert_eq!(&third, b"cccc");
    }

    #[test]
    fn a_short_read_does_not_see_the_previous_message() {
        // The receiver reads m_sizes[index] bytes, not the slot's full width.
        // Reading the width would return the tail of whatever was there.
        let (mut data, mut sizes) = store(2, 8);
        let mut slab = SlotStore {
            data: &mut data,
            sizes: &mut sizes,
            max_size: 8,
        };
        slab.store(0, b"XXXXXXXX");
        slab.store(0, b"ab");
        let mut out = vec![0u8; slab.stored_size(0) as usize];
        slab.load(0, &mut out);
        assert_eq!(
            out, b"ab",
            "the read length came from the slot, not the message"
        );
    }

    #[test]
    #[should_panic(expected = "larger than the queue's message size")]
    fn an_oversized_message_is_refused() {
        let (mut data, mut sizes) = store(2, 4);
        let mut slab = SlotStore {
            data: &mut data,
            sizes: &mut sizes,
            max_size: 4,
        };
        slab.store(0, b"12345");
    }

    #[test]
    #[should_panic(expected = "past the depth")]
    fn a_store_past_the_depth_is_refused() {
        let (mut data, mut sizes) = store(2, 4);
        let mut slab = SlotStore {
            data: &mut data,
            sizes: &mut sizes,
            max_size: 4,
        };
        slab.store(2, b"12");
    }
}
