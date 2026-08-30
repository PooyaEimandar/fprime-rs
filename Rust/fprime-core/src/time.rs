//! The arithmetic behind `Fw::Time` (fprime-rs Phase 14).
//!
//! Mirrors `Fw/Time/Time.cpp`. Every timestamp the framework produces —
//! every event, every telemetry sample, every data product — is compared and
//! offset by this code, and all of it is carry/borrow arithmetic on a
//! (seconds, microseconds) pair with a base tag. That makes it the kind of
//! thing worth moving: no I/O, no allocation, and a handful of edge cases
//! (the carry at exactly 1,000,000 µs, the borrow across a second boundary,
//! and F64 rounding that can carry a whole second) that are easy to get
//! subtly wrong and hard to notice.
//!
//! The C++ keeps the object and its storage; this owns the decisions.

use fprime_config::{FwTimeBaseStoreType, FwTimeContextStoreType};

/// Microseconds in a second. The C++ writes `1000000` inline; naming it makes
/// the carry and borrow below read as what they are.
pub const USEC_PER_SEC: u32 = 1_000_000;

/// Mirrors `Fw::TimeComparison`; the discriminants are the C++ enum's, which
/// the shim pins with `static_assert`s.
///
/// Note the numbering: `Fw/Time/Time.fpp` gives `LT = -1, EQ = 0, GT = 1`,
/// which reads as a C-style comparison result rather than a 0-based enum.
/// Assuming the obvious 0/1/2/3 here would invert every time comparison in the
/// framework, silently — which is why the shim asserts on all four.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum Comparison {
    /// `time1 < time2`
    Lt = -1,
    /// `time1 == time2`
    Eq = 0,
    /// `time1 > time2`
    Gt = 1,
    /// Different time bases: no ordering exists between them
    Incomparable = 2,
}

impl Comparison {
    /// The integer the C++ compares against `Fw::TimeComparison`.
    pub const fn code(self) -> i32 {
        self as i32
    }
}

/// A time as the C++ object holds it, flattened for the boundary.
///
/// `repr(C)` so it crosses directly: `Fw::Time` itself derives
/// `Fw::Serializable` and has a vtable, so the object cannot cross, but these
/// four fields are its whole state.
#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Time {
    /// Which clock this is measured against (`Fw::TimeBase`'s repr)
    pub time_base: FwTimeBaseStoreType,
    /// Caller-defined context within that base
    pub context: FwTimeContextStoreType,
    /// Whole seconds
    pub seconds: u32,
    /// Microseconds within the second, always below `USEC_PER_SEC`
    pub useconds: u32,
}

/// Order two times, or report that they cannot be ordered.
///
/// The time *context* is deliberately not compared — two times from the same
/// base but different contexts are still ordered — which mirrors the C++ and
/// is the kind of asymmetry worth stating out loud.
pub fn compare(a: &Time, b: &Time) -> Comparison {
    if a.time_base != b.time_base {
        return Comparison::Incomparable;
    }
    match (a.seconds.cmp(&b.seconds), a.useconds.cmp(&b.useconds)) {
        (core::cmp::Ordering::Less, _) => Comparison::Lt,
        (core::cmp::Ordering::Greater, _) => Comparison::Gt,
        (_, core::cmp::Ordering::Less) => Comparison::Lt,
        (_, core::cmp::Ordering::Greater) => Comparison::Gt,
        _ => Comparison::Eq,
    }
}

/// Normalize a (seconds, microseconds) pair that may carry one second.
///
/// The C++ asserts `useconds < 1999999` before carrying, which is what makes
/// a single carry sufficient: the inputs are each already normalized, so
/// their sum cannot reach two whole seconds. Returning `None` for anything
/// larger keeps that precondition explicit rather than assumed.
pub fn carry(seconds: u32, useconds: u32) -> Option<(u32, u32)> {
    if useconds >= 2 * USEC_PER_SEC - 1 {
        return None;
    }
    if useconds >= USEC_PER_SEC {
        Some((seconds.wrapping_add(1), useconds - USEC_PER_SEC))
    } else {
        Some((seconds, useconds))
    }
}

/// `Time::add(a, b)`: sum two times sharing a base.
///
/// `None` when the bases differ or the carry precondition is violated; the
/// C++ asserts in both cases and the shim reproduces that.
pub fn add(a: &Time, b: &Time) -> Option<Time> {
    if a.time_base != b.time_base {
        return None;
    }
    let (seconds, useconds) = carry(
        a.seconds.wrapping_add(b.seconds),
        a.useconds.wrapping_add(b.useconds),
    )?;
    Some(Time {
        time_base: a.time_base,
        // A context mismatch yields 0, the same as `sub`: the result belongs
        // to neither context.
        context: if a.context == b.context { a.context } else { 0 },
        seconds,
        useconds,
    })
}

/// `Time::sub(minuend, subtrahend)`, borrowing across the second boundary.
///
/// `None` when the bases differ or the subtrahend is the larger — the C++
/// asserts `minuend >= subtrahend`, because the result is unsigned and an
/// underflow here would silently produce a time far in the future.
pub fn sub(minuend: &Time, subtrahend: &Time) -> Option<Time> {
    if minuend.time_base != subtrahend.time_base {
        return None;
    }
    if compare(minuend, subtrahend) == Comparison::Lt {
        return None;
    }
    let mut seconds = minuend.seconds - subtrahend.seconds;
    let useconds = if subtrahend.useconds > minuend.useconds {
        seconds -= 1;
        minuend.useconds + USEC_PER_SEC - subtrahend.useconds
    } else {
        minuend.useconds - subtrahend.useconds
    };
    Some(Time {
        time_base: minuend.time_base,
        // A context mismatch yields 0: the result belongs to neither.
        context: if minuend.context == subtrahend.context {
            minuend.context
        } else {
            0
        },
        seconds,
        useconds,
    })
}

/// Split a floating-point number of seconds into whole and microseconds.
///
/// **Does not carry.** The rounding below can produce exactly 1,000,000
/// microseconds, and that value is returned as-is, because
/// `Fw::Time::parseUSeconds` is a public static whose documented result is
/// the raw rounded count — `Fw/Time/test/ut` asserts `parseUSeconds(0.999…) ==
/// 1000000` directly. Carrying is the *caller's* job: `set(F64)` and
/// `add(F64)` each run the pair through [`carry`] afterwards, which is exactly
/// what the C++ does.
///
/// `None` for a negative input or NaN, which the C++ asserts on.
pub fn from_f64(seconds: f64) -> Option<(u32, u32)> {
    // NaN is rejected as well as negatives: the C++ asserts `seconds >= 0.0`,
    // which NaN also fails, and a NaN cast to u32 is otherwise a silent zero.
    if seconds.is_nan() || seconds < 0.0 {
        return None;
    }
    let whole = seconds as u32;
    let fractional = seconds - f64::from(whole);
    let micros = (fractional * 1_000_000.0 + 0.5) as u32;
    Some((whole, micros))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn t(base: FwTimeBaseStoreType, seconds: u32, useconds: u32) -> Time {
        Time {
            time_base: base,
            context: 0,
            seconds,
            useconds,
        }
    }

    #[test]
    fn orders_by_seconds_then_microseconds() {
        assert_eq!(compare(&t(0, 1, 0), &t(0, 2, 0)), Comparison::Lt);
        assert_eq!(compare(&t(0, 2, 0), &t(0, 1, 0)), Comparison::Gt);
        assert_eq!(compare(&t(0, 1, 5), &t(0, 1, 6)), Comparison::Lt);
        assert_eq!(compare(&t(0, 1, 6), &t(0, 1, 5)), Comparison::Gt);
        assert_eq!(compare(&t(0, 1, 5), &t(0, 1, 5)), Comparison::Eq);
    }

    #[test]
    fn different_bases_are_incomparable_not_merely_unequal() {
        assert_eq!(compare(&t(0, 1, 0), &t(1, 1, 0)), Comparison::Incomparable);
        // Even when every other field matches.
        assert_eq!(compare(&t(2, 7, 7), &t(3, 7, 7)), Comparison::Incomparable);
    }

    #[test]
    fn context_does_not_affect_ordering() {
        let mut a = t(0, 5, 5);
        let mut b = t(0, 5, 5);
        a.context = 1;
        b.context = 9;
        assert_eq!(compare(&a, &b), Comparison::Eq);
    }

    #[test]
    fn addition_carries_at_exactly_one_second() {
        let sum = add(&t(0, 1, 600_000), &t(0, 2, 400_000)).unwrap();
        assert_eq!((sum.seconds, sum.useconds), (4, 0));
        let no_carry = add(&t(0, 1, 400_000), &t(0, 2, 400_000)).unwrap();
        assert_eq!((no_carry.seconds, no_carry.useconds), (3, 800_000));
    }

    #[test]
    fn addition_zeroes_a_mismatched_context() {
        // Same rule as `sub`. A comment here once claimed `add` kept the left
        // operand's context; Fw/Time's own suite disagreed.
        let mut a = t(0, 1, 0);
        let mut b = t(0, 2, 0);
        a.context = 1;
        b.context = 1;
        assert_eq!(add(&a, &b).unwrap().context, 1);
        b.context = 2;
        assert_eq!(add(&a, &b).unwrap().context, 0);
    }

    #[test]
    fn addition_across_bases_is_refused() {
        assert!(add(&t(0, 1, 0), &t(1, 1, 0)).is_none());
    }

    #[test]
    fn subtraction_borrows_across_the_second() {
        let d = sub(&t(0, 3, 100_000), &t(0, 1, 400_000)).unwrap();
        assert_eq!((d.seconds, d.useconds), (1, 700_000));
        let exact = sub(&t(0, 3, 0), &t(0, 1, 0)).unwrap();
        assert_eq!((exact.seconds, exact.useconds), (2, 0));
    }

    #[test]
    fn subtraction_below_zero_is_refused_rather_than_wrapping() {
        // Unsigned: an underflow here would read as a time far in the future.
        assert!(sub(&t(0, 1, 0), &t(0, 2, 0)).is_none());
        assert!(sub(&t(0, 1, 0), &t(0, 1, 1)).is_none());
        assert!(sub(&t(0, 1, 0), &t(0, 1, 0)).is_some(), "equal is allowed");
    }

    #[test]
    fn subtraction_zeroes_a_mismatched_context() {
        let mut a = t(0, 5, 0);
        let mut b = t(0, 1, 0);
        a.context = 3;
        b.context = 3;
        assert_eq!(sub(&a, &b).unwrap().context, 3);
        b.context = 4;
        assert_eq!(sub(&a, &b).unwrap().context, 0);
    }

    #[test]
    fn f64_rounds_to_the_nearest_microsecond() {
        assert_eq!(from_f64(1.5), Some((1, 500_000)));
        assert_eq!(from_f64(0.0), Some((0, 0)));
        assert_eq!(from_f64(2.000_001), Some((2, 1)));
    }

    #[test]
    fn f64_rounding_reports_a_full_second_without_carrying_it() {
        // 0.999999999 rounds up to exactly 1,000,000 µs. That is returned
        // as-is: `Fw::Time::parseUSeconds` is a public static and its suite
        // asserts this value, so carrying here would change its contract.
        assert_eq!(from_f64(0.999_999_999_999_99), Some((0, 1_000_000)));
        // The caller carries, and that composition is what yields a valid time.
        let (seconds, useconds) = from_f64(0.999_999_999_999_99).unwrap();
        assert_eq!(carry(seconds, useconds), Some((1, 0)));
    }

    #[test]
    fn negative_seconds_are_refused() {
        assert!(from_f64(-0.5).is_none());
        assert!(from_f64(f64::NAN).is_none(), "NaN is not >= 0");
    }

    #[test]
    fn carry_refuses_more_than_one_second_of_microseconds() {
        assert_eq!(carry(1, 999_999), Some((1, 999_999)));
        assert_eq!(carry(1, 1_000_000), Some((2, 0)));
        assert_eq!(carry(1, 1_999_998), Some((2, 999_998)));
        assert!(carry(1, 1_999_999).is_none(), "the C++ asserts here");
    }
}
