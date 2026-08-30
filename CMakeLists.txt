####
# CMakeLists.txt:
#
# Build core F prime.
####
cmake_minimum_required(VERSION 3.18)
project(FPrime C CXX)
set(FPRIME_INCLUDE_FRAMEWORK_CODE OFF)
find_package(FPrime REQUIRED PATHS "${CMAKE_CURRENT_LIST_DIR}")
register_fprime_project()

# Validate F Prime against the warning set it publishes. Specific build modules that do not comply
# with these flags can disable one or more of them.
#
# The flags F Prime *requires* in order to build are not listed here: they are usage requirements
# carried by every F Prime target, set up in cmake/flags.cmake.
fprime_add_validation_flags()
# fprime-rs availability + cargo target, needed before framework modules that
# may select Rust-backed implementation sources
include("${CMAKE_CURRENT_LIST_DIR}/Rust/enable.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/FPrime-Code.cmake")

# Rust/ is registered as a core package in cmake/FPrime.cmake, alongside Fw and
# Os, so that find_package consumers get it too. Only enable.cmake is included
# here, and it must be: the framework modules read FPRIME_RS_AVAILABLE while
# choosing their sources.
