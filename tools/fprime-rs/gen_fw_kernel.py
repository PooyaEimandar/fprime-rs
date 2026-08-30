#!/usr/bin/env python3
"""Generate Fw/Types/SerializableRust.cpp from Fw/Types/Serializable.cpp.

fprime-rs Phase 8, the flip point: the Fw serialization kernel's eleven
primitive scalar methods call into Rust instead of manipulating bytes in C++.
Everything else in the file -- every other method, the class layout, the
``#if`` structure -- is the original's, byte for byte.

The Rust-backed file is a near-copy of an 800-line upstream file, so it is
*generated*, never hand-edited: an upstream change to ``Serializable.cpp``
otherwise silently fails to reach the Rust build, and the two implementations
drift apart with nothing to catch it. ``--check`` (run unconditionally by the
Rust CI job, and again by every build that sets ``FPRIME_RUST_IMPL_FW_TYPES``)
fails loudly the moment they do.

  python3 tools/fprime-rs/gen_fw_kernel.py           # regenerate
  python3 tools/fprime-rs/gen_fw_kernel.py --check   # verify, exit 1 on drift

The remaining signed and floating-point overloads are deliberately NOT
replaced: they already delegate to the primitives below, so replacing these
eleven routes every primitive SCALAR serialization through Rust. Bulk byte
movement (serializeFrom(const U8*, ...), deserializeTo(U8*, ...), setBuff,
copyRaw/copyRawOffset, copyFrom) stays C++ memcpy and is untouched.
"""

import argparse
import difflib
import pathlib
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO / "Fw" / "Types" / "Serializable.cpp"
TARGET = REPO / "Fw" / "Types" / "SerializableRust.cpp"

BANNER = """\
// ======================================================================
// SerializableRust.cpp
//
// Rust-backed implementation of the Fw serialization kernel (fprime-rs
// Phase 8 -- the flip point). Every method, class and semantic is the
// original's; what changed is that the primitive reads and writes call into
// Rust (Rust/fprime-core/src/lib.rs Cursor) instead of manipulating bytes
// here. The remaining signed and floating-point overloads are untouched
// because they already delegate to these primitives, so every primitive
// SCALAR serialization goes through Rust. Bulk byte movement (setBuff,
// copyRaw, the pointer/length overloads) stays C++ memcpy.
//
// Selected instead of Serializable.cpp via FPRIME_RUST_IMPL_FW_TYPES; the
// oracle is the entire existing C++ test suite.
//
// GENERATED from Fw/Types/Serializable.cpp by tools/fprime-rs/gen_fw_kernel.py
// -- do not edit. Change the original (or the generator) and regenerate.
// ======================================================================

"""

KERNEL_INCLUDE = '#include "Rust/RustFwKernel.h"\n'

# Pins the wire codes Rust/fprime-core's Endianness::from_code expects. The
# shim forwards the raw enum value, so a reordering of the C++ enum would
# otherwise silently swap byte order across the whole framework.
ENDIAN_GUARD = """\
// The endianness mode is passed to Rust as its raw enum value rather than
// re-encoded, so an out-of-range mode still trips the assert (in the Rust
// panic hook) exactly as the original's `default: FW_ASSERT(false)` did.
// These pin the wire codes Rust/fprime-core's Endianness::from_code expects:
// a future reordering of the enum would otherwise silently swap byte order.
static_assert(static_cast<int32_t>(Fw::Endianness::BIG) == 0, "Rust kernel expects Endianness::BIG == 0");
static_assert(static_cast<int32_t>(Fw::Endianness::LITTLE) == 1, "Rust kernel expects Endianness::LITTLE == 1");

"""

STATE = "this->m_buffAddr, this->m_capacity, &this->m_serLoc, &this->m_deserLoc"

# Bodies are emitted already wrapped the way clang-format would wrap them.
# The generated file lives under Fw/, which the format-check workflow checks,
# while `--check` below compares byte for byte -- so a body clang-format would
# rewrap would make those two jobs mutually exclusive. The repository style is
# Chromium at 120 columns: arguments bin-packed greedily, continuations
# aligned under the open bracket.
COLUMN_LIMIT = 120


def _pack(prefix, args, suffix):
    """Bin-pack `args` after `prefix`, aligning continuations under the bracket."""
    indent = " " * len(prefix)
    lines = []
    current = prefix
    for i, arg in enumerate(args):
        piece = arg + ("," if i + 1 < len(args) else suffix)
        candidate = (
            current
            + (" " if current != prefix and not current.endswith(" ") else "")
            + piece
        )
        if current == prefix:
            candidate = prefix + piece
        if len(candidate) <= COLUMN_LIMIT or current == prefix:
            current = candidate
        else:
            lines.append(current)
            current = indent + piece
    lines.append(current)
    return "\n".join(lines) + "\n"


def _args(tail, mode):
    out = STATE.split(", ") + [tail]
    if mode:
        out.append("static_cast<int32_t>(mode)")
    return out


def _wrap(lead, fn, args, suffix):
    """Wrap one call the way clang-format does.

    It prefers breaking once after the outer open bracket when the whole
    argument list then fits on a single continuation line, and otherwise
    bin-packs the arguments aligned under the inner bracket.
    """
    oneline = f"        {fn}({', '.join(args)}{suffix}"
    if len(oneline) <= COLUMN_LIMIT:
        return f"{lead}\n{oneline}\n"
    return _pack(f"{lead}{fn}(", args, suffix)


def _call(fn, tail, mode):
    """One `static_cast<SerializeStatus>(fprime_rs_lbb_...)` call, wrapped."""
    return _wrap(
        "    return static_cast<SerializeStatus>(", fn, _args(tail, mode), "));"
    )


BYTE_NOTE = "    (void)mode;  // a single byte has no byte order\n"

# (signature fragment, generated body). A single byte has no byte order, and
# bool is carried as a single configured byte, so those four take no mode.
REPLACEMENTS = [
    (
        "SerializeStatus LinearBufferBase::serializeFrom(U8 val, Endianness mode)",
        BYTE_NOTE + _call("fprime_rs_lbb_ser_u8", "val", False),
    ),
    (
        "SerializeStatus LinearBufferBase::serializeFrom(bool val, Endianness mode)",
        "    (void)mode;\n" + _call("fprime_rs_lbb_ser_bool", "val", False),
    ),
    (
        "SerializeStatus LinearBufferBase::deserializeTo(U8& val, Endianness mode)",
        BYTE_NOTE + _call("fprime_rs_lbb_deser_u8", "&val", False),
    ),
    (
        "SerializeStatus LinearBufferBase::deserializeTo(bool& val, Endianness mode)",
        "    (void)mode;\n" + _call("fprime_rs_lbb_deser_bool", "&val", False),
    ),
    # The I8 read is the one signed overload upstream does NOT delegate to its
    # unsigned sibling (serializeFrom(I8) does). Route it explicitly so that
    # "every primitive scalar goes through Rust" holds without exception.
    (
        "SerializeStatus LinearBufferBase::deserializeTo(I8& val, Endianness mode)",
        BYTE_NOTE
        + "    U8 raw = 0;\n"
        + _wrap(
            "    const SerializeStatus status = static_cast<SerializeStatus>(",
            "fprime_rs_lbb_deser_u8",
            _args("&raw", False),
            "));",
        )
        + "    if (status == FW_SERIALIZE_OK) {\n"
        + "        val = static_cast<I8>(raw);\n"
        + "    }\n"
        + "    return status;\n",
    ),
]

for _width in ("U16", "U32", "U64"):
    _rs = _width.lower()
    REPLACEMENTS.append(
        (
            f"SerializeStatus LinearBufferBase::serializeFrom({_width} val, Endianness mode)",
            _call(f"fprime_rs_lbb_ser_{_rs}", "val", True),
        )
    )
    REPLACEMENTS.append(
        (
            f"SerializeStatus LinearBufferBase::deserializeTo({_width}& val, Endianness mode)",
            _call(f"fprime_rs_lbb_deser_{_rs}", "&val", True),
        )
    )


def find_body(text, open_brace):
    """Return the index just past the brace matching the one at open_brace.

    Skips braces inside comments and character/string literals. The bodies
    being replaced are plain arithmetic, but a mis-matched brace here would
    corrupt the framework's hottest file silently, so the scan is careful.
    """
    assert text[open_brace] == "{"
    depth = 0
    i = open_brace
    end = len(text)
    while i < end:
        c = text[i]
        if c == "/" and i + 1 < end and text[i + 1] == "/":
            i = text.find("\n", i)
            if i == -1:
                break
            continue
        if c == "/" and i + 1 < end and text[i + 1] == "*":
            i = text.find("*/", i + 2)
            if i == -1:
                raise ValueError("unterminated block comment")
            i += 2
            continue
        if c in "\"'":
            quote = c
            i += 1
            while i < end and text[i] != quote:
                i += 2 if text[i] == "\\" else 1
            i += 1
            continue
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    raise ValueError("unbalanced braces")


def generate():
    text = SOURCE.read_text()

    for signature, body in REPLACEMENTS:
        count = text.count(signature)
        if count != 1:
            raise SystemExit(
                f"{SOURCE.relative_to(REPO)}: expected exactly one "
                f"'{signature}', found {count}. The upstream kernel changed "
                f"shape; update {pathlib.Path(__file__).name}."
            )
        start = text.index(signature)
        open_brace = text.index("{", start + len(signature))
        close = find_body(text, open_brace)
        text = text[: open_brace + 1] + "\n" + body + "}" + text[close:]

    # Replace the original banner (everything up to the first #include) and
    # add the kernel declarations next to it.
    first_include = text.index("#include")
    text = BANNER + text[first_include:]
    text = text.replace(KERNEL_INCLUDE, "", 1)  # idempotent if the source ever grows it
    # clang-format's include ordering puts the quoted project header after
    # the angle-bracket ones, so emit it there rather than next to the banner.
    anchor = "#include <cstring>  // memcpy\n"
    if anchor not in text:
        raise SystemExit("could not locate the cstring include")
    text = text.replace(anchor, anchor + KERNEL_INCLUDE, 1)

    namespace = "namespace Fw {\n"
    if text.count(namespace) != 1:
        raise SystemExit("expected exactly one 'namespace Fw {' opening")
    text = text.replace(namespace, ENDIAN_GUARD + namespace, 1)

    return text


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="verify the checked-in file matches; exit 1 on drift",
    )
    args = parser.parse_args()

    generated = generate()

    if not args.check:
        TARGET.write_text(generated)
        print(f"wrote {TARGET.relative_to(REPO)}")
        return 0

    if not TARGET.exists():
        print(f"{TARGET.relative_to(REPO)} is missing; run without --check")
        return 1
    current = TARGET.read_text()
    if current == generated:
        print(f"{TARGET.relative_to(REPO)} is up to date")
        return 0

    sys.stdout.writelines(
        difflib.unified_diff(
            current.splitlines(keepends=True),
            generated.splitlines(keepends=True),
            fromfile=f"{TARGET.relative_to(REPO)} (checked in)",
            tofile=f"{TARGET.relative_to(REPO)} (regenerated)",
        )
    )
    print(
        f"\n{TARGET.relative_to(REPO)} is stale with respect to "
        f"{SOURCE.relative_to(REPO)}.\n"
        "Run: python3 tools/fprime-rs/gen_fw_kernel.py",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
