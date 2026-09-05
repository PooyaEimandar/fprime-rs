####
# Svc/RustDemo: fprime-rs Phase 7 demonstration component.
#
# There is no hand-written C++ here. RustDemo.hpp and RustDemoShim.cpp are
# both generated from RustDemo.fpp by tools/fprime-rs/fpp_to_rust.py
# --component, along with the C header and the Rust dispatch layer; the model,
# the Rust implementation and this file are the whole component.
#
# Those four outputs are checked in rather than generated at configure time,
# because two of them are C++ the build has to compile. Regenerate them with
#
#     tools/fprime-rs/regen_component_glue.sh
#
# and CI runs the same script with --check, so a hand edit to any of them
# fails rather than surviving.
#
# The module builds only when the Rust workspace is available; there is no C++
# fallback implementation, because the point of the component is that its
# implementation is Rust.
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
