####
# F prime CMakeLists.txt:
#
# SOURCE_FILES: combined list of source and autocoding files
# MOD_DEPS: (optional) module dependencies
#
####
if (NOT FPRIME_USE_STUBBED_DRIVERS)
    restrict_platforms(Linux)
endif()

if(FPRIME_USE_STUBBED_DRIVERS)
    register_fprime_module(
      AUTOCODER_INPUTS
        "${CMAKE_CURRENT_LIST_DIR}/LinuxGpioDriver.fpp"
      SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/LinuxGpioDriverCommon.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/LinuxGpioDriverStub.cpp"
      HEADERS
        "${CMAKE_CURRENT_LIST_DIR}/LinuxGpioDriver.hpp"
    )
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    register_fprime_module(
      AUTOCODER_INPUTS
        "${CMAKE_CURRENT_LIST_DIR}/LinuxGpioDriver.fpp"
      SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/LinuxGpioDriverCommon.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/LinuxGpioDriver.cpp"
      HEADERS
        "${CMAKE_CURRENT_LIST_DIR}/LinuxGpioDriver.hpp"
    )
endif()

### UTs ###
# The interrupt loop itself needs a real gpiochip, but the decision it makes
# on each poll() result does not -- classify_poll_result lives in
# LinuxGpioDriverCommon.cpp, which every platform builds, so this runs
# wherever the module does.
if (FPRIME_USE_STUBBED_DRIVERS OR ${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    register_fprime_ut(
        LinuxGpioDriverPollTest
      SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/test/ut/GpioPollTest.cpp"
      DEPENDS
        Drv_LinuxGpioDriver
    )
endif()
