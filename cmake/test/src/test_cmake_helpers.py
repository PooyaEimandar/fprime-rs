"""Regression tests for the build-system test harness."""

import json
import os
from pathlib import Path
import signal
import subprocess
import sys

import pytest

from . import cmake

pytest_plugins = ["pytester"]


@pytest.mark.parametrize("mode", ["collect", "skip", "run"])
def test_build_fixture_lifecycle(pytester, mode):
    """Collection and skipped tests must not configure or build a deployment."""
    test_module = f"""
from pathlib import Path
import runpy
import pytest

namespace = runpy.run_path({str(Path(cmake.__file__))!r})
get_build = namespace["get_build"]

def configure(source, build, options):
    Path("configured").write_text(str(build))
    return 0, ["configured"], []

def build(directory, target):
    Path("built").write_text(target)
    return 0, ["built"], []

get_build.__globals__["run_cmake"] = configure
get_build.__globals__["run_make"] = build
BUILD = get_build("BUILD", Path.cwd(), make_targets=["example"])

@pytest.mark.skipif({mode == 'skip'}, reason="Deliberately skipped")
def test_build(BUILD):
    assert BUILD["build"].is_dir()
    assert BUILD["targets"]["example"][0] == 0
"""
    pytester.makepyfile(test_build=test_module)
    args = ["--collect-only"] if mode == "collect" else []
    result = pytester.runpytest_subprocess(*args, "-q")
    assert result.ret == 0
    if mode == "run":
        result.assert_outcomes(passed=1)
        assert (pytester.path / "built").read_text() == "example"
        build_directory = Path((pytester.path / "configured").read_text())
        assert not build_directory.exists(), "Build cache leaked after fixture teardown"
    else:
        if mode == "skip":
            result.assert_outcomes(skipped=1)
        assert not (pytester.path / "configured").exists()
        assert not (pytester.path / "built").exists()


@pytest.mark.parametrize("first_stream", ["stdout", "stderr"])
def test_subprocess_drains_both_streams(tmp_path, first_stream):
    """A verbose child must not block when either output pipe fills up."""
    script = """
import json
import runpy
import sys

helper = runpy.run_path(sys.argv[1])["subprocess_helper"]
print(json.dumps(helper([sys.executable, "-c", sys.argv[2]], sys.argv[3])))
"""
    second_stream = "stderr" if first_stream == "stdout" else "stdout"
    child = f"""
import sys
for name in ({first_stream!r}, {second_stream!r}):
    stream = getattr(sys, name)
    stream.write(name + "\\n" + "x" * 1048576 + "\\n\\n")
    stream.flush()
sys.exit(7)
"""
    with subprocess.Popen(
        [sys.executable, "-c", script, str(Path(cmake.__file__)), child, str(tmp_path)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        start_new_session=True,
    ) as process:
        try:
            stdout, stderr = process.communicate(timeout=10)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.communicate()
            pytest.fail("Build helper deadlocked while draining stdout and stderr")
    assert process.returncode == 0, stderr
    return_code, stdout_lines, stderr_lines = json.loads(stdout)
    assert return_code == 7
    assert stdout_lines == ["stdout\n", "x" * 1048576 + "\n"]
    assert stderr_lines == ["stderr\n", "x" * 1048576 + "\n"]
