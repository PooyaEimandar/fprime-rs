#!/usr/bin/env python3
"""Tests for the shim generators' drift check.

Every `--check` invocation in CI and in the build runs against a pristine
tree, so it can only ever exercise the *pass* path. That leaves the thing the
check exists for -- catching drift -- untested, and a check that silently
stopped catching anything would look exactly like a healthy one.

This mutates copies of the upstream sources and the generated files, and
asserts what each generator decides. The mutations are chosen to be real
divergences, several of them the kind that would matter: a `..` that escapes
the sandbox root, an inverted heap tie-break, a moved buffer boundary.

    python3 tools/fprime-rs/test_shimgen.py

Exits non-zero on the first failure.
"""

import copy
import pathlib
import shutil
import sys
import tempfile

import shimgen
from shimgen import REPO

import gen_fw_kernel
import gen_fw_strings
import gen_fw_time
import gen_os_filepath
import gen_os_maxheap
import gen_os_rawtime

# (shim, [(description, which file, find, replace, expected_up_to_date)])
#
# `which` is "source" for an upstream edit or "target" for an edit to the
# generated file. `expected_up_to_date` False means the check must report
# drift.
CASES = [
    (
        gen_fw_kernel.SHIM,
        [
            (
                "reformat of the generated file",
                "target",
                "namespace Fw {",
                "namespace Fw\n{",
                True,
            ),
            (
                "content edit of the generated file",
                "target",
                "FW_ASSERT(src);",
                "FW_ASSERT(src != nullptr);",
                False,
            ),
            (
                "upstream edit inside a replaced body",
                "source",
                "this->m_deserLoc = 0;",
                "this->m_deserLoc = 1;",
                False,
            ),
            (
                "upstream edit outside every replaced body",
                "source",
                "SerializeStatus LinearBufferBase::setBuffLen(Serializable::SizeType length) {\n"
                "    if (this->m_capacity < length) {",
                "SerializeStatus LinearBufferBase::setBuffLen(Serializable::SizeType length) {\n"
                "    if (this->m_capacity < length + 1) {",
                False,
            ),
        ],
    ),
    (
        gen_fw_time.SHIM,
        [
            (
                "upstream edit inside a replaced body (compare)",
                "source",
                "return TimeComparison::INCOMPARABLE;",
                "return TimeComparison::EQ;",
                False,
            ),
            (
                "upstream edit outside every replaced body",
                "source",
                "this->m_val.set_timeBase(timeBase);",
                "this->m_val.set_timeBase(TimeBase::TB_NONE);",
                False,
            ),
        ],
    ),
    (
        gen_fw_strings.SHIM,
        [
            (
                "upstream edit inside a replaced body (string_length)",
                "source",
                "if (source[length] == '\\0') {",
                "if (source[length] == ' ') {",
                False,
            ),
            (
                "upstream edit inside a replaced body: the zero-size match index",
                "source",
                "match_index = source_size - 1;",
                "match_index = source_size - 2;",
                False,
            ),
            (
                # Every definition in this file is replaced, so the only
                # content outside a body is the include block -- which is
                # exactly where a new upstream dependency would appear.
                "upstream edit outside every replaced body",
                "source",
                "#include <Fw/Types/ExternalString.hpp>",
                "#include <Fw/Types/StringBase.hpp>",
                False,
            ),
        ],
    ),
    (
        gen_os_rawtime.SHIM,
        [
            (
                "upstream edit inside the replaced body: the borrow condition",
                "source",
                "if (t1.tv_nsec < t2.tv_nsec) {",
                "if (t1.tv_nsec <= t2.tv_nsec) {",
                False,
            ),
            (
                "upstream edit inside the replaced body: the operand swap",
                "source",
                "if ((t1.tv_sec < t2.tv_sec) or (t1.tv_sec == t2.tv_sec and t1.tv_nsec < t2.tv_nsec)) {",
                "if ((t1.tv_sec < t2.tv_sec) or (t1.tv_sec == t2.tv_sec and t1.tv_nsec > t2.tv_nsec)) {",
                False,
            ),
            (
                # The clock deliberately stays in C++, so an edit to it is
                # exactly the kind of upstream change the shim must notice.
                "upstream edit outside every replaced body: the clock",
                "source",
                "clock_gettime(CLOCK_REALTIME",
                "clock_gettime(CLOCK_MONOTONIC",
                False,
            ),
        ],
    ),
    (
        gen_os_maxheap.SHIM,
        [
            (
                "upstream edit inside a replaced body (push)",
                "source",
                "if (this->isFull()) {",
                "if (this->isFull() || true) {",
                False,
            ),
            (
                "upstream edit inside a REMOVED body: the stability tie-break",
                "source",
                "if (aAge > bAge) {",
                "if (aAge < bAge) {",
                False,
            ),
            (
                "upstream edit outside every replaced or removed body",
                "source",
                "this->m_capacity = 0;",
                "this->m_capacity = 1;",
                False,
            ),
        ],
    ),
    (
        gen_os_filepath.SHIM,
        [
            (
                "upstream edit inside a replaced body (checkContainment)",
                "source",
                "if (allowedLen == 0 || pathLen == 0) {",
                "if (allowedLen == 0 || pathLen == 1) {",
                False,
            ),
            (
                "upstream edit inside a REMOVED body: `..` escapes the root",
                "source",
                "if (writePos > 1) {\n                writePos--;",
                "if (writePos > 0) {\n                writePos--;",
                False,
            ),
            (
                "upstream edit inside a REMOVED body: TOO_LONG boundary moved",
                "source",
                "if (pathLen + 1 > resolvedSize) {",
                "if (pathLen + 2 > resolvedSize) {",
                False,
            ),
            (
                "upstream edit outside every replaced or removed body",
                "source",
                'return resolvePath(path, "/", resolvedOut, resolvedSize);',
                'return resolvePath(path, "//", resolvedOut, resolvedSize);',
                False,
            ),
        ],
    ),
]


def run_case(shim, description, which, find, replace, expected, tmp):
    """Apply one mutation to copies and report whether the check agrees."""
    source = tmp / shim.source.name
    target = tmp / shim.target.name
    shutil.copyfile(shim.source, source)
    shutil.copyfile(shim.target, target)

    mutated = source if which == "source" else target
    text = mutated.read_text()
    if text.count(find) < 1:
        return f"anchor not found in {mutated.name}: {find!r}"
    mutated.write_text(text.replace(find, replace, 1))

    probe = copy.copy(shim)
    probe.source = source
    probe.target = target
    actual = shimgen.is_up_to_date(probe)
    if actual != expected:
        verb = "reported drift" if not actual else "reported up to date"
        want = "up to date" if expected else "drift"
        return f"{verb}, expected {want}"
    return None


def main():
    failures = 0
    checked = 0
    for shim, cases in CASES:
        name = shim.target.relative_to(REPO)
        # The pristine tree must pass, or every other case is meaningless.
        if not shimgen.is_up_to_date(shim):
            print(f"FAIL  {name}: baseline is already stale; regenerate first")
            failures += 1
            continue
        print(f"ok    {name}: baseline up to date")
        checked += 1
        for description, which, find, replace, expected in cases:
            with tempfile.TemporaryDirectory() as raw:
                problem = run_case(
                    shim, description, which, find, replace, expected, pathlib.Path(raw)
                )
            checked += 1
            if problem:
                print(f"FAIL  {name}: {description} -- {problem}")
                failures += 1
            else:
                print(f"ok    {name}: {description}")

    print(f"\n{checked - failures}/{checked} checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
