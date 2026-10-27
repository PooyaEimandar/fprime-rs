//! The bounded string operations behind `Fw::StringUtils` (fprime-rs Phase 14).
//!
//! Mirrors `Fw/Types/StringUtils.cpp`. Nothing in flight software may scan to
//! an unbounded NUL, so every one of these carries an explicit bound — which
//! makes them exactly the class of code where an off-by-one is both easy and
//! expensive.
//!
//! The one operation that is *not* here is the bounded NUL scan itself. It
//! lives in `fprime-ffi` as `c_str_len`, because it is the step that turns a
//! raw pointer into a slice and so cannot be written in terms of one: the
//! caller's buffer may legitimately be shorter than the bound it passes, and
//! forming a slice that long to look for the terminator would be undefined
//! for a call the C++ answers fine. Everything below therefore receives
//! content that has already been measured.
//!
//! Two behaviours here look like mistakes and are not, so they are pinned by
//! tests rather than left to be "cleaned up" later:
//!
//! - an empty needle matches at `0` in a forward search but at
//!   `source_len - 1` in a reverse one — and in an empty haystack the reverse
//!   search does not match at all;
//! - `string_copy` always terminates at `num - 1`, so a truncated copy is
//!   still a valid C string.

/// Offset of the last `n` characters, or 0 when the string is shorter.
///
/// The C++ returns a pointer into the source; an offset is the same fact
/// without a pointer crossing the boundary.
pub fn last_n_offset(length: usize, n: usize) -> usize {
    length.saturating_sub(n)
}

/// First index at which `needle` occurs in `haystack`.
///
/// An empty needle matches at 0, including in an empty haystack.
pub fn find(haystack: &[u8], needle: &[u8]) -> Option<usize> {
    if needle.is_empty() {
        return Some(0);
    }
    if haystack.len() < needle.len() {
        return None;
    }
    (0..=haystack.len() - needle.len()).find(|&at| &haystack[at..at + needle.len()] == needle)
}

/// Last index at which `needle` occurs in `haystack`.
///
/// An empty needle matches at `haystack.len() - 1`, not at 0 and not at
/// `len()` — the C++ reports the last valid character position.
///
/// In an *empty* haystack an empty needle does not match at all, which is the
/// one place the two searches disagree about more than the index. The C++
/// guards its zero-size special case with `source_size > 0`, so an empty
/// haystack falls through to the loop — and that loop's body is
/// `for (sub_index = 0; sub_index < sub_size; ...)`, which never runs when
/// `sub_size` is 0, so nothing ever returns a match and it reports -1.
pub fn rfind(haystack: &[u8], needle: &[u8]) -> Option<usize> {
    if needle.is_empty() {
        // Not `Some(0)`: see above. The forward search answers 0 here.
        return haystack.len().checked_sub(1);
    }
    if haystack.len() < needle.len() {
        return None;
    }
    (0..=haystack.len() - needle.len())
        .rev()
        .find(|&at| &haystack[at..at + needle.len()] == needle)
}

/// Copy `source` into `destination`, truncating to `num` bytes and always
/// terminating.
///
/// `source` is the string's *content* — the caller has already measured it and
/// stopped at the terminator, per the module note above — so this does no
/// scanning of its own and copies whatever it is given, interior NULs
/// included.
///
/// Returns the number of bytes written including the terminator. The C++ is
/// `strncpy` followed by `destination[num - 1] = '\0'`: the tail is NUL-padded
/// out to `num` and a source longer than the destination is truncated rather
/// than overflowing. Callers must not overlap the two — the C++ asserts it,
/// and this takes two distinct slices, so the borrow checker enforces it here.
pub fn string_copy(destination: &mut [u8], source: &[u8], num: usize) -> usize {
    string_copy_output(crate::output::OutputBytes::new(destination), source, num)
}

/// Copy and NUL-pad into storage that may initially be uninitialized.
pub fn string_copy_output(
    mut destination: crate::output::OutputBytes<'_>,
    source: &[u8],
    num: usize,
) -> usize {
    if num == 0 || destination.is_empty() {
        return 0;
    }
    let limit = num.min(destination.len());
    // `limit - 1`, never `limit`: the C++ forces a terminator at `num - 1`,
    // so a source that fills the destination loses its last character rather
    // than its terminator.
    let copied = source.len().min(limit.saturating_sub(1));
    destination.write(0, &source[..copied]);
    // strncpy pads the remainder of the first `num` bytes with NUL.
    destination.fill(copied, limit, 0);
    copied + 1
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn last_n_keeps_the_whole_string_when_it_is_shorter() {
        assert_eq!(last_n_offset(10, 3), 7);
        assert_eq!(last_n_offset(3, 10), 0);
        assert_eq!(last_n_offset(3, 3), 0, "equal length keeps everything");
    }

    #[test]
    fn find_returns_the_first_match() {
        assert_eq!(find(b"abcabc", b"abc"), Some(0));
        assert_eq!(find(b"xabc", b"abc"), Some(1));
        assert_eq!(find(b"abc", b"d"), None);
        assert_eq!(find(b"ab", b"abc"), None, "needle longer than haystack");
    }

    #[test]
    fn rfind_returns_the_last_match() {
        assert_eq!(rfind(b"abcabc", b"abc"), Some(3));
        assert_eq!(rfind(b"abcx", b"abc"), Some(0));
        assert_eq!(rfind(b"abc", b"d"), None);
        assert_eq!(rfind(b"ab", b"abc"), None);
    }

    #[test]
    fn an_empty_needle_matches_at_opposite_ends() {
        // Not symmetric, and deliberately so: this mirrors the C++, where the
        // forward search returns 0 and the reverse returns source_size - 1.
        assert_eq!(find(b"abcdef", b""), Some(0));
        assert_eq!(rfind(b"abcdef", b""), Some(5));
        // An empty haystack is where they disagree outright. The forward
        // search short-circuits and matches at 0; the reverse one requires
        // source_size > 0 for its special case, falls through to a loop whose
        // body never executes for a zero-size needle, and reports no match.
        assert_eq!(find(b"", b""), Some(0));
        assert_eq!(rfind(b"", b""), None);
    }

    #[test]
    fn copy_always_terminates() {
        let mut dst = [b'X'; 8];
        let written = string_copy(&mut dst, b"abc", 8);
        assert_eq!(written, 4);
        assert_eq!(&dst[..4], b"abc\0");
    }

    #[test]
    fn copy_reproduces_whatever_content_it_is_given() {
        // No scanning here: the boundary already stopped at the terminator,
        // so an interior NUL is data and is copied as such.
        let mut dst = [b'X'; 8];
        assert_eq!(string_copy(&mut dst, b"a\0b", 8), 4);
        assert_eq!(&dst[..], b"a\0b\0\0\0\0\0");
    }

    #[test]
    fn copy_truncates_rather_than_overflowing() {
        let mut dst = [b'X'; 4];
        string_copy(&mut dst, b"abcdefgh", 4);
        assert_eq!(&dst[..], b"abc\0", "truncated, and still a valid C string");
    }

    #[test]
    fn copy_of_zero_bytes_writes_nothing() {
        let mut dst = [b'X'; 4];
        assert_eq!(string_copy(&mut dst, b"abc", 0), 0);
        assert_eq!(&dst[..], b"XXXX", "the destination is untouched");
    }

    #[test]
    fn copy_pads_the_remainder_as_strncpy_does() {
        let mut dst = [b'X'; 6];
        string_copy(&mut dst, b"ab", 6);
        assert_eq!(
            &dst[..],
            b"ab\0\0\0\0",
            "the tail is NUL-filled, not left as X"
        );
    }
}
