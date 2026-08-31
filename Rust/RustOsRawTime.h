// ======================================================================
// \title  RustOsRawTime.h
// \brief  C declarations for the Rust-backed Os::RawTime arithmetic
//
// fprime-rs Phase 15, the first slice. Only the arithmetic crosses: the
// difference between two instants, as the (seconds, microseconds) pair
// Fw::TimeInterval::set takes.
//
// The clock does not cross, and that is the design rather than an omission.
// PosixRawTime::now() is a clock_gettime call whose failure is read out of
// errno, and errno is thread-local and overwritten by the next failing call --
// so the syscall and the errno read have to stay in the same language. This
// slice moves the part with no operating system in it.
//
// The timespec fields are passed individually rather than as a mirrored
// struct: a timespec's layout belongs to the platform, and there is nothing
// to gain by restating it when the entire payload is four integers.
// ======================================================================
#ifndef FPRIME_RUST_OS_RAWTIME_H
#define FPRIME_RUST_OS_RAWTIME_H

#include <stdint.h>

#include <Fw/FPrimeBasicTypes.h>

#ifdef __cplusplus
extern "C" {
#endif

//! Interval between two instants, in whichever order they are given.
//!
//! Writes the pair the caller hands to Fw::TimeInterval::set. That call still
//! happens on the C++ side, so its `useconds < 1000000` assert fires in the
//! same place with the same argument as before.
void fprime_rs_rawtime_interval(int64_t aSeconds,
                                int64_t aNanoseconds,
                                int64_t bSeconds,
                                int64_t bNanoseconds,
                                uint32_t* outSeconds,
                                uint32_t* outUseconds);

#ifdef __cplusplus
}
#endif

#endif  // FPRIME_RUST_OS_RAWTIME_H
