// ======================================================================
// \title  RustFwStrings.h
// \brief  C declarations for the Rust-backed Fw::StringUtils
//
// fprime-rs Phase 14. These five functions are what every string in the
// framework rests on: Fw::StringBase::length is string_length, every
// Fw::String assignment is string_copy, and Os::FilePathUtils searches with
// substring_find. All of it is character arithmetic against an explicit
// bound -- no allocation, no I/O -- which is why it moves, and why an
// off-by-one here would be expensive.
//
// The pointer-level preconditions stay in C++: the null asserts, the
// self-copy short-circuit, the non-overlap assert, and the checks that a
// match index is representable as FwSignedSizeType. Those are facts about
// pointers, and Rust's two independent slices cannot express the relationship
// between them once the pointers have been split apart. What crosses the
// boundary is the decision, not the guard.
//
// string_last_n returns an offset rather than the original's pointer, so
// nothing pointer-derived makes the round trip and has to come back valid;
// the shim adds it to the pointer it already holds.
// ======================================================================
#ifndef FPRIME_RUST_FW_STRINGS_H
#define FPRIME_RUST_FW_STRINGS_H

#include <stdint.h>

#include <Fw/FPrimeBasicTypes.h>

#ifdef __cplusplus
extern "C" {
#endif

//! Length up to the first NUL, or bufferSize if there is none within it.
FwSizeType fprime_rs_string_length(const char* source, FwSizeType bufferSize);

//! Offset of the last n characters, or 0 for a string shorter than n.
FwSizeType fprime_rs_string_last_n_offset(const char* source, FwSizeType n, FwSizeType bufferSize);

//! Copy at most num bytes, always terminating; returns the count written
//! including the terminator, which the shim discards in favour of the
//! original's return of `destination`.
FwSizeType fprime_rs_string_copy(char* destination, const char* source, FwSizeType num);

//! Index of the first match, or -1. A zero-size substring matches at 0.
FwSignedSizeType fprime_rs_substring_find(const char* source,
                                          FwSizeType sourceSize,
                                          const char* sub,
                                          FwSizeType subSize);

//! Index of the last match, or -1. A zero-size substring matches at
//! sourceSize - 1 -- and, in an empty source, does not match at all.
FwSignedSizeType fprime_rs_substring_find_last(const char* source,
                                               FwSizeType sourceSize,
                                               const char* sub,
                                               FwSizeType subSize);

#ifdef __cplusplus
}
#endif

#endif  // FPRIME_RUST_FW_STRINGS_H
