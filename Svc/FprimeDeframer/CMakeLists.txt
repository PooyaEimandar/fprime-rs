####
# F prime CMakeLists.txt:
#
# SOURCE_FILES: combined list of source and autocoding files
# MOD_DEPS: (optional) module dependencies
#
# Note: using PROJECT_NAME as EXECUTABLE_NAME
####


# fprime-rs: the frame-validation logic has a Rust implementation behind the
# unchanged component class; FPRIME_RUST_IMPL_SVC_FPRIME_DEFRAMER selects it
# (see Rust/enable.cmake).
if (FPRIME_RS_AVAILABLE AND FPRIME_RUST_IMPL_SVC_FPRIME_DEFRAMER)
  set(FPRIME_DEFRAMER_IMPL "${CMAKE_CURRENT_LIST_DIR}/FprimeDeframerRust.cpp")
else()
  set(FPRIME_DEFRAMER_IMPL "${CMAKE_CURRENT_LIST_DIR}/FprimeDeframer.cpp")
endif()

register_fprime_module(
  AUTOCODER_INPUTS
    "${CMAKE_CURRENT_LIST_DIR}/FprimeDeframer.fpp"
  SOURCES
    "${FPRIME_DEFRAMER_IMPL}"
  HEADERS
    "${CMAKE_CURRENT_LIST_DIR}/FprimeDeframer.hpp"
  DEPENDS
    Utils_Types
    Svc_FprimeProtocol
)

if (FPRIME_RS_AVAILABLE AND FPRIME_RUST_IMPL_SVC_FPRIME_DEFRAMER AND NOT FPRIME_IS_SUB_BUILD)
  target_link_libraries(Svc_FprimeDeframer PUBLIC fprime_rust)
endif()


#### UTs ####

register_fprime_ut(
  AUTOCODER_INPUTS
    "${CMAKE_CURRENT_LIST_DIR}/FprimeDeframer.fpp"
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/FprimeDeframerTester.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/FprimeDeframerTestMain.cpp"
  DEPENDS
    STest
  UT_AUTO_HELPERS
)
