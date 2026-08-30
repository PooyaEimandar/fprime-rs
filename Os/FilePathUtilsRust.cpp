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

#include <Fw/Types/Assert.hpp>
#include <Fw/Types/StringUtils.hpp>
#include <Os/FilePathUtils.hpp>
#include <Os/FileSystem.hpp>
#include <cstring>
#include "Rust/RustOsKernel.h"

// clang-format off
// Fingerprints of the upstream bodies this generator replaced or
// removed. The generated file does not contain them, so nothing else
// would notice if upstream changed one -- a mismatch here means the
// C++ that the Rust implementation mirrors has moved and needs
// re-reviewing. The `removed:` entries are the helper functions the
// Rust reimplements, and are the reason this table exists at all.
//
//   Status resolvePath(const char*, const char*, char*, FwSizeType)                        37b926f8d386
//   Status checkContainment(const char*, const char*)                                      1c604262bbf5
//   removed: static bool isValidCString(FwSizeType, FwSizeType)                            a239b9569452
//   removed: static Status buildAbsolutePath(const char*, const char*, char*, FwSizeType)  0b1f03cde25d
//   removed: static Status resolveInPlace(char*)                                           d07761397a8a
// clang-format on

static_assert(static_cast<int32_t>(Os::FilePathUtils::VALID) == 0, "Rust expects VALID == 0");
static_assert(static_cast<int32_t>(Os::FilePathUtils::OUTSIDE_SANDBOX) == 1, "Rust expects OUTSIDE_SANDBOX == 1");
static_assert(static_cast<int32_t>(Os::FilePathUtils::INVALID_PATH) == 2, "Rust expects INVALID_PATH == 2");
static_assert(static_cast<int32_t>(Os::FilePathUtils::TOO_LONG) == 3, "Rust expects TOO_LONG == 3");

namespace Os {
namespace FilePathUtils {

Status resolvePath(const char* path, const char* baseDir, char* resolvedOut, FwSizeType resolvedSize) {
    // Asserted here rather than in Rust so that a null output buffer faults
    // before anything else is examined, as the original did.
    FW_ASSERT(resolvedOut != nullptr);
    return static_cast<Status>(fprime_rs_path_resolve(path, baseDir, resolvedOut, resolvedSize, MAX_PATH_LENGTH));
}

Status resolvePath(const Fw::ConstStringBase& path, const Fw::ConstStringBase& baseDir, Fw::StringBase& resolvedOut) {
    // SAFETY: write directly into resolvedOut's internal buffer to avoid a temporary copy.
    // StringBase::toChar() returns a pointer to the internal buffer, and resolvePath always
    // null-terminates the output, keeping the StringBase in a valid state.
    char* outBuffer = const_cast<char*>(resolvedOut.toChar());
    // Cap at MAX_PATH_LENGTH so resolveInPlace's string_length scan stays consistent
    const FwSizeType capacity = static_cast<FwSizeType>(resolvedOut.getCapacity());
    const FwSizeType resolvedSize = (capacity > MAX_PATH_LENGTH) ? MAX_PATH_LENGTH : capacity;
    return resolvePath(path.toChar(), baseDir.toChar(), outBuffer, resolvedSize);
}

Status resolveFromCwd(const char* path, char* resolvedOut, FwSizeType resolvedSize) {
    FW_ASSERT(path != nullptr);
    FW_ASSERT(resolvedOut != nullptr);

    if (path[0] != '/') {
        char cwdBuffer[MAX_PATH_LENGTH];
        const Os::FileSystem::Status cwdStatus = Os::FileSystem::getWorkingDirectory(cwdBuffer, MAX_PATH_LENGTH);
        if (cwdStatus != Os::FileSystem::Status::OP_OK) {
            return INVALID_PATH;
        }
        return resolvePath(path, cwdBuffer, resolvedOut, resolvedSize);
    }
    return resolvePath(path, "/", resolvedOut, resolvedSize);
}

// Internal containment check on already-resolved paths.
// Verifies that resolvedPath starts with allowedDirectory as a prefix,
// and that the match occurs at a `/` boundary.
Status checkContainment(const char* resolvedPath, const char* allowedDirectory) {
    FW_ASSERT(resolvedPath != nullptr);
    FW_ASSERT(allowedDirectory != nullptr);
    return static_cast<Status>(fprime_rs_path_check_containment(resolvedPath, allowedDirectory, MAX_PATH_LENGTH));
}

}  // namespace FilePathUtils
}  // namespace Os
