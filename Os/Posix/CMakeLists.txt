####
# F prime CMakeLists.txt:
#
# SOURCE_FILES: combined list of source and autocoding files
# MOD_DEPS: (optional) module dependencies
#
####
restrict_platforms(Posix)
add_custom_target("${FPRIME_CURRENT_MODULE}")

# -----------------------------------------
## Shared Section
# -----------------------------------------

if (BUILD_TESTING)
    add_compile_options(
        -Wno-conversion
    )
endif()

register_fprime_module(
    Os_Posix_Shared
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/error.cpp"
  HEADERS
    "${CMAKE_CURRENT_LIST_DIR}/error.hpp"
  DEPENDS
    Fw_Time
    Fw_Types
)

# Set up Posix implementations
register_os_implementation("File;FileSystem;Directory" Posix Os_Posix_Shared)
register_os_implementation("Console" Posix)

register_os_implementation("Task" Posix Os_Posix_Shared Fw_Time)
register_os_implementation("Mutex;ConditionVariable" Posix Os_Posix_Shared)
# POSIX unnamed semaphores (sem_init) are non-functional on macOS (return
# ENOSYS) and sem_timedwait does not exist. The Darwin implementation
# covers macOS; exclude the Posix CountingSemaphore entirely.
if(NOT APPLE)
    register_os_implementation("CountingSemaphore" Posix Os_Posix_Shared)
endif()
# fprime-rs Phase 15: the interval arithmetic behind Os::RawTime can run in
# Rust (see Rust/enable.cmake). RawTimeRust.cpp is generated from RawTime.cpp
# by tools/fprime-rs/gen_os_rawtime.py; the clock, errno translation, handle
# and serialization stay in the original.
if (FPRIME_RS_AVAILABLE AND FPRIME_RUST_IMPL_OS_RAWTIME)
    set(FPRIME_OS_SOURCE_RawTime "${CMAKE_CURRENT_LIST_DIR}/RawTimeRust.cpp")

    # The shim IS the implementation of RawTime.cpp, so it must fault as
    # RawTime.cpp. Recompute ASSERT_FILE_ID and ASSERT_RELATIVE_PATH from the
    # original's path, as Fw/Time and Fw/Types do.
    string(REPLACE "${FPRIME_FRAMEWORK_PATH}/" "" FPRIME_RAWTIME_SHORT_SRC
           "${CMAKE_CURRENT_LIST_DIR}/RawTime.cpp")
    string(MD5 FPRIME_RAWTIME_HASH "${FPRIME_RAWTIME_SHORT_SRC}")
    string(SUBSTRING "${FPRIME_RAWTIME_HASH}" 0 8 FPRIME_RAWTIME_FILE_ID)
    set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/RawTimeRust.cpp" PROPERTIES
        COMPILE_FLAGS
        "-DASSERT_FILE_ID=0x${FPRIME_RAWTIME_FILE_ID} -DASSERT_RELATIVE_PATH='\"${FPRIME_RAWTIME_SHORT_SRC}\"'")
endif()
register_os_implementation("RawTime" Posix Os_Posix_Shared)
if (FPRIME_RS_AVAILABLE AND FPRIME_RUST_IMPL_OS_RAWTIME AND NOT FPRIME_IS_SUB_BUILD)
    target_link_libraries(Os_RawTime_Posix_Implementation PUBLIC fprime_rust)

    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/RawTimeRust.verified"
        COMMAND "${PYTHON}" "${FPRIME_FRAMEWORK_PATH}/tools/fprime-rs/gen_os_rawtime.py" "--check"
        COMMAND "${CMAKE_COMMAND}" "-E" "touch" "${CMAKE_CURRENT_BINARY_DIR}/RawTimeRust.verified"
        DEPENDS "${CMAKE_CURRENT_LIST_DIR}/RawTime.cpp"
                "${CMAKE_CURRENT_LIST_DIR}/RawTimeRust.cpp"
                "${FPRIME_FRAMEWORK_PATH}/tools/fprime-rs/gen_os_rawtime.py"
                "${FPRIME_FRAMEWORK_PATH}/tools/fprime-rs/shimgen.py"
        COMMENT "[rust] Verifying the Rust-backed Os::RawTime matches RawTime.cpp"
        VERBATIM
    )
    add_custom_target(Os_RawTime_rust_verify
                      DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/RawTimeRust.verified")
    add_dependencies(Os_RawTime_Posix_Implementation Os_RawTime_rust_verify)
endif()

# -----------------------------------------
### Os/File/Posix Test Section
# -----------------------------------------
register_fprime_ut(PosixFileTest 
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/../test/ut/file/CommonTests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../test/ut/file/FileRules.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/PosixFileTests.cpp"     
  DEPENDS
    Fw_Types
    Os_Test_File_SyntheticFileSystem
    STest
  CHOOSES_IMPLEMENTATIONS
    "Os_File_Posix" 
)

# -----------------------------------------
### Os/Console/Posix Test Section
# -----------------------------------------
register_fprime_ut(
     PosixConsoleTest 
  SOURCES
     "${CMAKE_CURRENT_LIST_DIR}/test/ut/PosixConsoleTests.cpp"
  DEPENDS
    Fw_Types
    STest
  CHOOSES_IMPLEMENTATIONS
    "Os_Console_Posix"
)

# -----------------------------------------
### Os/Task/Posix Test Section
# -----------------------------------------
register_fprime_ut(
     PosixTaskTest 
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/../test/ut/task/CommonTests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../test/ut/task/TaskRules.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/PosixTaskTests.cpp"   
  DEPENDS
    Fw_Types
    STest
  CHOOSES_IMPLEMENTATIONS
     "Os_Task_Posix"
)

# -----------------------------------------
### Os/Mutex/Posix Test Section
# -----------------------------------------

register_fprime_ut(
     PosixMutexTest 
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/../test/ut/mutex/CommonTests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../test/ut/mutex/MutexRules.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/PosixMutexTests.cpp"
  DEPENDS
    Fw_Types
    STest
  CHOOSES_IMPLEMENTATIONS
    "Os_Mutex_Posix"
)

# -----------------------------------------
### Os/FileSystem/Posix Test Section
# -----------------------------------------
register_fprime_ut(
     PosixFileSystemTest 
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/../test/ut/filesystem/CommonTests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../test/ut/filesystem/FileSystemRules.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/PosixFileSystemTests.cpp"     
  DEPENDS
    Fw_Types
    STest
  CHOOSES_IMPLEMENTATIONS
    "Os_File_Posix"
)

# -----------------------------------------
### Os/Directory/Posix Test Section
# -----------------------------------------

register_fprime_ut(
     PosixDirectoryTest 
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/../test/ut/directory/CommonTests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../test/ut/directory/DirectoryRules.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/PosixDirectoryTests.cpp"
  DEPENDS
    Fw_Types
    STest
  CHOOSES_IMPLEMENTATIONS
    "Os_File_Posix"
)

# -----------------------------------------
### Os/ConditionVariable/Posix Test Section
# -----------------------------------------

register_fprime_ut(
     PosixConditionTests 
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/../test/ut/condition/CommonTests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../test/ut/condition/ConditionRules.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/PosixConditionTests.cpp"     
  DEPENDS
    Fw_Types
    STest
  CHOOSES_IMPLEMENTATIONS
    "Os_Mutex_Posix"
)

# -----------------------------------------
### Os/CountingSemaphore/Posix Test Section
# -----------------------------------------
if(NOT APPLE)
register_fprime_ut(
     PosixCountingSemaphoreTest 
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/../test/ut/countingsemaphore/CommonTests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../test/ut/countingsemaphore/CountingSemaphoreRules.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/PosixCountingSemaphoreTests.cpp"     
  DEPENDS
    Fw_Types
    STest
  CHOOSES_IMPLEMENTATIONS
    "Os_CountingSemaphore_Posix"
)
endif()

# -----------------------------------------
### Os/RawTime/Posix Test Section
# -----------------------------------------
register_fprime_ut(
     PosixRawTimeTest 
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/../test/ut/rawtime/CommonTests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../test/ut/rawtime/RawTimeRules.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/PosixRawTimeTests.cpp"     
  DEPENDS
    Fw_Types
    STest
  CHOOSES_IMPLEMENTATIONS
    "Os_RawTime_Posix"
)
