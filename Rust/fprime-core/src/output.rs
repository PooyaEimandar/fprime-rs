//! Write-only access to caller storage, with an initialized prefix for reads.

use core::mem::MaybeUninit;

/// Storage may start uninitialized. Writes never read the previous contents,
/// and only the contiguous initialized prefix can be borrowed as bytes.
pub struct OutputBytes<'a> {
    storage: &'a mut [MaybeUninit<u8>],
    initialized: usize,
}

impl<'a> OutputBytes<'a> {
    pub fn new(storage: &'a mut [u8]) -> Self {
        let initialized = storage.len();
        // SAFETY: MaybeUninit<u8> has the same layout as u8. This wrapper
        // never writes uninitialized values, so the original slice stays valid.
        let storage =
            unsafe { core::slice::from_raw_parts_mut(storage.as_mut_ptr().cast(), initialized) };
        Self {
            storage,
            initialized,
        }
    }

    pub fn uninit(storage: &'a mut [MaybeUninit<u8>]) -> Self {
        Self {
            storage,
            initialized: 0,
        }
    }

    pub fn len(&self) -> usize {
        self.storage.len()
    }

    pub fn is_empty(&self) -> bool {
        self.storage.is_empty()
    }

    pub fn write(&mut self, offset: usize, bytes: &[u8]) {
        let end = offset
            .checked_add(bytes.len())
            .expect("output offset overflow");
        for (slot, byte) in self.storage[offset..end].iter_mut().zip(bytes) {
            slot.write(*byte);
        }
        if offset <= self.initialized {
            self.initialized = self.initialized.max(end);
        }
    }

    pub fn fill(&mut self, start: usize, end: usize, byte: u8) {
        for slot in &mut self.storage[start..end] {
            slot.write(byte);
        }
        if start <= self.initialized {
            self.initialized = self.initialized.max(end);
        }
    }

    pub fn initialized(&self) -> &[u8] {
        // SAFETY: construction and writes establish every byte in this prefix.
        unsafe { core::slice::from_raw_parts(self.storage.as_ptr().cast(), self.initialized) }
    }

    pub fn initialized_mut(&mut self) -> &mut [u8] {
        // SAFETY: as above, with the exclusive borrow held by this wrapper.
        unsafe {
            core::slice::from_raw_parts_mut(self.storage.as_mut_ptr().cast(), self.initialized)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn only_initialized_bytes_become_readable() {
        let mut storage = [MaybeUninit::uninit(); 8];
        let mut out = OutputBytes::uninit(&mut storage);
        assert!(out.initialized().is_empty());
        out.write(0, b"abc");
        assert_eq!(out.initialized(), b"abc");
        out.fill(3, 5, 0);
        assert_eq!(out.initialized(), b"abc\0\0");
    }

    #[test]
    fn writing_does_not_modify_unused_storage() {
        let mut storage = [0xa5; 8];
        let mut out = OutputBytes::new(&mut storage);
        out.write(0, b"abc");
        assert_eq!(
            out.initialized(),
            &[b'a', b'b', b'c', 0xa5, 0xa5, 0xa5, 0xa5, 0xa5]
        );
    }
}
