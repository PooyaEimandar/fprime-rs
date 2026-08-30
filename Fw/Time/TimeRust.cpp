// ======================================================================
// TimeRust.cpp
//
// Rust-backed Fw::Time arithmetic (fprime-rs Phase 14). The class, its
// storage, its serialization and its accessors are the original's; what moved
// is the arithmetic -- ordering, the carry on addition, the borrow on
// subtraction, and the rounding that splits an F64 into seconds and
// microseconds.
//
// Fw::Time cannot cross extern "C" (it derives Fw::Serializable and so has a
// vtable), so each body flattens the object into the FprimeRsTime triple,
// calls across, and reads the result back. A false return means a precondition
// the original asserted on was violated, and the FW_ASSERT is reproduced here.
//
// Selected instead of Time.cpp via FPRIME_RUST_IMPL_FW_TIME; the oracle is the
// existing Fw/Time unit-test suite.
//
// GENERATED from Fw/Time/Time.cpp by tools/fprime-rs/gen_fw_time.py -- do not
// edit. Change the original (or the generator) and regenerate.
// ======================================================================

#include <Fw/FPrimeBasicTypes.hpp>
#include <Fw/Time/Time.hpp>
#include "Rust/RustFwTime.h"

// clang-format off
// Fingerprints of the upstream bodies this generator replaced or
// removed. The generated file does not contain them, so nothing else
// would notice if upstream changed one -- a mismatch here means the
// C++ that the Rust implementation mirrors has moved and needs
// re-reviewing. The `removed:` entries are the helper functions the
// Rust reimplements, and are the reason this table exists at all.
//
//   TimeComparison Time ::compare(const Time&, const Time&)  aaea33ef7871
//   Time Time ::add(const Time&, const Time&)                e7d3b361550d
//   Time Time ::sub(const Time&, )                           e1f7e1e7d522
//   add(U32, U32)                                            481baac452f1
//   Fw::parseSeconds(F64)                                    9823384352d8
//   Fw::parseUSeconds(F64)                                   e5131fcae9dd
// clang-format on

// LT is -1, not 0: the model writes these as a C-style comparison result.
// Assuming otherwise inverts every comparison, so all four are pinned.
static_assert(static_cast<int32_t>(Fw::TimeComparison::LT) == -1, "Rust expects TimeComparison::LT == -1");
static_assert(static_cast<int32_t>(Fw::TimeComparison::EQ) == 0, "Rust expects TimeComparison::EQ == 0");
static_assert(static_cast<int32_t>(Fw::TimeComparison::GT) == 1, "Rust expects TimeComparison::GT == 1");
static_assert(static_cast<int32_t>(Fw::TimeComparison::INCOMPARABLE) == 2,
              "Rust expects TimeComparison::INCOMPARABLE == 2");

//! Flatten an Fw::Time for the boundary.
static FprimeRsTime fprimeRsFlatten(const Fw::Time& time) {
    FprimeRsTime out;
    out.timeBase = static_cast<FwTimeBaseStoreType>(time.getTimeBase().e);
    out.context = time.getContext();
    out.seconds = time.getSeconds();
    out.useconds = time.getUSeconds();
    return out;
}

namespace Fw {
const Time ZERO_TIME = Time();

Time::Time() : m_val() {
    m_val.set_timeBase(TimeBase::TB_NONE);
    m_val.set_timeContext(0);
    m_val.set_seconds(0);
    m_val.set_useconds(0);
}

Time::~Time() {}

Time::Time(U32 seconds, U32 useconds) {
    this->set(TimeBase::TB_NONE, 0, seconds, useconds);
}

Time::Time(TimeBase timeBase, U32 seconds, U32 useconds) {
    this->set(timeBase, 0, seconds, useconds);
}

void Time::set(U32 seconds, U32 useconds) {
    this->set(this->m_val.get_timeBase(), this->m_val.get_timeContext(), seconds, useconds);
}

void Time::set(TimeBase timeBase, U32 seconds, U32 useconds) {
    this->set(timeBase, this->m_val.get_timeContext(), seconds, useconds);
}

Time::Time(TimeBase timeBase, FwTimeContextStoreType context, U32 seconds, U32 useconds) {
    this->set(timeBase, context, seconds, useconds);
}

void Time::set(TimeBase timeBase, FwTimeContextStoreType context, U32 seconds, U32 useconds) {
    // Assert microseconds portion is less than 10^6
    FW_ASSERT(useconds < 1000000, static_cast<FwAssertArgType>(useconds));
    this->m_val.set(timeBase, context, seconds, useconds);
}

Fw::Time::Time(F64 seconds) {
    this->set(seconds);
}

void Fw::Time::set(F64 seconds) {
    U32 parsedSeconds = this->parseSeconds(seconds);
    U32 parsedUseconds = this->parseUSeconds(seconds);
    // parseUSeconds rounds up to a whole second at 999999.5 or above; carry it as add() does
    if (parsedUseconds >= 1000000) {
        parsedSeconds++;
        parsedUseconds -= 1000000;
    }
    this->set(parsedSeconds, parsedUseconds);
}

Time& Time::operator=(const Time& other) {
    if (this != &other) {
        this->m_val = other.m_val;
    }
    return *this;
}

Time& Fw::Time::operator=(F64 seconds) {
    this->set(seconds);
    return *this;
}

Time& Fw::Time::operator+=(F64 seconds) {
    this->add(seconds);
    return *this;
}

bool Time::operator==(const Time& other) const {
    return (Time::compare(*this, other) == TimeComparison::EQ);
}

bool Time::operator!=(const Time& other) const {
    return (Time::compare(*this, other) != TimeComparison::EQ);
}

bool Time::operator>(const Time& other) const {
    return (Time::compare(*this, other) == TimeComparison::GT);
}

bool Time::operator<(const Time& other) const {
    return (Time::compare(*this, other) == TimeComparison::LT);
}

bool Time::operator>=(const Time& other) const {
    TimeComparison c = Time::compare(*this, other);
    return ((TimeComparison::GT == c) or (TimeComparison::EQ == c));
}

bool Time::operator<=(const Time& other) const {
    TimeComparison c = Time::compare(*this, other);
    return ((TimeComparison::LT == c) or (TimeComparison::EQ == c));
}

Fw::Time::operator F64() const {
    const U32 seconds = this->m_val.get_seconds();
    const U32 useconds = this->m_val.get_useconds();
    return static_cast<F64>(seconds) + (static_cast<F64>(useconds) / 1000000.0);
}

TimeValue Time::asTimeValue() const {
    return this->m_val;
}

SerializeStatus Time::serializeTo(SerialBufferBase& buffer, Fw::Endianness mode) const {
    return this->m_val.serializeTo(buffer, mode);
}

SerializeStatus Time::deserializeFrom(SerialBufferBase& buffer, Fw::Endianness mode) {
    TimeValue value;
    const SerializeStatus status = value.deserializeFrom(buffer, mode);
    if (status != FW_SERIALIZE_OK) {
        return status;
    }
    // Reject out-of-range microseconds rather than admitting a value that asserts on later use
    if (value.get_useconds() >= 1000000) {
        return FW_DESERIALIZE_FORMAT_ERROR;
    }
    this->m_val = value;
    return FW_SERIALIZE_OK;
}

U32 Time::getSeconds() const {
    return this->m_val.get_seconds();
}

U32 Time::getUSeconds() const {
    return this->m_val.get_useconds();
}

TimeBase Time::getTimeBase() const {
    return this->m_val.get_timeBase();
}

FwTimeContextStoreType Time::getContext() const {
    return this->m_val.get_timeContext();
}

Time Time ::zero(TimeBase timeBase) {
    Time time(timeBase, 0, 0, 0);
    return time;
}

TimeComparison Time ::compare(const Time& time1, const Time& time2) {
    const FprimeRsTime a = fprimeRsFlatten(time1);
    const FprimeRsTime b = fprimeRsFlatten(time2);
    return static_cast<TimeComparison::t>(fprime_rs_time_compare(&a, &b));
}

Time Time ::add(const Time& a, const Time& b) {
    const FprimeRsTime lhs = fprimeRsFlatten(a);
    const FprimeRsTime rhs = fprimeRsFlatten(b);
    FprimeRsTime sum;
    // False means a precondition the original asserted on was violated: the
    // time bases differ, or the microseconds carried more than one second.
    FW_ASSERT(fprime_rs_time_add(&lhs, &rhs, &sum), static_cast<FwAssertArgType>(a.getTimeBase()),
              static_cast<FwAssertArgType>(b.getTimeBase()));
    return Time(static_cast<TimeBase::t>(sum.timeBase), sum.context, sum.seconds, sum.useconds);
}

Time Time ::sub(const Time& minuend,    //!< Time minuend
                const Time& subtrahend  //!< Time subtrahend
) {
    const FprimeRsTime lhs = fprimeRsFlatten(minuend);
    const FprimeRsTime rhs = fprimeRsFlatten(subtrahend);
    FprimeRsTime difference;
    // False means the bases differ or the subtrahend was the larger, both of
    // which the original asserted on.
    FW_ASSERT(fprime_rs_time_sub(&lhs, &rhs, &difference), static_cast<FwAssertArgType>(minuend.getTimeBase()),
              static_cast<FwAssertArgType>(subtrahend.getTimeBase()));
    return Time(static_cast<TimeBase::t>(difference.timeBase), difference.context, difference.seconds,
                difference.useconds);
}

U32 Fw::Time::parseSeconds(F64 seconds) {
    U32 parsedSeconds = 0;
    U32 parsedUSeconds = 0;
    FW_ASSERT(fprime_rs_time_from_f64(seconds, &parsedSeconds, &parsedUSeconds));
    (void)parsedUSeconds;
    return parsedSeconds;
}

U32 Fw::Time::parseUSeconds(F64 seconds) {
    U32 parsedSeconds = 0;
    U32 parsedUSeconds = 0;
    FW_ASSERT(fprime_rs_time_from_f64(seconds, &parsedSeconds, &parsedUSeconds));
    (void)parsedSeconds;
    return parsedUSeconds;
}

void Time::add(U32 seconds, U32 useconds) {
    U32 newSeconds = 0;
    U32 newUSeconds = 0;
    FW_ASSERT(fprime_rs_time_carry(this->m_val.get_seconds() + seconds, this->m_val.get_useconds() + useconds,
                                   &newSeconds, &newUSeconds),
              static_cast<FwAssertArgType>(this->m_val.get_useconds() + useconds));
    this->set(newSeconds, newUSeconds);
}

void Time::add(F64 seconds) {
    U32 parsedSeconds = this->parseSeconds(seconds);
    U32 parsedUseconds = this->parseUSeconds(seconds);
    // Add parsed seconds and useconds
    this->add(parsedSeconds, parsedUseconds);
}

void Time::setTimeBase(TimeBase timeBase) {
    this->m_val.set_timeBase(timeBase);
}

void Time::setTimeContext(FwTimeContextStoreType context) {
    this->m_val.set_timeContext(context);
}

#ifdef BUILD_UT
std::ostream& operator<<(std::ostream& os, const Time& val) {
    os << "(" << val.getTimeBase() << "," << val.getUSeconds() << "," << val.getSeconds() << ")";
    return os;
}
#endif

}  // namespace Fw
