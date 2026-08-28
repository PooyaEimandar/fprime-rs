option(FPRIME_ENABLE_RUST "Build the fprime-rs Rust components" ON)
if (NOT FPRIME_ENABLE_RUST)
    return()
endif()

find_program(FPRIME_CARGO cargo)
if (NOT FPRIME_CARGO)
    if (NOT FPRIME_IS_SUB_BUILD)
        message(STATUS "fprime-rs: cargo not found; skipping Rust components (set FPRIME_ENABLE_RUST=OFF to silence)")
    endif()
    return()
endif()

if (CMAKE_CROSSCOMPILING AND NOT FPRIME_IS_SUB_BUILD)
    message(WARNING
        "fprime-rs: cargo cross-compilation is not wired yet; libfprime_rust.a is built for the HOST "
        "architecture and linking will fail. Configure with -DFPRIME_ENABLE_RUST=OFF for cross builds.")
endif()

set(FPRIME_RUST_MANIFEST "${CMAKE_CURRENT_LIST_DIR}/Cargo.toml")
set(FPRIME_RUST_TARGET_DIR "${CMAKE_BINARY_DIR}/fprime-rust-target")
set(FPRIME_RUST_LIB "${FPRIME_RUST_TARGET_DIR}/release/libfprime_rust.a")

if (NOT FPRIME_IS_SUB_BUILD)
    add_custom_target(fprime_rust_cargo
        COMMAND "${FPRIME_CARGO}" build --release --manifest-path "${FPRIME_RUST_MANIFEST}"
                --target-dir "${FPRIME_RUST_TARGET_DIR}"
        BYPRODUCTS "${FPRIME_RUST_LIB}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
        COMMENT "Building Rust workspace (libfprime_rust.a)"
        VERBATIM
    )
    add_library(fprime_rust STATIC IMPORTED GLOBAL)
    set_target_properties(fprime_rust PROPERTIES IMPORTED_LOCATION "${FPRIME_RUST_LIB}")
    add_dependencies(fprime_rust fprime_rust_cargo)
endif()

register_fprime_module(
        Rust_Codec
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/RustPanicHook.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/RustConfigCheck.cpp"
    HEADERS
        "${CMAKE_CURRENT_LIST_DIR}/RustCodec.h"
    DEPENDS
        Fw_Types
)

if (NOT FPRIME_IS_SUB_BUILD)
    target_link_libraries(Rust_Codec PUBLIC fprime_rust)
    set_target_properties(fprime_rust PROPERTIES INTERFACE_LINK_LIBRARIES Rust_Codec)

    if (BUILD_TESTING AND FPRIME_ENABLE_FRAMEWORK_UTS)
        register_fprime_ut(
                Rust_Codec_differential_ut
            SOURCES
                "${CMAKE_CURRENT_LIST_DIR}/test/ut/RustCodecDifferentialTest.cpp"
            DEPENDS
                Rust_Codec
                Fw_Types
        )

        add_test(NAME Rust_cargo_test
                 COMMAND "${FPRIME_CARGO}" test --workspace --exclude fprime-ffi
                         --manifest-path "${FPRIME_RUST_MANIFEST}"
                         --target-dir "${FPRIME_RUST_TARGET_DIR}"
                 WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}")
        fprime_util_metadata_add_test("Rust_cargo_test")
    endif()
endif()
