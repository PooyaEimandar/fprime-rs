include("${CMAKE_CURRENT_LIST_DIR}/enable.cmake")
if (NOT FPRIME_RS_AVAILABLE)
    return()
endif()

register_fprime_module(
        Rust_Codec
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/RustPanicHook.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/RustConfigCheck.cpp"
    HEADERS
        "${CMAKE_CURRENT_LIST_DIR}/RustCodec.h"
        "${CMAKE_CURRENT_LIST_DIR}/RustUtils.h"
    DEPENDS
        Fw_Types
)

if (NOT FPRIME_IS_SUB_BUILD)
    target_link_libraries(Rust_Codec PUBLIC fprime_rust)
    # libfprime_rust.a carries an undefined reference to fprime_rs_panic_hook,
    # defined in libRust_Codec.a — a mutual dependency between static archives.
    # Declaring the back-edge makes CMake repeat the archives on the link line
    # (macOS ld tolerates single-pass ordering; GNU ld does not).
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

        # Rust-side unit tests run under the same ctest umbrella as the C++
        # UTs, so `fprime-util check` covers both languages. fprime-ffi is
        # excluded: its staticlib crate-type cannot build under the test
        # profile's forced unwind, and its coverage is the differential UT.
        add_test(NAME Rust_cargo_test
                 COMMAND "${FPRIME_CARGO}" test --workspace --exclude fprime-ffi
                         --manifest-path "${FPRIME_RUST_MANIFEST}"
                         --target-dir "${FPRIME_RUST_TARGET_DIR}"
                 WORKING_DIRECTORY "${FPRIME_RUST_WORKSPACE_DIR}")
        fprime_util_metadata_add_test("Rust_cargo_test")
    endif()
endif()
