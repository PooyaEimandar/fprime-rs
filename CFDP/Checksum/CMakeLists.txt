####
# F prime CMakeLists.txt:
#
# SOURCE_FILES: combined list of source and autocoding files
# MOD_DEPS: (optional) module dependencies
#
####

# fprime-rs: the checksum algorithm has a Rust implementation behind the
# unchanged header; FPRIME_RUST_IMPL_CFDP_CHECKSUM selects it (see
# Rust/enable.cmake).
if (FPRIME_RS_AVAILABLE AND FPRIME_RUST_IMPL_CFDP_CHECKSUM)
  set(CHECKSUM_IMPL "${CMAKE_CURRENT_LIST_DIR}/ChecksumRust.cpp")
else()
  set(CHECKSUM_IMPL "${CMAKE_CURRENT_LIST_DIR}/Checksum.cpp")
endif()

register_fprime_module(
  SOURCES
    "${CHECKSUM_IMPL}"
  HEADERS
    "${CMAKE_CURRENT_LIST_DIR}/Checksum.hpp"
  DEPENDS
    Fw_Types
)

if (FPRIME_RS_AVAILABLE AND FPRIME_RUST_IMPL_CFDP_CHECKSUM AND NOT FPRIME_IS_SUB_BUILD)
  target_link_libraries(CFDP_Checksum PUBLIC fprime_rust)
endif()


register_fprime_ut(
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/ChecksumMain.cpp"
  DEPENDS
    CFDP_Checksum
    Fw_Types
)
# Add GTest directory
add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/GTest")
