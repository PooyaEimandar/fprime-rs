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
      into Rust for **every byte it moves between a caller and its storage**:
      the eleven primitive scalar operations (`U8`/`U16`/`U32`/`U64`/`bool` ×
      serialize/deserialize, plus `deserializeTo(I8&)`, the one signed
      overload upstream does not delegate to its unsigned sibling), and the
      six bulk paths that carry the payloads —
      `serializeFrom(const U8*, …)`, `serializeFrom(const LinearBufferBase&)`,
      `deserializeTo(U8*, …)`, `deserializeTo(LinearBufferBase&)`, `setBuff`
      and `copyFrom`, with `copyRaw`/`copyRawOffset` covered transitively.
      The only `memcpy` left in the file is the four float bit-casts, which
      are type punning rather than buffer movement.

      Four of the six bulk paths had **no** test in `Fw/Types/test/ut` at all,
      so `Rust/test/ut/RustFwBulkTest.cpp` pins all six plus
      `copyRaw`/`copyRawOffset` — 20 cases covering each status on each path,
      the exact-fit and one-past boundaries, and what a refused transfer
      leaves behind. It uses only the public `Fw` API, so it passes against
      either implementation and CI runs it in both switch positions: a
      differential oracle rather than a test of the Rust alone.

      `Rust/fprime-core`'s `Cursor` owns the scalar work and
      `plan_bulk_read`/`plan_bulk_write` the bulk bounds decisions — in safe,
      unit-tested code, with checked arithmetic where the C++ expression can
      wrap for a wire-supplied length. The C++ object still owns its storage
      and its `m_serLoc`/`m_deserLoc` cursors and passes them across. Scalar
      reads borrow through a `Reader` that is `const` and spans only
      `min(m_serLoc, m_capacity)` bytes — a deserialize cannot touch a byte
      past the write high-water mark, so it must not borrow one either. The
      bulk paths build no slice at all, because the C++ contract permits the
      source to overlap the destination; they take raw pointers and do one
      memmove-semantics copy.

      `Fw/Types/SerializableRust.cpp` is *generated* from the upstream
      `Serializable.cpp` by `tools/fprime-rs/gen_fw_kernel.py`, not forked
      from it: it is an 800-line near-copy of a file upstream keeps changing,
      and a hand-maintained duplicate would drift silently. It replaces
      exactly those seventeen bodies and nothing else, plus the banner, the
      kernel include, and two `static_assert`s pinning
      `Endianness::BIG == 0`/`LITTLE == 1` — the shim forwards the raw enum
      value rather than re-encoding it, so an out-of-range mode still faults
      instead of being silently read as `LITTLE`.

      The drift check is the point of generating it, so it is built to not be
      defeatable by accident:

      - it compares **content, not formatting** — whitespace, wrapping and
        include order are normalized away — so the repository's format check
        and this check can never contradict each other, a conflict an earlier
        version of this generator did create;
      - it fingerprints each upstream body the generator **replaces or
        removes** into the generated file. Those regions are otherwise a
        blind spot: the output does not contain them, so an upstream fix
        inside one would leave the generated file identical and go unnoticed.
        Removed bodies are the larger hole of the two — for the Phase 9 shims
        they are precisely the helpers the Rust reimplements — and they were
        genuinely unguarded until a review caught it. A changed fingerprint
        says the C++ this Rust mirrors has moved and needs re-reviewing.

      Because every `--check` in CI and in the build runs against a pristine
      tree, it only ever exercises the *pass* path — which would make a check
      that had silently stopped catching anything look healthy.
      `tools/fprime-rs/test_shimgen.py` closes that: it mutates copies and
      asserts the verdict for 14 cases across the three generators, including
      a `..` that escapes the sandbox root and an inverted heap tie-break. It
      runs in CI. `--check` itself runs unconditionally in the Rust job and
      again on every build that enables a shim.

      **Off by default**, alone among the `FPRIME_RUST_IMPL_*` switches. This
      is the only one that taxes the whole framework rather than the module it
      names — see Cost below. Turn it on deliberately, per deployment.

      **What this is not.** The phase's stated goal — "serialization, types,
      buffers become the canonical Rust crates … C++ `Fw` reduced to
      wrappers" — is **not** met, and Rust is not the type authority. What
      moved is one class's byte handling: 17 of the 90 `LinearBufferBase::`
      definitions call Rust; the other 73 are the original C++ verbatim, and
      they are the arithmetic, the cursor bookkeeping and the class plumbing.
      Everything else in `Fw` is untouched: `Fw::Buffer`, the string types,
      `ComPacket`/`CmdPacket`/`TlmPacket`/`LogPacket`, and the `Serializable`
      class hierarchy (`Serializable.hpp` is unmodified). C++ still defines
      the classes, owns the storage and drives the calls; Rust is a callee
      inside them. That is a real reversal of direction, and now a complete
      one for byte movement, but it is not a re-hosting.

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
- [~] **Phase 9 — partial: `Os/Generic` and path safety in Rust.** Two OSAL
      pieces move, chosen because they are algorithm rather than syscall:

      - **`Types::MaxHeap`** (`Os/Generic/Types/`, 260 → 121 lines) — the
        stable max heap that orders *every* message queued to an active
        component, through `Os::Generic::PriorityQueue` (the platform default
        queue on Linux and Darwin) and `PriorityMemQueue`. Rust owns push,
        pop and heapify, including the age tie-break that makes equal
        priorities FIFO — the property a naive heap silently loses. The node
        array is allocated by C++ and indexed by both languages, so the shim
        asserts size, alignment and every field offset against the layout Rust
        reports, once per heap creation: a disagreement would corrupt a queue
        rather than fail.
      - **`Os::FilePathUtils`** (213 → 85 lines) — resolution and containment:
        the code deciding whether a path the spacecraft was *told* to open
        stays inside the directory it is allowed to touch. Pure character
        arithmetic, no filesystem access. `resolveFromCwd` stays C++ because
        its substance is a `getWorkingDirectory` syscall.

      Both are generated by the same mechanism as Phase 8's kernel
      (`tools/fprime-rs/shimgen.py`, now shared by three generators), with the
      same content-not-formatting drift check and upstream-body fingerprints —
      including for the helper functions each generator *removes*, which for
      these two shims are the very functions the Rust reimplements.
      Their oracles are the existing suites: `Types_Max_Heap_test` (rules-based
      via STest), `Os_FilePathUtils_Test` (24 cases), `Os_SandboxedFile_Test`,
      and the three priority-queue suites that run on top of the heap.

      **What was deliberately not ported, and why.** Everything in
      `Os/Posix`, `Os/Darwin` and `Os/Linux` is a thin libc/pthread wrapper —
      `Os/Posix/Mutex.cpp` is 51 lines of pthread, `Os/Darwin/Memory.cpp` is
      one mach call. Routing those through Rust adds an FFI hop and the
      archive tax and buys nothing: the syscall is still the syscall.
      `AtomicQueue` and `LocklessPriorityQueue` are worse than pointless to
      port — their correctness *is* memory ordering, Rust uses the same model,
      and a port would blind the TSan suite that is currently the only thing
      checking them. The meaty logic in `Os/File.cpp`, `FileSystem.cpp`,
      `Directory.cpp` and `Task.cpp` is syscall-free but sits entirely on
      `m_delegate` calls, so moving it means a large hot-path callback table
      and generators for six more upstream files.

      **The "component long tail" is not addressed, and the honest count is
      zero.** There is one real deployment in-tree (`TestDeploymentsProject/Ref`).
      Its topology instantiates 34 framework components; 3 are Rust-backed
      (ComQueue, FrameAccumulator, FprimeRouter) — and the FprimeFramer and
      FprimeDeframer switches from Phase 2 apply to *no* instantiated
      component, because Ref uses the ComCcsds subtopology rather than
      ComFprime. Running Phase 7's generator against the real models: it
      refuses 30 of the 34 outright, and of the 4 it accepts, 3 cannot compile
      because the autocoded base leaves command handlers pure-virtual. Exactly
      one component in the whole deployment is reachable end to end today —
      `Svc::FatalHandler`, whose entire body is four syscalls. The generator
      also cannot reproduce Phases 2/3/5's own shipped work.

      The blocker is not the primitives-only parameter rule; it is that the
      generated glue is a **one-way sink**. Rust cannot call an output port,
      emit an event or telemetry, read a parameter, or answer a command.
      Ordered by how many components each unblocks, the generator needs:
      output-port callbacks (a vtable of `extern "C"` thunks passed alongside
      the instance pointer), then return values across the FFI (which alone
      unblocks `Svc::ApidManager` — 156 lines of pure enum/integer table
      lookup with 272 lines of tests, the best available proving ground), then
      `ref` parameters for serializable types
- [ ] **Phase 10 — Drop the C++ toolchain** (per deployment): no fpp-to-cpp, no
      C++ objects, cargo-only build

