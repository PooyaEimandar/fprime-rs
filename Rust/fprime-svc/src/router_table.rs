//! The buffer-to-context association table of
//! `Svc/FprimeRouter/FprimeRouter.cpp` (`insertContext`/`takeContext`):
//! a fixed pool of slots keyed by the buffer's data pointer, marked
//! active/inactive.
//!
//! The table storage lives inside the C++ component (the header is
//! unchanged), so this module operates on it through a strided raw view:
//! the shim passes the address of the first entry's `state` and `key` fields
//! plus the entry stride, and the numeric ACTIVE/INACTIVE values. The context
//! payload itself is copied by the shim at the returned slot index — Rust
//! owns the search/claim/release state machine, C++ owns the C++ object.

/// Strided view over the C++ `BufferContextEntry` array's state/key fields.
///
/// # Safety contract (upheld by the shim)
/// `state0`/`key0` point at the `state`/`key` fields of entry 0 of a live
/// array of `count` entries, each `stride` bytes apart; `state` is a 32-bit
/// enum storage and `key` a pointer-sized field; no other code touches the
/// table for the duration of a call.
pub struct TableView {
    pub state0: *mut u8,
    pub key0: *mut u8,
    pub stride: usize,
    pub count: usize,
    pub active: i32,
    pub inactive: i32,
}

impl TableView {
    unsafe fn state_ptr(&self, index: usize) -> *mut i32 {
        self.state0.add(index * self.stride) as *mut i32
    }
    unsafe fn key_ptr(&self, index: usize) -> *mut usize {
        self.key0.add(index * self.stride) as *mut usize
    }

    /// Mirrors `insertContext` slot selection: claim the first inactive slot
    /// for `key`. Returns the slot index, or `None` when the table is full
    /// (`FileOutContextTableFull`/`UnknownDataOutContextTableFull` on the
    /// C++ side).
    ///
    /// # Safety
    /// See the type-level contract.
    pub unsafe fn insert(&self, key: usize) -> Option<usize> {
        for i in 0..self.count {
            if core::ptr::read(self.state_ptr(i)) == self.inactive {
                core::ptr::write(self.state_ptr(i), self.active);
                core::ptr::write(self.key_ptr(i), key);
                return Some(i);
            }
        }
        None
    }

    /// Mirrors `takeContext` slot selection: find the active slot for `key`
    /// and release it. Returns the slot index for the shim to copy the
    /// context from, or `None` (`BufferContextNotFound` on the C++ side).
    ///
    /// Ordering dependency: the slot is released BEFORE the shim copies the
    /// context out. This is safe because the router's `dataIn` and
    /// `fileBufferReturnIn` are guarded ports (serialized by the component
    /// mutex); if those ports ever became `sync`, a concurrent insert could
    /// reuse the slot before the copy.
    ///
    /// # Safety
    /// See the type-level contract.
    pub unsafe fn take(&self, key: usize) -> Option<usize> {
        for i in 0..self.count {
            if core::ptr::read(self.state_ptr(i)) == self.active
                && core::ptr::read(self.key_ptr(i)) == key
            {
                core::ptr::write(self.state_ptr(i), self.inactive);
                return Some(i);
            }
        }
        None
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const ACTIVE: i32 = 7;
    const INACTIVE: i32 = 3;

    #[repr(C)]
    #[derive(Clone, Copy)]
    struct Entry {
        state: i32,
        key: usize,
        payload: u64, // stands in for the C++ context object
    }

    fn view(entries: &mut [Entry]) -> TableView {
        TableView {
            state0: &mut entries[0].state as *mut i32 as *mut u8,
            key0: &mut entries[0].key as *mut usize as *mut u8,
            stride: core::mem::size_of::<Entry>(),
            count: entries.len(),
            active: ACTIVE,
            inactive: INACTIVE,
        }
    }

    fn fresh(count: usize) -> Vec<Entry> {
        vec![
            Entry {
                state: INACTIVE,
                key: 0,
                payload: 0
            };
            count
        ]
    }

    #[test]
    fn insert_take_roundtrip() {
        let mut entries = fresh(4);
        let table = view(&mut entries);
        let slot = unsafe { table.insert(0x1000) }.unwrap();
        entries[slot].payload = 42;

        let table = view(&mut entries);
        let found = unsafe { table.take(0x1000) }.unwrap();
        assert_eq!(found, slot);
        assert_eq!(entries[found].payload, 42);
        assert_eq!(entries[found].state, INACTIVE);
    }

    #[test]
    fn table_full_and_unknown_key() {
        let mut entries = fresh(2);
        let table = view(&mut entries);
        unsafe {
            assert!(table.insert(1).is_some());
            assert!(table.insert(2).is_some());
            assert_eq!(table.insert(3), None); // full
            assert_eq!(table.take(99), None); // unknown key
                                              // Releasing one frees a slot for reuse
            assert!(table.take(1).is_some());
            assert_eq!(table.insert(3), Some(0));
        }
    }

    #[test]
    fn same_key_takes_first_active_match() {
        let mut entries = fresh(3);
        let table = view(&mut entries);
        unsafe {
            let a = table.insert(5).unwrap();
            let b = table.insert(5).unwrap();
            assert_ne!(a, b);
            assert_eq!(table.take(5), Some(a));
            assert_eq!(table.take(5), Some(b));
            assert_eq!(table.take(5), None);
        }
    }
}
