"""Keep unsupported Rust archives out of native and sub-build module graphs."""

import pytest

from . import cmake, settings


@pytest.mark.parametrize("sub_build", [False, True])
@pytest.mark.parametrize("mode", ["native", "cross", "shared", "32bit", "disabled"])
def test_rust_availability(tmp_path, mode, sub_build):
    settings_lines = {
        "native": "",
        "cross": "set(CMAKE_CROSSCOMPILING TRUE)",
        "shared": "set(BUILD_SHARED_LIBS ON)",
        "32bit": "set(CMAKE_SIZEOF_VOID_P 4)",
        "disabled": 'set(FPRIME_ENABLE_RUST OFF CACHE BOOL "" FORCE)',
    }
    expected = "TRUE" if mode == "native" else "FALSE"
    source = f"""
cmake_minimum_required(VERSION 3.16)
project(RustAvailability NONE)
set(CMAKE_SIZEOF_VOID_P 8)
set(FPRIME_IS_SUB_BUILD {"TRUE" if sub_build else "FALSE"})
# An existing executable stands in for Cargo; configuration must not invoke it.
set(FPRIME_CARGO "${{CMAKE_COMMAND}}")
{settings_lines[mode]}
include("{settings.FRAMEWORK_PATH}/Rust/enable.cmake")
if(NOT "${{FPRIME_RS_AVAILABLE}}" STREQUAL "{expected}")
    message(FATAL_ERROR "Unexpected Rust availability: ${{FPRIME_RS_AVAILABLE}}")
endif()
if({"TRUE" if mode != "native" or sub_build else "FALSE"})
    if(TARGET fprime_rust OR TARGET fprime_rust_cargo)
        message(FATAL_ERROR "Unexpected Rust build targets")
    endif()
elseif(NOT TARGET fprime_rust OR NOT TARGET fprime_rust_cargo)
    message(FATAL_ERROR "Native static build is missing Rust targets")
endif()
"""
    (tmp_path / "CMakeLists.txt").write_text(source)
    build = tmp_path / "build"
    build.mkdir()
    return_code, stdout, stderr = cmake.run_cmake(tmp_path, build)
    assert return_code == 0, "".join(stdout + stderr)
