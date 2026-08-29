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
