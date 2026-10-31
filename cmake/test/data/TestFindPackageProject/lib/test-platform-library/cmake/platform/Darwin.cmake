####
# Darwin.cmake:
#
# Test platform file provided by lib/test-platform-library.
# Defers to the framework Darwin platform and proves this file was loaded.
####
include("${FPRIME_FRAMEWORK_PATH}/cmake/platform/Darwin.cmake")
set(TEST_PLATFORM_LIBRARY_LOADED TRUE CACHE INTERNAL "Marker proving the library platform file was loaded")
