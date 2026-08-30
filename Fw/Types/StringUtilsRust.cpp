// ======================================================================
// StringUtilsRust.cpp
//
// Rust-backed Fw::StringUtils (fprime-rs Phase 14). Every string in the
// framework rests on these five functions: Fw::StringBase::length is
// string_length, every Fw::String assignment is string_copy, and
// Os::FilePathUtils searches with substring_find.
//
// The decisions moved; the guards did not. The null asserts, the self-copy
// short-circuit, the non-overlap assert and the representability checks are
// still performed here, in the same order and on the same conditions as the
// original, so a violated precondition faults in the same place with the same
// arguments. Only the character arithmetic crosses the boundary.
//
// Selected instead of StringUtils.cpp via FPRIME_RUST_IMPL_FW_STRINGS; the
// oracle is the existing Fw/Types unit-test suite.
//
// GENERATED from Fw/Types/StringUtils.cpp by tools/fprime-rs/gen_fw_strings.py
// -- do not edit. Change the original (or the generator) and regenerate.
// ======================================================================

#include <Fw/Types/Assert.hpp>
#include <Fw/Types/ExternalString.hpp>
#include <cstring>
#include <limits>
#include "Rust/RustFwStrings.h"
#include "StringUtils.hpp"

// clang-format off
// Fingerprints of the upstream bodies this generator replaced or
// removed. The generated file does not contain them, so nothing else
// would notice if upstream changed one -- a mismatch here means the
// C++ that the Rust implementation mirrors has moved and needs
// re-reviewing. The `removed:` entries are the helper functions the
// Rust reimplements, and are the reason this table exists at all.
//
//   Fw::string_copy(char*, const char*, FwSizeType)                            ab4686bbf790
//   Fw::string_last_n(const char*, const FwSizeType, const FwSizeType)         fd40af3216bf
//   Fw::string_length(const CHAR*, FwSizeType)                                 605115dbc3ef
//   Fw::substring_find(const CHAR*, FwSizeType, const CHAR*, FwSizeType)       2755b4c1a973
//   Fw::substring_find_last(const CHAR*, FwSizeType, const CHAR*, FwSizeType)  7abf3c478c36
// clang-format on

char* Fw::StringUtils::string_copy(char* destination, const char* source, FwSizeType num) {
    // Handle self-copy and 0 bytes copy
    if (destination == source || num == 0) {
        return destination;
    }
    FW_ASSERT(source != nullptr);
    FW_ASSERT(destination != nullptr);

    // Copying an overlapping range is undefined. Both pointers are needed to
    // decide this, so it cannot move: Rust receives two independent slices
    // and has nothing left to compare.
    FwSizeType source_len = string_length(source, num) + 1;
    FW_ASSERT(source + source_len <= destination || destination + num <= source);

    // Rust reports the count written; the original returns its destination.
    (void)fprime_rs_string_copy(destination, source, num);
    return destination;
}

const char* Fw::StringUtils::string_last_n(const char* source, const FwSizeType n, const FwSizeType buffer_size) {
    FW_ASSERT(source != nullptr);
    // An offset, not a pointer: nothing pointer-derived crosses the boundary
    // and has to come back still valid.
    return source + fprime_rs_string_last_n_offset(source, n, buffer_size);
}

FwSizeType Fw::StringUtils::string_length(const CHAR* source, FwSizeType buffer_size) {
    FW_ASSERT(source != nullptr);
    return fprime_rs_string_length(source, buffer_size);
}

FwSignedSizeType Fw::StringUtils::substring_find(const CHAR* source_string,
                                                 FwSizeType source_size,
                                                 const CHAR* sub_string,
                                                 FwSizeType sub_size) {
    FW_ASSERT(source_string != nullptr);
    FW_ASSERT(sub_string != nullptr);

    // Confirm that the output type can hold the range of valid results. The
    // original reaches this check only past its two early returns -- a
    // zero-size substring and a substring longer than the source -- so the
    // same two conditions gate it here, or a source longer than
    // FwSignedSizeType can hold would fault on a call the original answers.
    if ((sub_size != 0) && (source_size >= sub_size)) {
        FW_ASSERT(source_size - sub_size <= static_cast<FwSizeType>(std::numeric_limits<FwSignedSizeType>::max()));
    }
    return fprime_rs_substring_find(source_string, source_size, sub_string, sub_size);
}

FwSignedSizeType Fw::StringUtils::substring_find_last(const CHAR* source_string,
                                                      FwSizeType source_size,
                                                      const CHAR* sub_string,
                                                      FwSizeType sub_size) {
    FW_ASSERT(source_string != nullptr);
    FW_ASSERT(sub_string != nullptr);

    // The two places the original checks representability. The third -- the
    // one after a loop match -- is subsumed: a match index never exceeds
    // source_size - sub_size, which the second branch already bounds.
    if ((source_size > 0) && (0 == sub_size)) {
        // The zero-size substring matches at the last character.
        FW_ASSERT(source_size - 1 <= static_cast<FwSizeType>(std::numeric_limits<FwSignedSizeType>::max()));
    } else if (source_size >= sub_size) {
        FW_ASSERT(source_size - sub_size <= static_cast<FwSizeType>(std::numeric_limits<FwSignedSizeType>::max()));
    }
    return fprime_rs_substring_find_last(source_string, source_size, sub_string, sub_size);
}
