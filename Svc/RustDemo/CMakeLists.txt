####
# Svc/RustDemo: fprime-rs Phase 7 demonstration component.
#
# Every line of glue between the autocoded C++ base and the Rust
# implementation is generated from RustDemo.fpp by
# tools/fprime-rs/fpp_to_rust.py --component. The module builds only when the
# Rust workspace is available; there is no C++ fallback implementation,
# because the point of the component is that its implementation is Rust.
####
if (NOT FPRIME_RS_AVAILABLE)
    return()
endif()

register_fprime_module(
  AUTOCODER_INPUTS
    "${CMAKE_CURRENT_LIST_DIR}/RustDemoPorts.fpp"
    "${CMAKE_CURRENT_LIST_DIR}/RustDemo.fpp"
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/RustDemoShim.cpp"
  HEADERS
    "${CMAKE_CURRENT_LIST_DIR}/RustDemo.hpp"
  DEPENDS
    Fw_Types
)

if (NOT FPRIME_IS_SUB_BUILD)
  target_link_libraries(Svc_RustDemo PUBLIC fprime_rust)
endif()

#### UTs ####
register_fprime_ut(
  AUTOCODER_INPUTS
    "${CMAKE_CURRENT_LIST_DIR}/RustDemoPorts.fpp"
    "${CMAKE_CURRENT_LIST_DIR}/RustDemo.fpp"
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/RustDemoTestMain.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/RustDemoTester.cpp"
  DEPENDS
    Svc_RustDemo
  UT_AUTO_HELPERS
)
