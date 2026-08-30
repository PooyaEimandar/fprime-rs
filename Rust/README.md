# fprime-rs

Incremental Rust port of F´ components, keeping FPP as the source of truth and
mixed C++/Rust deployments working at every step.

## Roadmap

- [x] **Phase 0 — Skeleton**: cargo workspace, `fprime-config` generated from
      the FPP config, wire-compatible `no_std` codec, panic→`FW_ASSERT` bridge,
      CMake/cargo integration, differential UT green under `fprime-util check`
- [x] **Phase 1 — Foundation libraries**: CircularBuffer byte engine, CRC32,
      and CFDP checksum in Rust behind unchanged C++ headers, existing UTs as
      oracle; per-module `FPRIME_RUST_IMPL_*` rollback switches, whose OFF
      position CI *builds* but does not test — no suite is ever run against a
      C++ original, and the OFF side is reached only through the global
      `FPRIME_ENABLE_RUST=OFF`, never a per-module switch. Queue's
      orchestration stays C++; an earlier note here said Phase 4 codegen would
      unblock it, which was never true — `Utils/Types` has no FPP model at all,
      so the type generator was never a route to it. Queue and `SpscQueue`
      remain untouched
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
      differential test:** arrays, floats, 64-bit members, member arrays and
      structs-of-structs. The generator emits all of them, but nothing pins
      them: there is **no test of `fpp_to_rust.py` at all** (the only generator
      test in the tree, `test_shimgen.py`, covers the three shim generators),
      and no checked-in generated module contains an array, a float or a
      64-bit member. The primitive codec underneath *is* differentially pinned
      for U64/I64/F32/F64 — the gap is generated *types* carrying them.

      The dominant real-world refusal is not strings but **array sizes given
      by a named constant**: the generator requires an integer literal, and
      every one of the 11 `array` definitions in `Fw`/`Svc`/`Drv`/`Ref` uses a
      constant. FPP's own `valueMap` already resolves these — it is what Phase
      4 used for defaults — so this is a small fix that has simply never been
      made. Strings, abstract types as struct members (`Fw.Buffer` in
      `CommsPorts` and `Fw/Dp`), and defaults on struct- or array-typed members
      are also refused. The retrofit onto generated types is the deframer's
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

      - **`Types::MaxHeap`** (`Os/Generic/Types/`, 260 → 126 lines) — the
        stable max heap that orders *every* message queued to an active
        component, through `Os::Generic::PriorityQueue` (the platform default
        queue on Linux and Darwin) and `PriorityMemQueue`. Rust owns push,
        pop and heapify, including the age tie-break that makes equal
        priorities FIFO — the property a naive heap silently loses. The node
        array is allocated by C++ and indexed by both languages, so the shim
        asserts size, alignment and every field offset against the layout Rust
        reports, once per heap creation: a disagreement would corrupt a queue
        rather than fail.
      - **`Os::FilePathUtils`** (213 → 95 lines) — resolution and containment:
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
- [~] **Phase 10 — partial, and the goal as stated is not reachable.** The
      exit criterion was a cargo-only deployment: no fpp-to-cpp, no C++
      objects. Measured against `TestDeploymentsProject/Ref`'s actual link
      line, that means replacing **261,923 lines of C++** (398 sources,
      524 headers), of which **81.7% is fpp-to-cpp autocode**. The autocode
      is not a windfall: **117,232 of those lines are the 41 `ComponentAc`
      classes** — the dispatch loops, queue consumption, and
      command/telemetry/event/parameter machinery — and *nothing in this tree
      generates any of it*. `RefTopologyAc.cpp` alone is 15,530 lines from a
      582-line model. The whole Rust workspace is ~5,600 lines, `no_std`, with
      no threads, no allocator and no OSAL. This phase is not one more
      increment; it is a component runtime plus three generators.

      Two measurements cut the other way and are worth recording. The **GDS
      dictionary is already free**: `fpp-to-dict` builds Ref's 398 KB
      dictionary from FPP models with zero object files, so a cargo-only
      deployment would keep its ground-system link provided FPP stays the
      source of truth. And **fpp-to-json already exposes the whole resolved
      topology** — 48 instances with baseId/priority/stack/queue/cpu, and all
      357 connections with resolved port numbers — so the topology half is
      mechanical, and checkable against an oracle that already exists.

      **What this phase actually delivered.**

      - **A cargo-only binary that links no C++ at all**
        (`Rust/fprime-selftest`). Until it existed the claim was
        *unfalsifiable from inside the repository*: `fprime-ffi` is a
        staticlib whose panic handler calls `fprime_rs_panic_hook`, defined in
        24 lines of C++, so nothing the workspace built could link alone. The
        binary exercises serialization, CRC32, the CFDP checksum, the OSAL max
        heap and path containment, and CI asserts its link contains no C++
        runtime. It is **not** a deployment — no runtime, no OSAL, no topology
        — but "does a cargo-only build produce a working binary" is now a
        question with a checkable answer.
      - **The component generator can carry a real component's ports.** Phase
        7's generator refused every in-tree component; Phase 9 measured the
        reachable count at zero. It now supports **return values**, **FPP enum
        parameters**, and **emitting events from Rust**. Counted directly, the
        tree has four pre-existing port definitions with return types
        (`ApidSequenceCount`, `SdlsKey`, `GetParameter`, `NoArgsReturn`);
        three are now expressible, and `SdlsKey` is not — it takes a `ref`
        buffer and returns an enum, both still refused. Verified two ways:
        `Svc/RustDemo` exercises
        all three end to end through its real ports, and the generator's
        output for `Svc::Ccsds::ApidManager` — a real in-tree component —
        **compiles against its real autocoded base**, enum parameter passed as
        `.e`, event emitted through a captureless lambda reaching the base's
        protected emitter.

      Enum parameters work because an FPP enum compiles to a *polymorphic*
      class (16 bytes, vptr, non-standard-layout) that cannot cross
      `extern "C"` — but its `.e` member is exactly the repr the typeMap
      already reports, so there is nothing to marshal. Events cannot be
      emitted by Rust alone: the autocoded base owns the time port and the
      event buffer, so they go back out through a generated `Env` vtable the
      shim fills per call.

      **What still blocks a real component**, beyond the runtime: the handler
      pointer needs somewhere to live, and an upstream component has no such
      member — wiring `ApidManager` would mean editing its header, which is
      upstream surgery this phase did not do. Ports carrying `Fw::Buffer`,
      structs or arrays are still refused, as are commands, telemetry and
      parameters.

      **What Phase 10 was, and what happens to it.** The goal — a cargo-only
      deployment — was never a tenth step of the same size as the nine before
      it; the original plan estimated 2.5–3 years solo to a first all-Rust
      deployment, and Phases 0–9 each fit in a sitting. Treating them as
      comparable was the planning error. The goal is not abandoned: it is
      **Phase 20** below, and Phases 11–19 are the steps that reach it, each
      with its own exit criterion and its own oracle.

## The road to a cargo-only deployment

Phases 11–20 replace the single Phase 10 goal. They are ordered by what
unblocks what, not by size, and the size column is deliberately honest: the
first three are sittings, the middle ones are not, and pretending otherwise is
what produced a partial Phase 10.

Every phase names the thing that tells you it worked. Where an oracle already
exists in the tree — a generated C++ file to diff against, a component's own
unit-test suite — the phase is built around it, because that is what made
Phases 1–9 verifiable and Phase 10 not.

- [x] **Phase 11 — Topology as data.** `fpp_to_rust.py --topology` reads the
      resolved topology out of `fpp-to-json` and emits it as Rust data: the
      instance table (base ID, priority, stack, queue, CPU, and whether the
      component is active) and every connection with its resolved port
      numbers. `--verify-cpp` then checks those facts against the topology
      `fpp-to-cpp` generated. Nothing links and nothing runs — the deliverable
      is the answer, not the module.

      **The answer: the topology is mechanical, apart from one escape hatch.**
      Every fact checked in `RefTopologyAc.cpp` — 15,530 lines from a 582-line
      model — is derivable from the model: 48 instances, 48 base IDs, **357
      connections**, 21 started components, and the ordering of its 8-step
      `setup()` and 4-step `teardown()`. The emitted module compiles.

      The exception is `phase Fpp.ToCpp.Phases.X """..."""`
      blocks: literal C++ pasted into the generated topology. Ref has **23**
      of them across six phases. Nothing derives them and nothing can
      translate them — a Rust-native topology would need them rewritten by
      hand — so the generator emits them as a `PHASE_ESCAPES` table with the
      C++ verbatim, to make that cost visible now rather than at porting time.

      One of them is why this phase was worth doing before the runtime.
      `CdhCore::version` is a **passive** component that the topology
      nonetheless starts, because a `startTasks` block says
      `CdhCore::version.start()`. A runtime built on "active components get a
      thread, passive ones don't" would be wrong about Ref on day one. The
      verification caught it as a 20-vs-21 mismatch, which is exactly the
      class of assumption this phase existed to test.

      Folded in: a project consuming F´ via `find_package` now actually gets
      the Rust implementations. It did not before — `Rust/enable.cmake`, which
      sets `FPRIME_RS_AVAILABLE`, was included only from the framework's own
      root `CMakeLists.txt`, so an external consumer built every C++ original
      while still linking `libfprime_rust.a` as dead weight. Adding `Rust` to
      `_FP_CORE_PACKAGES` was necessary but **not sufficient**, and shipping
      only that would have left the bug in place looking fixed: the list is
      walked in order and `Rust` sits last, long after `Fw`, `Svc`, `Os`,
      `Drv`, `CFDP` and `Utils` have each read `FPRIME_RS_AVAILABLE` to choose
      their sources. `cmake/FPrime.cmake` therefore includes `enable.cmake`
      *ahead of* the loop; `include_guard(GLOBAL)` keeps that idempotent with
      the root's own include. Verified by configuring a minimal external
      `find_package` project: it now compiles `CircularBufferRust`,
      `Crc32Rust`, `MaxHeapRust` and `FilePathUtilsRust`, and none of the C++
      originals.

- [~] **Phase 12 — partial: the two port shapes that matter most.** Counted
      across `Fw`, `Svc` and `Drv`, the framework's port definitions have 79
      parameters. **20 are `ref`** and **10 are `Fw.Buffer`** — the two largest
      groups after primitives, and the generator now carries both.

      A `ref` parameter crosses as a pointer and the handler takes `&mut T`,
      so what Rust writes is what the caller sees. `Fw::Buffer` cannot cross
      as an object — it derives `Fw::Serializable`, so it has a vtable — but
      it is really `(data, size, context)`, and the shim marshals that triple
      in and writes `size` and `context` back. It deliberately does not write
      `data` back: `Fw::Buffer::setData` is bounds-checked against the
      original allocation, and letting Rust move the pointer would be a
      different contract than the class documents.

      **Not done:** FPP structs and arrays as port parameters are still
      refused, and so is the **handler-pointer problem** — the generated shim
      calls `this->m_rustHandlers`, which an upstream component has no member
      for, so a real component still cannot be wired without editing its
      header. That is the remaining blocker for using any of this outside the
      demo component.

- [~] **Phase 13 — partial: commands and telemetry, not parameters.** A
      command was what made a generated shim *abstract*: the autocoded base
      leaves `<NAME>_cmdHandler` pure virtual, so a component with a command
      could not be generated at all. Rust now implements it, and answers the
      dispatcher itself — `env.respond(opcode, cmd_seq, cmd_response::OK)` —
      because the status a command returns is a decision, and decisions are
      what moved to Rust. Telemetry goes the same way: the autocoded base owns
      the channel ID and the time tag, so `env.tlm_<Channel>(value)` calls
      back out.

      `Svc/RustDemo` now exercises the whole surface end to end in 11 tests —
      primitives, bools, an enum parameter, a return value, a `ref`
      parameter, a `ref Fw::Buffer`, an event, a command answered `OK`, a
      command **rejected** with `VALIDATION_ERROR`, and a telemetry channel.

      A command whose handlers were never installed now answers the dispatcher
      with `EXECUTION_ERROR` rather than returning silently. A port call with
      no handlers is a no-op, but a command is not: the dispatcher blocks
      until it is answered, so a silent return would hang it.

      **Not done:** parameters (`paramGet`/`paramSet`) are not carried, and
      the target was the demo component rather than a real framework one,
      because of the handler-pointer blocker above. Phase 13's stated exit
      criterion — "one real in-tree component with a command and a channel
      runs its logic in Rust, its own suite green" — is **not** met.

- [~] **Phase 14 — partial: `Fw::Time`'s arithmetic and `Fw::StringUtils`.** Every timestamp the
      framework stamps on an event, a telemetry sample or a data product is
      ordered and offset by this code, and all of it is carry/borrow
      arithmetic on a (seconds, microseconds) pair. Comparison, addition,
      subtraction and the floating-point split now run in
      `Rust/fprime-core/src/time.rs`; the class, its storage, its
      serialization and its accessors stay C++. `Fw::Time` cannot cross
      `extern "C"` — it derives `Fw::Serializable` and so has a vtable — so
      each body flattens the object into a four-field `repr(C)` struct, calls
      across, and reads the result back. `Fw/Time/TimeRust.cpp` is generated
      by `tools/fprime-rs/gen_fw_time.py`, with the same drift check as the
      other shims. Oracle: `Fw/Time`'s own suite, 15/15.

      **The guard earned its keep immediately.** I assumed `Fw::TimeComparison`
      numbered from zero; it is `LT = -1, EQ = 0, GT = 1, INCOMPARABLE = 2` —
      a C-style comparison result. The `static_assert`s the shim emits caught
      it at compile time. Without them every time comparison in the framework
      would have been silently inverted, which no test of Rust alone would
      have found. The existing suite then caught a second one: I had written
      in a comment that `Time::add` keeps the left operand's context, when it
      zeroes a mismatch exactly as `sub` does.

      Two consequences worth recording. `parseSeconds`/`parseUSeconds` are
      public statics whose contract is the *un-carried* rounded pair —
      `parseUSeconds(0.999…)` is specified to return 1,000,000 — so the Rust
      split does not carry, and the callers (`set(F64)`, `add(F64)`) carry as
      the C++ does. And because the implementation file is renamed,
      `FW_FILENAME_ASSERT` (the default) reports `TimeRust.cpp` rather than
      `Time.cpp`; the suite's death tests pinned that path, and now accept
      either. Under the file-ID and relative-path assert modes the CMake
      restores the original's identity outright, so a flight fault signature is
      unchanged there.

      **`Fw::StringUtils` followed, and is the same shape.** The five bounded
      C-string primitives — `string_length`, `string_copy`, `string_last_n`
      and both substring searches — now run in
      `Rust/fprime-core/src/strings.rs`, selected by
      `FPRIME_RUST_IMPL_FW_STRINGS` (default ON), with
      `Fw/Types/StringUtilsRust.cpp` generated by
      `tools/fprime-rs/gen_fw_strings.py`. Every `Fw::String` assignment and
      every `Fw::StringBase::length` goes through these, and
      `Os::FilePathUtils` — the sandbox check from Phase 9 — searches with
      them, so this is the second time a Phase 9 result now rests on Phase 14
      code.

      **What did not move is the point.** Every null assert, the self-copy
      short-circuit, the non-overlap assert and the `FwSignedSizeType`
      representability checks stay in C++, in the same order and gated on the
      same conditions as the original. They are facts about *pointers*, and
      once two pointers have been split into two independent Rust slices the
      relationship between them is no longer expressible — `destination` and
      `source` overlapping is not something a `&mut [u8]` and a `&[u8]` can
      still be asked about. Only the character arithmetic crosses. The one
      guard that *is* subsumed is the searches' per-iteration read-overrun
      assert: a Rust slice index cannot exceed its slice.

      Reading the C++ line by line caught a divergence the suite would not
      have: `substring_find_last("", "")` returns **-1**, not 0. Its zero-size
      special case is gated on `source_size > 0`, so an empty source falls
      through to a loop whose inner body — `for (sub_index = 0; sub_index <
      sub_size; ...)` — never executes, and nothing ever returns a match. The
      forward search short-circuits and answers 0. I had written the obvious
      symmetric thing. `Fw/Types`' suite pins the forward case and not the
      reverse, so nothing would have failed; the missing case is now in
      `TypesTest.cpp` alongside it, which pins both switch positions.

      **A differential oracle, because the suite is not enough.**
      `Rust/test/differential/StringUtilsDiff.cpp` links *both*
      implementations into one process — the C++ original out of a switch-OFF
      `libFw_Types.a`, the Rust one out of `libfprime_rust.a`, which have
      different symbol names and so simply coexist — and compares them over
      every string up to length four on `{a, b, NUL}`: 7,121 comparisons, zero
      mismatches, under ASan and UBSan with each buffer sized exactly to its
      bound and backed onto a `PROT_NONE` guard page. Run it with
      `Rust/test/differential/run.sh`; it is not in CI, because it needs two
      caches configured in opposite switch positions, and the drift check and
      unit suites already guard regression.

      It was then checked by mutation, which is the part that matters: nine
      deliberate defects injected into the Rust and the FFI, seven caught. The
      two survivors turned out to be *equivalent* mutants rather than misses —
      scanning to the bound inclusive feeds `unwrap_or(limit)`, which yields
      the same value either way, so under LTO the compiler deletes the
      out-of-bounds load outright. Worth recording because the first
      explanation I reached for — that ASan cannot see reads inside an
      uninstrumented Rust archive, which is true — was not the reason here.

      One finding along the way: `strings::string_length` was structurally
      dead. The boundary must scan for the terminator before it can form a
      slice at all, so by the time the core function saw the content, the only
      byte its own scan could stop at was the terminator the boundary had
      already found — it could return `length` or `length + 1`, and both
      produce identical output. Mutating it changed nothing. It is gone, and
      `string_copy` now takes measured content; the scan lives once, in the
      FFI, where the raw pointer is.

      **The rest of the phase was audited, and the plan changed.** Thirteen
      agents surveyed and adversarially audited the four remaining candidates
      — `Fw::Buffer`, `PolyType`, `StringBase`, and the Com/Cmd/Tlm/Log packet
      types — across 52 hazards. The conclusion, which holds up on inspection:
      **none of them is a good next slice, and the reason is Phase 8.** All
      four are descriptor/delegation classes whose substantive bodies are
      virtual calls on `Fw::SerialBufferBase`, and `SerializableRust.cpp`
      already backs those primitives in Rust *from underneath*. Porting the
      delegation layer above them adds boundary surface and moves no
      decisions. That is a general result: it will keep being true of any
      descriptor class, so the phase's original framing — these four "as
      canonical Rust" — is largely obsolete rather than merely unfinished.

      What the audit did surface is an oracle problem worth fixing on its own
      terms, and `Fw::Buffer`'s is the sharpest. `Fw::Buffer::SERIALIZED_SIZE`
      is load-bearing outside its module — `Svc/ComQueue/ComQueueRust.cpp:124`
      sizes queue entries from it — and the wire order (`data, size, context,
      offset, capacity`, which is *not* the declaration order) was pinned by
      nothing but a round trip through the same pair of functions, which
      cannot fail when both change together. Separately, no assertion anywhere
      in `Fw/Buffer`'s suite compared two buffers as *unequal*, so a
      constant-true `operator==` passed all of it. Three tests now close both
      gaps, and mutation testing confirms they discriminate: a consistent
      wire-order swap is caught by the new `WireFormat` test and by nothing
      else, and `Equality` catches both a constant-true operator and — more
      to the point — one written to `Buffer.hpp`'s doc comment, which claims
      equality is data, size and context when the implementation also compares
      offset and capacity. That divergence is now pinned to the
      implementation, since that is what callers actually get.

      Four defects in the C++ came out of the audit, and all four are now
      addressed. Each is upstream framework code rather than port code, so
      each was scoped to the smallest correct change:

      All four were then put through a 19-agent adversarial review, which
      raised 14 findings and refuted 5 of its own. Seven of the nine survivors
      were worth acting on, and two of those changed the shape of the fix:
      the `ComPacket` guard now returns `TYPE_MISMATCH` rather than
      `FORMAT_ERROR`, and the `TlmPacket` boundary tests no longer depend on
      the configuration landing favourably. Both are described below. The
      review's most useful single observation was that the first draft of the
      invalid-descriptor test scanned only the top of the range, so narrowing
      the guard to `serVal > 0x0FFF` would have survived it — re-admitting
      exactly the in-range non-enumerator values a real uplink is most likely
      to carry.

      * **`Fw::ComPacket::deserializeBase` cast an uplinked descriptor with no
        validity check** — the only one of the four that is undefined
        behaviour rather than a wrong answer. `ComPacketType` is
        `ComCfg::Apid::T`, a plain enum whose largest enumerator is `0x800`,
        giving it a value range of `[0, 0xFFF]`; `FwPacketDescriptorType` is
        a `U16`, so a descriptor above `0xFFF` — attacker-controlled on an
        uplink — was cast outside the enum's range. The generated enum class
        supplies exactly the check needed and its own constructors already
        assert on it; storing the raw `T` is what bypassed that. It now
        returns `FW_DESERIALIZE_TYPE_MISMATCH` — deliberately, and not
        `FORMAT_ERROR` as first written. Every derived packet that checks its
        own descriptor already answers `TYPE_MISMATCH` for one it does not
        recognise, and `Svc/CmdDispatcher` telemeters that status verbatim as
        `MalformedCommand`'s only argument, which is ground software's sole
        discriminator for why a command was refused. Matching it keeps this a
        pure removal of undefined behaviour rather than a change anyone can
        observe. The one packet whose behaviour does move is `LogPacket`,
        which has no descriptor check of its own and so used to accept an
        unrecognised one outright.

        **`Fw/Com` had no test directory at all**, so it has one now: four
        cases covering the default, a round trip of every valid descriptor,
        and a rejection of every invalid one. Both scans are derived from the
        generated `isValid()` and sweep the whole 16-bit space rather than
        sampling — `config/ComCfg.fpp` is a default a deployment replaces, so
        a hardcoded list would test this repository's configuration instead of
        the code, and a sample from one end would leave the guard's predicate
        unpinned.
      * **`Fw::TlmPacket::addValue`'s `>` boundary was untested.** The audit
        corrected its own survey here: `TlmPacketSerializeFill` does not pin
        it, because fixed-size entries leave a remainder that only lands on
        `needed == left` when the entry stride happens to divide the space.
        Two tests now drive the packet to an exact amount of free space and
        add a value sized onto the boundary. The first draft of them filled
        the packet and hoped the remainder landed right, which the review
        showed would silently `GTEST_SKIP` for about three quarters of
        plausible deployment configurations — a test that reports success
        while testing nothing. They now hold for any configuration, with no
        skip path at all. Changing `>` to `>=` fails the exact-fill test and
        nothing else.
      * **`Buffer.hpp`'s `operator==` doc comment contradicted the
        implementation**, claiming equality is data, size and context when
        the code also compares offset and capacity. The comment was wrong,
        not the code, so the comment is fixed and the behaviour is pinned.
      * **`advance`'s third guard is not a tautology after all.** It looks
        like one — `offset + size` is exactly invariant under the two
        statements above it — but that only means it cannot fire on a
        well-formed buffer. It *can* fire on one whose invariant was already
        broken, and `deserializeFrom` assigns all four fields straight from
        the wire without relating them, so that state is reachable. It stays,
        with a comment saying what it is actually for.

      **What this is not.** The phase's goal — `Fw::Buffer`, the string types,
      `PolyType`, and the Com/Cmd/Tlm/Log packet types as canonical Rust — is
      still early. `Fw::Time` (273 lines) and `Fw::StringUtils` (137) are 410
      of the roughly 15,890 that goal covers, and in both cases only the
      arithmetic moved. `Fw::Buffer` (250 lines), `PolyType` (654),
      `StringBase` (139) and the four packet types (344) are untouched, and
      the ~53,015-line Enum/Serializable/Array/Constants tier that Phase 4
      showed is mechanical has not been generated for real types. These were
      two sittings; the phase is sized in weeks and that estimate still
      stands.

- [ ] **Phase 15 — The OSAL syscall layer** *(weeks)*. `Os::File`,
      `FileSystem`, `Directory`, `Task`, `Mutex`, `Queue`, `RawTime` in Rust
      behind the existing delegate interfaces. Mostly libc bindings: Rust
      `std` is usable only on hosted targets and drops flight-critical
      properties there — priority inheritance, scheduling policy, CPU
      affinity, `CLOCK_MONOTONIC` selection — so expect raw `pthread`.
      **Exit:** a deployment runs with the Rust OSAL selected. **Oracle:** the
      `Os` suites, which are already implementation-independent.

- [ ] **Phase 16 — The component runtime** *(months — this is the project)*.
      A Rust-native equivalent of `ActiveComponentBase`/`QueuedComponentBase`:
      one thread per active component, a bounded priority queue, the dispatch
      loop, port invocation, and the serialization of port arguments into
      queue messages. For Ref that is 21 threads, 21 queues, 13 mutexes.
      **Exit:** one active component runs end to end on the Rust runtime with
      C++ only at the topology edge. **Oracle:** that component's suite.

- [ ] **Phase 17 — Generate the component bases** *(months)*. Extend
      `fpp_to_rust` to emit what `fpp-to-cpp` emits for a component: the
      117,232 lines of `ComponentAc` across Ref's 41 components. Event
      emission alone is 28.8% of that (82 generated lines per event), so it
      is the first target, and telemetry (32 lines each) the second.
      **Exit:** generated Rust bases for a stated set of components.
      **Oracle:** behavioral equivalence against the C++ base, component
      suite by component suite.

- [ ] **Phase 18 — A Rust-native deployment** *(weeks, once 16 and 17 land)*.
      Not Ref: a small purpose-built topology — two or three components, a
      rate group, a comms path — that builds and runs cargo-only. The GDS
      dictionary comes free, since `fpp-to-dict` already builds it from FPP
      with no C++ involved.
      **Exit:** a deployment that talks to the ground system with no C++
      object in its link. This is the first honest realization of what
      Phase 10 asked for.

- [ ] **Phase 19 — Ref's long tail** *(months)*. Port the framework
      components Ref actually instantiates. Phase 9 measured the reachable
      count at zero with the tooling of the time; after Phases 12, 13 and 17
      that number is what decides whether this is tractable or whether Ref
      needs a reduced topology.

- [ ] **Phase 20 — Ref, cargo-only** *(the original Phase 10)*. No
      fpp-to-cpp, no C++ objects. 261,923 lines replaced.
      **Exit:** `cargo build` produces a Ref-equivalent binary that passes the
      existing Ref GDS integration tests.

