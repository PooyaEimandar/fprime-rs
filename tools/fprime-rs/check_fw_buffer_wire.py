#!/usr/bin/env python3
"""Check that Fw::Buffer and its Rust mirror serialize the same fields in order.

`Fw::Buffer` is an FPP abstract type, so nothing generates its wire format and
`Rust/fprime-core/src/fw_buffer.rs` mirrors it from facts a person read out of
`Fw/Buffer/Buffer.cpp`. Two checks already guard part of that:

  * `Rust/RustAbstractTypeCheck.cpp` pins the serialized SIZE against the C++
    constant, and the pointer width it depends on.
  * `Fw/Buffer/test/ut/TestBuffer.cpp`'s WireFormat test pins the C++ order,
    and fw_buffer.rs's own unit test pins the Rust order.

None of them pins the two orders *together*. Swapping two same-width fields on
both sides of the C++ -- or on the Rust side alone -- leaves the size correct,
leaves each side self-consistent, and leaves both suites green while the two
languages disagree about the bytes. This closes that.

It compares the ORDER of fields, not the text: reformatting either file is
invisible to it, and a reordering is not.

    python3 tools/fprime-rs/check_fw_buffer_wire.py
    python3 tools/fprime-rs/check_fw_buffer_wire.py --self-test

Exits non-zero on a mismatch. `--self-test` proves the comparison still
catches a reorder, since a check that has never been seen to fail is not
evidence of anything.
"""

import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]
CPP = REPO / "Fw" / "Buffer" / "Buffer.cpp"
RUST = REPO / "Rust" / "fprime-core" / "src" / "fw_buffer.rs"

# The C++ member and the Rust field are spelled differently in one case only.
CPP_TO_FIELD = {"bufferData": "data"}


def body_of(text, signature, where):
    """The text between the braces of the named function."""
    start = text.find(signature)
    if start == -1:
        sys.exit(f"{where}: could not find '{signature}'")
    open_brace = text.index("{", start)
    depth = 0
    for i in range(open_brace, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[open_brace : i + 1]
    sys.exit(f"{where}: unbalanced braces after '{signature}'")


def cpp_order(body):
    """Fields named by `this->m_x` in the order they appear."""
    return [CPP_TO_FIELD.get(m, m) for m in re.findall(r"this->m_(\w+)", body)]


def rust_order(body):
    """Fields named by `self.x`, or bound by `let x = buf.deser_...`."""
    fields = re.findall(r"self\.(\w+)", body)
    if fields:
        return fields
    return re.findall(r"let\s+(\w+)\s*=\s*buf\.deser_", body)


def compare(cpp, rust):
    """The two orders, per direction, as (label, from_cpp, from_rust)."""
    return [
        (
            "serialize",
            cpp_order(body_of(cpp, "SerializeStatus Buffer::serializeTo(", CPP.name)),
            rust_order(body_of(rust, "pub fn serialize_to(", RUST.name)),
        ),
        (
            "deserialize",
            cpp_order(body_of(cpp, "SerializeStatus Buffer::deserializeFrom(", CPP.name)),
            rust_order(body_of(rust, "pub fn deserialize_from(", RUST.name)),
        ),
    ]


def disagrees(cases):
    return any(not r or c[: len(r)] != r for _, c, r in cases)


def self_test():
    """Prove the comparison still catches a reorder.

    Worth having because the interesting mutation is invisible to everything
    else: swapping two same-width fields consistently on the Rust side leaves
    SERIALIZED_SIZE at 36, leaves fw_buffer.rs's own round-trip test passing,
    and leaves TestBuffer.cpp's WireFormat test passing, because each side is
    still self-consistent. Only comparing the two orders sees it.
    """
    cpp = CPP.read_text()
    rust = RUST.read_text()
    if disagrees(compare(cpp, rust)):
        print("FAIL  self-test: the tree already disagrees; fix that first")
        return 1

    swaps = [
        ("buf.ser_u64(self.size, mode)?;", "buf.ser_u64(self.SWAP, mode)?;"),
        ("buf.ser_u64(self.offset, mode)?;", "buf.ser_u64(self.size, mode)?;"),
        ("let size = buf.deser_u64(mode)?;", "let SWAP = buf.deser_u64(mode)?;"),
        ("let offset = buf.deser_u64(mode)?;", "let size = buf.deser_u64(mode)?;"),
    ]
    mutated = rust
    for find, replace in swaps:
        if find not in mutated:
            print(f"FAIL  self-test: anchor not found: {find!r}")
            return 1
        mutated = mutated.replace(find, replace, 1)
    mutated = mutated.replace("self.SWAP", "self.offset").replace("let SWAP", "let offset")

    if not disagrees(compare(cpp, mutated)):
        print("FAIL  self-test: a consistent same-width reorder was NOT caught")
        return 1
    print("ok    self-test: a consistent same-width reorder is caught")
    return 0


def main():
    if "--self-test" in sys.argv[1:]:
        return self_test()
    cpp = CPP.read_text()
    rust = RUST.read_text()

    cases = compare(cpp, rust)

    failures = 0
    for what, from_cpp, from_rust in cases:
        # The C++ deserialize path assigns members after reading, and its
        # trailing setExtBuffer names members again; compare the leading run
        # that corresponds to the wire, which is as long as the Rust side.
        trimmed = from_cpp[: len(from_rust)]
        if not from_rust:
            print(f"FAIL  {what}: found no fields on the Rust side")
            failures += 1
        elif trimmed != from_rust:
            print(f"FAIL  {what}: the two languages disagree about the wire order")
            print(f"        {CPP.name}:  {trimmed}")
            print(f"        {RUST.name}: {from_rust}")
            failures += 1
        else:
            print(f"ok    {what}: {' '.join(from_rust)}")

    if failures:
        print(
            "\nFw::Buffer's wire format is mirrored by hand. If the C++ order "
            "changed deliberately, update Rust/fprime-core/src/fw_buffer.rs to "
            "match and re-run.",
            file=sys.stderr,
        )
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
