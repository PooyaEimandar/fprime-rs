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

# Phase 1 scope: implementation selection applies to framework-tree builds
# (this repository's root CMakeLists.txt, including TestDeploymentsProject,
# which processes it). External projects consuming F Prime via find_package
# do not process this file yet and silently keep the C++ implementations;
# wiring selection into fprime_setup_included_code is Phase 2+ work.

option(FPRIME_ENABLE_RUST "Build the fprime-rs Rust components" ON)

# Per-module implementation switches: Rust implementation when ON (and Rust is
# available), original C++ implementation when OFF. One-flag rollback per
# module; CI can run the same UT suites in both positions.
option(FPRIME_RUST_IMPL_UTILS_TYPES "Use the Rust CircularBuffer engine" ON)
option(FPRIME_RUST_IMPL_UTILS_HASH "Use the Rust CRC32 implementation" ON)
option(FPRIME_RUST_IMPL_CFDP_CHECKSUM "Use the Rust CFDP checksum" ON)
option(FPRIME_RUST_IMPL_SVC_FPRIME_DEFRAMER "Use the Rust FprimeDeframer logic" ON)
option(FPRIME_RUST_IMPL_SVC_FPRIME_FRAMER "Use the Rust FprimeFramer logic" ON)
option(FPRIME_RUST_IMPL_SVC_FRAME_ACCUMULATOR "Use the Rust FrameAccumulator orchestration" ON)
option(FPRIME_RUST_IMPL_SVC_FPRIME_ROUTER "Use the Rust FprimeRouter context table" ON)

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
