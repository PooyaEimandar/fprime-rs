####
# F prime CMakeLists.txt:
#
# SOURCE_FILES: combined list of source and autocoding diles
# MOD_DEPS: (optional) module dependencies
#
# Note: using PROJECT_NAME as EXECUTABLE_NAME
####

# fprime-rs: the priority scheduling has a Rust implementation behind the
# unchanged component class; FPRIME_RUST_IMPL_SVC_COM_QUEUE selects it (see
# Rust/enable.cmake). ComQueue is an active component -- the autocoded base
# still owns its thread and queue.
if (FPRIME_RS_AVAILABLE AND FPRIME_RUST_IMPL_SVC_COM_QUEUE)
  set(COM_QUEUE_IMPL "${CMAKE_CURRENT_LIST_DIR}/ComQueueRust.cpp")
else()
  set(COM_QUEUE_IMPL "${CMAKE_CURRENT_LIST_DIR}/ComQueue.cpp")
endif()

register_fprime_library(
  SOURCES
    "${COM_QUEUE_IMPL}"
  AUTOCODER_INPUTS
    "${CMAKE_CURRENT_LIST_DIR}/ComQueue.fpp"
  DEPENDS
    Fw_Types
    Utils_Types
    Fw_Buffer
)

if (FPRIME_RS_AVAILABLE AND FPRIME_RUST_IMPL_SVC_COM_QUEUE AND NOT FPRIME_IS_SUB_BUILD)
  target_link_libraries(Svc_ComQueue PUBLIC fprime_rust)
endif()

### UTs ###
register_fprime_ut(
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/ComQueueTestMain.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/ComQueueTester.cpp"
  AUTOCODER_INPUTS
    "${CMAKE_CURRENT_LIST_DIR}/ComQueue.fpp"
  UT_AUTO_HELPERS
)

set (UT_TARGET_NAME "${FPRIME_CURRENT_MODULE}_ut_exe")
if (TARGET "${UT_TARGET_NAME}")
    target_compile_options("${UT_TARGET_NAME}" PRIVATE -Wno-conversion)
endif()
