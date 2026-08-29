//! The stateful orchestration of `Svc/FrameAccumulator/FrameAccumulator.cpp`
//! (`processBuffer` + `processRing`): feed incoming bytes into the
//! accumulation ring, run the frame detector, and extract detected frames.
//!
//! The logic talks to the outside world only through the [`Env`] trait —
//! across the FFI, each method is a C++ trampoline over the component's ring
//! (whose byte engine is itself Rust under `FPRIME_RUST_IMPL_UTILS_TYPES`),
//! the polymorphic `FrameDetector`, the `bufferAllocate` port, and the
//! component's events. Rust never holds references to the ring's state across
//! those calls, so the aliasing rules stay satisfied.
//!
//! Every FW_ASSERT of the original is an `assert!` here (a panic routes into
//! the FW_ASSERT fault path), preserving the original's defensive posture.

/// `Svc::FrameDetector::Status` values, pinned by static_asserts in the shim.
pub const FRAME_DETECTED: i32 = 0;
pub const NO_FRAME_DETECTED: i32 = 1;
pub const MORE_DATA_NEEDED: i32 = 2;

/// The component-side operations the orchestration drives. Sizes are
/// `FwSizeType` (u64 per config).
pub trait Env {
    fn ring_free(&mut self) -> u64;
    fn ring_allocated(&mut self) -> u64;
    fn ring_capacity(&mut self) -> u64;
    /// `m_inRing.serialize(bytes)`; returns the `Fw::SerializeStatus` code.
    fn ring_serialize(&mut self, bytes: &[u8]) -> i32;
    /// `m_inRing.rotate(amount)`; returns the status code.
    fn ring_rotate(&mut self, amount: u64) -> i32;
    /// `m_detector->detect(m_inRing, size_out)`: returns (status, size_out).
    fn detect(&mut self) -> (i32, u64);
    /// `bufferAllocate_out(0, size)`, keeping the buffer in the trampoline
    /// context; returns whether the buffer is valid.
    fn allocate_frame(&mut self, size: u64) -> bool;
    /// `m_inRing.peek(<held buffer data>, size)` + `setSize`; returns status.
    fn peek_into_frame(&mut self, size: u64) -> i32;
    /// `dataOut_out(0, <held buffer>, context)`.
    fn emit_frame(&mut self);
    fn log_frame_size_error(&mut self, size: u64);
    fn log_no_buffer(&mut self);
    fn log_frame_dropped(&mut self);
}

const OK: i32 = 0; // Fw::FW_SERIALIZE_OK

/// Mirrors `FrameAccumulator::processBuffer`.
pub fn process_buffer(env: &mut impl Env, data: &[u8]) {
    let buffer_size = data.len() as u64;
    let mut offset: u64 = 0;
    let mut remaining: u64 = buffer_size;

    let mut i: u64 = 0;
    while i < buffer_size {
        // If there is no data left or no space, exit the loop
        if remaining == 0 || env.ring_free() == 0 {
            break;
        }
        // Compute the size of data to serialize
        let ring_free_size = env.ring_free();
        let ser_size = if ring_free_size <= remaining {
            ring_free_size
        } else {
            remaining
        };
        // Serialize data into the ring buffer
        let status = env.ring_serialize(&data[offset as usize..(offset + ser_size) as usize]);
        // If data does not fit, there is a coding error
        assert!(status == OK);
        // Process the data
        process_ring(env);
        // Update buffer offset and remaining
        offset += ser_size;
        remaining -= ser_size;
        i += 1;
    }
    // Either all the bytes from the data buffer must be processed, or the
    // ring must be full
    assert!(remaining == 0 || env.ring_free() == 0);
}

/// Mirrors `FrameAccumulator::processRing`.
pub fn process_ring(env: &mut impl Env) {
    let ring_capacity = env.ring_capacity();

    let mut i: u64 = 0;
    while i < ring_capacity {
        i += 1;
        // Get the number of bytes remaining in the ring buffer
        let remaining = env.ring_allocated();
        // If there are none, we are done
        if remaining == 0 {
            break;
        }
        // Attempt to detect the frame without changing the circular buffer
        let (status, size_out) = env.detect();
        // Detect must not consume data in the ring buffer
        assert!(env.ring_allocated() == remaining);

        // Drop frames that are too large to handle
        if size_out > ring_capacity {
            env.log_frame_size_error(size_out);
            // Discard a single byte of data and start again
            let _ = env.ring_rotate(1);
            assert!(env.ring_allocated() == remaining - 1);
            continue;
        }

        if status == FRAME_DETECTED {
            // size_out must be set (non-zero) and must fit the available data
            assert!(size_out != 0);
            assert!(size_out <= remaining);
            if env.allocate_frame(size_out) {
                // Copy data out of the ring buffer into the allocated buffer
                let peek_status = env.peek_into_frame(size_out);
                assert!(peek_status == OK);
                // Consume (rotate) the data from the ring buffer
                let rotate_status = env.ring_rotate(size_out);
                assert!(rotate_status == OK);
                assert!(env.ring_allocated() == remaining - size_out);
                env.emit_frame();
            } else {
                // No buffer is available
                env.log_no_buffer();
                // If the ring is also full, drop the frame to keep the
                // system afloat (back pressure would otherwise assert)
                if env.ring_free() == 0 {
                    let rotate_status = env.ring_rotate(size_out);
                    assert!(rotate_status == OK);
                    assert!(env.ring_allocated() == remaining - size_out);
                    env.log_frame_dropped();
                }
                break;
            }
        } else if status == MORE_DATA_NEEDED {
            // Detection must request more than the available data
            assert!(size_out > remaining);
            // Suspend detection until we receive another buffer
            break;
        } else {
            // No frame detected (or unknown status): discard a single byte
            let _ = env.ring_rotate(1);
            assert!(env.ring_allocated() == remaining - 1);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fprime_utils::circular::{RingMut, RingRef};
    use std::vec::Vec;

    /// Mock environment: a real ring (the Phase 1 engine) plus a scripted
    /// detector that recognizes [0xA5, len, payload...] pseudo-frames.
    struct Mock {
        store: Vec<u8>,
        head: u64,
        alloc: u64,
        high: u64,
        frame: Vec<u8>,
        emitted: Vec<Vec<u8>>,
        allow_alloc: bool,
        size_errors: u64,
        no_buffer: u64,
        dropped: u64,
    }

    impl Mock {
        fn new(capacity: usize) -> Self {
            Self {
                store: vec![0; capacity],
                head: 0,
                alloc: 0,
                high: 0,
                frame: Vec::new(),
                emitted: Vec::new(),
                allow_alloc: true,
                size_errors: 0,
                no_buffer: 0,
                dropped: 0,
            }
        }
        fn ring_mut(&mut self) -> RingMut<'_> {
            RingMut {
                store: &mut self.store,
                head_idx: &mut self.head,
                allocated_size: &mut self.alloc,
                high_water_mark: &mut self.high,
            }
        }
        fn ring_ref(&self) -> RingRef<'_> {
            RingRef {
                store: &self.store,
                head_idx: self.head,
                allocated_size: self.alloc,
            }
        }
    }

    impl Env for Mock {
        fn ring_free(&mut self) -> u64 {
            self.store.len() as u64 - self.alloc
        }
        fn ring_allocated(&mut self) -> u64 {
            self.alloc
        }
        fn ring_capacity(&mut self) -> u64 {
            self.store.len() as u64
        }
        fn ring_serialize(&mut self, bytes: &[u8]) -> i32 {
            match self.ring_mut().serialize(bytes) {
                Ok(()) => 0,
                Err(e) => e.code(),
            }
        }
        fn ring_rotate(&mut self, amount: u64) -> i32 {
            match self.ring_mut().rotate(amount) {
                Ok(()) => 0,
                Err(e) => e.code(),
            }
        }
        fn detect(&mut self) -> (i32, u64) {
            // Pseudo-protocol: frame = [0xA5, len, payload(len)]
            let first = match self.ring_ref().peek_u8(0) {
                Ok(byte) => byte,
                Err(_) => return (MORE_DATA_NEEDED, 1),
            };
            if first != 0xA5 {
                return (NO_FRAME_DETECTED, 0);
            }
            match self.ring_ref().peek_u8(1) {
                Ok(len) => {
                    let size = 2 + len as u64;
                    if size <= self.alloc {
                        (FRAME_DETECTED, size)
                    } else {
                        (MORE_DATA_NEEDED, size)
                    }
                }
                Err(_) => (MORE_DATA_NEEDED, 2),
            }
        }
        fn allocate_frame(&mut self, size: u64) -> bool {
            if self.allow_alloc {
                self.frame = vec![0; size as usize];
                true
            } else {
                false
            }
        }
        fn peek_into_frame(&mut self, size: u64) -> i32 {
            let mut out = core::mem::take(&mut self.frame);
            let status = match self.ring_ref().peek_bytes(&mut out[..size as usize], 0) {
                Ok(()) => 0,
                Err(e) => e.code(),
            };
            self.frame = out;
            status
        }
        fn emit_frame(&mut self) {
            self.emitted.push(core::mem::take(&mut self.frame));
        }
        fn log_frame_size_error(&mut self, _size: u64) {
            self.size_errors += 1;
        }
        fn log_no_buffer(&mut self) {
            self.no_buffer += 1;
        }
        fn log_frame_dropped(&mut self) {
            self.dropped += 1;
        }
    }

    #[test]
    fn two_frames_in_one_buffer() {
        let mut env = Mock::new(64);
        process_buffer(&mut env, &[0xA5, 2, 7, 8, 0xA5, 1, 9]);
        assert_eq!(env.emitted, vec![vec![0xA5, 2, 7, 8], vec![0xA5, 1, 9]]);
        assert_eq!(env.alloc, 0);
    }

    #[test]
    fn partial_frame_waits_for_more_data() {
        let mut env = Mock::new(64);
        process_buffer(&mut env, &[0xA5, 4, 1]);
        assert!(env.emitted.is_empty());
        assert_eq!(env.alloc, 3);
        process_buffer(&mut env, &[2, 3, 4]);
        assert_eq!(env.emitted, vec![vec![0xA5, 4, 1, 2, 3, 4]]);
    }

    #[test]
    fn garbage_is_discarded_byte_by_byte() {
        let mut env = Mock::new(64);
        process_buffer(&mut env, &[1, 2, 3, 0xA5, 1, 5]);
        assert_eq!(env.emitted, vec![vec![0xA5, 1, 5]]);
        assert_eq!(env.alloc, 0);
    }

    #[test]
    fn no_buffer_backpressure_keeps_data() {
        let mut env = Mock::new(64);
        env.allow_alloc = false;
        process_buffer(&mut env, &[0xA5, 1, 5]);
        assert_eq!(env.no_buffer, 1);
        assert_eq!(env.dropped, 0);
        assert_eq!(env.alloc, 3); // frame retained for a retry

        // Buffers come back: the retained frame is processed on next input
        env.allow_alloc = true;
        process_buffer(&mut env, &[0xA5, 0]);
        assert_eq!(env.emitted, vec![vec![0xA5, 1, 5], vec![0xA5, 0]]);
    }

    #[test]
    fn no_buffer_with_full_ring_drops_frame() {
        let mut env = Mock::new(3);
        env.allow_alloc = false;
        process_buffer(&mut env, &[0xA5, 1, 5]);
        assert_eq!(env.no_buffer, 1);
        assert_eq!(env.dropped, 1);
        assert_eq!(env.alloc, 0); // frame discarded to relieve back pressure
    }

    #[test]
    fn oversized_frame_report_discards_bytes() {
        let mut env = Mock::new(4);
        // len 200 -> size 202 > capacity 4: drop bytes until garbage clears
        process_buffer(&mut env, &[0xA5, 200, 1, 2]);
        assert!(env.size_errors > 0);
        assert!(env.emitted.is_empty());
    }
}
