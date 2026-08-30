####
# F prime CMakeLists.txt:
####
include(global_interface)
# fprime-rs Phase 8: the serialization kernel's primitives can run in Rust
# (see Rust/enable.cmake). This is the framework's hottest path -- every
# component and autocoded type serializes through it -- so it is switchable.
if (FPRIME_RS_AVAILABLE AND FPRIME_RUST_IMPL_FW_TYPES)
    set(FW_SERIALIZABLE_IMPL "${CMAKE_CURRENT_LIST_DIR}/SerializableRust.cpp")
else()
    set(FW_SERIALIZABLE_IMPL "${CMAKE_CURRENT_LIST_DIR}/Serializable.cpp")
endif()

register_fprime_module(
    Fw_Types
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/Assert.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ConstStringBase.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/MallocAllocator.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/MemAllocator.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/PolyType.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/SerialBuffer.cpp"
    "${FW_SERIALIZABLE_IMPL}"
    "${CMAKE_CURRENT_LIST_DIR}/StringBase.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/StringUtils.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/StringToNumber.cpp"
  AUTOCODER_INPUTS
    "${CMAKE_CURRENT_LIST_DIR}/Types.fpp"
  DEPENDS
    ${FPRIME_GLOBAL_INTERFACE_TARGET} # Most users should prefer Fw_Types instead over the direct global interface.
  REQUIRES_IMPLEMENTATIONS
    Fw_StringFormat
    Fw_StringScan
)

if (FPRIME_RS_AVAILABLE AND FPRIME_RUST_IMPL_FW_TYPES AND NOT FPRIME_IS_SUB_BUILD)
    # Fw_Types and the Rust staticlib are mutually dependent: the kernel calls
    # into Rust, and the Rust panic hook calls FW_ASSERT back. Declaring the
    # edge lets CMake repeat the archives on the link line. The back-edge
    # (fprime_rust -> Rust_Codec) is declared in Rust/CMakeLists.txt, closing
    # the cycle so the repetition also satisfies GNU ld's single pass.
    target_link_libraries(Fw_Types PUBLIC fprime_rust)

    # SerializableRust.cpp is a generated near-copy of Serializable.cpp. An
    # upstream edit to the original would otherwise never reach the Rust
    # build and the two would silently diverge, so verify on every build that
    # the checked-in copy is still what the generator produces.
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/SerializableRust.verified"
        COMMAND "${PYTHON}" "${FPRIME_FRAMEWORK_PATH}/tools/fprime-rs/gen_fw_kernel.py" "--check"
        COMMAND "${CMAKE_COMMAND}" "-E" "touch" "${CMAKE_CURRENT_BINARY_DIR}/SerializableRust.verified"
        DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Serializable.cpp"
                "${CMAKE_CURRENT_LIST_DIR}/SerializableRust.cpp"
                "${FPRIME_FRAMEWORK_PATH}/tools/fprime-rs/gen_fw_kernel.py"
        COMMENT "[rust] Verifying the Rust-backed Fw kernel matches Serializable.cpp"
        VERBATIM
    )
    add_custom_target(Fw_Types_rust_kernel_verify
                      DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/SerializableRust.verified")
    add_dependencies(Fw_Types Fw_Types_rust_kernel_verify)
endif()

register_fprime_implementation(
    Fw_StringFormat_snprintf
  IMPLEMENTS
    Fw_StringFormat
  DEPENDS
    Fw_Types
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/snprintf_format.cpp"
)

register_fprime_implementation(
    Fw_StringScan_sscanf
  IMPLEMENTS
    Fw_StringScan
  DEPENDS
    Fw_Types
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/sscanf_scan.cpp"
)


### UTs ###
register_fprime_ut(
    Fw_Types_ut_exe
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/ExternalSerializeBufferTest.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/AssertTypesTest.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/TypesTest.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/CAssertTest.cpp"
  DEPENDS
    Os
)
if (TARGET Fw_Types_ut_exe)
    target_compile_options(Fw_Types_ut_exe PRIVATE -Wno-conversion)
endif()

register_fprime_ut(
    Fw_StringFormat_snprintf_ut_exe
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/SnPrintfFormatTest.cpp"
  DEPENDS
    Fw_Types
  CHOOSES_IMPLEMENTATIONS
    Fw_StringFormat_snprintf
)


if (FPRIME_PLATFORM STREQUAL "Linux")
    register_fprime_ut(
        Fw_Types_Mmap_ut_exe
      SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/test/ut/MmapAllocatorTest.cpp"
      DEPENDS
        Fw_Types
    )
endif()
register_fprime_ut(
    Fw_StringScan_sscanf_ut_exe
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/ScanfScanTest.cpp"
  DEPENDS
    Fw_Types
  CHOOSES_IMPLEMENTATIONS
    Fw_StringScan_sscanf
)

register_fprime_ut(
    Fw_Types_Optional_ut_exe
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/OptionalTest.cpp"
  DEPENDS
    Fw_Types
)

# Non-test directory
add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/GTest")
