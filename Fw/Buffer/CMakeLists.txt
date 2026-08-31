####
# F prime CMakeLists.txt:
#
# SOURCE_FILES: combined list of source and autocoding files
# MOD_DEPS: (optional) module dependencies
#
####


# fprime-rs: Fw::Buffer is an FPP abstract type, so the model says nothing
# about how it serializes and Rust/fprime-core/src/fw_buffer.rs mirrors it from
# facts read out of the C++ by hand. RustAbstractTypeCheck.cpp asserts those
# facts. It compiles here rather than in Rust_Codec, which sits below Fw_Types
# in the link order and cannot depend on this module without a cycle.
set(FPRIME_BUFFER_SOURCES "${CMAKE_CURRENT_LIST_DIR}/Buffer.cpp")
if (FPRIME_RS_AVAILABLE)
    list(APPEND FPRIME_BUFFER_SOURCES "${FPRIME_FRAMEWORK_PATH}/Rust/RustAbstractTypeCheck.cpp")
endif()

register_fprime_module(
  AUTOCODER_INPUTS
    "${CMAKE_CURRENT_LIST_DIR}/Buffer.fpp"
  SOURCES
    "${FPRIME_BUFFER_SOURCES}"
  HEADERS
    "${CMAKE_CURRENT_LIST_DIR}/Buffer.hpp"
  DEPENDS
    Fw_Types
)

register_fprime_ut(
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/TestBuffer.cpp"
)
