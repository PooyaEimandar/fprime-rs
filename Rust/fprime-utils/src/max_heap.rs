//! The stable max-heap behind the OSAL priority queue.
//!
//! Mirrors `Os/Generic/Types/MaxHeap.cpp`, which is the ordering core of
//! `Os::Generic::PriorityQueue` — every message a deployment queues to an
//! active component through that queue is ordered by this code.
//!
//! It is the only consumer. `PriorityMemQueue` and `LocklessPriorityQueue`
//! are built on `Types::AtomicQueue` and never touch the heap; earlier
//! versions of this comment claimed all three.
//!
//! Two properties define it, and both are preserved here exactly:
//!
//! - it is a **max** heap: the highest priority pops first;
//! - it is **stable**: equal priorities pop in FIFO order. Stability is what
//!   the `order` field buys — `max()` breaks ties by age, not by index — and
//!   it is the property a naive heap silently loses.
//!
//! Storage belongs to the caller (the C++ `create()` places the node array in
//! memory the queue supplies); this module allocates nothing.

use fprime_config::{FwQueuePriorityType, FwSizeType};

/// One heap entry. `repr(C)` because the C++ owns the allocation and indexes
/// the same array: the shim static_asserts size, alignment and every offset
/// against the C++ `MaxHeap::Node`.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Node {
    /// The entry's priority
    pub value: FwQueuePriorityType,
    /// Push sequence number, used to keep equal priorities FIFO
    pub order: FwSizeType,
    /// Caller-supplied identifier returned by `pop`
    pub id: FwSizeType,
}

/// A borrowed view of a C++ `MaxHeap`'s state for one operation.
///
/// `nodes.len()` is the C++ `m_capacity`; `size` and `order` are the object's
/// own counters, updated in place.
pub struct MaxHeap<'a> {
    /// The node array, exactly `capacity` long
    pub nodes: &'a mut [Node],
    /// Number of entries currently in the heap (`m_size`)
    pub size: &'a mut FwSizeType,
    /// Count of pushes ever made (`m_order`), the tie-breaker's clock
    pub order: &'a mut FwSizeType,
}

impl MaxHeap<'_> {
    fn len(&self) -> usize {
        *self.size as usize
    }

    /// Asserts an index refers to a live entry, mirroring the C++
    /// `FW_ASSERT(x < m_size)` bounds that guard against a corrupted size.
    fn in_use(&self, index: usize) {
        assert!(index < self.len(), "max-heap index is past the live size");
    }

    /// `isFull()`
    pub fn is_full(&self) -> bool {
        self.len() == self.nodes.len()
    }

    /// `isEmpty()`
    pub fn is_empty(&self) -> bool {
        *self.size == 0
    }

    /// `push(value, id)`; false when the heap is full.
    pub fn push(&mut self, value: FwQueuePriorityType, id: FwSizeType) -> bool {
        if self.is_full() {
            return false;
        }
        let mut index = self.len();

        // The C++ caps this walk to bound the damage from a corrupted size
        // (its "bit flip protection"); keep the same cap rather than trusting
        // the loop to terminate.
        let max_iter = self.len() + 1;
        let mut i = 0;
        while i < max_iter && index != 0 {
            let parent = (index - 1) / 2;
            // assert!, not debug_assert!: Os/Generic/Types/MaxHeap.cpp:87 is an
            // unconditional FW_ASSERT, and this port's rule is that guards stay.
            // [profile.release] does not enable debug assertions, so a
            // debug_assert! here is simply absent from every shipped build.
            assert!(parent < index, "max-heap parent index must move up");
            if value <= self.nodes[parent].value {
                break;
            }
            self.nodes[index] = self.nodes[parent];
            index = parent;
            i += 1;
        }
        assert!(i < max_iter, "max-heap push exceeded its iteration bound");
        // The second post-loop guard the C++ carries (MaxHeap.cpp:101), which
        // this mirror had dropped. It catches a sift-up that walked past the
        // end of the live region -- a bit flip, or a corrupted size.
        assert!(index <= self.len(), "max-heap push landed outside the heap");

        self.nodes[index] = Node {
            value,
            order: *self.order,
            id,
        };
        *self.size += 1;
        *self.order += 1;
        true
    }

    /// `pop(value, id)`; `None` when the heap is empty.
    pub fn pop(&mut self) -> Option<(FwQueuePriorityType, FwSizeType)> {
        if self.is_empty() {
            return None;
        }
        let top = self.nodes[0];

        // Move the last entry to the root and shrink, then restore the heap
        // property downwards.
        let last = self.len() - 1;
        self.nodes[0] = self.nodes[last];
        *self.size -= 1;
        self.heapify();

        Some((top.value, top.id))
    }

    /// Restore the heap property from the root down, non-recursively.
    fn heapify(&mut self) {
        let mut index = 0usize;
        let max_iter = self.len() + 1;
        let mut i = 0;

        while i < max_iter && index <= self.len() {
            let left = 2 * index + 1;
            let right = 2 * index + 2;
            if left >= self.len() {
                break;
            }
            let mut largest = self.max(left, index);
            if right < self.len() {
                largest = self.max(right, largest);
            }
            if largest == index {
                break;
            }
            self.in_use(index);
            self.in_use(largest);
            self.nodes.swap(index, largest);
            index = largest;
            i += 1;
        }
        assert!(
            i < max_iter,
            "max-heap heapify exceeded its iteration bound"
        );
    }

    /// The higher-priority of two entries, breaking ties by age so that equal
    /// priorities stay FIFO. This is the whole of the heap's stability.
    fn max(&self, a: usize, b: usize) -> usize {
        // Bound against the live size, not the capacity. Rust's own indexing
        // would only catch a read past the allocation; the C++ asserts
        // `a < m_size` deliberately, as protection against a corrupted size
        // reading a stale node that is still inside the array.
        self.in_use(a);
        self.in_use(b);
        let (a_value, b_value) = (self.nodes[a].value, self.nodes[b].value);
        if a_value == b_value {
            // Ages, not raw order values: `m_order` only ever increases, so
            // the older entry has the larger age. Computed the same way as the
            // C++ so that a wrapped `m_order` compares identically.
            let a_age = self.order.wrapping_sub(self.nodes[a].order);
            let b_age = self.order.wrapping_sub(self.nodes[b].order);
            return if a_age > b_age { a } else { b };
        }
        if a_value > b_value {
            a
        } else {
            b
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    struct Heap {
        nodes: Vec<Node>,
        size: FwSizeType,
        order: FwSizeType,
    }

    impl Heap {
        fn new(capacity: usize) -> Self {
            Heap {
                nodes: vec![Node::default(); capacity],
                size: 0,
                order: 0,
            }
        }
        fn view(&mut self) -> MaxHeap<'_> {
            MaxHeap {
                nodes: &mut self.nodes,
                size: &mut self.size,
                order: &mut self.order,
            }
        }
        fn drain(&mut self) -> Vec<(FwQueuePriorityType, FwSizeType)> {
            let mut out = Vec::new();
            while let Some(entry) = self.view().pop() {
                out.push(entry);
            }
            out
        }
    }

    #[test]
    fn pops_in_priority_order() {
        let mut h = Heap::new(8);
        for (value, id) in [(1u8, 10u64), (5, 50), (3, 30), (2, 20), (4, 40)] {
            assert!(h.view().push(value, id));
        }
        assert_eq!(h.drain(), vec![(5, 50), (4, 40), (3, 30), (2, 20), (1, 10)]);
    }

    #[test]
    fn equal_priorities_pop_fifo() {
        // The stability property: without the age tie-break these come back
        // in an arbitrary order.
        let mut h = Heap::new(16);
        for id in 0..8u64 {
            assert!(h.view().push(7, id));
        }
        let ids: Vec<u64> = h.drain().into_iter().map(|(_, id)| id).collect();
        assert_eq!(ids, (0..8).collect::<Vec<_>>());
    }

    #[test]
    fn interleaved_priorities_are_stable_within_each_priority() {
        let mut h = Heap::new(16);
        for (value, id) in [(2u8, 0u64), (1, 1), (2, 2), (1, 3), (2, 4), (1, 5)] {
            assert!(h.view().push(value, id));
        }
        assert_eq!(
            h.drain(),
            vec![(2, 0), (2, 2), (2, 4), (1, 1), (1, 3), (1, 5)]
        );
    }

    #[test]
    fn push_refuses_when_full_and_pop_reports_empty() {
        let mut h = Heap::new(2);
        assert!(h.view().push(1, 1));
        assert!(h.view().push(2, 2));
        assert!(!h.view().push(3, 3), "a full heap must refuse the push");
        assert_eq!(h.view().pop(), Some((2, 2)));
        assert_eq!(h.view().pop(), Some((1, 1)));
        assert_eq!(h.view().pop(), None);
        assert!(h.view().is_empty());
    }

    #[test]
    fn refill_after_drain_keeps_ordering() {
        let mut h = Heap::new(4);
        for id in 0..4u64 {
            assert!(h.view().push(1, id));
        }
        assert_eq!(h.drain().len(), 4);
        for (value, id) in [(1u8, 100u64), (9, 101), (1, 102)] {
            assert!(h.view().push(value, id));
        }
        assert_eq!(h.drain(), vec![(9, 101), (1, 100), (1, 102)]);
    }

    #[test]
    fn matches_a_reference_model_over_a_long_pseudo_random_run() {
        // A deterministic LCG stands in for a fuzzer: push/pop against a
        // sorted reference that encodes the same (priority, then age) order.
        let mut h = Heap::new(64);
        let mut model: Vec<(FwQueuePriorityType, FwSizeType, FwSizeType)> = Vec::new();
        let mut rng: u64 = 0x2545_F491_4F6C_DD1D;
        let mut next_id: FwSizeType = 0;
        let mut age: FwSizeType = 0;

        for _ in 0..20_000 {
            rng = rng
                .wrapping_mul(6364136223846793005)
                .wrapping_add(1442695040888963407);
            let pushing = !(rng >> 33).is_multiple_of(3);
            if pushing && model.len() < 64 {
                let value = ((rng >> 17) % 5) as FwQueuePriorityType;
                assert!(h.view().push(value, next_id));
                model.push((value, age, next_id));
                next_id += 1;
                age += 1;
            } else {
                let popped = h.view().pop();
                if model.is_empty() {
                    assert_eq!(popped, None);
                    continue;
                }
                // Highest priority wins; ties go to the oldest push.
                let best = model
                    .iter()
                    .enumerate()
                    .max_by_key(|(_, (value, a, _))| (*value, core::cmp::Reverse(*a)))
                    .map(|(i, _)| i)
                    .unwrap();
                let (value, _, id) = model.remove(best);
                assert_eq!(popped, Some((value, id)));
            }
        }
    }
}
