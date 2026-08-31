// ======================================================================
// RawTimeRust.cpp
//
// Rust-backed Os::RawTime interval arithmetic (fprime-rs Phase 15). The
// clock, the errno translation, the timespec handle, serialization and the
// delegate lifetime are the original's; what moved is the subtraction --
// the operand ordering, the borrow across the second, and the nanosecond
// truncation.
//
// Fw::TimeInterval::set is still called from here, so its
// `useconds < 1000000` assert fires in the same place with the same argument
// as before. That matters: the pair Rust returns can exceed that bound for an
// unnormalized tv_nsec, which PosixRawTime::deserializeFrom can produce from
// the wire without checking.
//
// Selected instead of RawTime.cpp via FPRIME_RUST_IMPL_OS_RAWTIME; the oracle
// is the existing Os/test/ut/rawtime suite, which compares against a
// std::chrono shadow model.
//
// GENERATED from Os/Posix/RawTime.cpp by tools/fprime-rs/gen_os_rawtime.py --
// do not edit. Change the original (or the generator) and regenerate.
// ======================================================================

#include <Fw/Logger/Logger.hpp>
#include <Fw/Types/Assert.hpp>
#include <Os/Posix/RawTime.hpp>
#include <Os/Posix/error.hpp>
#include <cerrno>
#include "Rust/RustOsRawTime.h"

namespace Os {
namespace Posix {
namespace RawTime {

// clang-format off
// Fingerprints of the upstream bodies this generator replaced or
// removed. The generated file does not contain them, so nothing else
// would notice if upstream changed one -- a mismatch here means the
// C++ that the Rust implementation mirrors has moved and needs
// re-reviewing. The `removed:` entries are the helper functions the
// Rust reimplements, and are the reason this table exists at all.
//
//   getTimeInterval(const Os::RawTime&, Fw::TimeInterval& interval))  dad4c841f05a
// clang-format on

PosixRawTime::Status PosixRawTime::now() {
    int status = clock_gettime(CLOCK_REALTIME, &this->m_handle.m_timespec);
    if (status != 0) {
        return errno_to_rawtime_status(errno);
    }
    return Status::OP_OK;
}

PosixRawTime::Status PosixRawTime::getTimeInterval(const Os::RawTime& other, Fw::TimeInterval& interval) const {
    const timespec t1 = this->m_handle.m_timespec;
    const timespec t2 = static_cast<PosixRawTimeHandle*>(const_cast<Os::RawTime&>(other).getHandle())->m_timespec;

    U32 seconds = 0;
    U32 useconds = 0;
    fprime_rs_rawtime_interval(static_cast<int64_t>(t1.tv_sec), static_cast<int64_t>(t1.tv_nsec),
                               static_cast<int64_t>(t2.tv_sec), static_cast<int64_t>(t2.tv_nsec), &seconds, &useconds);

    // Still set() rather than assigning the members: its `useconds < 1000000`
    // assert is a precondition of Fw::TimeInterval, and this must fault in the
    // same place, with the same argument, as the original did.
    interval.set(seconds, useconds);
    return Status::OP_OK;
}

Fw::SerializeStatus PosixRawTime::serializeTo(Fw::SerialBufferBase& buffer, Fw::Endianness mode) const {
    static_assert(PosixRawTime::SERIALIZED_SIZE >= 2 * sizeof(U32),
                  "PosixRawTime implementation requires at least 2*sizeof(U32) serialization size");
    Fw::SerializeStatus status = buffer.serializeFrom(static_cast<U32>(this->m_handle.m_timespec.tv_sec), mode);
    if (status != Fw::SerializeStatus::FW_SERIALIZE_OK) {
        return status;
    }
    return buffer.serializeFrom(static_cast<U32>(this->m_handle.m_timespec.tv_nsec), mode);
}

Fw::SerializeStatus PosixRawTime::deserializeFrom(Fw::SerialBufferBase& buffer, Fw::Endianness mode) {
    static_assert(PosixRawTime::SERIALIZED_SIZE >= 2 * sizeof(U32),
                  "PosixRawTime implementation requires at least 2*sizeof(U32) serialization size");
    U32 sec = 0;
    U32 nsec = 0;
    Fw::SerializeStatus status = buffer.deserializeTo(sec, mode);
    if (status != Fw::SerializeStatus::FW_SERIALIZE_OK) {
        return status;
    }
    status = buffer.deserializeTo(nsec, mode);
    if (status != Fw::SerializeStatus::FW_SERIALIZE_OK) {
        return status;
    }
    this->m_handle.m_timespec = {static_cast<time_t>(sec), static_cast<long>(nsec)};
    return Fw::SerializeStatus::FW_SERIALIZE_OK;
}

RawTimeHandle* PosixRawTime::getHandle() {
    return &this->m_handle;
}

}  // namespace RawTime
}  // namespace Posix
}  // namespace Os
