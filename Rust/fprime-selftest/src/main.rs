//! The first fprime-rs artifact that runs with **no C++ linked at all**.
//!
//! Phase 10's goal is a cargo-only deployment. Through Phases 0-9 that goal
//! was unfalsifiable from inside this repository: `fprime-ffi` is a staticlib
//! whose panic handler calls `fprime_rs_panic_hook`, defined in
//! `Rust/RustPanicHook.cpp`, so nothing the workspace produced could link
//! without a C++ object. One symbol, twenty-four lines of C++, was the entire
//! language-level dependency -- and it was enough to make "cargo-only"
//! untestable.
//!
//! This binary removes that. It exercises `fprime-core` (the serialization
//! kernel) and `fprime-utils` (CRC32, the CFDP checksum, the OSAL max heap
//! and path resolution) with the platform libc underneath and no C++ runtime.
//! `fprime-svc` is a declared dependency so that it too is built and linked
//! by this binary, but its component logic needs a running component to
//! exercise, so nothing here calls into it.
//!
//! It is emphatically **not** a deployment. There is no component runtime, no
//! OSAL, no topology and no ground-system link; it does not prove Phase 10 is
//! close. What it proves is narrower and was previously untested: that this
//! Rust actually stands up on its own, and that "does a cargo-only build
//! produce a working binary" is now a question with a checkable answer.
//!
//! CI runs it and asserts the linked libraries contain no C++ runtime.

use fprime_core::{Endianness, SerBuffer};
use fprime_utils::{
    checksum, crc32,
    max_heap::{MaxHeap, Node},
    path,
};

/// Exit status: any failure is a hard failure, so CI notices.
fn check(name: &str, ok: bool) -> bool {
    println!("{:<28} {}", name, if ok { "ok" } else { "FAILED" });
    ok
}

fn serialization_round_trips() -> bool {
    let mut store = [0u8; 32];
    let mut buf = SerBuffer::new(&mut store);
    buf.ser_u32(0xDEAD_BEEF, Endianness::Big).is_ok()
        && buf.ser_u16(0x1234, Endianness::Little).is_ok()
        && buf.len() == 6
        && store[..4] == [0xDE, 0xAD, 0xBE, 0xEF]
        && store[4..6] == [0x34, 0x12]
}

fn crc_and_checksum_agree_with_known_vectors() -> bool {
    // The standard CRC-32/ISO-HDLC check vector. F Prime's routine performs
    // neither the initial nor the final one's complement, so both are applied
    // here -- the same way `Utils::Hash` uses it.
    let crc = !crc32::update(0xFFFF_FFFF, b"123456789");
    let mut sum = 0u32;
    checksum::update(&mut sum, b"\x01\x02\x03\x04", 0);
    crc == 0xCBF4_3926 && sum == 0x0102_0304
}

fn max_heap_is_stable() -> bool {
    let mut nodes = [Node::default(); 8];
    let (mut size, mut order) = (0, 0);
    {
        let mut heap = MaxHeap {
            nodes: &mut nodes,
            size: &mut size,
            order: &mut order,
        };
        for id in 0..4u64 {
            if !heap.push(1, id) {
                return false;
            }
        }
        // Equal priorities must come back in the order they went in.
        for id in 0..4u64 {
            if heap.pop() != Some((1, id)) {
                return false;
            }
        }
    }
    size == 0
}

fn path_resolution_contains_the_sandbox() -> bool {
    let mut out = [0u8; 128];
    let escaped = path::resolve_path(b"/data/uplink/../../etc/passwd\0", None, &mut out, 128);
    let end = out.iter().position(|&b| b == 0).unwrap_or(out.len());
    escaped == path::Status::Valid
        && &out[..end] == b"/etc/passwd"
        && path::check_containment(b"/etc/passwd\0", b"/data/uplink/\0", 128)
            == path::Status::OutsideSandbox
}

fn main() {
    println!("fprime-rs self-test: the Rust crates, running with no C++ linked");
    println!("{}", "-".repeat(64));

    let results = [
        check("serialization", serialization_round_trips()),
        check(
            "crc32 + cfdp checksum",
            crc_and_checksum_agree_with_known_vectors(),
        ),
        check("max heap (stable order)", max_heap_is_stable()),
        check("path resolution", path_resolution_contains_the_sandbox()),
    ];

    let failed = results.iter().filter(|ok| !**ok).count();
    println!("{}", "-".repeat(64));
    if failed == 0 {
        println!("{} checks passed", results.len());
    } else {
        eprintln!("{failed} of {} checks FAILED", results.len());
        std::process::exit(1);
    }
}
