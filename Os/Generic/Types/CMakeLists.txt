# fprime-rs Phase 9: the heap algorithm behind the OSAL priority queue can run
# in Rust (see Rust/enable.cmake). MaxHeapRust.cpp is generated from
# MaxHeap.cpp by tools/fprime-rs/gen_os_maxheap.py.
if (FPRIME_RS_AVAILABLE AND FPRIME_RUST_IMPL_OS_MAXHEAP)
    set(FPRIME_MAXHEAP_IMPL "${CMAKE_CURRENT_LIST_DIR}/MaxHeapRust.cpp")
else()
    set(FPRIME_MAXHEAP_IMPL "${CMAKE_CURRENT_LIST_DIR}/MaxHeap.cpp")
endif()

register_fprime_module(
  SOURCES
    "${FPRIME_MAXHEAP_IMPL}"
    "${CMAKE_CURRENT_LIST_DIR}/AtomicQueue.cpp"
  HEADERS
    "${CMAKE_CURRENT_LIST_DIR}/MaxHeap.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/AtomicQueue.hpp"
  DEPENDS
    Fw_Types
    Fw_Logger
    Os_CountingSemaphore
)

if (FPRIME_RS_AVAILABLE AND FPRIME_RUST_IMPL_OS_MAXHEAP AND NOT FPRIME_IS_SUB_BUILD)
    target_link_libraries(Os_Generic_Types PUBLIC fprime_rust)

    # MaxHeapRust.cpp is a generated near-copy of MaxHeap.cpp; verify on every
    # build that it is still what the generator produces, so an upstream edit
    # cannot silently fail to reach the Rust build.
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/MaxHeapRust.verified"
        COMMAND "${PYTHON}" "${FPRIME_FRAMEWORK_PATH}/tools/fprime-rs/gen_os_maxheap.py" "--check"
        COMMAND "${CMAKE_COMMAND}" "-E" "touch" "${CMAKE_CURRENT_BINARY_DIR}/MaxHeapRust.verified"
        DEPENDS "${CMAKE_CURRENT_LIST_DIR}/MaxHeap.cpp"
                "${CMAKE_CURRENT_LIST_DIR}/MaxHeapRust.cpp"
                "${FPRIME_FRAMEWORK_PATH}/tools/fprime-rs/gen_os_maxheap.py"
                "${FPRIME_FRAMEWORK_PATH}/tools/fprime-rs/shimgen.py"
        COMMENT "[rust] Verifying the Rust-backed MaxHeap matches MaxHeap.cpp"
        VERBATIM
    )
    add_custom_target(Os_Generic_Types_rust_verify
                      DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/MaxHeapRust.verified")
    add_dependencies(Os_Generic_Types Os_Generic_Types_rust_verify)
endif()

# Rules based unit testing

set(UT_TARGET_NAME "Types_Max_Heap_test")
register_fprime_ut("${UT_TARGET_NAME}"
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/MaxHeap/MaxHeapTest.cpp"
  DEPENDS
    STest
    Fw_Types
)
if (TARGET "${UT_TARGET_NAME}")
    target_compile_options("${UT_TARGET_NAME}" PRIVATE -Wno-conversion)
endif()

# AtomicQueue unit test
set(UT_TARGET_NAME "Types_Atomic_Queue_test")
register_fprime_ut("${UT_TARGET_NAME}"
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/AtomicQueue/AtomicQueueTest.cpp"
  DEPENDS
    STest
    Fw_Types
    Fw_Time
    Os
    Os_CountingSemaphore
)
if (TARGET "${UT_TARGET_NAME}")
    target_compile_options("${UT_TARGET_NAME}" PRIVATE -Wno-conversion)
endif()
