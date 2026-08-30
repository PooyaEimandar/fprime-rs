####
# Rust/enable.cmake:
#
# Included from the root CMakeLists.txt BEFORE framework code so that modules
# processed during FPrime-Code (Utils, CFDP, ...) can select Rust-backed
# implementation sources and link the imported fprime_rust target. Sets
# FPRIME_RS_AVAILABLE (identically in sub-builds, keeping module source lists
# consistent) and creates the cargo build + imported library in main builds.
####
include_guard(GLOBAL)

# Included from two places, and `include_guard(GLOBAL)` above makes that safe:
# this repository's root CMakeLists.txt for framework-tree builds, and
# cmake/FPrime.cmake ahead of the core-package loop so that projects consuming
# F Prime via find_package get the same implementation selection. Both must
# run before Fw/Svc/Os/Drv/CFDP/Utils, which read FPRIME_RS_AVAILABLE while
# choosing their sources.

option(FPRIME_ENABLE_RUST "Build the fprime-rs Rust components" ON)

# Per-module implementation switches: Rust implementation when ON (and Rust is
# available), original C++ implementation when OFF. One-flag rollback per
# module. CI builds the framework in the OFF position too, so the C++ files a
# switch displaces stay compilable -- with a switch ON they are not in the
# build at all, and an upstream change could break them unnoticed.
option(FPRIME_RUST_IMPL_UTILS_TYPES "Use the Rust CircularBuffer engine" ON)
option(FPRIME_RUST_IMPL_UTILS_HASH "Use the Rust CRC32 implementation" ON)
option(FPRIME_RUST_IMPL_CFDP_CHECKSUM "Use the Rust CFDP checksum" ON)
option(FPRIME_RUST_IMPL_SVC_FPRIME_DEFRAMER "Use the Rust FprimeDeframer logic" ON)
option(FPRIME_RUST_IMPL_SVC_FPRIME_FRAMER "Use the Rust FprimeFramer logic" ON)
option(FPRIME_RUST_IMPL_SVC_FRAME_ACCUMULATOR "Use the Rust FrameAccumulator orchestration" ON)
option(FPRIME_RUST_IMPL_SVC_FPRIME_ROUTER "Use the Rust FprimeRouter context table" ON)
option(FPRIME_RUST_IMPL_SVC_COM_QUEUE "Use the Rust ComQueue priority scheduling" ON)
# Phase 8, the flip point: the Fw serialization kernel's primitive reads and
# writes call into Rust. Every component and every autocoded type depends on
# this path, so the switch exists to isolate it instantly if needed.
#
# Default OFF, unlike every switch above it. This is the only one that taxes
# the whole framework rather than the module it names: each primitive becomes
# a cross-language call the compiler cannot inline (measured +40-80% on a
# multi-field serialize, and it makes FpConfig's FW_SERIALIZE_FORCE_INLINE_LBB
# knob a no-op), and linking the Rust archive into Fw_Types adds ~300 KB to
# every binary. Turn it on deliberately, per deployment, against a measured
# budget -- see Rust/README.md, Phase 8.
option(FPRIME_RUST_IMPL_FW_TYPES "Use the Rust-backed Fw serialization kernel" OFF)

# Phase 9, the OSAL: the stable max heap that orders every message queued to
# an active component. Unlike the Fw kernel above this is a bounded, per-queue
# algorithm rather than a per-field tax, so it defaults ON like Phases 1-7.
option(FPRIME_RUST_IMPL_OS_MAXHEAP "Use the Rust MaxHeap behind the OSAL priority queue" ON)
# Phase 9: path resolution and sandbox containment -- the check that keeps a
# commanded file path inside the directory a deployment allows.
option(FPRIME_RUST_IMPL_OS_FILEPATH "Use the Rust path resolution and containment checks" ON)

# Phase 14, the Fw core types: the arithmetic behind Fw::Time -- ordering, the
# carry on addition, the borrow on subtraction, and the F64 split. Bounded
# per-call work on a type every timestamp goes through.
option(FPRIME_RUST_IMPL_FW_TIME "Use the Rust Fw::Time arithmetic" ON)
# Phase 14: Fw::StringUtils -- bounded length, copy, last-N and both substring
# searches. Every Fw::String assignment and every Fw::StringBase::length goes
# through these, but each call is one bounded scan rather than the per-field
# tax the Fw kernel above imposes, so this defaults ON.
option(FPRIME_RUST_IMPL_FW_STRINGS "Use the Rust Fw::StringUtils primitives" ON)

# Phase 4: verify the checked-in generated Rust still matches its FPP model as
# part of the build. Opt-in because the generator needs fpp-to-json on PATH
# (the fprime venv provides it). Must be registered before module detection.
option(FPRIME_RUST_AUTOCODE "Verify generated Rust against FPP models during the build" OFF)
if (FPRIME_RUST_AUTOCODE)
    register_fprime_build_autocoder("autocoder/rust" FALSE)
endif()

set(FPRIME_RS_AVAILABLE FALSE)
if (FPRIME_ENABLE_RUST)
    find_program(FPRIME_CARGO cargo)
    if (FPRIME_CARGO)
        set(FPRIME_RS_AVAILABLE TRUE)
    elseif (NOT FPRIME_IS_SUB_BUILD)
        message(STATUS "fprime-rs: cargo not found; skipping Rust components (set FPRIME_ENABLE_RUST=OFF to silence)")
    endif()
endif()

if (FPRIME_RS_AVAILABLE)
    if (CMAKE_CROSSCOMPILING AND NOT FPRIME_IS_SUB_BUILD)
        message(WARNING
            "fprime-rs: cargo cross-compilation is not wired yet; libfprime_rust.a is built for the HOST "
            "architecture and linking will fail. Configure with -DFPRIME_ENABLE_RUST=OFF for cross builds.")
    endif()

    set(FPRIME_RUST_MANIFEST "${CMAKE_CURRENT_LIST_DIR}/Cargo.toml")
    set(FPRIME_RUST_WORKSPACE_DIR "${CMAKE_CURRENT_LIST_DIR}")
    set(FPRIME_RUST_TARGET_DIR "${CMAKE_BINARY_DIR}/fprime-rust-target")
    set(FPRIME_RUST_LIB "${FPRIME_RUST_TARGET_DIR}/release/libfprime_rust.a")

    if (NOT FPRIME_IS_SUB_BUILD)
        # cargo is its own incremental build system: run it every build; a no-op
        # rebuild is cheap and BYPRODUCTS lets Ninja track the archive.
        # WORKING_DIRECTORY matters: rustup resolves rust-toolchain.toml from the
        # working directory, not the manifest path — this pins the toolchain.
        add_custom_target(fprime_rust_cargo
            COMMAND "${FPRIME_CARGO}" build --release --manifest-path "${FPRIME_RUST_MANIFEST}"
                    --target-dir "${FPRIME_RUST_TARGET_DIR}"
            BYPRODUCTS "${FPRIME_RUST_LIB}"
            WORKING_DIRECTORY "${FPRIME_RUST_WORKSPACE_DIR}"
            COMMENT "Building Rust workspace (libfprime_rust.a)"
            VERBATIM
        )
        add_library(fprime_rust STATIC IMPORTED GLOBAL)
        set_target_properties(fprime_rust PROPERTIES IMPORTED_LOCATION "${FPRIME_RUST_LIB}")
        add_dependencies(fprime_rust fprime_rust_cargo)
    endif()
endif()
