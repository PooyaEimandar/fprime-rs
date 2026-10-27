# fprime-rs safety and qualification posture

What is enforced today, what a qualified toolchain would cost, and what is
deliberately not claimed. Phase 6 of the [roadmap](README.md).

## Enforced mechanically

Two lints, denied workspace-wide via `[workspace.lints]` in `Cargo.toml`, both
chosen because they force the C++/Rust contract to be written down where it
actually lives — the unsafe surface — and because review alone catches these
poorly:

| Lint | Effect |
| --- | --- |
| `unsafe_op_in_unsafe_fn` (deny) | An unsafe operation inside an `unsafe fn` must still sit in an explicit `unsafe` block, so the reviewed unit is the operation rather than the whole function. |
| `clippy::undocumented_unsafe_blocks` (deny) | Every `unsafe` block carries a `SAFETY:` comment. An unjustified block in this port is an unreviewed contract. |

Already in force from earlier phases: `panic = "abort"` in both profiles,
`#![no_std]` in every crate that ships into a deployment, exports declared
`extern "C"` (never `"C-unwind"`), a `#[panic_handler]` routed into the
`FW_ASSERT` fault path, status codes rather than `Result` across the boundary,
caller-provided storage in the shipping crates (they allocate nothing), and a
const assertion that `usize` is at least as wide as `FwSizeType`.

The one exception is `fprime-selftest` (Phase 10), which is deliberately a
`std` binary: its whole purpose is to be an executable that links no C++, so
it prints and exits. It is not linked into any deployment and nothing depends
on it.

`fprime-ffi` sets `unsafe_op_in_unsafe_fn = "allow"`. Every function in that
crate is an `unsafe extern "C"` boundary whose body *is* the unsafe operation,
and each carries a `# Safety` contract in its docs; wrapping each body in a
redundant inner block is queued cleanup, not a safety gap. The lint that
matters there — `undocumented_unsafe_blocks` — is denied and passing.

### Deliberately not enabled

Recorded so the omissions are decisions rather than oversights:

- **`clippy::arithmetic_side_effects`, `clippy::indexing_slicing`** — fire
  pervasively in codec and ring-buffer code whose wrapping and indexing *are*
  the specified behavior, mirroring C++ that wraps identically. Enabling them
  would mean hundreds of per-site allows, hiding the few sites worth attention.
- **`clippy::unwrap_used` / `expect_used`** — the only uses are in unit tests,
  where a panic is the reporting mechanism.
- **`missing_docs`** — generated modules would need blanket allows, weakening
  the signal for hand-written code.
- **`clippy::cast_possible_truncation`** — the FFI's width conversions are
  guarded by the `usize >= FwSizeType` const assertion, which is the check
  that actually matters.

## Ferrocene: evaluated, not adopted

[Ferrocene](https://ferrocene.dev/) is a downstream distribution of upstream
rustc from Ferrous Systems, TÜV SÜD certified for **ISO 26262 ASIL D / TCL 3**,
**IEC 61508 SIL 3**, and **IEC 62304 Class C**, with a certified *subset* of
`core` at SIL 2 / ASIL B.

Adopting it is not currently recommended for this port, for four concrete
reasons — each of which is a fact about Ferrocene, not a judgment about Rust:

1. **DO-178C is not certified.** Ferrous states the toolchain *supports*
   customer certification efforts toward DO-178C (DAL C); it does not carry
   the certification. For a flight-software audience this is the headline.
2. **It would regress the compiler version.** The current release (26.05.0)
   folds in Rust 1.93–1.95; this workspace pins 1.97.0. Adoption means moving
   backwards and reverting anything relying on newer stabilizations.
3. **`--crate-type staticlib` is not in the qualified options list.** The
   entire fprime-rs design is a staticlib linked into C++, so the emission
   path this port depends on sits outside qualified scope and would need a
   separate verification argument.
4. **Only x86-64 Linux glibc hosts produce qualified output.** Development on
   macOS and any non-Linux CI would be non-qualified by construction.

It is also commercial (per-seat subscription, no free tier found) and installs
through `criticalup` rather than rustup.

If a project does adopt it, the mechanics are small. Add a `criticalup.toml`:

```toml
manifest-version = 1
[products.ferrocene]
release = "stable-26.05.0"
packages = ["rustc-${rustc-host}", "rust-std-${rustc-host}", "cargo-${rustc-host}", "rust-src"]
```

then `criticalup install && criticalup link create`, and point
`rust-toolchain.toml` at the linked `ferrocene` channel. Align the build flags
Ferrocene requires against the certified `core`: `-C panic=abort` (already
set), `--edition 2021` (already set), and `-C opt-level=2` — note the release
profile currently uses cargo's default `opt-level = 3`, which would need
pinning.

What Ferrocene would *not* cover, and what stays this project's burden either
way: `std`/`alloc`, cargo and build scripts, clippy and rustfmt, every
third-party crate, all first-party Rust and C++ code, and the FFI boundary
itself.

## Safety-Critical Rust Consortium guidelines: nothing to adopt yet

The Rust Foundation's Safety-Critical Rust Consortium publishes
[coding guidelines](https://coding-guidelines.arewesafetycriticalyet.org/),
but as of this writing they are not usable as a normative standard for this
port: the document carries on the order of two dozen directives, and the
chapters that would govern this codebase — `unsafety`, `ffi`, `concurrency`,
`ownership-and-destruction` — are **empty**. There is no published MISRA Rust
standard.

That is precisely why the enforced posture above is lint-based: mechanical
enforcement is the substance available today, and it targets the unsafe and
FFI surface the guidelines do not yet cover.

## Unsafe inventory

The hand-written unsafe code is concentrated in these modules:

- **`fprime-ffi`** — the `extern "C"` surface. Raw-pointer parameters from
  C++, converted through null-safe `view`/`output` helpers (a null pointer
  with zero length is accepted, matching the `Fw::LinearBufferBase` contract).
- **`fprime-utils::circular` and `fprime-svc::router_table`** — raw-pointer
  routines that exist specifically to tolerate what safe Rust forbids:
  overlap-tolerant per-byte copies matching the C++ loops they mirror, and a
  strided view over a C++-resident entry array.
- **`fprime-core::output`** — a bounded write-only view over `MaybeUninit<u8>`
  storage. Its safe API exposes byte slices only over a known initialized prefix.

Phases 12 and 13 add unsafe to *generated* code for the first time. The
component glue now dereferences two kinds of caller-owned pointer: a `ref`
parameter (`&mut *value`) and an `Fw::Buffer`'s payload
(`slice::from_raw_parts`). Both are valid only for the duration of the port
call, which is exactly the scope they are created in, and the generator emits
a SAFETY comment on each — the workspace lints require it, so a generator
change that dropped one would fail the build rather than pass quietly. The
buffer's `as_slice` is `unsafe fn` rather than safe, because its validity
depends on the C++ object outliving the call, which Rust cannot check.

The Phase 9 OSAL work adds **one** new unsafe pattern: `c_str`/`c_str_len` in
`fprime-ffi` take a C string whose length the caller does not pass, scan it
byte by byte for the terminator, and only then build a slice of the length
found. The scan is deliberately not a slice: the caller's string may be
shorter than the `max_path` bound, and a slice spanning past its allocation
is UB even if nothing reads that far. An unterminated string within the bound
is reported as a malformed path rather than scanned past, which is what the
C++ `Fw::StringUtils::string_length` bound does.

Phase 16 adds `queue_slots` on the same footing as `max_heap`: a safe module
whose FFI wrapper builds slices over storage the C++ `PriorityQueueHandle`
allocated. It adds no new unsafe pattern — the two constructors assert the
pointers are non-null unless the depth is zero, exactly as `heap` does, and
the slab's `depth * max_size` product is computed with `checked_mul` before it
becomes a slice length, because a wrap there would produce a slice shorter
than the allocation it describes rather than a failure. Every one of these
entry points is called by the C++ only while holding the handle's
`m_data_lock`, so none of them synchronizes: the lock is the C++ side's, and
moving it would have split one mutex across two languages.

Otherwise `max_heap`, `queue_slots` and `path` are safe modules in `fprime-utils`, and their
FFI wrappers build a slice over caller-provided storage exactly as the Phase 8
scalar entries do. The one
thing worth naming is that `MaxHeap`'s node array is allocated and indexed by
C++ as well as Rust, so the two `Node` layouts must agree: the Rust side
reports its own through `fprime_rs_maxheap_node_layout`, and the generated
shim asserts size, alignment and every field offset against the C++ `Node` at
heap creation. A disagreement would corrupt a queue silently rather than
fail, which is why it is checked rather than assumed.

`fprime-config` contains no unsafe code. Outside `output`, `fprime-core`
contains no unsafe code, and neither does the rest of `fprime-utils`.
`fprime-svc` has one additional hand-written exception:
`rust_demo.rs` calls `Buffer::as_slice`, which is `unsafe fn` because the
payload is valid only for the duration of the port call. That is the shape
every component handler taking an `Fw::Buffer` will have, so it is named here
rather than left to look like an oversight.

### Borrow width at the FFI boundary

Building a slice from a `(pointer, length)` pair asserts that the *whole*
range is one live allocation, which is a stronger claim than the C++ it
replaces makes — that only ever indexed the bytes it actually touched. The
`Fw` kernel entry points (Phase 8) therefore borrow as narrowly as the
operation allows:

- primitive writes (`fprime_rs_lbb_ser_*`) borrow only the unused suffix
  from `m_serLoc` to `m_capacity` as `MaybeUninit<u8>`; the capacity must
  describe a live writable allocation, but unused bytes need not be initialized;
- reads (`fprime_rs_lbb_deser_*`) take a `const` pointer and borrow only
  `min(m_serLoc, m_capacity)` bytes — a read can never reach past the write
  high-water mark, so the borrow must not span past it either.

The **bulk** entry points (`fprime_rs_lbb_ser_bytes`, `deser_bytes`,
`set_buff`, `copy_bytes`) build no slice at all. They cannot: the C++ contract
explicitly permits the source of a bulk serialize to overlap the destination,
and two aliasing Rust slices are UB regardless of what is read. So they take
raw pointers, make the bounds decision in safe code
(`fprime_core::plan_bulk_read`/`plan_bulk_write`, with checked arithmetic
where the C++ expression can wrap), and then perform one `core::ptr::copy`.
`copy` has memmove semantics throughout, including where the mirrored C++ used
`memcpy` — a superset, so these are defined wherever the original was, and
defined in the overlapping case where it was not. That is a deliberate,
one-directional divergence: no call that was well-defined changes behavior.

Both kinds assert the buffer is non-null whenever they will touch it, keeping
the `FW_ASSERT(buffAddr != nullptr)` that each C++ primitive performed. What
neither can check is a `capacity` that overstates the real allocation: no
guard exists on either side of the boundary, and `Fw::Buffer::deserializeFrom`
takes a pointer and a size off the wire without validating them. That is a
pre-existing framework exposure, not one this kernel introduces — but under
the C++ kernel it was an out-of-bounds *access* that ASan catches, and here it
is UB at slice construction, which ASan does not see. Weigh that before
enabling `FPRIME_RUST_IMPL_FW_TYPES` (it is OFF by default).

### Borrowing uninitialized storage

C++ output buffers routinely start uninitialized, including serialization
buffers, strings, resolved paths, and frame allocations. The FFI `output`
helper borrows these as `&mut [MaybeUninit<u8>]`, never as `&mut [u8]`.
`fprime-core::output::OutputBytes` writes with bounds checks, tracks the
contiguous initialized prefix, and exposes byte slices only within that
prefix. Framing computes its CRC after initializing the header and payload;
path normalization reads only the constructed, terminated path. Neither
operation initializes or reads unused output capacity.

Sparse queue slabs use raw copies of the stored message length instead of a
slice over the whole slab. Circular-buffer scalar reads copy only the
allocated ring region into initialized local bytes; metadata-only operations
do not borrow the backing storage. These contracts preserve C++ callers that
initialize only part of their allocation. The boundary tests pass
`MaybeUninit` storage through serialization, strings, paths, framing, queues,
and wrapped ring reads, and check preserved prefixes and unused bytes.

### A shim reports its own filename

`FW_FILENAME_ASSERT`, the default assert mode, reports `__FILE__`. A generated
shim is a different file from the one it replaces, so an assert inside it
names e.g. `TimeRust.cpp` rather than `Time.cpp`. Under the other two modes
this is avoidable and is avoided: `Fw/Time/CMakeLists.txt` and
`Fw/Types/CMakeLists.txt` recompute `ASSERT_FILE_ID` and
`ASSERT_RELATIVE_PATH` from each original's path, so the fault identity a
flight system keys on is unchanged. Under `FW_FILENAME_ASSERT` it is not
avoidable without misreporting line numbers, and `Fw/Time`'s death tests were
widened to accept either name.

This matters more for `StringUtilsRust.cpp` than for the others, because
`Fw::StringUtils` asserts on caller mistakes — a null string, an overlapping
copy — rather than on internal invariants. Those are the asserts a deployment
is most likely to actually hit, so they are the ones whose reported identity
is worth keeping stable.

## Reading a Rust-side fault in telemetry

A Rust panic is not a C++ `FW_ASSERT` and does not report like one. Every
panic routes through `Rust/RustPanicHook.cpp`, so the assert's file identity
is always `RustPanicHook.cpp` and its line is always the hook's — never the
failing serialization site. The information identifying the real site is
demoted to the **first assert argument**, which carries the line number
within `Rust/fprime-ffi/src/lib.rs` (or whichever Rust file panicked).

So `Assert: "Rust/RustPanicHook.cpp:19" 69` means *a Rust panic occurred at
`fprime-ffi/src/lib.rs:69`*, not a fault in the hook. Operators and fault
handlers keying on `ASSERT_FILE_ID` will see one identity for every Rust-side
failure in the framework; the discriminator is the argument.
