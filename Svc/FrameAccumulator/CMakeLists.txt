####
# F prime CMakeLists.txt:
#
# SOURCE_FILES: combined list of source and autocoding files
# MOD_DEPS: (optional) module dependencies
# UT_SOURCE_FILES: list of source files for unit tests
#
####

# fprime-rs: the accumulate/detect/extract orchestration has a Rust
# implementation behind the unchanged component class;
# FPRIME_RUST_IMPL_SVC_FRAME_ACCUMULATOR selects it (see Rust/enable.cmake).
if (FPRIME_RS_AVAILABLE AND FPRIME_RUST_IMPL_SVC_FRAME_ACCUMULATOR)
  set(FRAME_ACCUMULATOR_IMPL "${CMAKE_CURRENT_LIST_DIR}/FrameAccumulatorRust.cpp")
else()
  set(FRAME_ACCUMULATOR_IMPL "${CMAKE_CURRENT_LIST_DIR}/FrameAccumulator.cpp")
endif()

register_fprime_library(
  SOURCES
    "${FRAME_ACCUMULATOR_IMPL}"
    "${CMAKE_CURRENT_LIST_DIR}/FrameDetector/FprimeFrameDetector.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/FrameDetector/CcsdsTcFrameDetector.cpp"
  AUTOCODER_INPUTS
    "${CMAKE_CURRENT_LIST_DIR}/FrameAccumulator.fpp"
  DEPENDS
    Utils_Types
    Svc_FprimeProtocol
    Svc_Ccsds_Types
)
target_compile_options(${FPRIME_CURRENT_MODULE} PRIVATE -Wno-tautological-constant-out-of-range-compare -Wno-type-limits)

if (FPRIME_RS_AVAILABLE AND FPRIME_RUST_IMPL_SVC_FRAME_ACCUMULATOR AND NOT FPRIME_IS_SUB_BUILD)
  target_link_libraries("${FPRIME_CURRENT_MODULE}" PUBLIC fprime_rust)
endif()

#### UTs ####
register_fprime_ut(
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/FrameAccumulatorTester.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/FrameAccumulatorTestMain.cpp"
  AUTOCODER_INPUTS
    "${CMAKE_CURRENT_LIST_DIR}/FrameAccumulator.fpp"
  DEPENDS
    STest
  UT_AUTO_HELPERS
)

#### FrameDetector tests ####

register_fprime_ut(
  "Svc_FrameAccumulator_FprimeFrameDetector_test"
  SOURCES 
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/detectors/FprimeFrameDetectorTestMain.cpp"
  HEADERS
    "${CMAKE_CURRENT_LIST_DIR}/FrameDetector/FprimeFrameDetector.hpp"
  DEPENDS
    STest
  UT_AUTO_HELPERS
)

register_fprime_ut(
  "Svc_FrameAccumulator_CcsdsTcFrameDetector_test"
  SOURCES 
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/detectors/CcsdsTcFrameDetectorTestMain.cpp"
  HEADERS
    "${CMAKE_CURRENT_LIST_DIR}/FrameDetector/CcsdsTcFrameDetector.hpp"
  DEPENDS
    STest
  UT_AUTO_HELPERS
)
