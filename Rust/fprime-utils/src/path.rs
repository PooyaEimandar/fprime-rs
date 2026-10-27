//! Textual path resolution and sandbox containment.
//!
//! Mirrors `Os/FilePathUtils.cpp`. This is the code that decides whether a
//! path a spacecraft was *told* to open stays inside the directory it is
//! allowed to touch — `..` collapsing and prefix containment — so it is
//! exactly the kind of byte handling worth moving out of C++: no filesystem
//! access, no syscalls, purely a question of getting the character arithmetic
//! right.
//!
//! The C++ resolves in place with a read cursor ahead of a write cursor over
//! one buffer, relying on `writePos <= readPos` to make the overlap safe.
//! That invariant is preserved here and expressed with index arithmetic on a
//! single slice, so the compiler checks the bounds the C++ reasoned about in
//! a comment.

use fprime_config::FwSizeType;
use fprime_core::output::OutputBytes;

/// Mirrors `Os::FilePathUtils::Status`; the discriminants are the C++ enum's.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum Status {
    /// Path is valid, and within the allowed directory when one was checked
    Valid = 0,
    /// Resolved path falls outside the allowed directory
    OutsideSandbox = 1,
    /// Path is malformed or cannot be resolved
    InvalidPath = 2,
    /// Combined path length exceeds the output buffer
    TooLong = 3,
}

impl Status {
    /// The integer the C++ caller compares against `Os::FilePathUtils::Status`.
    pub const fn code(self) -> i32 {
        self as i32
    }
}

/// Length of the C string in `buf`, or `None` when it is not terminated
/// within `limit` bytes.
///
/// The C++ calls `Fw::StringUtils::string_length` and then checks the result
/// against the limit to detect a missing terminator; folding both into one
/// `Option` makes the unterminated case impossible to forget at a call site.
fn c_len(buf: &[u8], limit: usize) -> Option<usize> {
    let bounded = &buf[..buf.len().min(limit)];
    bounded.iter().position(|&b| b == 0)
}

/// Build the unresolved absolute path into `out`, prepending `base` when
/// `path` is relative. Returns the byte length written, excluding the
/// terminator.
fn build_absolute(
    path: &[u8],
    base: Option<&[u8]>,
    out: &mut OutputBytes<'_>,
    max_path: usize,
) -> Result<usize, Status> {
    let path_len = c_len(path, max_path).ok_or(Status::InvalidPath)?;

    if path.first() != Some(&b'/') {
        // Relative: a base directory is required, and must itself be absolute.
        let base = base.ok_or(Status::InvalidPath)?;
        let base_len = c_len(base, max_path).ok_or(Status::InvalidPath)?;
        if base_len == 0 || base[0] != b'/' {
            return Err(Status::InvalidPath);
        }
        let needs_slash = usize::from(base[base_len - 1] != b'/');
        // `+ 1` for the terminator, as the C++ does.
        if base_len + needs_slash + path_len + 1 > out.len() {
            return Err(Status::TooLong);
        }
        out.write(0, &base[..base_len]);
        let mut pos = base_len;
        if needs_slash == 1 {
            out.write(pos, b"/");
            pos += 1;
        }
        out.write(pos, &path[..path_len]);
        pos += path_len;
        out.write(pos, &[0]);
        Ok(pos)
    } else {
        if path_len + 1 > out.len() {
            return Err(Status::TooLong);
        }
        out.write(0, &path[..path_len]);
        out.write(path_len, &[0]);
        Ok(path_len)
    }
}

/// Collapse `.`, `..` and repeated separators in place.
///
/// The write cursor never overtakes the read cursor because resolution only
/// ever shortens a path, so the segment moves are always backwards.
fn resolve_in_place(buf: &mut [u8], max_path: usize) -> Result<(), Status> {
    let length = c_len(buf, max_path).ok_or(Status::InvalidPath)?;
    if buf.first() != Some(&b'/') {
        return Err(Status::InvalidPath);
    }

    let mut write = 1usize; // past the root '/'
    let mut read = 1usize;

    while read <= length {
        let start = read;
        while read < length && buf[read] != b'/' {
            read += 1;
        }
        let seg_len = read - start;
        read = if read < length { read + 1 } else { length + 1 };

        // Empty (from `//`) and `.` segments contribute nothing.
        if seg_len == 0 || (seg_len == 1 && buf[start] == b'.') {
            continue;
        }
        // `..` backs up to the previous separator, never past the root.
        if seg_len == 2 && buf[start] == b'.' && buf[start + 1] == b'.' {
            if write > 1 {
                write -= 1;
                while write > 1 && buf[write - 1] != b'/' {
                    write -= 1;
                }
            }
            continue;
        }

        // A normal segment shifts left. `write <= start` always, so the
        // ranges either coincide or move backwards.
        buf.copy_within(start..start + seg_len, write);
        write += seg_len;
        buf[write] = b'/';
        write += 1;
    }

    // Drop the trailing separator, except for the root itself.
    if write > 1 {
        write -= 1;
    }
    buf[write] = 0;
    Ok(())
}

/// `resolvePath(const char*, const char*, char*, FwSizeType)`.
///
/// `out` is the caller's buffer; on success it holds a null-terminated
/// absolute path with all `.`/`..`/`//` collapsed.
pub fn resolve_path(
    path: &[u8],
    base: Option<&[u8]>,
    out: &mut [u8],
    max_path: FwSizeType,
) -> Status {
    resolve_path_output(path, base, OutputBytes::new(out), max_path)
}

/// Resolve into caller storage without assuming its unused bytes are initialized.
pub fn resolve_path_output(
    path: &[u8],
    base: Option<&[u8]>,
    mut out: OutputBytes<'_>,
    max_path: FwSizeType,
) -> Status {
    let max_path = max_path as usize;
    if path.is_empty() || path[0] == 0 {
        return Status::InvalidPath;
    }
    let length = match build_absolute(path, base, &mut out, max_path) {
        Ok(length) => length,
        Err(status) => return status,
    };
    match resolve_in_place(&mut out.initialized_mut()[..=length], max_path) {
        Ok(()) => Status::Valid,
        Err(status) => status,
    }
}

/// `checkContainment(const char*, const char*)`: is `resolved` inside
/// `allowed`?
///
/// `allowed` must end in `/`, which is what makes the prefix test a directory
/// boundary test rather than a string test — without it `/data/uplink-evil`
/// would count as inside `/data/uplink`.
pub fn check_containment(resolved: &[u8], allowed: &[u8], max_path: FwSizeType) -> Status {
    let max_path = max_path as usize;
    let allowed_len = match c_len(allowed, max_path) {
        Some(n) => n,
        None => return Status::InvalidPath,
    };
    let path_len = match c_len(resolved, max_path) {
        Some(n) => n,
        None => return Status::InvalidPath,
    };
    if allowed_len == 0 || path_len == 0 {
        return Status::OutsideSandbox;
    }
    if allowed[allowed_len - 1] != b'/' {
        return Status::OutsideSandbox;
    }
    // The path may be the sandbox directory itself, without its trailing '/'.
    if path_len == allowed_len - 1 && resolved[..path_len] == allowed[..path_len] {
        return Status::Valid;
    }
    if path_len < allowed_len {
        return Status::OutsideSandbox;
    }
    if resolved[..allowed_len] != allowed[..allowed_len] {
        return Status::OutsideSandbox;
    }
    Status::Valid
}

#[cfg(test)]
mod tests {
    use super::*;

    const MAX: FwSizeType = 256;

    fn resolve(path: &str, base: Option<&str>) -> (Status, String) {
        let mut out = [0u8; 256];
        let path_c = format!("{path}\0");
        let base_c = base.map(|b| format!("{b}\0"));
        let status = resolve_path(
            path_c.as_bytes(),
            base_c.as_ref().map(|b| b.as_bytes()),
            &mut out,
            MAX,
        );
        let end = out.iter().position(|&b| b == 0).unwrap_or(out.len());
        (status, String::from_utf8_lossy(&out[..end]).into_owned())
    }

    fn contained(path: &str, allowed: &str) -> Status {
        check_containment(
            format!("{path}\0").as_bytes(),
            format!("{allowed}\0").as_bytes(),
            MAX,
        )
    }

    #[test]
    fn collapses_dot_and_dotdot_and_double_slashes() {
        assert_eq!(resolve("/a/./b", None), (Status::Valid, "/a/b".into()));
        assert_eq!(resolve("/a/b/../c", None), (Status::Valid, "/a/c".into()));
        assert_eq!(resolve("/a//b", None), (Status::Valid, "/a/b".into()));
        assert_eq!(resolve("/a/b/", None), (Status::Valid, "/a/b".into()));
    }

    #[test]
    fn dotdot_cannot_escape_the_root() {
        // The property the sandbox depends on: no amount of `..` climbs above
        // `/`, so a resolved path can never start outside it.
        assert_eq!(resolve("/../../..", None), (Status::Valid, "/".into()));
        assert_eq!(resolve("/a/../../b", None), (Status::Valid, "/b".into()));
        assert_eq!(resolve("/", None), (Status::Valid, "/".into()));
    }

    #[test]
    fn relative_paths_resolve_against_the_base() {
        assert_eq!(resolve("b/c", Some("/a")), (Status::Valid, "/a/b/c".into()));
        assert_eq!(
            resolve("b/c", Some("/a/")),
            (Status::Valid, "/a/b/c".into()),
            "a base already ending in '/' must not gain a second one"
        );
        assert_eq!(
            resolve("../x", Some("/a/b")),
            (Status::Valid, "/a/x".into())
        );
    }

    #[test]
    fn relative_path_without_a_usable_base_is_refused() {
        assert_eq!(resolve("b/c", None).0, Status::InvalidPath);
        assert_eq!(resolve("b/c", Some("")).0, Status::InvalidPath);
        assert_eq!(
            resolve("b/c", Some("relative")).0,
            Status::InvalidPath,
            "the base must itself be absolute"
        );
        assert_eq!(resolve("", None).0, Status::InvalidPath);
    }

    #[test]
    fn a_path_that_does_not_fit_reports_too_long() {
        let mut out = [0u8; 8];
        let status = resolve_path(b"/aaaaaaaaaaaaaaaa\0", None, &mut out, MAX);
        assert_eq!(status, Status::TooLong);
    }

    #[test]
    fn unterminated_input_is_refused_rather_than_scanned_past() {
        let mut out = [0u8; 64];
        // No terminator within the limit: the C++ detects this by comparing
        // the measured length against the buffer size.
        let status = resolve_path(&[b'/'; 32], None, &mut out, 16);
        assert_eq!(status, Status::InvalidPath);
    }

    #[test]
    fn containment_requires_a_directory_boundary() {
        assert_eq!(contained("/data/uplink/f", "/data/uplink/"), Status::Valid);
        assert_eq!(
            contained("/data/uplink", "/data/uplink/"),
            Status::Valid,
            "the sandbox directory itself is inside the sandbox"
        );
        // The attack the trailing slash exists to stop.
        assert_eq!(
            contained("/data/uplink-evil/f", "/data/uplink/"),
            Status::OutsideSandbox
        );
        assert_eq!(
            contained("/data/other/f", "/data/uplink/"),
            Status::OutsideSandbox
        );
        assert_eq!(contained("/data", "/data/uplink/"), Status::OutsideSandbox);
    }

    #[test]
    fn containment_refuses_an_allowed_directory_without_a_trailing_slash() {
        assert_eq!(
            contained("/data/uplink/f", "/data/uplink"),
            Status::OutsideSandbox
        );
        assert_eq!(contained("/a", ""), Status::OutsideSandbox);
        assert_eq!(contained("", "/a/"), Status::OutsideSandbox);
    }

    #[test]
    fn traversal_attempts_resolve_inside_before_containment_is_checked() {
        // End to end: this is the sequence SandboxedFile relies on.
        let (status, resolved) = resolve("/data/uplink/../../etc/passwd", None);
        assert_eq!(status, Status::Valid);
        assert_eq!(resolved, "/etc/passwd");
        assert_eq!(
            contained(&resolved, "/data/uplink/"),
            Status::OutsideSandbox
        );

        let (status, resolved) = resolve("/data/uplink/./sub/../f", None);
        assert_eq!(status, Status::Valid);
        assert_eq!(resolved, "/data/uplink/f");
        assert_eq!(contained(&resolved, "/data/uplink/"), Status::Valid);
    }
}
