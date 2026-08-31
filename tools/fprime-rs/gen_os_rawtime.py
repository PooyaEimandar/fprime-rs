#!/usr/bin/env python3
"""Generate Os/Posix/RawTimeRust.cpp from Os/Posix/RawTime.cpp.

fprime-rs Phase 15, the first slice: the interval arithmetic behind
Os::RawTime runs in Rust. Everything with an operating system in it stays --
`now()` and its clock_gettime, the errno translation, the timespec handle,
serialization, and the delegate lifetime.

  python3 tools/fprime-rs/gen_os_rawtime.py           # regenerate
  python3 tools/fprime-rs/gen_os_rawtime.py --check   # verify, exit 1 on drift

Only one body moves, and the reason the others do not is worth stating: errno
is thread-local and is overwritten by the next failing call, so a syscall and
the errno read that interprets it cannot be split across a language boundary.
`getTimeInterval` is the one function here that touches no syscall at all.

See tools/fprime-rs/shimgen.py for the generation and drift-check mechanism.
"""

import sys

import shimgen
from shimgen import REPO

SOURCE = REPO / "Os" / "Posix" / "RawTime.cpp"
TARGET = REPO / "Os" / "Posix" / "RawTimeRust.cpp"

BANNER = """\
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

"""

KERNEL_INCLUDE = '#include "Rust/RustOsRawTime.h"\n'

REPLACEMENTS = [
    (
        "PosixRawTime::Status PosixRawTime::getTimeInterval(const Os::RawTime& other, Fw::TimeInterval& interval) const",
        """    const timespec t1 = this->m_handle.m_timespec;
    const timespec t2 = static_cast<PosixRawTimeHandle*>(const_cast<Os::RawTime&>(other).getHandle())->m_timespec;

    U32 seconds = 0;
    U32 useconds = 0;
    fprime_rs_rawtime_interval(static_cast<int64_t>(t1.tv_sec), static_cast<int64_t>(t1.tv_nsec),
                               static_cast<int64_t>(t2.tv_sec), static_cast<int64_t>(t2.tv_nsec), &seconds,
                               &useconds);

    // Still set() rather than assigning the members: its `useconds < 1000000`
    // assert is a precondition of Fw::TimeInterval, and this must fault in the
    // same place, with the same argument, as the original did.
    interval.set(seconds, useconds);
    return Status::OP_OK;
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
            "#include <cerrno>\n",
            "#include <cerrno>\n" + KERNEL_INCLUDE,
            True,
        ),
    ],
    preamble=("PosixRawTime::Status PosixRawTime::now()", ""),
)


if __name__ == "__main__":
    sys.exit(shimgen.main(SHIM, __doc__))
