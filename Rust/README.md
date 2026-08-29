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
- [ ] **Phase 8 — `Fw` core in Rust**: serialization/types/buffers become the
      canonical Rust crates; C++ consumes them via reverse shims (the flip point)
- [ ] **Phase 9 — OSAL in Rust + component long tail**: port what deployments
      actually instantiate
- [ ] **Phase 10 — Drop the C++ toolchain** (per deployment): no fpp-to-cpp, no
      C++ objects, cargo-only build

