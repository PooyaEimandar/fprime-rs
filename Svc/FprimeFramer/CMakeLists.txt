####
# FPrime CMakeLists.txt:
#
# SOURCE_FILES: combined list of source and autocoding files
# MOD_DEPS: (optional) module dependencies
# UT_SOURCE_FILES: list of source files for unit tests
#
# More information in the F´ CMake API documentation:
# https://fprime.jpl.nasa.gov/latest/documentation/reference
#
####

# fprime-rs: the framing logic has a Rust implementation behind the unchanged
# component class; FPRIME_RUST_IMPL_SVC_FPRIME_FRAMER selects it (see
# Rust/enable.cmake).
if (FPRIME_RS_AVAILABLE AND FPRIME_RUST_IMPL_SVC_FPRIME_FRAMER)
  set(FPRIME_FRAMER_IMPL "${CMAKE_CURRENT_LIST_DIR}/FprimeFramerRust.cpp")
else()
  set(FPRIME_FRAMER_IMPL "${CMAKE_CURRENT_LIST_DIR}/FprimeFramer.cpp")
endif()

register_fprime_module(
  AUTOCODER_INPUTS
    "${CMAKE_CURRENT_LIST_DIR}/FprimeFramer.fpp"
  SOURCES
    "${FPRIME_FRAMER_IMPL}"
  HEADERS
    "${CMAKE_CURRENT_LIST_DIR}/FprimeFramer.hpp"
  DEPENDS
    Svc_FprimeProtocol
)

if (FPRIME_RS_AVAILABLE AND FPRIME_RUST_IMPL_SVC_FPRIME_FRAMER AND NOT FPRIME_IS_SUB_BUILD)
  target_link_libraries(Svc_FprimeFramer PUBLIC fprime_rust)
endif()


### Unit Tests ###
register_fprime_ut(
  AUTOCODER_INPUTS
    "${CMAKE_CURRENT_LIST_DIR}/FprimeFramer.fpp"
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/FprimeFramerTestMain.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/FprimeFramerTester.cpp"
  UT_AUTO_HELPERS
)
