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
