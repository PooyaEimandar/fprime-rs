#!/usr/bin/env python3
"""Generate Os/FilePathUtilsRust.cpp from Os/FilePathUtils.cpp.

fprime-rs Phase 9: path resolution and sandbox containment run in Rust. This
is the code deciding whether a path the spacecraft was *told* to open stays
inside the directory it is allowed to touch -- `..` collapsing and prefix
containment -- and it is pure character arithmetic with no filesystem access,
so it moves cleanly and the move is worth making.

  python3 tools/fprime-rs/gen_os_filepath.py           # regenerate
  python3 tools/fprime-rs/gen_os_filepath.py --check   # verify, exit 1 on drift

`resolveFromCwd` stays in C++: its only non-trivial part is
`Os::FileSystem::getWorkingDirectory`, a syscall, and it delegates the actual
resolution to the flipped `resolvePath`. The `Fw::ConstStringBase` overload
also stays, because it exists to reach into a `StringBase`'s buffer.

See tools/fprime-rs/shimgen.py for the generation and drift-check mechanism.
"""

import sys

import shimgen
from shimgen import REPO

SOURCE = REPO / "Os" / "FilePathUtils.cpp"
TARGET = REPO / "Os" / "FilePathUtilsRust.cpp"

BANNER = """\
// ======================================================================
// FilePathUtilsRust.cpp
//
// Rust-backed path resolution and sandbox containment (fprime-rs Phase 9).
// The API and its semantics are the original's; the character arithmetic --
// collapsing `.`, `..` and `//`, and testing containment at a directory
// boundary -- runs in Rust (Rust/fprime-utils/src/path.rs).
//
// resolveFromCwd stays here: its substance is a getWorkingDirectory syscall,
// and it delegates resolution to the flipped resolvePath. The
// Fw::ConstStringBase overload stays too, since it exists to write into a
// StringBase's own buffer.
//
// Selected instead of FilePathUtils.cpp via FPRIME_RUST_IMPL_OS_FILEPATH; the
// oracle is the existing Os_FilePathUtils_Test and Os_SandboxedFile_Test
// suites.
//
// GENERATED from Os/FilePathUtils.cpp by tools/fprime-rs/gen_os_filepath.py
// -- do not edit. Change the original (or the generator) and regenerate.
// ======================================================================

"""

KERNEL_INCLUDE = '#include "Rust/RustOsKernel.h"\n'

# Rust hardcodes these discriminants (Rust/fprime-utils/src/path.rs) and the
# shim static_casts the returned int straight back to Status, so the two must
# agree. `Status` is an unscoped enum with implicit values: reordering it
# upstream would silently turn "outside the sandbox" into "valid".
STATUS_GUARD = """\
static_assert(static_cast<int32_t>(Os::FilePathUtils::VALID) == 0, "Rust expects VALID == 0");
static_assert(static_cast<int32_t>(Os::FilePathUtils::OUTSIDE_SANDBOX) == 1, "Rust expects OUTSIDE_SANDBOX == 1");
static_assert(static_cast<int32_t>(Os::FilePathUtils::INVALID_PATH) == 2, "Rust expects INVALID_PATH == 2");
static_assert(static_cast<int32_t>(Os::FilePathUtils::TOO_LONG) == 3, "Rust expects TOO_LONG == 3");

"""

REPLACEMENTS = [
    (
        "Status resolvePath(const char* path, const char* baseDir, char* resolvedOut, FwSizeType resolvedSize)",
        """    // Asserted here rather than in Rust so that a null output buffer faults
    // before anything else is examined, as the original did.
    FW_ASSERT(resolvedOut != nullptr);
    return static_cast<Status>(
        fprime_rs_path_resolve(path, baseDir, resolvedOut, resolvedSize, MAX_PATH_LENGTH));
""",
    ),
    (
        "Status checkContainment(const char* resolvedPath, const char* allowedDirectory)",
        """    FW_ASSERT(resolvedPath != nullptr);
    FW_ASSERT(allowedDirectory != nullptr);
    return static_cast<Status>(
        fprime_rs_path_check_containment(resolvedPath, allowedDirectory, MAX_PATH_LENGTH));
""",
    ),
]

SHIM = shimgen.Shim(
    source=SOURCE,
    target=TARGET,
    banner=BANNER,
    replacements=REPLACEMENTS,
    edits=[
        (
            "#include <cstring>\n",
            "#include <cstring>\n" + KERNEL_INCLUDE,
            True,
        ),
    ],
    # These three file-static helpers existed only to serve resolvePath and
    # checkContainment. With both rerouted they are unreachable, and leaving
    # them would put a second, silently diverging copy of the resolution
    # algorithm in the tree -- the exact hazard this generator exists to stop.
    removals=[
        "static bool isValidCString(FwSizeType measuredLen, FwSizeType bufferLen)",
        "static Status buildAbsolutePath(const char* path, const char* baseDir, char* resolvedOut,"
        " FwSizeType resolvedSize)",
        "static Status resolveInPlace(char* resolvedOut)",
    ],
    preamble=("namespace Os {\n", STATUS_GUARD),
)


if __name__ == "__main__":
    sys.exit(shimgen.main(SHIM, __doc__))
