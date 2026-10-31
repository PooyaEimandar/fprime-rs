"""
cmake.py:

A wrapper used to call CMake given a directory, a target directory to build, and a list of expected
outputs for the given run. Options to the run and build targets (make targets) can optionally be
supplied. CMake is run as a command line call to the cmake system. Thus CMake must be installed.

@author mstarch
"""

import multiprocessing
from concurrent.futures import ThreadPoolExecutor
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import pytest

# Constants to supplied to the calls to subprocess
CPUS = multiprocessing.cpu_count()
CMAKE = "cmake"
MAKE_CALL = "make"
MAKE_ARGS = [f"-j{CPUS}"]


def subprocess_helper(args, cwd):
    """Subprocess helper used to 'tee' the output to: console and capture"""

    def capture_stream(stream, destination):
        """Drain one pipe independently so neither pipe can block the child."""
        lines = []
        for line in stream:
            lines.append(line)
            if "-s" in sys.argv or "--capture=no" in sys.argv:
                print(line, end="", file=destination)
        return [line for line in lines if line.strip() != ""]

    # Verbose output desired from pytest
    with subprocess.Popen(
        args=args,
        cwd=str(cwd),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    ) as proc:
        with ThreadPoolExecutor(max_workers=2) as readers:
            stdout = readers.submit(capture_stream, proc.stdout, sys.stdout)
            stderr = readers.submit(capture_stream, proc.stderr, sys.stderr)
            output = stdout.result(), stderr.result()
        return proc.wait(), output[0], output[1]


def run_cmake(source_directory, build_path, options=None):
    """
    Runs the cmake in the current directory with the given options and build_path

    :param source_directory: source directory to run CMake within
    :param build_path: path to build
    :param options: options to pass CMake
    :return: True if successful, False otherwise
    """
    args = [CMAKE]
    options = {} if options is None else options
    for option in options.keys():
        value = options[option]
        args.append("-D{0}={1}".format(option, value))
    args.append(source_directory)

    return subprocess_helper(args, build_path)


def run_make(build_directory, target):
    """
    Runs the make command to ensure that the CMake system can follow through and finish the build.
    Note: this assumes that the provided application built properly. Thus, those unit tests should
    run first.

    :param build_directory: build directory to change to
    :param target: target to the make command
    :return: True if successful, False otherwise
    """
    args = [MAKE_CALL]
    if target != "":
        args.append(target)
    args.extend(MAKE_ARGS)
    return subprocess_helper(args, build_directory)


def assert_process_success(
    data_object, errors_ok=False, warnings_ok=False, targets=None
):
    """Assert the subprocess runs worked as expected"""
    for field in ["source", "build", "install", "cmake", "targets"]:
        assert field in data_object, f"Data object malformed: missing '{field}' field"
        assert field in data_object, f"Data object malformed: missing '{field}' field"

    return_code, stdout, stderr = data_object["cmake"]
    assert return_code == 0, f"CMake generation failed with return code {return_code}"
    assert stdout, "CMake generated no standard out process"
    if stderr and not errors_ok:
        if warnings_ok:
            has_errors = any(
                "CMake Error" in line or "CMake Fatal Error" in line for line in stderr
            )
            assert not has_errors, f"CMake generated errors:\n{''.join(stderr)}"
        else:
            assert False, f"CMake generated errors:\n{''.join(stderr)}"

    targets = data_object["targets"].keys() if targets is None else targets
    filtered = [
        (target, output)
        for target, output in data_object["targets"].items()
        if target in targets
    ]

    for target, output in filtered:
        return_code, stdout, stderr = output
        assert return_code == 0, f"CMake failed building '{target}'"
        assert stdout, f"CMake generated no standard out building '{target}'"


def get_build(
    fixture_name,
    source_directory,
    cmake_arguments=None,
    make_targets=None,
    install_directory=None,
):
    """Return a fixture that generates and builds a deployment on first use."""
    base_cmake_arguments = {"FPRIME_SUB_BUILD_JOBS": f"{CPUS}"}
    cmake_arguments = {} if cmake_arguments is None else dict(cmake_arguments)
    cmake_arguments.update(base_cmake_arguments)
    install_directory_calc = Path(
        Path(source_directory) / "build-artifacts"
        if install_directory is None
        else install_directory
    )
    make_targets = ["all"] if make_targets is None else make_targets
    if install_directory is not None:
        cmake_arguments["FPRIME_INSTALL_DEST"] = str(install_directory)

    @pytest.fixture(scope="session", name=fixture_name)
    def fixture_function():
        build_directory = Path(tempfile.mkdtemp())
        try:
            cmake_output = run_cmake(source_directory, build_directory, cmake_arguments)
            target_outputs = {
                target: run_make(build_directory, target) for target in make_targets
            }
            yield {
                "source": source_directory,
                "build": build_directory,
                "install": install_directory_calc,
                "cmake": cmake_output,
                "targets": target_outputs,
            }
        finally:
            shutil.rmtree(build_directory, ignore_errors=True)
            if install_directory is not None:
                shutil.rmtree(install_directory, ignore_errors=True)

    return fixture_function
