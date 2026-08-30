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
        "${CMAKE_CURRENT_LIST_DIR}/RustSvc.h"
        "${CMAKE_CURRENT_LIST_DIR}/RustGenerated.h"
    DEPENDS
        Fw_Types
)

if (NOT FPRIME_IS_SUB_BUILD)
    target_link_libraries(Rust_Codec PUBLIC fprime_rust)
    # libfprime_rust.a carries an undefined reference to fprime_rs_panic_hook,
    # defined in libRust_Codec.a — a mutual dependency between static archives.
    # Declaring the back-edge closes the cycle, which CMake detects as a
    # strongly connected component and repeats on the link line (verified:
    # Fw_Types_ut_exe links ...Fw_Types fprime_rust Rust_Codec Fw_Types
    # fprime_rust Rust_Codec...). That repetition is CMake's, so it is
    # generator- and linker-independent — GNU ld resolves it in its single
    # pass, not just macOS ld. Note the repeat count is LINK_INTERFACE_-
    # MULTIPLICITY (default 1, i.e. two copies): a future back-edge from the
    # Rust archive into a fourth library would need it raised.
    set_target_properties(fprime_rust PROPERTIES INTERFACE_LINK_LIBRARIES Rust_Codec)
endif()

# Unit tests are registered in sub-builds too: the info-cache sub-build must
# see the FPP inputs below to compute their dependency cache, which the main
# build's fpp autocoder then reads. Only the artifacts that need the imported
# cargo target stay behind the sub-build guard.
if (BUILD_TESTING AND FPRIME_ENABLE_FRAMEWORK_UTS)
    register_fprime_ut(
            Rust_Codec_differential_ut
        SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/test/ut/RustCodecDifferentialTest.cpp"
        DEPENDS
            Rust_Codec
            Fw_Types
    )

    # Phase 4: prove the fpp-to-rust GENERATED types serialize byte-for-byte
    # like the C++ types fpp autocodes from the same model
    register_fprime_ut(
            Rust_generated_types_ut
        SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/test/ut/RustGeneratedTypesTest.cpp"
        DEPENDS
            Rust_Codec
            Fw_Types
            Svc_FprimeProtocol
    )

    # Phase 5: the ComQueue scheduler across the shipped FFI, covering the
    # round-robin rotation that ComQueue's own suite never observes.
    register_fprime_ut(
            Rust_comqueue_scheduler_ut
        SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/test/ut/RustComQueueSchedulerTest.cpp"
        DEPENDS
            Rust_Codec
            Fw_Types
            Svc_ComQueue
    )

    if (NOT FPRIME_IS_SUB_BUILD)
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
