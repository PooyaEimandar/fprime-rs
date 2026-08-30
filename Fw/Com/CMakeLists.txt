####
# F prime CMakeLists.txt:
#
# SOURCE_FILES: combined list of source and autocoding files
# MOD_DEPS: (optional) module dependencies
#
####
register_fprime_module(
  AUTOCODER_INPUTS
    "${CMAKE_CURRENT_LIST_DIR}/Com.fpp"
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/ComPacket.cpp"
  HEADERS
    "${CMAKE_CURRENT_LIST_DIR}/ComBuffer.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/ComPacket.hpp"
)

### UTs ###
register_fprime_ut(
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/ComPacketTest.cpp"
  DEPENDS
    Fw_Com
    Fw_Types
)
