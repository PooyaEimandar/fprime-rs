#!/usr/bin/env python3
"""Generate Fw/Types/SerializableRust.cpp from Fw/Types/Serializable.cpp.

fprime-rs Phase 8, the flip point: the Fw serialization kernel's primitive
scalar methods and its bulk byte paths call into Rust instead of manipulating
bytes in C++. Everything else in the file -- every other method, the class
layout, the ``#if`` structure -- is the original's.

  python3 tools/fprime-rs/gen_fw_kernel.py           # regenerate
  python3 tools/fprime-rs/gen_fw_kernel.py --check   # verify, exit 1 on drift

The remaining signed and floating-point overloads are deliberately NOT
replaced: they already delegate to the primitives below. Together with the
bulk paths, that puts every byte this class moves between a caller and its
storage through Rust; the only memcpy left is the float bit-casts, which are
type punning rather than buffer movement.

See tools/fprime-rs/shimgen.py for the generation and drift-check mechanism.
"""

import sys

import shimgen
from shimgen import REPO

SOURCE = REPO / "Fw" / "Types" / "Serializable.cpp"
TARGET = REPO / "Fw" / "Types" / "SerializableRust.cpp"

BANNER = """\
// ======================================================================
// SerializableRust.cpp
//
// Rust-backed implementation of the Fw serialization kernel (fprime-rs
// Phase 8 -- the flip point). Every method, class and semantic is the
// original's; what changed is that the primitive reads and writes, and the
// bulk byte transfers, call into Rust (Rust/fprime-core, Rust/fprime-ffi)
// instead of manipulating bytes here. The remaining signed and floating-point
// overloads are untouched because they already delegate to these primitives,
// and copyRaw/copyRawOffset are covered transitively through the
// destination's setBuff and serializeFrom. What is left in C++ is the four
// memcpy-based float bit-casts, which are type punning rather than buffer
// movement, and the arithmetic around the calls.
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
# The read entry points take the cursors by value: they never move them.
READ_STATE = "this->m_buffAddr, this->m_capacity, this->m_serLoc, this->m_deserLoc"

# Formatting is clang-format's job, not this generator's: `--check` below
# compares content with whitespace collapsed, so a reformat of either file
# cannot make the format check and the drift check contradict each other. The
# emitted wrapping is only a starting point, tidied by clang-format when it is
# available at generation time.
COLUMN_LIMIT = 120


def _pack(prefix, args, suffix):
    """Bin-pack `args` after `prefix`, aligning continuations under the bracket."""
    indent = " " * len(prefix)
    lines = []
    current = prefix
    for i, arg in enumerate(args):
        piece = arg + ("," if i + 1 < len(args) else suffix)
        candidate = current + (" " if current != prefix else "") + piece
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
    """Wrap one call, breaking after the outer bracket when that fits."""
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

# --- Bulk transfers -------------------------------------------------------
#
# The payload paths. Only the byte movement and the cursor update move to
# Rust; the surrounding logic stays C++ because it calls back into other C++
# objects (getSize/getCapacity/getBuffAddr/setBuffLen) and because the exact
# order of the original's room check and its null assert is observable -- a
# too-long transfer from a null source returns NO_ROOM_LEFT rather than
# faulting, so that check is left where it was. Rust re-checks anyway, with
# checked arithmetic, since it owns the copy.

BULK_REPLACEMENTS = [
    (
        """SerializeStatus LinearBufferBase::serializeFrom(const U8* buff,
                                FwSizeType length,
                                Serialization::t lengthMode,
                                Endianness endianMode)""",
        """    if (lengthMode == Serialization::INCLUDE_LENGTH) {
        const SerializeStatus stat = this->serializeFrom(static_cast<FwSizeStoreType>(length), endianMode);
        if (stat != FW_SERIALIZE_OK) {
            return stat;
        }
    }
    if (this->m_serLoc + length > this->m_capacity) {
        return FW_SERIALIZE_NO_ROOM_LEFT;
    }
    if (length > 0) {
        FW_ASSERT(buff != nullptr);
    }
    FW_ASSERT(this->m_buffAddr != nullptr);
    return static_cast<SerializeStatus>(fprime_rs_lbb_ser_bytes(STATE, buff, length));
""",
    ),
    (
        "SerializeStatus LinearBufferBase::serializeFrom(const LinearBufferBase& val, Endianness mode)",
        """    const Serializable::SizeType size = val.getSize();
    if (this->m_serLoc + size + static_cast<Serializable::SizeType>(sizeof(FwSizeStoreType)) > this->m_capacity) {
        return FW_SERIALIZE_NO_ROOM_LEFT;
    }
    const SerializeStatus stat = this->serializeFrom(static_cast<FwSizeStoreType>(size), mode);
    if (stat != FW_SERIALIZE_OK) {
        return stat;
    }
    const U8* valBuffAddr = val.getBuffAddr();
    FW_ASSERT(this->m_buffAddr != nullptr);
    FW_ASSERT(valBuffAddr != nullptr);
    return static_cast<SerializeStatus>(fprime_rs_lbb_ser_bytes(STATE, valBuffAddr, size));
""",
    ),
    (
        """SerializeStatus LinearBufferBase::deserializeTo(U8* buff,
                                Serializable::SizeType& length,
                                Serialization::t lengthMode,
                                Endianness endianMode)""",
        """    FW_ASSERT(this->m_buffAddr != nullptr);
    FwSizeType transfer = static_cast<FwSizeType>(length);
    if (lengthMode == Serialization::INCLUDE_LENGTH) {
        FwSizeStoreType storedLength = 0;
        const SerializeStatus stat = this->deserializeTo(storedLength, endianMode);
        if (stat != FW_SERIALIZE_OK) {
            return stat;
        }
        // Both halves of the original's check stay here, and stay ahead of
        // the assert below: a length that overruns the buffer is a status,
        // not a fault, and splitting the test across the language boundary
        // would let the assert win.
        if ((storedLength > this->getDeserializeSizeLeft()) || (storedLength > length)) {
            return FW_DESERIALIZE_SIZE_MISMATCH;
        }
        transfer = static_cast<FwSizeType>(storedLength);
    } else if (transfer > this->getDeserializeSizeLeft()) {
        return FW_DESERIALIZE_SIZE_MISMATCH;
    }
    if (transfer > 0) {
        FW_ASSERT(buff != nullptr);
    }
    const SerializeStatus status =
        static_cast<SerializeStatus>(fprime_rs_lbb_read_bytes(READ_STATE, buff, transfer));
    if (status != FW_SERIALIZE_OK) {
        return status;
    }
    length = static_cast<Serializable::SizeType>(transfer);
    this->m_deserLoc += static_cast<Serializable::SizeType>(transfer);
    return FW_SERIALIZE_OK;
""",
    ),
    (
        "SerializeStatus LinearBufferBase::deserializeTo(LinearBufferBase& val, Endianness mode)",
        """    U8* valBuffAddr = val.getBuffAddr();
    FW_ASSERT(valBuffAddr != nullptr);
    FwSizeStoreType storedLength = 0;
    SerializeStatus stat = this->deserializeTo(storedLength, mode);
    if (stat != FW_SERIALIZE_OK) {
        return stat;
    }
    if ((storedLength > val.getCapacity()) || (storedLength > this->getDeserializeSizeLeft())) {
        return FW_DESERIALIZE_SIZE_MISMATCH;
    }
    FW_ASSERT(this->m_buffAddr != nullptr);
    stat = static_cast<SerializeStatus>(fprime_rs_lbb_read_bytes(READ_STATE, valBuffAddr, storedLength));
    if (stat != FW_SERIALIZE_OK) {
        return stat;
    }
    // Advanced here, after setBuffLen, exactly as the original does. Moving
    // it into the copy and subtracting back on failure is not equivalent when
    // `val` aliases this object: setBuffLen would zero the cursor and the
    // compensation would then undo the wrong value.
    stat = val.setBuffLen(storedLength);
    if (stat != FW_SERIALIZE_OK) {
        return stat;
    }
    this->m_deserLoc += storedLength;
    return FW_SERIALIZE_OK;
""",
    ),
    (
        "SerializeStatus LinearBufferBase::setBuff(const U8* src, Serializable::SizeType length)",
        """    if (this->m_capacity < length) {
        return FW_SERIALIZE_NO_ROOM_LEFT;
    }
    FW_ASSERT(src);
    FW_ASSERT(this->m_buffAddr != nullptr);
    return static_cast<SerializeStatus>(fprime_rs_lbb_set_buff(STATE, src, length));
""",
    ),
    (
        "void LinearBufferBase::copyFrom(const LinearBufferBase& src)",
        """    // Statement order is the original's: cursors first, then the three
    // asserts in their original sequence, then the copy. Only the copy is
    // Rust's; reordering would change which assert fires, and so which fault
    // an operator sees, for an object that violates more than one.
    this->m_serLoc = src.m_serLoc;
    this->m_deserLoc = src.m_deserLoc;
    FW_ASSERT(src.m_buffAddr != nullptr);
    FW_ASSERT(this->m_buffAddr != nullptr);
    FW_ASSERT(src.m_serLoc <= this->m_capacity, static_cast<FwAssertArgType>(src.m_serLoc),
              static_cast<FwAssertArgType>(this->m_capacity));
    fprime_rs_lbb_copy_bytes(this->m_buffAddr, this->m_capacity, src.m_buffAddr, this->m_serLoc);
""",
    ),
]

REPLACEMENTS += [
    (sig, body.replace("READ_STATE", READ_STATE).replace("STATE", STATE))
    for sig, body in BULK_REPLACEMENTS
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


SHIM = shimgen.Shim(
    source=SOURCE,
    target=TARGET,
    banner=BANNER,
    replacements=REPLACEMENTS,
    edits=[
        # clang-format's include ordering puts the quoted project header after
        # the angle-bracket ones, so emit it there rather than by the banner.
        (
            "#include <cstring>  // memcpy\n",
            "#include <cstring>  // memcpy\n" + KERNEL_INCLUDE,
            True,
        ),
    ],
    preamble=("namespace Fw {\n", ENDIAN_GUARD),
)


if __name__ == "__main__":
    sys.exit(shimgen.main(SHIM, __doc__))
