####
# F prime CMakeLists.txt:
#
# SOURCE_FILES: combined list of source and autocoding files
# MOD_DEPS: (optional) module dependencies
#
####
restrict_platforms(Linux Darwin)

register_fprime_module(
  AUTOCODER_INPUTS
    "${CMAKE_CURRENT_LIST_DIR}/LinuxUartDriver.fpp"
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/LinuxUartDriver.cpp"
  HEADERS
    "${CMAKE_CURRENT_LIST_DIR}/LinuxUartDriver.hpp"
  DEPENDS
    Os
)

#### UTs ####
# Characterization tests: this driver previously had none. They pin the
# open/write behavior (via a pseudo-terminal and a deliberately-full pipe)
# so any future rewrite has an oracle. Linked against util for openpty.
register_fprime_ut(
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/LinuxUartDriverTestMain.cpp"
  DEPENDS
    Drv_LinuxUartDriver
    Os
)
if (TARGET Drv_LinuxUartDriver_ut_exe AND NOT APPLE)
  # openpty lives in libutil on Linux; it is in libSystem on macOS
  target_link_libraries(Drv_LinuxUartDriver_ut_exe PRIVATE util)
endif()
