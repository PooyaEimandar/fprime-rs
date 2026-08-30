# fprime-rs

Incremental Rust port of F´ components, keeping FPP as the source of truth and
mixed C++/Rust deployments working at every step.

## Roadmap

- [x] **Phase 0 — Skeleton**: cargo workspace, `fprime-config` generated from
      the FPP config, wire-compatible `no_std` codec, panic→`FW_ASSERT` bridge,
      CMake/cargo integration, differential UT green under `fprime-util check`
- [x] **Phase 1 — Foundation libraries**: CircularBuffer byte engine, CRC32,
      and CFDP checksum in Rust behind unchanged C++ headers, existing UTs as
      oracle; per-module `FPRIME_RUST_IMPL_*` rollback switches proven both
      ways (Queue's orchestration stays C++ atop the Rust engine — its header
      encapsulation blocks direct porting until Phase 4 codegen)
- [x] **Phase 2 — First components**: `Svc/FprimeDeframer` and
      `Svc/FprimeFramer` logic in Rust (`fprime-svc` crate) behind unchanged
      component classes; the framer calls the `bufferAllocate` returning port
      from inside Rust via a trampoline; component UTs green Rust-backed, and
      the Ref GDS integration suite (11 tests) passes against the mixed
      deployment (Ref's CCSDS stack runs the Phase 1 Rust engine; these two
      components serve ComFprime-based deployments)
- [x] **Phase 3 — Stateful data path**: `FrameAccumulator`'s full
      accumulate/detect/extract orchestration in Rust driving the component
      through a 13-callback trampoline table (ring, detector, allocation,
      events); `FprimeRouter`'s buffer-context table state machine in Rust
      over the C++-resident entries; ring engine made overlap-tolerant
      (raw-pointer per-byte copy, matching the C++ loop) after in-tree
      detector tests exposed self-serialization; all suites green and Ref GDS
      integration 11/11 with the Rust orchestration live in the uplink
- [x] **Phase 4 — Codegen (types)**: `tools/fprime-rs/fpp_to_rust.py` generates
      Rust structs/enums/arrays from FPP models via `fpp-to-json` (the FPP
      compiler is never modified), folding `default` clauses through FPP's own
      `valueMap` and emitting `SERIALIZED_SIZE` to match the autocode;
      `cmake/autocoder/rust.cmake` implements the autocoder plugin contract to
      verify the checked-in generated code against its model at build time
      (`-DFPRIME_RUST_AUTOCODE=ON`, exercised in CI). **Scope, stated plainly:**
      this generates *types*, not component handlers or FFI glue — a new
      component still needs hand-written shims (component generation is Phase 7
      work). Differential coverage is `Svc/FprimeProtocol` (flat U32 structs)
      and `default/config/ComCfg` (enums with explicit reprs and `default`
      clauses, a nine-member struct with defaults, nested enums, bools, several
      integer widths), each compared default-constructed, round-tripped, and on
      `SERIALIZED_SIZE`, in both endianness modes. **Not yet covered by a
      differential test:** arrays, floats, 64-bit members, and member arrays —
      the generator handles them and they have generator-level tests, but no
      byte-for-byte comparison against autocode. String members are refused
      with a clear error. The retrofit onto generated types is the deframer's
      header parse and the protocol size constants; the trailer read and the
      framer's writes are still hand-rolled.
- [~] **Phase 5 — partial**: what moved to Rust is ComQueue's *priority-selection
      predicate* — roughly 19 lines deciding which queue to drain next and how
      far its same-priority run extends (`fprime-svc/src/comqueue.rs`). **This is
      not an active-component port:** the thread, the `Os::Queue`, enqueue, the
      three commands, overflow hooks, buffer ownership and the rotation itself
      all remain C++ in a shim that is slightly *longer* than the file it
      replaces. Equivalence to the original algorithm was brute-forced over
      ~12M (list, priority, emptiness) combinations, and `Rust_comqueue_scheduler_ut`
      covers the round-robin rotation across the FFI — which ComQueue's own
      suite never exercises. **`LinuxUartDriver`: characterized and its
      defect fixed.** It had no tests at all; it now has five, driving a real
      pseudo-terminal and a deliberately-filled non-blocking pipe. With that
      oracle in place, the partial-write data loss was fixed as a test-pinned
      divergence: `writeAll` resumes across short writes and retries `EINTR`
      instead of reporting the whole send failed, every byte that reached the
      device is counted, and a send that consumed nothing now returns
      `SEND_RETRY` rather than `OTHER_ERROR`. The driver's own logic stays
      C++ — it is termios configuration and libc I/O, with no algorithmic
      core worth moving. **`LinuxGpioDriver` and `LinuxI2cDriver` remain
      untouched**: both drive char devices through `ioctl` and need new test
      infrastructure first, so the GPIO poll-error spin is still unfixed.
      I2C's `test/ut` is unregistered, does not compile against the current
      `Fw::Buffer` API, and needs real hardware — it is not coverage
- [x] **Phase 6 — Qualification hardening**: a safety lint posture enforced
      workspace-wide (`unsafe_op_in_unsafe_fn` and
      `clippy::undocumented_unsafe_blocks`, both denied and passing — every
      unsafe block now carries a justification), plus [SAFETY.md](SAFETY.md)
      recording what is enforced, what is deliberately *not* enforced and why,
      the unsafe inventory, and a concrete Ferrocene evaluation. **Ferrocene
      was evaluated and not adopted**, for reasons that are facts about the
      toolchain: DO-178C is not certified (only "supports" DAL C efforts), its
      current release would regress this workspace from Rust 1.97 to ~1.95,
      `--crate-type staticlib` — the whole design — is outside its qualified
      options, and only x86-64 Linux hosts produce qualified output. The
      Safety-Critical Rust Consortium's coding guidelines have nothing to
      adopt yet: their `unsafety`, `ffi`, and `concurrency` chapters are
      empty
- [~] **Phase 7 — partial: generated component glue.** `fpp_to_rust.py
      --component` now generates, from a component's FPP model, the three
      layers that Phases 2/3/5 hand-wrote per component: a Rust handler trait,
      the `extern "C"` dispatch stubs, and the C++ shim that forwards each
      port handler across. `Svc/RustDemo` demonstrates it end to end — a real
      framework component driven through its real ports into Rust, with no
      hand-written glue. Dispatch carries the component **instance** (an
      opaque pointer installed by `setRustHandlers` before ports connect), not
      a per-type global, so multiple instances do not share state and no
      `static mut` is read unsynchronized.

      **What this is not.** The phase's stated goal — "a component runs with
      no per-component C++ at all" — is **not** met: the generated shim and a
      hand-written component header are still per-component C++, and the
      autocoded C++ base still owns ports, threading and all framework
      plumbing. Nothing of the dispatch loop, queue consumption, or
      command/telemetry/event/parameter handling moved to Rust. The generator
      accepts only input ports whose parameters are primitives or bools passed
      by value with no return type, and refuses everything else by name —
      which means essentially no existing framework component qualifies today
      (they carry `Fw::Buffer`, user types and `ref` parameters), and the
      demonstration component was written to fit those limits. The
      hand-written shims from Phases 2, 3 and 5 were **not** retrofitted onto
      the generator
- [~] **Phase 8 — partial: the serialization kernel flips.** The interop
      direction reverses for the first time. Through Phases 1–7 Rust
      reimplemented logic that C++ called into at the edges; here C++'s own
      `Fw::LinearBufferBase` — the primitive read/write path underneath every
      component, every autocoded type and every packet on the wire — calls
      into Rust for all eleven primitive scalar operations (`U8`/`U16`/`U32`/
      `U64`/`bool` × serialize/deserialize, plus `deserializeTo(I8&)`, the one
      signed overload upstream does not delegate to its unsigned sibling).
      The remaining signed and floating-point overloads delegate to those, so
      **every primitive scalar serialization in the framework goes through
      Rust**. `Rust/fprime-core`'s `Cursor` owns the byte work; the C++ object
      still owns its storage and its `m_serLoc`/`m_deserLoc` cursors and
      passes them across. Reads borrow through a separate `Reader` that is
      `const` and spans only `min(m_serLoc, m_capacity)` bytes — a
      deserialize cannot touch a byte past the write high-water mark, so it
      must not borrow one either.

      `Fw/Types/SerializableRust.cpp` is *generated* from the upstream
      `Serializable.cpp` by `tools/fprime-rs/gen_fw_kernel.py`, not forked
      from it: it is an 800-line near-copy of a file upstream keeps changing,
      and a hand-maintained duplicate would drift silently. The generator
      replaces exactly those eleven bodies and nothing else (the full diff
      against the original is 15 hunks: the eleven bodies — the `I8` one
      splitting in two — plus the banner, the kernel include, and two
      `static_assert`s pinning `Endianness::BIG == 0`/`LITTLE == 1`, which the
      shim relies on by forwarding the raw enum value rather than re-encoding
      it — so an out-of-range mode still faults instead of silently reading as
      `LITTLE`). Output is emitted pre-wrapped to the repository's
      clang-format style, so the format check and the drift check can both
      pass. `--check` runs unconditionally in the Rust CI job, and again on
      every build that enables the kernel; `Fw/Types/**` is in the workflow's
      path filter, so an upstream edit that never reached the Rust kernel
      trips it instead of quietly diverging.

      **Off by default**, alone among the `FPRIME_RUST_IMPL_*` switches. This
      is the only one that taxes the whole framework rather than the module it
      names — see Cost below. Turn it on deliberately, per deployment.

      **What this is not.** The phase's stated goal — "serialization, types,
      buffers become the canonical Rust crates … C++ `Fw` reduced to
      wrappers" — is **not** met, and Rust is not the type authority. What
      moved is the primitive scalar kernel: 11 of the 90 `LinearBufferBase::`
      definitions in the file call Rust; the other 79 are the original C++
      verbatim. Everything else in `Fw` is untouched: `Fw::Buffer`, the string
      types, `ComPacket`/`CmdPacket`/`TlmPacket`/`LogPacket`, the
      `Serializable` class hierarchy (`Serializable.hpp` is unmodified), and
      all bulk byte movement — `serializeFrom(const U8*, …)`,
      `deserializeTo(U8*, …)`, `setBuff`, `copyRaw`/`copyRawOffset`,
      `copyFrom` — which stays C++ `memcpy` and carries the majority of the
      actual bytes for strings, `Fw::Buffer` payloads and DP containers. C++
      still defines the classes, owns the storage and drives the calls; Rust
      is a callee inside them. That is a real reversal of direction, but not a
      re-hosting.

      **Cost**, measured, not estimated (`-O2`, arm64, identical bytes out of
      both implementations):

      - an 8-field struct round-trip, which is what an autocoded type does:
        **24.8–27.3 ns → 38.8–39.4 ns, about +55%**
      - a single `U32` serialize+deserialize pair: 4.4 ns → 5.2 ns, +19%
      - **+308 KB per binary** (`Fw_Types_ut_exe`: 7,003,424 → 7,312,208
        bytes, +4.4%). The Rust workspace is one codegen unit and F´ passes no
        `--gc-sections`/`-dead_strip`, so referencing one `fprime_rs_lbb_*`
        symbol pulls the whole FFI surface. Because `Fw_Types` links
        `fprime_rust` `PUBLIC`, this lands on every binary — including
        deployments that instantiate no Rust-backed component at all.

      Each primitive is now a cross-language call the compiler cannot inline,
      and `FpConfig.h`'s `FW_SERIALIZE_FORCE_INLINE_LBB` knob (shipped empty,
      offered as a tuning option) can no longer do anything for these bodies,
      since there is no cross-language LTO configured. The full UT suite's
      wall clock is *not* usable as evidence here — those builds are Debug
      with sanitizers and also link the Rust archive into every test binary.

      **Safety, honestly.** Building a slice from a `(pointer, capacity)` pair
      asserts the whole range is one live allocation — a stronger claim than
      the C++ made, which only indexed the bytes it touched. The dropped
      per-primitive `FW_ASSERT(buffAddr != nullptr)` is restored on the Rust
      side, and reads borrow only the written region, but a `capacity` that
      overstates the real allocation is unguarded on both sides — and
      `Fw::Buffer::deserializeFrom` takes a pointer and a size off the wire
      without validating them. That exposure predates this phase, but under
      the C++ kernel it was an out-of-bounds access ASan catches, and here it
      is UB at slice construction, which ASan does not see. See
      `Rust/SAFETY.md`.

      **What CI actually proves.** The framework-check job builds with
      `FPRIME_RUST_IMPL_FW_TYPES=ON` and runs the `Fw/Types` suite (the
      kernel's oracle) plus the other Rust-backed module suites — not the
      whole tree. Locally the full 136-suite run passes with the kernel ON,
      and passes with it OFF with zero references to the Rust kernel in the
      build. No-cargo builds (the RHEL8 job, which is also the only GNU-ld
      job) and every external project consuming F´ via `find_package` silently
      build the original C++ kernel, so the exit criterion is unverified
      outside framework-root builds on a machine with cargo
- [ ] **Phase 9 — OSAL in Rust + component long tail**: port what deployments
      actually instantiate
- [ ] **Phase 10 — Drop the C++ toolchain** (per deployment): no fpp-to-cpp, no
      C++ objects, cargo-only build

