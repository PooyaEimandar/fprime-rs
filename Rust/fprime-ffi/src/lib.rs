//! The `extern "C"` surface of the F Prime Rust crates.
//!
//! Contract, per the fprime-rs interop rules:
//! - exports are `extern "C"`, never `"C-unwind"`; the workspace builds the
//!   staticlib with `panic = "abort"`, and the panic handler below routes
//!   through the FW_ASSERT fault path before halting
//! - only status codes (`Fw::SerializeStatus` integer values) cross the
//!   boundary; `Result` never does
//! - all storage is caller-provided; this crate allocates nothing
//! - a null pointer is accepted wherever the paired length/capacity is zero,
//!   matching the `Fw::LinearBufferBase` contract
#![no_std]

use fprime_core::{status_code, DeserReader, Endianness, LengthMode, SerBuffer};

#[cfg(not(test))]
extern "C" {
    /// Provided by the C++ side (Rust/RustPanicHook.cpp): routes a Rust panic
    /// into the FW_ASSERT fault path. Must not return; if it does, we halt.
    fn fprime_rs_panic_hook(file: *const u8, file_len: usize, line: core::ffi::c_uint);
}

#[cfg(not(test))]
#[panic_handler]
fn panic(info: &core::panic::PanicInfo) -> ! {
    let (file, len, line) = match info.location() {
        Some(loc) => (loc.file().as_ptr(), loc.file().len(), loc.line()),
        None => (core::ptr::null(), 0, 0),
    };
    // SAFETY: the hook is provided by the C++ side (Rust/RustPanicHook.cpp) and
    // linked into every build that links this staticlib; `file`/`len` describe
    // a live &str from the panic location.
    unsafe { fprime_rs_panic_hook(file, len, line) };
    // The hook must not return (it aborts). Halt if it somehow does:
    // unwinding into -fno-exceptions C++ is not an option.
    loop {}
}

/// Null-safe view over caller memory: C++ callers legitimately pass
/// (nullptr, 0) for empty buffers, and `from_raw_parts` on a null pointer is
/// UB even for length zero.
///
/// # Safety
/// When `len > 0`, `ptr` must point to `len` readable bytes.
unsafe fn view<'a>(ptr: *const u8, len: usize) -> &'a [u8] {
    if len == 0 {
        &[]
    } else {
        core::slice::from_raw_parts(ptr, len)
    }
}

/// Mutable counterpart of [`view`].
///
/// # Safety
/// When `len > 0`, `ptr` must point to `len` writable bytes.
unsafe fn view_mut<'a>(ptr: *mut u8, len: usize) -> &'a mut [u8] {
    if len == 0 {
        &mut []
    } else {
        core::slice::from_raw_parts_mut(ptr, len)
    }
}

fn endianness(mode: i32) -> Endianness {
    // An unknown mode is a caller contract violation; C++ FW_ASSERTs here.
    match Endianness::from_code(mode) {
        Some(m) => m,
        None => panic!("invalid endianness mode"),
    }
}

/// ABI version tag so the C++ side can sanity-check the linked library.
#[no_mangle]
pub extern "C" fn fprime_rs_abi_version() -> u32 {
    1
}

macro_rules! ffi_codec {
    ($ser_name:ident, $deser_name:ident, $ty:ty, $ser:ident, $deser:ident) => {
        /// Serialize one value into `out`; on success `*written` is the byte
        /// count. Returns an `Fw::SerializeStatus` integer value.
        ///
        /// # Safety
        /// `out` must point to `capacity` writable bytes (null allowed when
        /// `capacity` is 0); `written` must be a valid pointer.
        #[no_mangle]
        pub unsafe extern "C" fn $ser_name(
            value: $ty,
            mode: i32,
            out: *mut u8,
            capacity: usize,
            written: *mut usize,
        ) -> i32 {
            let mut buf = SerBuffer::new(view_mut(out, capacity));
            let result = buf.$ser(value, endianness(mode));
            *written = buf.len();
            status_code(&result)
        }

        /// Deserialize one value from `bytes`; on success `*value` holds the
        /// result and `*consumed` the bytes read. The source may be any
        /// length, as in C++. Returns an `Fw::SerializeStatus` integer value.
        ///
        /// # Safety
        /// `bytes` must point to `len` readable bytes (null allowed when
        /// `len` is 0); `value` and `consumed` must be valid pointers.
        #[no_mangle]
        pub unsafe extern "C" fn $deser_name(
            bytes: *const u8,
            len: usize,
            mode: i32,
            value: *mut $ty,
            consumed: *mut usize,
        ) -> i32 {
            let mut reader = DeserReader::new(view(bytes, len));
            let result = reader.$deser(endianness(mode));
            match result {
                Ok(v) => {
                    *value = v;
                    *consumed = reader.consumed();
                    0
                }
                Err(e) => e.code(),
            }
        }
    };
}

ffi_codec!(fprime_rs_ser_u8, fprime_rs_deser_u8, u8, ser_u8, deser_u8);
ffi_codec!(fprime_rs_ser_i8, fprime_rs_deser_i8, i8, ser_i8, deser_i8);
ffi_codec!(
    fprime_rs_ser_u16,
    fprime_rs_deser_u16,
    u16,
    ser_u16,
    deser_u16
);
ffi_codec!(
    fprime_rs_ser_i16,
    fprime_rs_deser_i16,
    i16,
    ser_i16,
    deser_i16
);
ffi_codec!(
    fprime_rs_ser_u32,
    fprime_rs_deser_u32,
    u32,
    ser_u32,
    deser_u32
);
ffi_codec!(
    fprime_rs_ser_i32,
    fprime_rs_deser_i32,
    i32,
    ser_i32,
    deser_i32
);
ffi_codec!(
    fprime_rs_ser_u64,
    fprime_rs_deser_u64,
    u64,
    ser_u64,
    deser_u64
);
ffi_codec!(
    fprime_rs_ser_i64,
    fprime_rs_deser_i64,
    i64,
    ser_i64,
    deser_i64
);
ffi_codec!(
    fprime_rs_ser_f32,
    fprime_rs_deser_f32,
    f32,
    ser_f32,
    deser_f32
);
ffi_codec!(
    fprime_rs_ser_f64,
    fprime_rs_deser_f64,
    f64,
    ser_f64,
    deser_f64
);
ffi_codec!(
    fprime_rs_ser_bool,
    fprime_rs_deser_bool,
    bool,
    ser_bool,
    deser_bool
);

/// Serialize a byte buffer with a `FwSizeStoreType` length token
/// (`Fw::Serialization::INCLUDE_LENGTH`). Implemented with raw pointer copies
/// (memmove semantics) because the mirrored C++ overload explicitly permits
/// `bytes` to overlap `out`; materializing overlapping Rust slices would be
/// UB. As in C++, a body that does not fit leaves the length token written
/// (`*written` reports it).
///
/// # Safety
/// `bytes` must point to `len` readable bytes and `out` to `capacity`
/// writable bytes (null allowed when the paired size is 0); `written` must be
/// a valid pointer. Overlap between `bytes` and `out` is permitted.
#[no_mangle]
pub unsafe extern "C" fn fprime_rs_ser_bytes(
    bytes: *const u8,
    len: usize,
    mode: i32,
    out: *mut u8,
    capacity: usize,
    written: *mut usize,
) -> i32 {
    *written = 0;
    // Length token first, mirroring the C++ unchecked narrowing of len.
    let token = len as fprime_core::FwSizeStoreType;
    let token_bytes = match endianness(mode) {
        Endianness::Big => token.to_be_bytes(),
        Endianness::Little => token.to_le_bytes(),
    };
    if token_bytes.len() > capacity {
        return fprime_core::SerError::NoRoomLeft.code();
    }
    core::ptr::copy_nonoverlapping(token_bytes.as_ptr(), out, token_bytes.len());
    *written = token_bytes.len();
    if token_bytes.len() + len > capacity {
        return fprime_core::SerError::NoRoomLeft.code();
    }
    if len > 0 {
        // memmove semantics: source may overlap the destination region.
        core::ptr::copy(bytes, out.add(token_bytes.len()), len);
    }
    *written = token_bytes.len() + len;
    0
}

/// Deserialize a length-prefixed byte buffer (`INCLUDE_LENGTH`). On success
/// `*out_len` is the stored length. The source may be any length, as in C++.
/// Unlike [`fprime_rs_ser_bytes`], `bytes` and `out` must NOT overlap — the
/// mirrored C++ path uses memcpy, so overlap is undefined there too.
///
/// # Safety
/// `bytes` must point to `len` readable bytes and `out` to `out_capacity`
/// writable bytes (null allowed when the paired size is 0); `out_len` must be
/// a valid pointer; `bytes` and `out` must not overlap.
#[no_mangle]
pub unsafe extern "C" fn fprime_rs_deser_bytes(
    bytes: *const u8,
    len: usize,
    mode: i32,
    out: *mut u8,
    out_capacity: usize,
    out_len: *mut usize,
) -> i32 {
    let mut reader = DeserReader::new(view(bytes, len));
    let dst = view_mut(out, out_capacity);
    match reader.deser_bytes(dst, LengthMode::IncludeLength, endianness(mode)) {
        Ok(n) => {
            *out_len = n;
            0
        }
        Err(e) => e.code(),
    }
}

/// The precompiled `core` rlib from rustup is built with unwind support and
/// carries references to this personality routine. With `panic = "abort"`
/// throughout the workspace it is unreachable; providing it satisfies the
/// linker on stable without enabling unwinding (`-Zbuild-std` would rebuild
/// `core` with abort, but requires nightly). Never link a std-using Rust
/// artifact into the same binary: the real personality symbol would collide.
#[cfg(not(test))]
#[no_mangle]
extern "C" fn rust_eh_personality() {}

// ---------------------------------------------------------------------------
// Utils: CircularBuffer engine, CRC32, CFDP checksum (Phase 1)
// ---------------------------------------------------------------------------

use fprime_config::FwSizeType;
use fprime_utils::{checksum, circular, crc32};

// The FFI converts FwSizeType (u64 per config) to usize with `as` casts. On a
// 32-bit target that would silently truncate sizes; refuse to build until the
// conversions are made checked (Phase 0/1 are host-only anyway).
const _: () = assert!(core::mem::size_of::<usize>() >= core::mem::size_of::<FwSizeType>());

/// # Safety
/// All pointers must be valid for the C++ CircularBuffer object's fields:
/// `store` points to `store_size` bytes (the ring storage), and `head`,
/// `alloc`, `high` point to the object's index/size fields. Preconditions
/// (setup called, non-null buffers) are FW_ASSERTed by the C++ shim.
unsafe fn ring_mut<'a>(
    store: *mut u8,
    store_size: FwSizeType,
    head: *mut FwSizeType,
    alloc: *mut FwSizeType,
    high: *mut FwSizeType,
) -> circular::RingMut<'a> {
    circular::RingMut {
        store: view_mut(store, store_size as usize),
        head_idx: &mut *head,
        allocated_size: &mut *alloc,
        high_water_mark: &mut *high,
    }
}

/// Mirrors `CircularBuffer::serialize(const U8*, FwSizeType)`.
///
/// # Safety
/// See [`ring_mut`]; `src` must point to `size` readable bytes and must NOT
/// overlap the ring store (the shim FW_ASSERTs this; overlapping slices would
/// be UB here where the C++ per-byte copy tolerated overlap).
#[no_mangle]
pub unsafe extern "C" fn fprime_rs_cb_serialize(
    store: *mut u8,
    store_size: FwSizeType,
    head: *mut FwSizeType,
    alloc: *mut FwSizeType,
    high: *mut FwSizeType,
    src: *const u8,
    size: FwSizeType,
) -> i32 {
    let head_value = *head;
    status_code(&circular::serialize_overlapping(
        store,
        store_size,
        head_value,
        &mut *alloc,
        &mut *high,
        src,
        size,
    ))
}

/// Bookkeeping for the C++ shim's direct-write serialize path.
///
/// # Safety
/// See [`ring_mut`].
#[no_mangle]
pub unsafe extern "C" fn fprime_rs_cb_commit(
    store: *mut u8,
    store_size: FwSizeType,
    head: *mut FwSizeType,
    alloc: *mut FwSizeType,
    high: *mut FwSizeType,
    size: FwSizeType,
) -> i32 {
    let mut ring = ring_mut(store, store_size, head, alloc, high);
    ring.commit(size);
    0
}

/// Mirrors `CircularBuffer::rotate`.
///
/// # Safety
/// See [`ring_mut`].
#[no_mangle]
pub unsafe extern "C" fn fprime_rs_cb_rotate(
    store: *mut u8,
    store_size: FwSizeType,
    head: *mut FwSizeType,
    alloc: *mut FwSizeType,
    high: *mut FwSizeType,
    amount: FwSizeType,
) -> i32 {
    let mut ring = ring_mut(store, store_size, head, alloc, high);
    status_code(&ring.rotate(amount))
}

/// Mirrors `CircularBuffer::trim`.
///
/// # Safety
/// See [`ring_mut`].
#[no_mangle]
pub unsafe extern "C" fn fprime_rs_cb_trim(
    store: *mut u8,
    store_size: FwSizeType,
    head: *mut FwSizeType,
    alloc: *mut FwSizeType,
    high: *mut FwSizeType,
    amount: FwSizeType,
) -> i32 {
    let mut ring = ring_mut(store, store_size, head, alloc, high);
    status_code(&ring.trim(amount))
}

/// Mirrors `CircularBuffer::peek(U8*, FwSizeType, FwSizeType)`.
///
/// # Safety
/// `store` must point to `store_size` readable bytes; `out` to `size`
/// writable bytes; `out` must NOT overlap the ring store (the shim FW_ASSERTs
/// this).
#[no_mangle]
pub unsafe extern "C" fn fprime_rs_cb_peek_bytes(
    store: *const u8,
    store_size: FwSizeType,
    head: FwSizeType,
    alloc: FwSizeType,
    offset: FwSizeType,
    out: *mut u8,
    size: FwSizeType,
) -> i32 {
    status_code(&circular::peek_bytes_overlapping(
        store, store_size, head, alloc, offset, out, size,
    ))
}

/// Mirrors `CircularBuffer::peek(U8&, FwSizeType)`.
///
/// # Safety
/// `store` must point to `store_size` readable bytes; `value` must be valid.
#[no_mangle]
pub unsafe extern "C" fn fprime_rs_cb_peek_u8(
    store: *const u8,
    store_size: FwSizeType,
    head: FwSizeType,
    alloc: FwSizeType,
    offset: FwSizeType,
    value: *mut u8,
) -> i32 {
    let ring = circular::RingRef {
        store: view(store, store_size as usize),
        head_idx: head,
        allocated_size: alloc,
    };
    match ring.peek_u8(offset) {
        Ok(v) => {
            *value = v;
            0
        }
        Err(e) => e.code(),
    }
}

/// Mirrors `CircularBuffer::peek(U32&, FwSizeType)` (big-endian).
///
/// # Safety
/// `store` must point to `store_size` readable bytes; `value` must be valid.
#[no_mangle]
pub unsafe extern "C" fn fprime_rs_cb_peek_u32(
    store: *const u8,
    store_size: FwSizeType,
    head: FwSizeType,
    alloc: FwSizeType,
    offset: FwSizeType,
    value: *mut u32,
) -> i32 {
    let ring = circular::RingRef {
        store: view(store, store_size as usize),
        head_idx: head,
        allocated_size: alloc,
    };
    match ring.peek_u32(offset) {
        Ok(v) => {
            *value = v;
            0
        }
        Err(e) => e.code(),
    }
}

/// Mirrors `CFDP::Checksum::update`.
///
/// # Safety
/// `value` must be valid; `data` must point to `length` readable bytes.
#[no_mangle]
pub unsafe extern "C" fn fprime_rs_cfdp_checksum_update(
    value: *mut u32,
    data: *const u8,
    offset: u32,
    length: u32,
) {
    checksum::update(&mut *value, view(data, length as usize), offset);
}

/// Mirrors `Utils::crc32_ieee802_3_update` (raw register, no complements).
///
/// # Safety
/// `data` must point to `length` readable bytes.
#[no_mangle]
pub unsafe extern "C" fn fprime_rs_crc32_update(
    data: *const u8,
    length: FwSizeType,
    crc: u32,
) -> u32 {
    crc32::update(crc, view(data, length as usize))
}

// ---------------------------------------------------------------------------
// Svc components: FprimeDeframer, FprimeFramer (Phase 2)
// ---------------------------------------------------------------------------

use fprime_config::FwPacketDescriptorType;
use fprime_svc::{deframer, framer};

/// Result of `fprime_rs_deframer_deframe`; field meanings mirror
/// `fprime_svc::deframer::Deframed`. Layout must match FprimeRsDeframeResult
/// in Rust/RustSvc.h.
#[repr(C)]
pub struct DeframeResultFfi {
    /// deframer::Outcome as i32 (0 Ok, 1 too-short, 2 start word, 3 length, 4 CRC)
    pub outcome: i32,
    /// 1 when the PayloadTooShort WARNING_LO applies (outcomes 0 and 4)
    pub payload_too_short: u8,
    /// 1 when `descriptor` holds a packet descriptor read from the payload
    pub has_descriptor: u8,
    pub descriptor: FwPacketDescriptorType,
}

/// Validate one F Prime frame. Pure computation: the shim maps the result to
/// events, context APID, buffer adjustment, and port emission.
///
/// # Safety
/// `data` must point to `len` readable bytes (null allowed when `len` is 0);
/// `result` must be a valid pointer.
#[no_mangle]
pub unsafe extern "C" fn fprime_rs_deframer_deframe(
    data: *const u8,
    len: FwSizeType,
    result: *mut DeframeResultFfi,
) {
    let deframed = deframer::deframe(view(data, len as usize));
    *result = DeframeResultFfi {
        outcome: deframed.outcome as i32,
        payload_too_short: deframed.payload_too_short as u8,
        has_descriptor: deframed.descriptor.is_some() as u8,
        descriptor: deframed.descriptor.unwrap_or(0),
    };
}

/// Frame storage view returned by the shim's allocator trampoline: the data
/// pointer and size of the `Fw::Buffer` obtained from the component's
/// `bufferAllocate` output port (null/0 when the port returned an invalid
/// buffer). Layout must match FprimeRsByteView in Rust/RustSvc.h.
#[repr(C)]
pub struct ByteViewFfi {
    pub ptr: *mut u8,
    pub len: FwSizeType,
}

/// The shim's allocator trampoline: calls `bufferAllocate_out` on the
/// component and reports the resulting buffer.
pub type AllocFfi =
    unsafe extern "C" fn(ctx: *mut core::ffi::c_void, size: FwSizeType) -> ByteViewFfi;

/// Build an F Prime frame around `payload`, obtaining storage through the
/// allocator — a synchronous, returning C++ port call made from inside Rust.
/// Returns 0 with `*written` set on success; 1 when no adequate buffer was
/// available (the shim owns the NoBufferAvailable event and cleanup, using
/// the `Fw::Buffer` it captured in the trampoline context).
///
/// # Safety
/// `payload` must point to `payload_len` readable bytes (null allowed when
/// zero); `alloc` must be a valid trampoline for `ctx`; `written` must be a
/// valid pointer. The allocated buffer must not overlap `payload`.
#[no_mangle]
pub unsafe extern "C" fn fprime_rs_framer_frame(
    payload: *const u8,
    payload_len: FwSizeType,
    alloc: AllocFfi,
    ctx: *mut core::ffi::c_void,
    written: *mut FwSizeType,
) -> i32 {
    let payload = view(payload, payload_len as usize);
    let result = framer::frame(payload, |size| {
        let view = alloc(ctx, size);
        if view.ptr.is_null() {
            None
        } else {
            Some(view_mut(view.ptr, view.len as usize))
        }
    });
    match result {
        Ok(count) => {
            *written = count as FwSizeType;
            0
        }
        Err(framer::NoBuffer) => 1,
    }
}

// ---------------------------------------------------------------------------
// Svc components: FrameAccumulator, FprimeRouter (Phase 3)
// ---------------------------------------------------------------------------

use fprime_svc::{accumulator, router_table};

/// The C++ trampoline table driving the FrameAccumulator orchestration.
/// Every member is a captureless-lambda trampoline over the component; the
/// layout must match FprimeRsAccumulatorOps in Rust/RustSvc.h. See that
/// header for the no-unwind contract on all callbacks.
#[repr(C)]
pub struct AccumulatorOpsFfi {
    pub ctx: *mut core::ffi::c_void,
    pub ring_free: unsafe extern "C" fn(*mut core::ffi::c_void) -> FwSizeType,
    pub ring_allocated: unsafe extern "C" fn(*mut core::ffi::c_void) -> FwSizeType,
    pub ring_capacity: unsafe extern "C" fn(*mut core::ffi::c_void) -> FwSizeType,
    pub ring_serialize: unsafe extern "C" fn(*mut core::ffi::c_void, *const u8, FwSizeType) -> i32,
    pub ring_rotate: unsafe extern "C" fn(*mut core::ffi::c_void, FwSizeType) -> i32,
    pub detect: unsafe extern "C" fn(*mut core::ffi::c_void, *mut FwSizeType) -> i32,
    pub allocate_frame: unsafe extern "C" fn(*mut core::ffi::c_void, FwSizeType) -> u8,
    pub peek_into_frame: unsafe extern "C" fn(*mut core::ffi::c_void, FwSizeType) -> i32,
    pub emit_frame: unsafe extern "C" fn(*mut core::ffi::c_void),
    pub log_frame_size_error: unsafe extern "C" fn(*mut core::ffi::c_void, FwSizeType),
    pub log_no_buffer: unsafe extern "C" fn(*mut core::ffi::c_void),
    pub log_frame_dropped: unsafe extern "C" fn(*mut core::ffi::c_void),
}

struct AccumulatorEnvFfi<'a> {
    ops: &'a AccumulatorOpsFfi,
}

// SAFETY (applies to every method below): `self.ops` was validated by the
// caller of `fprime_rs_accumulator_process`, which requires a fully populated
// trampoline table whose function pointers are valid for `ops.ctx` and do not
// unwind. Each call passes `ops.ctx` unchanged, and no Rust reference into the
// component's state is held across any of these calls.
impl accumulator::Env for AccumulatorEnvFfi<'_> {
    fn ring_free(&mut self) -> u64 {
        // SAFETY: see the impl-level note above.
        unsafe { (self.ops.ring_free)(self.ops.ctx) }
    }
    fn ring_allocated(&mut self) -> u64 {
        // SAFETY: see the impl-level note above.
        unsafe { (self.ops.ring_allocated)(self.ops.ctx) }
    }
    fn ring_capacity(&mut self) -> u64 {
        // SAFETY: see the impl-level note above.
        unsafe { (self.ops.ring_capacity)(self.ops.ctx) }
    }
    fn ring_serialize(&mut self, bytes: &[u8]) -> i32 {
        // SAFETY: see the impl-level note above; `bytes` is a live slice, so
        // its pointer and length are valid for the duration of the call.
        unsafe {
            (self.ops.ring_serialize)(self.ops.ctx, bytes.as_ptr(), bytes.len() as FwSizeType)
        }
    }
    fn ring_rotate(&mut self, amount: u64) -> i32 {
        // SAFETY: see the impl-level note above.
        unsafe { (self.ops.ring_rotate)(self.ops.ctx, amount) }
    }
    fn detect(&mut self) -> (i32, u64) {
        let mut size_out: FwSizeType = 0;
        // SAFETY: see the impl-level note above; `size_out` is a live local.
        let status = unsafe { (self.ops.detect)(self.ops.ctx, &mut size_out) };
        (status, size_out)
    }
    fn allocate_frame(&mut self, size: u64) -> bool {
        // SAFETY: see the impl-level note above.
        unsafe { (self.ops.allocate_frame)(self.ops.ctx, size) != 0 }
    }
    fn peek_into_frame(&mut self, size: u64) -> i32 {
        // SAFETY: see the impl-level note above.
        unsafe { (self.ops.peek_into_frame)(self.ops.ctx, size) }
    }
    fn emit_frame(&mut self) {
        // SAFETY: see the impl-level note above.
        unsafe { (self.ops.emit_frame)(self.ops.ctx) }
    }
    fn log_frame_size_error(&mut self, size: u64) {
        // SAFETY: see the impl-level note above.
        unsafe { (self.ops.log_frame_size_error)(self.ops.ctx, size) }
    }
    fn log_no_buffer(&mut self) {
        // SAFETY: see the impl-level note above.
        unsafe { (self.ops.log_no_buffer)(self.ops.ctx) }
    }
    fn log_frame_dropped(&mut self) {
        // SAFETY: see the impl-level note above.
        unsafe { (self.ops.log_frame_dropped)(self.ops.ctx) }
    }
}

/// Run the FrameAccumulator processing loop over one incoming buffer
/// (mirrors `processBuffer` + `processRing`).
///
/// # Safety
/// `ops` must point to a fully-populated trampoline table whose callbacks are
/// valid for `ops.ctx` and do not unwind; `data` must point to `len` readable
/// bytes (null allowed when `len` is 0).
#[no_mangle]
pub unsafe extern "C" fn fprime_rs_accumulator_process(
    ops: *const AccumulatorOpsFfi,
    data: *const u8,
    len: FwSizeType,
) {
    let mut env = AccumulatorEnvFfi { ops: &*ops };
    accumulator::process_buffer(&mut env, view(data, len as usize));
}

/// Claim a free slot in the router's buffer-context table for `key` (the
/// buffer's data pointer). Returns the slot index, or -1 when the table is
/// full. See Rust/fprime-svc/src/router_table.rs for the layout contract.
///
/// # Safety
/// `state0`/`key0`/`stride`/`count` must describe the live C++ entry array
/// per the TableView contract; no concurrent access during the call.
#[no_mangle]
pub unsafe extern "C" fn fprime_rs_router_table_insert(
    state0: *mut u8,
    key0: *mut u8,
    stride: FwSizeType,
    count: FwSizeType,
    active: i32,
    inactive: i32,
    key: *const u8,
) -> i64 {
    let table = router_table::TableView {
        state0,
        key0,
        stride: stride as usize,
        count: count as usize,
        active,
        inactive,
    };
    match table.insert(key as usize) {
        Some(index) => index as i64,
        None => -1,
    }
}

/// Find and release the active slot for `key`. Returns the slot index for
/// the shim to copy the context from, or -1 when not found.
///
/// # Safety
/// Same contract as [`fprime_rs_router_table_insert`].
#[no_mangle]
pub unsafe extern "C" fn fprime_rs_router_table_take(
    state0: *mut u8,
    key0: *mut u8,
    stride: FwSizeType,
    count: FwSizeType,
    active: i32,
    inactive: i32,
    key: *const u8,
) -> i64 {
    let table = router_table::TableView {
        state0,
        key0,
        stride: stride as usize,
        count: count as usize,
        active,
        inactive,
    };
    match table.take(key as usize) {
        Some(index) => index as i64,
        None => -1,
    }
}

// ---------------------------------------------------------------------------
// Generated-type differential proof surface (Phase 4)
// ---------------------------------------------------------------------------
// These exercise the fpp-to-rust GENERATED types (not hand-written ones) so
// the C++ differential test can prove their serialization is byte-identical
// to the C++ code FPP autocodes from the same model.

use fprime_svc::generated::fprime_protocol as gen_proto;

/// Serialize a generated `Svc.FprimeProtocol.FrameHeader`.
///
/// # Safety
/// `out` must point to `capacity` writable bytes (null allowed when 0);
/// `written` must be valid.
#[no_mangle]
pub unsafe extern "C" fn fprime_rs_gen_frameheader_ser(
    start_word: u32,
    length_field: u32,
    mode: i32,
    out: *mut u8,
    capacity: FwSizeType,
    written: *mut FwSizeType,
) -> i32 {
    let value = gen_proto::SvcFprimeProtocolFrameHeader {
        startWord: start_word,
        lengthField: length_field,
    };
    let mut buf = SerBuffer::new(view_mut(out, capacity as usize));
    let result = value.serialize_to(&mut buf, endianness(mode));
    *written = buf.len() as FwSizeType;
    status_code(&result)
}

/// Deserialize a generated `Svc.FprimeProtocol.FrameHeader`.
///
/// # Safety
/// `bytes` must point to `len` readable bytes; out params must be valid.
#[no_mangle]
pub unsafe extern "C" fn fprime_rs_gen_frameheader_deser(
    bytes: *const u8,
    len: FwSizeType,
    mode: i32,
    start_word: *mut u32,
    length_field: *mut u32,
) -> i32 {
    let mut reader = DeserReader::new(view(bytes, len as usize));
    match gen_proto::SvcFprimeProtocolFrameHeader::deserialize_from(&mut reader, endianness(mode)) {
        Ok(value) => {
            *start_word = value.startWord;
            *length_field = value.lengthField;
            0
        }
        Err(e) => e.code(),
    }
}

/// Serialize a generated `Svc.FprimeProtocol.FrameTrailer`.
///
/// # Safety
/// `out` must point to `capacity` writable bytes; `written` must be valid.
#[no_mangle]
pub unsafe extern "C" fn fprime_rs_gen_frametrailer_ser(
    crc_field: u32,
    mode: i32,
    out: *mut u8,
    capacity: FwSizeType,
    written: *mut FwSizeType,
) -> i32 {
    let value = gen_proto::SvcFprimeProtocolFrameTrailer {
        crcField: crc_field,
    };
    let mut buf = SerBuffer::new(view_mut(out, capacity as usize));
    let result = value.serialize_to(&mut buf, endianness(mode));
    *written = buf.len() as FwSizeType;
    status_code(&result)
}

// ---------------------------------------------------------------------------
// Multi-construct generated-type proof surface (Phase 4)
// ---------------------------------------------------------------------------
// Drives the types generated from Rust/test/ut/RustGenTypes.fpp, the model
// written to exercise every construct the generator supports. Two operations
// cover both directions with a tiny surface:
//
//   default  -- serialize a default-constructed value (catches dropped FPP
//               `default` clauses, which are invisible to a round-trip test)
//   roundtrip -- deserialize C++-produced bytes and re-serialize them; byte
//               equality proves field order, widths, and endianness agree
//
// Type ids match FPRIME_RS_GEN_* in Rust/RustGenerated.h.

use fprime_svc::generated::com_cfg as gen_test;

macro_rules! gen_test_dispatch {
    ($id:expr, $ty:ident, $body:expr) => {
        match $id {
            0 => {
                type $ty = gen_test::ComCfgApid;
                $body
            }
            1 => {
                type $ty = gen_test::ComCfgPvn;
                $body
            }
            2 => {
                type $ty = gen_test::ComCfgFrameContext;
                $body
            }
            3 => {
                type $ty = gen_test::TimeBase;
                $body
            }
            _ => fprime_core::SerError::FormatError.code(),
        }
    };
}

/// Serialize a default-constructed value of the selected generated type.
///
/// # Safety
/// `out` must point to `capacity` writable bytes; `written` must be valid.
#[no_mangle]
pub unsafe extern "C" fn fprime_rs_gen_test_default(
    type_id: i32,
    mode: i32,
    out: *mut u8,
    capacity: FwSizeType,
    written: *mut FwSizeType,
) -> i32 {
    gen_test_dispatch!(type_id, T, {
        let mut buf = SerBuffer::new(view_mut(out, capacity as usize));
        let result = T::default().serialize_to(&mut buf, endianness(mode));
        *written = buf.len() as FwSizeType;
        status_code(&result)
    })
}

/// Deserialize `bytes` into the selected generated type and re-serialize it.
/// Byte equality with the input proves the Rust view of the wire format
/// matches whatever produced those bytes.
///
/// # Safety
/// `bytes` must point to `len` readable bytes; `out` to `capacity` writable
/// bytes; `written` must be valid.
#[no_mangle]
pub unsafe extern "C" fn fprime_rs_gen_test_roundtrip(
    type_id: i32,
    bytes: *const u8,
    len: FwSizeType,
    mode: i32,
    out: *mut u8,
    capacity: FwSizeType,
    written: *mut FwSizeType,
) -> i32 {
    gen_test_dispatch!(type_id, T, {
        let mut reader = DeserReader::new(view(bytes, len as usize));
        match T::deserialize_from(&mut reader, endianness(mode)) {
            Ok(value) => {
                let mut buf = SerBuffer::new(view_mut(out, capacity as usize));
                let result = value.serialize_to(&mut buf, endianness(mode));
                *written = buf.len() as FwSizeType;
                status_code(&result)
            }
            Err(e) => e.code(),
        }
    })
}

/// The generated `SERIALIZED_SIZE` of the selected type, for comparison with
/// the autocoded C++ constant.
#[no_mangle]
pub extern "C" fn fprime_rs_gen_test_serialized_size(type_id: i32) -> FwSizeType {
    (match type_id {
        0 => gen_test::ComCfgApid::SERIALIZED_SIZE,
        1 => gen_test::ComCfgPvn::SERIALIZED_SIZE,
        2 => gen_test::ComCfgFrameContext::SERIALIZED_SIZE,
        3 => gen_test::TimeBase::SERIALIZED_SIZE,
        _ => 0,
    }) as FwSizeType
}

// ---------------------------------------------------------------------------
// Svc/ComQueue priority scheduling (Phase 5)
// ---------------------------------------------------------------------------

use fprime_svc::comqueue;

/// Mirrors `fprime_svc::comqueue::Decision`; layout must match
/// FprimeRsComQueueDecision in Rust/RustSvc.h.
#[repr(C)]
pub struct ComQueueDecisionFfi {
    /// Queue index to dequeue and send, or -1 when every queue is empty
    pub selected: i16,
    /// Position of the selected entry in the prioritized list
    pub position: FwSizeType,
    /// Exclusive end of the same-priority run to rotate
    pub rotate_end: FwSizeType,
}

/// Choose the next queue to drain, mirroring `ComQueue::processQueue`'s
/// selection loop. The shim performs the dequeue/send and the rotation of its
/// own QueueMetadata entries.
///
/// # Safety
/// `indices` and `priorities` must each point to `count` readable values;
/// `sizes` to `size_count` readable values; `out` must be valid.
#[no_mangle]
pub unsafe extern "C" fn fprime_rs_comqueue_select(
    indices: *const i16,
    priorities: *const i16,
    count: FwSizeType,
    sizes: *const FwSizeType,
    size_count: FwSizeType,
    out: *mut ComQueueDecisionFfi,
) {
    let count = count as usize;
    let index_slice = if count == 0 {
        &[][..]
    } else {
        core::slice::from_raw_parts(indices, count)
    };
    let priority_slice = if count == 0 {
        &[][..]
    } else {
        core::slice::from_raw_parts(priorities, count)
    };
    let size_slice = if size_count == 0 {
        &[][..]
    } else {
        core::slice::from_raw_parts(sizes, size_count as usize)
    };

    // The entry list is bounded by the component's port count; build it on the
    // stack so this allocates nothing. FPRIME_RS_COMQUEUE_MAX_ENTRIES in
    // Rust/RustSvc.h mirrors this, and ComQueueRust.cpp static_asserts that
    // its TOTAL_PORT_COUNT fits -- so truncation cannot happen silently.
    const MAX_ENTRIES: usize = 64;
    let usable = count.min(MAX_ENTRIES);
    let mut entries = [comqueue::Entry {
        index: 0,
        priority: 0,
    }; MAX_ENTRIES];
    for i in 0..usable {
        entries[i] = comqueue::Entry {
            index: index_slice[i],
            priority: priority_slice[i],
        };
    }

    match comqueue::select(&entries[..usable], size_slice) {
        Some(decision) => {
            *out = ComQueueDecisionFfi {
                selected: decision.selected,
                position: decision.position as FwSizeType,
                rotate_end: decision.rotate_end as FwSizeType,
            }
        }
        None => {
            *out = ComQueueDecisionFfi {
                selected: -1,
                position: 0,
                rotate_end: 0,
            }
        }
    }
}
