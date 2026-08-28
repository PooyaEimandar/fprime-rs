####
# F prime CMakeLists.txt:
####

# fprime-rs: the CircularBuffer byte engine has a Rust implementation behind
# the unchanged header; FPRIME_RUST_IMPL_UTILS_TYPES selects it (see
# Rust/enable.cmake). Queue's orchestration stays C++ and runs on whichever
# engine is selected.
if (FPRIME_RS_AVAILABLE AND FPRIME_RUST_IMPL_UTILS_TYPES)
    set(CIRCULAR_BUFFER_IMPL "${CMAKE_CURRENT_LIST_DIR}/CircularBufferRust.cpp")
else()
    set(CIRCULAR_BUFFER_IMPL "${CMAKE_CURRENT_LIST_DIR}/CircularBuffer.cpp")
endif()

register_fprime_module(
    SOURCES
        "${CIRCULAR_BUFFER_IMPL}"
        "${CMAKE_CURRENT_LIST_DIR}/Queue.cpp"
    DEPENDS
        Fw_Types

)

if (FPRIME_RS_AVAILABLE AND FPRIME_RUST_IMPL_UTILS_TYPES AND NOT FPRIME_IS_SUB_BUILD)
    target_link_libraries(Utils_Types PUBLIC fprime_rust)
endif()

set (UT_TARGET_NAME "Types_Circular_Buffer_ut_exe")
register_fprime_ut("${UT_TARGET_NAME}"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/test/ut/CircularBuffer/CircularState.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/test/ut/CircularBuffer/CircularRules.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/test/ut/CircularBuffer/CircularBufferTester.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/test/ut/CircularBuffer/Main.cpp"
    DEPENDS
        STest
        Fw_Types
    )
if (TARGET "${UT_TARGET_NAME}")
    target_compile_options("${UT_TARGET_NAME}" PRIVATE -Wno-conversion)
endif()

# Queue unit tests
register_fprime_ut(
    "Utils_Types_Queue_ut_exe"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/test/ut/Queue/QueueTest.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/test/ut/Queue/QueueRulesTest.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/test/ut/Queue/QueueSerializableTest.cpp"
    DEPENDS
        STest
        Fw_Types
        Fw_Buffer
)