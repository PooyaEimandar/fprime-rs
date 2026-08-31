//! The arithmetic behind `Os::RawTime` (fprime-rs Phase 15).
//!
//! Mirrors the two calculations in `Os/Posix/RawTime.cpp` and
//! `Os/DelegateRawTime.cpp`. Deliberately **not** the clock: `now()` is a
//! `clock_gettime` call whose failure is read out of `errno`, and errno is
//! thread-local and clobbered by the next failing call, so the syscall and the
//! errno read must stay together on the C++ side. What crosses here is four
//! integers and no operating system at all.
//!
//! Both functions reproduce the C++ exactly, including where the C++ is
//! lossy. Where that happens it is named rather than quietly improved, because
//! changing it is a behaviour change to flight software and belongs in its own
//! decision.

/// Nanoseconds in a second.
pub const NSEC_PER_SEC: i64 = 1_000_000_000;
/// Microseconds in a second — the bound `Fw::TimeInterval::set` asserts on.
pub const USEC_PER_SEC: u32 = 1_000_000;

/// One point in time, as a `timespec` holds it.
///
/// Signed, because `time_t` and `tv_nsec` are. A well-formed `timespec` has
/// `0 <= tv_nsec < 1e9`, but nothing in F Prime enforces that on a
/// deserialized one — see [`interval`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Instant {
    /// Whole seconds, as `timespec::tv_sec`.
    pub seconds: i64,
    /// Nanoseconds within the second, as `timespec::tv_nsec`.
    pub nanoseconds: i64,
}

/// `PosixRawTime::getTimeInterval`: the (seconds, microseconds) between two
/// instants, in whichever order they were given.
///
/// Returns the pair the C++ hands to `Fw::TimeInterval::set`, which asserts
/// `useconds < 1_000_000`. This does not pre-empt that assert: the caller
/// still calls `set`, so a violated precondition faults in the same place with
/// the same argument.
///
/// **Two ways this is lossy, both inherited.** The seconds difference is
/// computed in `i64` and narrowed to `u32`, so instants more than ~136 years
/// apart wrap silently — the C++ `static_cast<U32>` does exactly this. And the
/// borrow assumes a *normalized* `tv_nsec`: with `tv_nsec >= 1e9` the returned
/// microseconds can exceed 1e6, and `Fw::TimeInterval::set` then asserts.
/// `PosixRawTime::deserializeFrom` writes `tv_nsec` straight off the wire
/// without checking, so that input is reachable. Pinned by tests below rather
/// than fixed here.
pub fn interval(a: Instant, b: Instant) -> (u32, u32) {
    // Order so `later` is the greater, exactly as the C++ swap does. The
    // comparison is lexicographic on (seconds, nanoseconds), which is why an
    // unnormalized nanoseconds field does not break the ordering itself.
    let (later, earlier) =
        if (a.seconds < b.seconds) || (a.seconds == b.seconds && a.nanoseconds < b.nanoseconds) {
            (b, a)
        } else {
            (a, b)
        };

    let mut seconds = (later.seconds.wrapping_sub(earlier.seconds)) as u32;
    let nanoseconds = if later.nanoseconds < earlier.nanoseconds {
        // Borrow. Reachable only when the seconds differ, since equal seconds
        // put the larger nanoseconds in `later`, so this cannot underflow.
        seconds = seconds.wrapping_sub(1);
        later.nanoseconds + (NSEC_PER_SEC - earlier.nanoseconds)
    } else {
        later.nanoseconds - earlier.nanoseconds
    };
    (seconds, (nanoseconds / 1000) as u32)
}

// `RawTimeInterface::getDiffUsec` is deliberately NOT here. Its body is two
// overflow guards and an addition, and this port's rule is that guards stay in
// C++ -- so moving it would relocate an `a + b` across a boundary and leave
// the only interesting part behind. It also lives in Os/DelegateRawTime.cpp,
// a different module from the Posix implementation, so it would need its own
// switch to buy that nothing.

#[cfg(test)]
mod tests {
    use super::*;

    fn at(seconds: i64, nanoseconds: i64) -> Instant {
        Instant {
            seconds,
            nanoseconds,
        }
    }

    #[test]
    fn the_order_of_the_operands_does_not_matter() {
        let a = at(10, 500_000_000);
        let b = at(4, 250_000_000);
        assert_eq!(interval(a, b), interval(b, a));
        assert_eq!(interval(a, b), (6, 250_000));
    }

    #[test]
    fn a_borrow_crosses_the_second() {
        // 3.100s - 1.400s = 1.700s
        assert_eq!(
            interval(at(3, 100_000_000), at(1, 400_000_000)),
            (1, 700_000)
        );
        // Exactly on the boundary, no borrow.
        assert_eq!(interval(at(3, 0), at(1, 0)), (2, 0));
    }

    #[test]
    fn equal_instants_are_a_zero_interval() {
        assert_eq!(interval(at(7, 7), at(7, 7)), (0, 0));
    }

    #[test]
    fn nanoseconds_truncate_toward_zero_rather_than_rounding() {
        // 1999 ns is 1 microsecond, not 2. The C++ divides.
        assert_eq!(interval(at(0, 1_999), at(0, 0)), (0, 1));
        assert_eq!(interval(at(0, 999), at(0, 0)), (0, 0));
    }

    #[test]
    fn an_unnormalized_nanoseconds_field_escapes_the_microsecond_bound() {
        // Not a bug in this function: it reproduces the C++. The point is that
        // the result violates Fw::TimeInterval::set's `useconds < 1e6`
        // precondition, and PosixRawTime::deserializeFrom will happily build
        // such a timespec from the wire. Pinned so that if anyone normalizes
        // on deserialize, this test says what it changed.
        let (seconds, useconds) = interval(at(5, 2 * NSEC_PER_SEC), at(5, 0));
        assert_eq!(seconds, 0);
        assert!(
            useconds >= USEC_PER_SEC,
            "expected the bound to be exceeded, got {useconds}"
        );
    }
}
