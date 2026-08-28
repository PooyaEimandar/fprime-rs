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
- [ ] **Phase 2 — First components**: `Svc/FprimeDeframer`, then
      `Svc/FprimeFramer` (hand-written shims); Ref GDS integration in a mixed
      deployment
- [ ] **Phase 3 — Stateful data path**: `Svc/FrameAccumulator`,
      `Svc/FprimeRouter`; all-Rust uplink chain
- [ ] **Phase 4 — Codegen**: `fpp-to-rust` fed by `fpp-to-json` + a `rust.cmake`
      autocoder; retrofit ported components, prove generated glue identical
- [ ] **Phase 5 — Active components and drivers**: `Svc/ComQueue`, then
      I2C/UART/GPIO drivers after characterization tests (known defect fixes
      land as reviewed, test-pinned divergences)
- [ ] **Phase 6 — Qualification hardening**: Ferrocene toolchain option,
      Safety-Critical Rust coding guidelines
- [ ] **Phase 7 — Rust-native component runtime**: full components generated in
      Rust (dispatch, queues, cmd/tlm/event plumbing); no per-component C++
- [ ] **Phase 8 — `Fw` core in Rust**: serialization/types/buffers become the
      canonical Rust crates; C++ consumes them via reverse shims (the flip point)
- [ ] **Phase 9 — OSAL in Rust + component long tail**: port what deployments
      actually instantiate
- [ ] **Phase 10 — Drop the C++ toolchain** (per deployment): no fpp-to-cpp, no
      C++ objects, cargo-only build

