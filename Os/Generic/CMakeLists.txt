add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/Types")
add_custom_target("${FPRIME_CURRENT_MODULE}")

#### PriorityMemQueue (AtomicQueue-based) ####
register_fprime_module(
    Os_Generic_PriorityMemQueue_Implementation
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/PriorityMemQueue.cpp"
  HEADERS
    "${CMAKE_CURRENT_LIST_DIR}/PriorityMemQueue.hpp"
  DEPENDS
    Fw_Types
    Os_Generic_Types
    Os_CountingSemaphore
)
register_fprime_implementation(
    Os_Generic_PriorityMemQueue
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/DefaultPriorityMemQueue.cpp"
  IMPLEMENTS
    Os_Queue
  DEPENDS
    Fw_Types
    Os_Generic_PriorityMemQueue_Implementation
)

register_fprime_ut(
    PriorityMemQueue_UnitTest
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/PriorityMemQueueTests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/PriorityMemQueueInputValidationTests.cpp"
  DEPENDS
    Fw_Types
    Fw_Time
    Os
    STest
    Os_Generic_PriorityMemQueue_Implementation
  CHOOSES_IMPLEMENTATIONS
    Os_Generic_PriorityMemQueue
)

#### Os/Generic/Queue Section ####
# fprime-rs Phase 16: the queue's message-slot bookkeeping can run in Rust
# (see Rust/enable.cmake). PriorityQueueRust.cpp is generated from
# PriorityQueue.cpp by tools/fprime-rs/gen_os_priorityqueue.py.
if (FPRIME_RS_AVAILABLE AND FPRIME_RUST_IMPL_OS_PRIORITYQUEUE)
    set(FPRIME_PRIORITYQUEUE_IMPL "${CMAKE_CURRENT_LIST_DIR}/PriorityQueueRust.cpp")
else()
    set(FPRIME_PRIORITYQUEUE_IMPL "${CMAKE_CURRENT_LIST_DIR}/PriorityQueue.cpp")
endif()

register_fprime_module(
    Os_Generic_PriorityQueue_Implementation
  SOURCES
    "${FPRIME_PRIORITYQUEUE_IMPL}"
  HEADERS
    "${CMAKE_CURRENT_LIST_DIR}/PriorityQueue.hpp"
  DEPENDS
    Fw_Types
    Os_Generic_Types
)

if (FPRIME_RS_AVAILABLE AND FPRIME_RUST_IMPL_OS_PRIORITYQUEUE AND NOT FPRIME_IS_SUB_BUILD)
    target_link_libraries(Os_Generic_PriorityQueue_Implementation PUBLIC fprime_rust)

    # PriorityQueueRust.cpp is a generated near-copy of PriorityQueue.cpp;
    # verify on every build that it is still what the generator produces, so
    # an upstream edit cannot silently fail to reach the Rust build.
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/PriorityQueueRust.verified"
        COMMAND "${PYTHON}" "${FPRIME_FRAMEWORK_PATH}/tools/fprime-rs/gen_os_priorityqueue.py" "--check"
        COMMAND "${CMAKE_COMMAND}" "-E" "touch" "${CMAKE_CURRENT_BINARY_DIR}/PriorityQueueRust.verified"
        DEPENDS "${CMAKE_CURRENT_LIST_DIR}/PriorityQueue.cpp"
                "${CMAKE_CURRENT_LIST_DIR}/PriorityQueueRust.cpp"
                "${FPRIME_FRAMEWORK_PATH}/tools/fprime-rs/gen_os_priorityqueue.py"
                "${FPRIME_FRAMEWORK_PATH}/tools/fprime-rs/shimgen.py"
        COMMENT "[rust] Verifying the Rust-backed priority queue matches PriorityQueue.cpp"
        VERBATIM
    )
    add_custom_target(Os_Generic_PriorityQueue_rust_verify
                      DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/PriorityQueueRust.verified")
    add_dependencies(Os_Generic_PriorityQueue_Implementation Os_Generic_PriorityQueue_rust_verify)
endif()
register_fprime_implementation(
    Os_Generic_PriorityQueue
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/DefaultPriorityQueue.cpp"
  IMPLEMENTS
    Os_Queue
  DEPENDS
    Fw_Types
    Os_Generic_PriorityQueue_Implementation
)

register_fprime_ut(
    PriorityQueueTest
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/PriorityQueueTests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../test/ut/queue/CommonTests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../test/ut/queue/QueueRules.cpp"
  DEPENDS
    Fw_Types
    Fw_Time
    Os
    STest
  CHOOSES_IMPLEMENTATIONS
    Os_Generic_PriorityQueue
)
if (TARGET PriorityQueueTest)
    target_compile_options(PriorityQueueTest PRIVATE -Wno-conversion)
    target_include_directories(PriorityQueueTest PRIVATE "${CMAKE_CURRENT_LIST_DIR}/test/ut")
endif()

#### Os/Generic/LocklessQueue Section ####
register_fprime_module(
    Os_Generic_LocklessPriorityQueue_Implementation
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/LocklessPriorityQueue.cpp"
  HEADERS
    "${CMAKE_CURRENT_LIST_DIR}/LocklessPriorityQueue.hpp"
  DEPENDS
    Fw_Types
    Fw_Time
    Os
)
register_fprime_implementation(
    Os_Generic_LocklessPriorityQueue
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/DefaultLocklessPriorityQueue.cpp"
  IMPLEMENTS
    Os_Queue
  DEPENDS
    Fw_Types
    Os_Generic_LocklessPriorityQueue_Implementation
)

register_fprime_ut(
    LocklessPriorityQueueTest
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/LocklessPriorityQueueTests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../test/ut/queue/CommonTests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../test/ut/queue/QueueRules.cpp"
  DEPENDS
    Fw_Types
    Fw_Time
    Os
    STest
  CHOOSES_IMPLEMENTATIONS
    Os_Generic_LocklessPriorityQueue
)
if (TARGET LocklessPriorityQueueTest)
    target_compile_options(LocklessPriorityQueueTest PRIVATE -Wno-conversion)
    target_include_directories(LocklessPriorityQueueTest PRIVATE "${CMAKE_CURRENT_LIST_DIR}/test/ut")
endif()

register_fprime_ut(
    LocklessPriorityQueueTsanTest
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/LocklessPriorityQueueTsanTests.cpp"
  DEPENDS
    Fw_Types
    Fw_Time
    Os
  CHOOSES_IMPLEMENTATIONS
    Os_Generic_LocklessPriorityQueue
)
if (TARGET LocklessPriorityQueueTsanTest)
    target_compile_options(LocklessPriorityQueueTsanTest PRIVATE -Wno-conversion)
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(LocklessPriorityQueueTsanTest PRIVATE -Wno-unused-lambda-capture)
    endif()
    target_include_directories(LocklessPriorityQueueTsanTest PRIVATE "${CMAKE_CURRENT_LIST_DIR}/test/ut")
endif()
