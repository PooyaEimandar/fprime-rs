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
`#![no_std]` in every crate, exports declared `extern "C"` (never
`"C-unwind"`), a `#[panic_handler]` routed into the `FW_ASSERT` fault path,
status codes rather than `Result` across the boundary, caller-provided storage
(the crates allocate nothing), and a const assertion that `usize` is at least
as wide as `FwSizeType`.

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

All unsafe code is confined to two places, both by design:

- **`fprime-ffi`** — the `extern "C"` surface. Raw-pointer parameters from
  C++, converted through null-safe `view`/`view_mut` helpers (a null pointer
  with zero length is accepted, matching the `Fw::LinearBufferBase` contract).
- **`fprime-utils::circular` and `fprime-svc::router_table`** — raw-pointer
  routines that exist specifically to tolerate what safe Rust forbids:
  overlap-tolerant per-byte copies matching the C++ loops they mirror, and a
  strided view over a C++-resident entry array.

The pure-logic crates (`fprime-config`, `fprime-core`, and the rest of
`fprime-utils`/`fprime-svc`) contain no unsafe code.

### Borrow width at the FFI boundary

Building a slice from a `(pointer, length)` pair asserts that the *whole*
range is one live allocation, which is a stronger claim than the C++ it
replaces makes — that only ever indexed the bytes it actually touched. The
`Fw` kernel entry points (Phase 8) therefore borrow as narrowly as the
operation allows:

- writes (`fprime_rs_lbb_ser_*`) borrow `capacity` bytes mutably — that is
  what `m_capacity` means, so the claim is the object's own invariant;
- reads (`fprime_rs_lbb_deser_*`) take a `const` pointer and borrow only
  `min(m_serLoc, m_capacity)` bytes — a read can never reach past the write
  high-water mark, so the borrow must not span past it either.

Both assert the buffer is non-null whenever the borrow is non-empty, keeping
the `FW_ASSERT(buffAddr != nullptr)` that each C++ primitive performed. What
neither can check is a `capacity` that overstates the real allocation: no
guard exists on either side of the boundary, and `Fw::Buffer::deserializeFrom`
takes a pointer and a size off the wire without validating them. That is a
pre-existing framework exposure, not one this kernel introduces — but under
the C++ kernel it was an out-of-bounds *access* that ASan catches, and here it
is UB at slice construction, which ASan does not see. Weigh that before
enabling `FPRIME_RUST_IMPL_FW_TYPES` (it is OFF by default).

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
