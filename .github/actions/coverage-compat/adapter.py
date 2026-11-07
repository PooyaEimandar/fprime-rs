#!/usr/bin/env python3
"""Adapt coverage-common's CLI to the pinned fprime-tools 4.3 interface.

4.3 forwards --pass-through to both CTest and gcovr for check --coverage.
Run the tests first, then collect existing coverage with the gcovr arguments.
Ordinary checks and existing-data coverage calls retain their original behavior.
Remove this adapter when the pinned tools isolate gcovr's pass-through options.
"""

import os
import subprocess
import sys


# These boolean options only belong to the coverage half of a check.
COVERAGE_OPTIONS = {
    "--coverage",
    "--all-sources",
    "--comp-ac",
    "--port-ac",
    "--type-ac",
    "--test-ac",
    "--sm-ac",
    "--test-sources",
    "--enable-fw-assert-branch-coverage",
}


def commands(args):
    """Return the ordered invocations, preserving argument boundaries."""
    boundary = args.index("--pass-through") if "--pass-through" in args else len(args)
    options = args[:boundary]
    if (
        options[:1] == ["check"]
        and "--coverage" in options
        and "--existing" not in options
    ):
        return [
            [arg for arg in options if arg not in COVERAGE_OPTIONS],
            options + ["--existing"] + args[boundary:],
        ]
    return [args]


def main(args, executable):
    """Propagate a failed command's status without running later stages."""
    for command in commands(args):
        status = subprocess.run([executable, *command], check=False).returncode
        if status:
            return status
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:], os.environ["FPRIME_UTIL_REAL"]))
