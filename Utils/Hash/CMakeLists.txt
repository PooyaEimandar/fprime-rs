####
# F prime CMakeLists.txt:
#
# SOURCE_FILES: combined list of source and autocoding files
# MOD_DEPS: (optional) module dependencies
#
####

# fprime-rs: the CRC32 kernel has a Rust implementation behind the unchanged
# header; FPRIME_RUST_IMPL_UTILS_HASH selects it (see Rust/enable.cmake).
if (FPRIME_RS_AVAILABLE AND FPRIME_RUST_IMPL_UTILS_HASH)
    set(CRC32_IMPL "${CMAKE_CURRENT_LIST_DIR}/Crc32/Crc32Rust.cpp")
else()
    set(CRC32_IMPL "${CMAKE_CURRENT_LIST_DIR}/Crc32/Crc32.cpp")
endif()

register_fprime_library(
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/HashBufferCommon.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/HashCommon.cpp"

        "${CRC32_IMPL}"
        "${CMAKE_CURRENT_LIST_DIR}/Crc32/HashImpl.cpp"

        # Only provided to satisfy CCSDS
        "${CMAKE_CURRENT_LIST_DIR}/libcrc/lib_crc.c"
    HEADERS
        "${CMAKE_CURRENT_LIST_DIR}/Hash.hpp"
        "${CMAKE_CURRENT_LIST_DIR}/HashBuffer.hpp"
        "${CMAKE_CURRENT_LIST_DIR}/HashConfig.hpp"

        "${CMAKE_CURRENT_LIST_DIR}/Crc32/Crc32.hpp"

        # Only provided to satisfy CCSDS
        "${CMAKE_CURRENT_LIST_DIR}/libcrc/lib_crc.h"
    DEPENDS
        Fw_Types
)

if (FPRIME_RS_AVAILABLE AND FPRIME_RUST_IMPL_UTILS_HASH AND NOT FPRIME_IS_SUB_BUILD)
    target_link_libraries(Utils_Hash PUBLIC fprime_rust)
endif()

register_fprime_ut(
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/Crc32/test/ut/Crc32Test.cpp"
    DEPENDS
        Utils_Hash
)
