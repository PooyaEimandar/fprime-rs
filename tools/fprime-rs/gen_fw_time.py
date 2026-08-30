#!/usr/bin/env python3
"""Generate Fw/Time/TimeRust.cpp from Fw/Time/Time.cpp.

fprime-rs Phase 14: the arithmetic behind `Fw::Time` runs in Rust. Comparison,
addition, subtraction and the floating-point split move to
`Rust/fprime-core/src/time.rs`; the class, its storage and its serialization
stay exactly where they were.

  python3 tools/fprime-rs/gen_fw_time.py           # regenerate
  python3 tools/fprime-rs/gen_fw_time.py --check   # verify, exit 1 on drift

Each replaced body turns a Rust `false` back into the FW_ASSERT the original
performed, so a violated precondition still faults in the same place with the
same arguments rather than returning a quietly wrong time.

See tools/fprime-rs/shimgen.py for the generation and drift-check mechanism.
"""

import sys

import shimgen
from shimgen import REPO

SOURCE = REPO / "Fw" / "Time" / "Time.cpp"
TARGET = REPO / "Fw" / "Time" / "TimeRust.cpp"

BANNER = """\
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

"""

KERNEL_INCLUDE = '#include "Rust/RustFwTime.h"\n'

# The Rust `Comparison` discriminants are hardcoded, and the shim casts the
# returned int straight back, so a reordering of the C++ enum would silently
# invert an ordering. Pin them, as Phase 8 pins Endianness.
GUARD = """\
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

"""

REPLACEMENTS = [
    (
        "TimeComparison Time ::compare(const Time& time1, const Time& time2)",
        """    const FprimeRsTime a = fprimeRsFlatten(time1);
    const FprimeRsTime b = fprimeRsFlatten(time2);
    return static_cast<TimeComparison::t>(fprime_rs_time_compare(&a, &b));
""",
    ),
    (
        "Time Time ::add(const Time& a, const Time& b)",
        """    const FprimeRsTime lhs = fprimeRsFlatten(a);
    const FprimeRsTime rhs = fprimeRsFlatten(b);
    FprimeRsTime sum;
    // False means a precondition the original asserted on was violated: the
    // time bases differ, or the microseconds carried more than one second.
    FW_ASSERT(fprime_rs_time_add(&lhs, &rhs, &sum), static_cast<FwAssertArgType>(a.getTimeBase()),
              static_cast<FwAssertArgType>(b.getTimeBase()));
    return Time(static_cast<TimeBase::t>(sum.timeBase), sum.context, sum.seconds, sum.useconds);
""",
    ),
    (
        # Matched on a prefix: the full parameter list carries `//!<` comments,
        # which are not part of the signature the matcher collapses.
        "Time Time ::sub(const Time& minuend,",
        """    const FprimeRsTime lhs = fprimeRsFlatten(minuend);
    const FprimeRsTime rhs = fprimeRsFlatten(subtrahend);
    FprimeRsTime difference;
    // False means the bases differ or the subtrahend was the larger, both of
    // which the original asserted on.
    FW_ASSERT(fprime_rs_time_sub(&lhs, &rhs, &difference),
              static_cast<FwAssertArgType>(minuend.getTimeBase()),
              static_cast<FwAssertArgType>(subtrahend.getTimeBase()));
    return Time(static_cast<TimeBase::t>(difference.timeBase), difference.context, difference.seconds,
                difference.useconds);
""",
    ),
    (
        "void Time::add(U32 seconds, U32 useconds)",
        """    U32 newSeconds = 0;
    U32 newUSeconds = 0;
    FW_ASSERT(fprime_rs_time_carry(this->m_val.get_seconds() + seconds, this->m_val.get_useconds() + useconds,
                                   &newSeconds, &newUSeconds),
              static_cast<FwAssertArgType>(this->m_val.get_useconds() + useconds));
    this->set(newSeconds, newUSeconds);
""",
    ),
    (
        "U32 Fw::Time::parseSeconds(F64 seconds)",
        """    U32 parsedSeconds = 0;
    U32 parsedUSeconds = 0;
    FW_ASSERT(fprime_rs_time_from_f64(seconds, &parsedSeconds, &parsedUSeconds));
    (void)parsedUSeconds;
    return parsedSeconds;
""",
    ),
    (
        "U32 Fw::Time::parseUSeconds(F64 seconds)",
        """    U32 parsedSeconds = 0;
    U32 parsedUSeconds = 0;
    FW_ASSERT(fprime_rs_time_from_f64(seconds, &parsedSeconds, &parsedUSeconds));
    (void)parsedSeconds;
    return parsedUSeconds;
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
            "#include <Fw/Time/Time.hpp>\n",
            "#include <Fw/Time/Time.hpp>\n" + KERNEL_INCLUDE,
            True,
        ),
    ],
    preamble=("namespace Fw {\n", GUARD),
)


if __name__ == "__main__":
    sys.exit(shimgen.main(SHIM, __doc__))
