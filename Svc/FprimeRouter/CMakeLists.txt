####
# F prime CMakeLists.txt:
#
# SOURCE_FILES: combined list of source and autocoding files
# MOD_DEPS: (optional) module dependencies
# UT_SOURCE_FILES: list of source files for unit tests
#
####

# fprime-rs: the buffer-context table logic has a Rust implementation behind
# the unchanged component class; FPRIME_RUST_IMPL_SVC_FPRIME_ROUTER selects it
# (see Rust/enable.cmake).
if (FPRIME_RS_AVAILABLE AND FPRIME_RUST_IMPL_SVC_FPRIME_ROUTER)
  set(FPRIME_ROUTER_IMPL "${CMAKE_CURRENT_LIST_DIR}/FprimeRouterRust.cpp")
else()
  set(FPRIME_ROUTER_IMPL "${CMAKE_CURRENT_LIST_DIR}/FprimeRouter.cpp")
endif()

register_fprime_module(
  AUTOCODER_INPUTS
    "${CMAKE_CURRENT_LIST_DIR}/FprimeRouter.fpp"
  SOURCES
    "${FPRIME_ROUTER_IMPL}"
  HEADERS
    "${CMAKE_CURRENT_LIST_DIR}/FprimeRouter.hpp"
)

if (FPRIME_RS_AVAILABLE AND FPRIME_RUST_IMPL_SVC_FPRIME_ROUTER AND NOT FPRIME_IS_SUB_BUILD)
  target_link_libraries(Svc_FprimeRouter PUBLIC fprime_rust)
endif()


#### UTS ####

register_fprime_ut(
  AUTOCODER_INPUTS
    "${CMAKE_CURRENT_LIST_DIR}/FprimeRouter.fpp"
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/FprimeRouterTester.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/FprimeRouterTestMain.cpp"
  UT_AUTO_HELPERS
)
