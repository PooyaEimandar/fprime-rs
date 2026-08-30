// ======================================================================
// \title  RustFwTime.h
// \brief  C declarations for the Rust-backed Fw::Time arithmetic
//
// fprime-rs Phase 14. Every timestamp the framework stamps on an event, a
// telemetry sample or a data product is ordered and offset by this code.
//
// Fw::Time derives Fw::Serializable, so the object has a vtable and no stable
// ABI -- but its four fields are its whole state, and they cross as the
// struct below. fprime_rs_time_layout reports the Rust side's layout so the
// shim can pin it, as the MaxHeap node layout is pinned.
//
// Each call returns whether the C++ precondition held. The original asserts
// on a time-base mismatch, on a subtrahend larger than its minuend, and on a
// microsecond sum beyond one carry; the shim turns a false back into the same
// FW_ASSERT, so the fault surface is unchanged.
// ======================================================================
#ifndef FPRIME_RUST_FW_TIME_H
#define FPRIME_RUST_FW_TIME_H

#include <stdbool.h>
#include <stdint.h>

#include <Fw/FPrimeBasicTypes.h>

#ifdef __cplusplus
extern "C" {
#endif

//! The state of an Fw::Time, flattened for the boundary.
typedef struct {
    FwTimeBaseStoreType timeBase;
    FwTimeContextStoreType context;
    uint32_t seconds;
    uint32_t useconds;
} FprimeRsTime;

//! Fw::TimeComparison integer value for the ordering of two times.
int32_t fprime_rs_time_compare(const FprimeRsTime* a, const FprimeRsTime* b);

//! Sum two times; false if the bases differ or the carry exceeds one second.
bool fprime_rs_time_add(const FprimeRsTime* a, const FprimeRsTime* b, FprimeRsTime* out);

//! Difference of two times; false if the bases differ or the result is negative.
bool fprime_rs_time_sub(const FprimeRsTime* minuend, const FprimeRsTime* subtrahend, FprimeRsTime* out);

//! Normalize a (seconds, microseconds) pair that may carry one second.
bool fprime_rs_time_carry(uint32_t seconds, uint32_t useconds, uint32_t* outSeconds, uint32_t* outUseconds);

//! Split a floating-point second count; false for a negative value or NaN.
bool fprime_rs_time_from_f64(double seconds, uint32_t* outSeconds, uint32_t* outUseconds);

//! Reports the Rust struct layout, so the shim can prove the two agree.
void fprime_rs_time_layout(FwSizeType* size,
                           FwSizeType* align,
                           FwSizeType* baseOffset,
                           FwSizeType* secondsOffset,
                           FwSizeType* usecondsOffset);

#ifdef __cplusplus
}
#endif

#endif  // FPRIME_RUST_FW_TIME_H
