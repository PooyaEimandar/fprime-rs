//! The priority scheduling of `Svc/ComQueue/ComQueue.cpp` (`processQueue`):
//! choose which queue to drain next, and round-robin the queues that share the
//! chosen queue's priority so none of them starves.
//!
//! ComQueue is an *active* component. The autocoded C++ base owns the thread
//! and the `Os::Queue` that feeds it; this module owns none of that. What it
//! owns is the scheduling decision, which is the part that is easy to get
//! subtly wrong and cheap to test exhaustively.
//!
//! The shim (`Svc/ComQueue/ComQueueRust.cpp`) keeps everything that touches
//! C++ types: dequeueing, sending, buffer ownership, throttle flags, and the
//! entry rotation itself (rotating `QueueMetadata` structs stays on the side
//! that knows their layout).

/// One entry of the prioritized list, reduced to the two fields the
/// scheduling decision actually reads.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Entry {
    /// Index into the component's queue array
    pub index: i16,
    /// Scheduling priority; lower entries in the list are served first
    pub priority: i16,
}

/// What `processQueue` decided to do.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Decision {
    /// Queue index to dequeue and send
    pub selected: i16,
    /// Position of the selected entry in the prioritized list
    pub position: usize,
    /// Exclusive end of the run of same-priority entries following the
    /// selected one. The shim rotates `position + 1 .. rotate_end` so the
    /// dispatched entry ends up last among its priority peers.
    pub rotate_end: usize,
}

/// Mirrors `ComQueue::processQueue`'s selection and rotation bookkeeping.
///
/// `sizes` is indexed by queue index (`Entry::index`), holding each queue's
/// current message count. Returns `None` when every queue is empty, which is
/// the case where the C++ loop falls through without sending.
pub fn select(entries: &[Entry], sizes: &[u64]) -> Option<Decision> {
    // Walk the queues in priority order, taking the first with data. No
    // balancing happens in this loop -- that is what the rotation is for.
    let mut position = 0;
    while position < entries.len() {
        let entry = entries[position];
        let size = *sizes.get(entry.index as usize).unwrap_or(&0);
        if size != 0 {
            // Extend over the run of entries that share this priority
            let mut rotate_end = position + 1;
            while rotate_end < entries.len() && entries[rotate_end].priority == entry.priority {
                rotate_end += 1;
            }
            return Some(Decision {
                selected: entry.index,
                position,
                rotate_end,
            });
        }
        position += 1;
    }
    None
}

/// Apply the rotation a [`Decision`] describes to a slice of entries, moving
/// the dispatched entry behind its same-priority peers. The C++ shim performs
/// the equivalent rotation on its own `QueueMetadata` array; this exists so
/// the rotation semantics can be tested directly.
pub fn rotate(entries: &mut [Entry], decision: &Decision) {
    for position in (decision.position + 1)..decision.rotate_end {
        entries.swap(position - 1, position);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn entries(pairs: &[(i16, i16)]) -> Vec<Entry> {
        pairs
            .iter()
            .map(|&(index, priority)| Entry { index, priority })
            .collect()
    }

    #[test]
    fn empty_queues_select_nothing() {
        let list = entries(&[(0, 0), (1, 1)]);
        assert_eq!(select(&list, &[0, 0]), None);
    }

    #[test]
    fn highest_priority_nonempty_wins() {
        let list = entries(&[(2, 0), (0, 1), (1, 1)]);
        // Queue 2 (first in the list) is empty, so queue 0 is chosen
        let decision = select(&list, &[5, 0, 0]).unwrap();
        assert_eq!(decision.selected, 0);
        assert_eq!(decision.position, 1);
        // Entries 1 and 2 share priority 1, so the run ends at 3
        assert_eq!(decision.rotate_end, 3);
    }

    #[test]
    fn same_priority_queues_round_robin() {
        // Three queues all at priority 0, all with data
        let mut list = entries(&[(0, 0), (1, 0), (2, 0)]);
        let mut served = Vec::new();
        for _ in 0..6 {
            let decision = select(&list, &[1, 1, 1]).unwrap();
            served.push(decision.selected);
            rotate(&mut list, &decision);
        }
        // Each queue is served in turn, twice around
        assert_eq!(served, vec![0, 1, 2, 0, 1, 2]);
    }

    #[test]
    fn rotation_does_not_cross_priority_boundaries() {
        // Two at priority 0, one at priority 1
        let mut list = entries(&[(0, 0), (1, 0), (9, 1)]);
        let decision = select(&list, &[1, 1, 1]).unwrap();
        assert_eq!(decision.selected, 0);
        assert_eq!(decision.rotate_end, 2, "priority-1 entry must not rotate");
        rotate(&mut list, &decision);
        assert_eq!(list, entries(&[(1, 0), (0, 0), (9, 1)]));
        // The lower-priority queue is still last and still never preempts
        let next = select(&list, &[1, 1, 1]).unwrap();
        assert_eq!(next.selected, 1);
    }

    #[test]
    fn lower_priority_served_only_when_higher_is_empty() {
        let list = entries(&[(0, 0), (1, 5)]);
        assert_eq!(select(&list, &[0, 3]).unwrap().selected, 1);
        assert_eq!(select(&list, &[3, 3]).unwrap().selected, 0);
    }

    #[test]
    fn single_entry_rotation_is_a_no_op() {
        let mut list = entries(&[(4, 2)]);
        let decision = select(&list, &[0, 0, 0, 0, 7]).unwrap();
        assert_eq!(decision.selected, 4);
        assert_eq!(decision.rotate_end, 1);
        rotate(&mut list, &decision);
        assert_eq!(list, entries(&[(4, 2)]));
    }

    #[test]
    fn index_beyond_sizes_is_treated_as_empty() {
        // Defensive: a depth-0 queue may have no size entry
        let list = entries(&[(7, 0), (0, 1)]);
        assert_eq!(select(&list, &[4]).unwrap().selected, 0);
    }
}
