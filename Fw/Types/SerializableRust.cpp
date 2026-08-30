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

#include <Fw/FPrimeBasicTypes.hpp>
#include <Fw/Types/Assert.hpp>
#include <Fw/Types/Serializable.hpp>
#include <Fw/Types/StringType.hpp>
#include <cstdio>
#include <cstring>  // memcpy
#include "Rust/RustFwKernel.h"
#ifdef BUILD_UT
#include <Fw/Types/String.hpp>
#include <iomanip>
#endif

// clang-format off
// Fingerprints of the upstream bodies this generator replaced or
// removed. The generated file does not contain them, so nothing else
// would notice if upstream changed one -- a mismatch here means the
// C++ that the Rust implementation mirrors has moved and needs
// re-reviewing. The `removed:` entries are the helper functions the
// Rust reimplements, and are the reason this table exists at all.
//
//   serializeFrom(U8, Endianness)                                              11b69bd018ed
//   serializeFrom(bool, Endianness)                                            ac68e22174ba
//   deserializeTo(U8&, Endianness)                                             a10eb451635f
//   deserializeTo(bool&, Endianness)                                           948a237dcdc2
//   deserializeTo(I8&, Endianness)                                             fd2e4752a678
//   serializeFrom(const U8*, FwSizeType, Serialization::t, Endianness)         d29359500540
//   serializeFrom(const LinearBufferBase&, Endianness)                         0565502a4fcc
//   deserializeTo(U8*, Serializable::SizeType&, Serialization::t, Endianness)  6060ee6f4960
//   deserializeTo(LinearBufferBase&, Endianness)                               f38fe31ed97a
//   setBuff(const U8*, Serializable::SizeType)                                 517766c9ea89
//   copyFrom(const LinearBufferBase&)                                          a12294df9e6e
//   serializeFrom(U16, Endianness)                                             70c73a780321
//   deserializeTo(U16&, Endianness)                                            f75b53adceea
//   serializeFrom(U32, Endianness)                                             e9e0fa83674a
//   deserializeTo(U32&, Endianness)                                            3534d2242d2a
//   serializeFrom(U64, Endianness)                                             1de569b462bc
//   deserializeTo(U64&, Endianness)                                            0c5d31ef439e
// clang-format on

// The endianness mode is passed to Rust as its raw enum value rather than
// re-encoded, so an out-of-range mode still trips the assert (in the Rust
// panic hook) exactly as the original's `default: FW_ASSERT(false)` did.
// These pin the wire codes Rust/fprime-core's Endianness::from_code expects:
// a future reordering of the enum would otherwise silently swap byte order.
static_assert(static_cast<int32_t>(Fw::Endianness::BIG) == 0, "Rust kernel expects Endianness::BIG == 0");
static_assert(static_cast<int32_t>(Fw::Endianness::LITTLE) == 1, "Rust kernel expects Endianness::LITTLE == 1");

namespace Fw {

Serializable::Serializable() {}

Serializable::~Serializable() {}

// ----------------------------------------------------------------------
#if FW_SERIALIZABLE_TO_STRING || FW_ENABLE_TEXT_LOGGING || BUILD_UT

void Serializable::toString(StringBase& text) const {
    text = "NOSPEC";  // set to not specified.
}

#endif

#ifdef BUILD_UT
std::ostream& operator<<(std::ostream& os, const Serializable& val) {
    Fw::String out;
    val.toString(out);

    os << out;

    return os;
}
#endif

SerialBufferBase::~SerialBufferBase() {}

LinearBufferBase::LinearBufferBase(U8* buffAddr, Serializable::SizeType capacity)
    : m_buffAddr(buffAddr), m_capacity(capacity), m_serLoc(0), m_deserLoc(0) {
    // Ensure buffAddr & capacity are consistent
    FW_ASSERT((buffAddr == nullptr) == (capacity == 0), static_cast<FwAssertArgType>(capacity));
}

LinearBufferBase::~LinearBufferBase() {}

void LinearBufferBase::copyFrom(const LinearBufferBase& src) {
    // Statement order is the original's: cursors first, then the three
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
}

// Copy constructor doesn't make sense in this virtual class as there is nothing to copy. Derived classes should
// call the empty constructor and then call their own copy function
LinearBufferBase& LinearBufferBase::operator=(const LinearBufferBase& src) {  // lgtm[cpp/rule-of-two]
    this->copyFrom(src);
    return *this;
}

// serialization routines

FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::serializeFrom(U8 val, Endianness mode) {
    (void)mode;  // a single byte has no byte order
    return static_cast<SerializeStatus>(
        fprime_rs_lbb_ser_u8(this->m_buffAddr, this->m_capacity, &this->m_serLoc, &this->m_deserLoc, val));
}

FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::serializeFrom(I8 val, Endianness mode) {
    return serializeFrom(static_cast<U8>(val), mode);
}

#if FW_HAS_16_BIT == 1
FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::serializeFrom(U16 val, Endianness mode) {
    return static_cast<SerializeStatus>(fprime_rs_lbb_ser_u16(this->m_buffAddr, this->m_capacity, &this->m_serLoc,
                                                              &this->m_deserLoc, val, static_cast<int32_t>(mode)));
}

FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::serializeFrom(I16 val, Endianness mode) {
    return serializeFrom(static_cast<U16>(val), mode);
}
#endif
#if FW_HAS_32_BIT == 1
FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::serializeFrom(U32 val, Endianness mode) {
    return static_cast<SerializeStatus>(fprime_rs_lbb_ser_u32(this->m_buffAddr, this->m_capacity, &this->m_serLoc,
                                                              &this->m_deserLoc, val, static_cast<int32_t>(mode)));
}

FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::serializeFrom(I32 val, Endianness mode) {
    return serializeFrom(static_cast<U32>(val), mode);
}
#endif

#if FW_HAS_64_BIT == 1
FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::serializeFrom(U64 val, Endianness mode) {
    return static_cast<SerializeStatus>(fprime_rs_lbb_ser_u64(this->m_buffAddr, this->m_capacity, &this->m_serLoc,
                                                              &this->m_deserLoc, val, static_cast<int32_t>(mode)));
}

FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::serializeFrom(I64 val, Endianness mode) {
    return serializeFrom(static_cast<U64>(val), mode);
}
#endif

FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::serializeFrom(F64 val, Endianness mode) {
    // floating point values need to be byte-swapped as well, so copy to U64 and use that routine
    U64 u64Val;
    (void)memcpy(&u64Val, &val, sizeof(val));
    return this->serializeFrom(u64Val, mode);
}

FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::serializeFrom(F32 val, Endianness mode) {
    // floating point values need to be byte-swapped as well, so copy to U32 and use that routine
    U32 u32Val;
    (void)memcpy(&u32Val, &val, sizeof(val));
    return this->serializeFrom(u32Val, mode);
}

FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::serializeFrom(bool val, Endianness mode) {
    (void)mode;
    return static_cast<SerializeStatus>(
        fprime_rs_lbb_ser_bool(this->m_buffAddr, this->m_capacity, &this->m_serLoc, &this->m_deserLoc, val));
}

FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::serializeFrom(const void* val, Endianness mode) {
    if (this->m_serLoc + static_cast<Serializable::SizeType>(sizeof(void*)) - 1 >= this->m_capacity) {
        return FW_SERIALIZE_NO_ROOM_LEFT;
    }

    return this->serializeFrom(reinterpret_cast<PlatformPointerCastType>(val), mode);
}

FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::serializeFrom(const U8* buff,
                                                                              Serializable::SizeType length,
                                                                              Endianness endianMode) {
    return this->serializeFrom(buff, static_cast<FwSizeType>(length), Serialization::INCLUDE_LENGTH, endianMode);
}

FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::serializeFrom(const U8* buff,
                                                                              FwSizeType length,
                                                                              Serialization::t lengthMode,
                                                                              Endianness endianMode) {
    if (lengthMode == Serialization::INCLUDE_LENGTH) {
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
    return static_cast<SerializeStatus>(
        fprime_rs_lbb_ser_bytes(this->m_buffAddr, this->m_capacity, &this->m_serLoc, &this->m_deserLoc, buff, length));
}

FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::serializeFrom(const Serializable& val,
                                                                              Endianness mode) {
    return val.serializeTo(*this, mode);
}

FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::serializeFrom(const LinearBufferBase& val,
                                                                              Endianness mode) {
    const Serializable::SizeType size = val.getSize();
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
    return static_cast<SerializeStatus>(fprime_rs_lbb_ser_bytes(this->m_buffAddr, this->m_capacity, &this->m_serLoc,
                                                                &this->m_deserLoc, valBuffAddr, size));
}

FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::serializeSize(const FwSizeType size, Endianness mode) {
    SerializeStatus status = FW_SERIALIZE_OK;
    if ((size < std::numeric_limits<FwSizeStoreType>::min()) || (size > std::numeric_limits<FwSizeStoreType>::max())) {
        status = FW_SERIALIZE_FORMAT_ERROR;
    }
    if (status == FW_SERIALIZE_OK) {
        status = this->serializeFrom(static_cast<FwSizeStoreType>(size), mode);
    }
    return status;
}

// deserialization routines

FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::deserializeTo(U8& val, Endianness mode) {
    (void)mode;  // a single byte has no byte order
    return static_cast<SerializeStatus>(
        fprime_rs_lbb_deser_u8(this->m_buffAddr, this->m_capacity, &this->m_serLoc, &this->m_deserLoc, &val));
}

FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::deserializeTo(I8& val, Endianness mode) {
    (void)mode;  // a single byte has no byte order
    U8 raw = 0;
    const SerializeStatus status = static_cast<SerializeStatus>(
        fprime_rs_lbb_deser_u8(this->m_buffAddr, this->m_capacity, &this->m_serLoc, &this->m_deserLoc, &raw));
    if (status == FW_SERIALIZE_OK) {
        val = static_cast<I8>(raw);
    }
    return status;
}

#if FW_HAS_16_BIT == 1
FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::deserializeTo(U16& val, Endianness mode) {
    return static_cast<SerializeStatus>(fprime_rs_lbb_deser_u16(this->m_buffAddr, this->m_capacity, &this->m_serLoc,
                                                                &this->m_deserLoc, &val, static_cast<int32_t>(mode)));
}

FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::deserializeTo(I16& val, Endianness mode) {
    U16 unsignVal = 0;
    SerializeStatus res = deserializeTo(unsignVal, mode);
    if (res == SerializeStatus::FW_SERIALIZE_OK) {
        val = static_cast<I16>(unsignVal);
    }
    return res;
}
#endif
#if FW_HAS_32_BIT == 1
FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::deserializeTo(U32& val, Endianness mode) {
    return static_cast<SerializeStatus>(fprime_rs_lbb_deser_u32(this->m_buffAddr, this->m_capacity, &this->m_serLoc,
                                                                &this->m_deserLoc, &val, static_cast<int32_t>(mode)));
}

FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::deserializeTo(I32& val, Endianness mode) {
    U32 unsignVal = 0;
    SerializeStatus res = deserializeTo(unsignVal, mode);
    if (res == SerializeStatus::FW_SERIALIZE_OK) {
        val = static_cast<I32>(unsignVal);
    }
    return res;
}
#endif

#if FW_HAS_64_BIT == 1

FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::deserializeTo(U64& val, Endianness mode) {
    return static_cast<SerializeStatus>(fprime_rs_lbb_deser_u64(this->m_buffAddr, this->m_capacity, &this->m_serLoc,
                                                                &this->m_deserLoc, &val, static_cast<int32_t>(mode)));
}

FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::deserializeTo(I64& val, Endianness mode) {
    U64 unsignVal;
    SerializeStatus res = deserializeTo(unsignVal, mode);
    if (res == SerializeStatus::FW_SERIALIZE_OK) {
        val = static_cast<I64>(unsignVal);
    }
    return res;
}
#endif

FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::deserializeTo(F64& val, Endianness mode) {
    // deserialize as 64-bit int to handle endianness
    U64 tempVal;
    SerializeStatus stat = this->deserializeTo(tempVal, mode);
    if (stat != FW_SERIALIZE_OK) {
        return stat;
    }
    // copy to argument
    (void)memcpy(&val, &tempVal, sizeof(val));

    return FW_SERIALIZE_OK;
}

FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::deserializeTo(bool& val, Endianness mode) {
    (void)mode;
    return static_cast<SerializeStatus>(
        fprime_rs_lbb_deser_bool(this->m_buffAddr, this->m_capacity, &this->m_serLoc, &this->m_deserLoc, &val));
}

FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::deserializeTo(void*& val, Endianness mode) {
    // Deserialize as pointer cast, then convert to void*
    PlatformPointerCastType pointerCastVal = 0;
    const SerializeStatus stat = this->deserializeTo(pointerCastVal, mode);
    if (stat == FW_SERIALIZE_OK) {
        val = reinterpret_cast<void*>(pointerCastVal);
    }
    return stat;
}

FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::deserializeTo(F32& val, Endianness mode) {
    // deserialize as 32-bit int to handle endianness
    U32 tempVal = 0;
    SerializeStatus stat = this->deserializeTo(tempVal, mode);
    if (stat != FW_SERIALIZE_OK) {
        return stat;
    }
    (void)memcpy(&val, &tempVal, sizeof(val));

    return FW_SERIALIZE_OK;
}

FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::deserializeTo(U8* buff,
                                                                              Serializable::SizeType& length,
                                                                              Endianness endianMode) {
    FwSizeType length_in_out = static_cast<FwSizeType>(length);
    SerializeStatus status = this->deserializeTo(buff, length_in_out, Serialization::INCLUDE_LENGTH, endianMode);
    length = static_cast<Serializable::SizeType>(length_in_out);
    return status;
}

FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::deserializeTo(U8* buff,
                                                                              Serializable::SizeType& length,
                                                                              Serialization::t lengthMode,
                                                                              Endianness endianMode) {
    FW_ASSERT(this->m_buffAddr != nullptr);
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
    const SerializeStatus status = static_cast<SerializeStatus>(
        fprime_rs_lbb_read_bytes(this->m_buffAddr, this->m_capacity, this->m_serLoc, this->m_deserLoc, buff, transfer));
    if (status != FW_SERIALIZE_OK) {
        return status;
    }
    length = static_cast<Serializable::SizeType>(transfer);
    this->m_deserLoc += static_cast<Serializable::SizeType>(transfer);
    return FW_SERIALIZE_OK;
}

FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::deserializeTo(Serializable& val, Endianness mode) {
    return val.deserializeFrom(*this, mode);
}

FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::deserializeTo(LinearBufferBase& val, Endianness mode) {
    U8* valBuffAddr = val.getBuffAddr();
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
    stat = static_cast<SerializeStatus>(fprime_rs_lbb_read_bytes(this->m_buffAddr, this->m_capacity, this->m_serLoc,
                                                                 this->m_deserLoc, valBuffAddr, storedLength));
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
}

FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::deserializeSize(FwSizeType& size, Endianness mode) {
    FwSizeStoreType storedSize = 0;
    Fw::SerializeStatus status = this->deserializeTo(storedSize, mode);
    if (status == FW_SERIALIZE_OK) {
        size = static_cast<FwSizeType>(storedSize);
    }
    return status;
}

void LinearBufferBase::resetSer() {
    this->m_deserLoc = 0;
    this->m_serLoc = 0;
}

void LinearBufferBase::resetDeser() {
    this->m_deserLoc = 0;
}

FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::serializeSkip(FwSizeType numBytesToSkip) {
    Fw::SerializeStatus status = FW_SERIALIZE_OK;
    // compute new ser loc
    const FwSizeType newSerLoc = this->m_serLoc + numBytesToSkip;
    // check for room
    if (newSerLoc <= this->m_capacity) {
        // update ser loc
        this->m_serLoc = static_cast<Serializable::SizeType>(newSerLoc);
    } else {
        status = FW_SERIALIZE_NO_ROOM_LEFT;
    }
    return status;
}

FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::deserializeSkip(FwSizeType numBytesToSkip) {
    // check for room
    const Serializable::SizeType size = this->m_serLoc;
    if (size == this->m_deserLoc) {
        return FW_DESERIALIZE_BUFFER_EMPTY;
    } else if (size - this->m_deserLoc < numBytesToSkip) {
        return FW_DESERIALIZE_SIZE_MISMATCH;
    }
    // update location in buffer to skip the value
    this->m_deserLoc += static_cast<Serializable::SizeType>(numBytesToSkip);
    return FW_SERIALIZE_OK;
}

FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::moveSerToOffset(FwSizeType offset) {
    // Reset serialization
    this->resetSer();
    // Advance to offset
    return this->serializeSkip(offset);
}
FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::moveDeserToOffset(FwSizeType offset) {
    // Reset deserialization
    this->resetDeser();
    // Advance to offset
    return this->deserializeSkip(offset);
}

Serializable::SizeType LinearBufferBase::getSize() const {
    return this->m_serLoc;
}

FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::setBuff(const U8* src, Serializable::SizeType length) {
    if (this->m_capacity < length) {
        return FW_SERIALIZE_NO_ROOM_LEFT;
    }
    FW_ASSERT(src);
    FW_ASSERT(this->m_buffAddr != nullptr);
    return static_cast<SerializeStatus>(
        fprime_rs_lbb_set_buff(this->m_buffAddr, this->m_capacity, &this->m_serLoc, &this->m_deserLoc, src, length));
}

FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::setBuffLen(Serializable::SizeType length) {
    if (this->m_capacity < length) {
        return FW_SERIALIZE_NO_ROOM_LEFT;
    } else {
        this->m_serLoc = length;
        this->m_deserLoc = 0;
        return FW_SERIALIZE_OK;
    }
}

Serializable::SizeType LinearBufferBase::getDeserializeSizeLeft() const {
    FW_ASSERT(this->m_serLoc >= this->m_deserLoc, static_cast<FwAssertArgType>(this->m_serLoc),
              static_cast<FwAssertArgType>(this->m_deserLoc));
    return this->m_serLoc - this->m_deserLoc;
}

Serializable::SizeType LinearBufferBase::getSerializeSizeLeft() const {
    FW_ASSERT(this->m_capacity >= this->m_serLoc, static_cast<FwAssertArgType>(this->m_capacity),
              static_cast<FwAssertArgType>(this->m_serLoc));
    return this->m_capacity - this->m_serLoc;
}

FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::copyRaw(SerialBufferBase& dest,
                                                                        Serializable::SizeType size) {
    // make sure there is sufficient size in destination
    if (dest.getCapacity() < size) {
        return FW_SERIALIZE_NO_ROOM_LEFT;
    }
    // make sure there is sufficient buffer in source
    if (this->getDeserializeSizeLeft() < size) {
        return FW_DESERIALIZE_SIZE_MISMATCH;
    }

    // otherwise, set destination buffer to data from deserialization pointer plus size
    U8* buffAddr = this->m_buffAddr;

    FW_ASSERT(buffAddr);
    SerializeStatus stat = dest.setBuff(&buffAddr[this->m_deserLoc], size);
    if (stat == FW_SERIALIZE_OK) {
        this->m_deserLoc += size;
    }
    return stat;
}

FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::copyRawOffset(SerialBufferBase& dest,
                                                                              Serializable::SizeType size) {
    // make sure there is sufficient size in destination
    const Serializable::SizeType destCapacity = dest.getCapacity();
    const Serializable::SizeType destSize = dest.getSize();
    if (destCapacity < size + destSize) {
        return FW_SERIALIZE_NO_ROOM_LEFT;
    }
    // make sure there is sufficient buffer in source
    if (this->getDeserializeSizeLeft() < size) {
        return FW_DESERIALIZE_SIZE_MISMATCH;
    }

    // otherwise, serialize bytes to destination without writing length
    U8* buffAddr = this->m_buffAddr;
    SerializeStatus stat = dest.serializeFrom(&buffAddr[this->m_deserLoc], size, Serialization::OMIT_LENGTH);
    if (stat == FW_SERIALIZE_OK) {
        this->m_deserLoc += size;
    }
    return stat;
}

// return address of buffer not yet deserialized. This is used
// to copy the remainder of a buffer.
const U8* LinearBufferBase::getBuffAddrLeft() const {
    const U8* buffAddr = this->m_buffAddr;
    FW_ASSERT(buffAddr != nullptr);
    return &buffAddr[this->m_deserLoc];
}

//!< gets address of end of serialization. Used to manually place data at the end
U8* LinearBufferBase::getBuffAddrSer() {
    U8* buffAddr = this->m_buffAddr;
    FW_ASSERT(buffAddr != nullptr);
    return &buffAddr[this->m_serLoc];
}

#ifdef BUILD_UT
bool LinearBufferBase::operator==(const LinearBufferBase& other) const {
    if (this->m_serLoc != other.getSize()) {
        return false;
    }

    const U8* us = this->m_buffAddr;
    const U8* them = other.getBuffAddr();

    FW_ASSERT(us);
    FW_ASSERT(them);

    for (Serializable::SizeType byte = 0; byte < this->m_serLoc; byte++) {
        if (us[byte] != them[byte]) {
            return false;
        }
    }

    return true;
}

std::ostream& operator<<(std::ostream& os, const LinearBufferBase& buff) {
    const U8* us = buff.getBuffAddr();

    FW_ASSERT(us);

    for (Serializable::SizeType byte = 0; byte < buff.getSize(); byte++) {
        os << "[" << std::setw(2) << std::hex << std::setfill('0') << us[byte] << "]" << std::dec;
    }

    return os;
}
#endif

ExternalSerializeBuffer::ExternalSerializeBuffer(U8* buffPtr, Serializable::SizeType size)
    : LinearBufferBase(nullptr, 0) {
    this->setExtBuffer(buffPtr, size);
}

ExternalSerializeBuffer::ExternalSerializeBuffer() : LinearBufferBase(nullptr, 0) {
    this->clear();
}

void ExternalSerializeBuffer::setExtBuffer(U8* buffPtr, Serializable::SizeType size) {
    // Allow setting buffer with null pointer and zero size (for initialization/reset)
    // but reject null pointer with non-zero size
    FW_ASSERT((buffPtr != nullptr) || (size == 0), static_cast<FwAssertArgType>(size));
    this->clear();
    this->setBuffAddrInternal(buffPtr);
    this->setCapacityInternal(size);
}

void ExternalSerializeBuffer::clear() {
    this->resetSer();
    this->resetDeser();
    this->setBuffAddrInternal(nullptr);
    this->setCapacityInternal(0);
}

// ----------------------------------------------------------------------
// Deprecated method implementations for backward compatibility
// ----------------------------------------------------------------------

Serializable::SizeType LinearBufferBase::getBuffLength() const {
    return this->m_serLoc;
}

Serializable::SizeType LinearBufferBase::getBuffLeft() {
    return this->getDeserializeSizeLeft();
}

FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::serialize(U8 val) {
    return this->serializeFrom(val);
}
FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::serialize(I8 val) {
    return this->serializeFrom(val);
}
#if FW_HAS_16_BIT == 1
FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::serialize(U16 val) {
    return this->serializeFrom(val);
}
FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::serialize(I16 val) {
    return this->serializeFrom(val);
}
#endif
#if FW_HAS_32_BIT == 1
FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::serialize(U32 val) {
    return this->serializeFrom(val);
}
FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::serialize(I32 val) {
    return this->serializeFrom(val);
}
#endif
#if FW_HAS_64_BIT == 1
FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::serialize(U64 val) {
    return this->serializeFrom(val);
}
FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::serialize(I64 val) {
    return this->serializeFrom(val);
}
#endif
FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::serialize(F32 val) {
    return this->serializeFrom(val);
}
FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::serialize(F64 val) {
    return this->serializeFrom(val);
}
FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::serialize(bool val) {
    return this->serializeFrom(val);
}
FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::serialize(const void* val) {
    return this->serializeFrom(val);
}

// Deprecated method for backward compatibility
FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::serialize(const U8* buff,
                                                                          FwSizeType length,
                                                                          bool noLength) {
    const Serialization::t mode = noLength ? Serialization::OMIT_LENGTH : Serialization::INCLUDE_LENGTH;
    return this->serializeFrom(buff, length, mode);
}

FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::serialize(const U8* buff, FwSizeType length) {
    return this->serializeFrom(buff, length);
}
FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::serialize(const U8* buff,
                                                                          FwSizeType length,
                                                                          Serialization::t mode) {
    return this->serializeFrom(buff, length, mode);
}
FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::serialize(const Serializable& val) {
    return this->serializeFrom(val);
}
FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::serialize(const LinearBufferBase& val) {
    return this->serializeFrom(val);
}

FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::deserialize(U8& val) {
    return this->deserializeTo(val);
}
FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::deserialize(I8& val) {
    return this->deserializeTo(val);
}
#if FW_HAS_16_BIT == 1
FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::deserialize(U16& val) {
    return this->deserializeTo(val);
}
FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::deserialize(I16& val) {
    return this->deserializeTo(val);
}
#endif
#if FW_HAS_32_BIT == 1
FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::deserialize(U32& val) {
    return this->deserializeTo(val);
}
FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::deserialize(I32& val) {
    return this->deserializeTo(val);
}
#endif
#if FW_HAS_64_BIT == 1
FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::deserialize(U64& val) {
    return this->deserializeTo(val);
}
FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::deserialize(I64& val) {
    return this->deserializeTo(val);
}
#endif
FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::deserialize(F32& val) {
    return this->deserializeTo(val);
}
FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::deserialize(F64& val) {
    return this->deserializeTo(val);
}
FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::deserialize(bool& val) {
    return this->deserializeTo(val);
}
FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::deserialize(void*& val) {
    return this->deserializeTo(val);
}

// Deprecated method for backward compatibility
FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::deserialize(U8* buff,
                                                                            FwSizeType& length,
                                                                            bool noLength) {
    const Serialization::t mode = noLength ? Serialization::OMIT_LENGTH : Serialization::INCLUDE_LENGTH;
    return this->deserializeTo(buff, length, mode);
}

FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::deserialize(U8* buff, FwSizeType& length) {
    return this->deserializeTo(buff, length, Serialization::INCLUDE_LENGTH);
}

FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::deserialize(U8* buff,
                                                                            FwSizeType& length,
                                                                            Serialization::t mode) {
    return this->deserializeTo(buff, length, mode);
}

FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::deserialize(Serializable& val) {
    return this->deserializeTo(val);
}
FW_SERIALIZE_FORCE_INLINE_LBB SerializeStatus LinearBufferBase::deserialize(LinearBufferBase& val) {
    return this->deserializeTo(val);
}

Serializable::SizeType ExternalSerializeBuffer::getBuffCapacity() const {
    return this->getCapacity();
}

}  // namespace Fw
