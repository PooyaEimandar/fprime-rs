####
# F prime CMakeLists.txt:
#
# SOURCE_FILES: combined list of source and autocoding files
# MOD_DEPS: (optional) module dependencies
#
####
# fprime-rs Phase 14: the Fw::Time arithmetic can run in Rust (see
# Rust/enable.cmake). TimeRust.cpp is generated from Time.cpp by
# tools/fprime-rs/gen_fw_time.py.
if (FPRIME_RS_AVAILABLE AND FPRIME_RUST_IMPL_FW_TIME)
    set(FPRIME_FW_TIME_IMPL "${CMAKE_CURRENT_LIST_DIR}/TimeRust.cpp")
else()
    set(FPRIME_FW_TIME_IMPL "${CMAKE_CURRENT_LIST_DIR}/Time.cpp")
endif()

register_fprime_module(
  AUTOCODER_INPUTS
    "${CMAKE_CURRENT_LIST_DIR}/Time.fpp"
  SOURCES
    "${FPRIME_FW_TIME_IMPL}"
    "${CMAKE_CURRENT_LIST_DIR}/TimeInterval.cpp"
  HEADERS
    "${CMAKE_CURRENT_LIST_DIR}/Time.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/TimeInterval.hpp"
)
### UTs ###
register_fprime_ut(
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/TimeTestMain.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/TimeTester.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/TimeIntervalTester.cpp"
  DEPENDS
    Fw_Time
    Fw_Types
)

if (FPRIME_RS_AVAILABLE AND FPRIME_RUST_IMPL_FW_TIME)
    # The shim *is* the implementation of Time.cpp, so it must fault as
    # Time.cpp. F Prime derives ASSERT_FILE_ID and ASSERT_RELATIVE_PATH from
    # each source file's own path, which would make every assert in Fw::Time
    # report a file that does not exist upstream -- changing the fault
    # signature flight software keys on, and breaking the suite's death tests,
    # which match on the path. Recompute both from the original's path, the
    # same way cmake/utilities.cmake set_assert_flags does.
    string(REPLACE "${FPRIME_FRAMEWORK_PATH}/" "" FPRIME_TIME_SHORT_SRC
           "${CMAKE_CURRENT_LIST_DIR}/Time.cpp")
    string(MD5 FPRIME_TIME_HASH "${FPRIME_TIME_SHORT_SRC}")
    string(SUBSTRING "${FPRIME_TIME_HASH}" 0 8 FPRIME_TIME_FILE_ID)
    set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/TimeRust.cpp" PROPERTIES
        COMPILE_FLAGS
        "-DASSERT_FILE_ID=0x${FPRIME_TIME_FILE_ID} -DASSERT_RELATIVE_PATH='\"${FPRIME_TIME_SHORT_SRC}\"'")
endif()

if (FPRIME_RS_AVAILABLE AND FPRIME_RUST_IMPL_FW_TIME AND NOT FPRIME_IS_SUB_BUILD)
    target_link_libraries(Fw_Time PUBLIC fprime_rust)

    # TimeRust.cpp is a generated near-copy of Time.cpp; verify on every build
    # that it is still what the generator produces.
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/TimeRust.verified"
        COMMAND "${PYTHON}" "${FPRIME_FRAMEWORK_PATH}/tools/fprime-rs/gen_fw_time.py" "--check"
        COMMAND "${CMAKE_COMMAND}" "-E" "touch" "${CMAKE_CURRENT_BINARY_DIR}/TimeRust.verified"
        DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Time.cpp"
                "${CMAKE_CURRENT_LIST_DIR}/TimeRust.cpp"
                "${FPRIME_FRAMEWORK_PATH}/tools/fprime-rs/gen_fw_time.py"
                "${FPRIME_FRAMEWORK_PATH}/tools/fprime-rs/shimgen.py"
        COMMENT "[rust] Verifying the Rust-backed Fw::Time matches Time.cpp"
        VERBATIM
    )
    add_custom_target(Fw_Time_rust_verify
                      DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/TimeRust.verified")
    add_dependencies(Fw_Time Fw_Time_rust_verify)
endif()
