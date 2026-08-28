// ======================================================================
// \title  RustCodecDifferentialTest.cpp
// \brief  Differential test: Fw::ExternalSerializeBuffer vs the Rust codec
//
// Every case runs the C++ serializer and the Rust serializer over the same
// value and asserts identical bytes and identical statuses, then
// cross-deserializes each side's bytes with the other side's codec. This is
// the Phase 0 equivalence oracle for fprime-rs: if these pass, the Rust
// codec is byte- and status-compatible with the framework wire format.
// ======================================================================

#include <gtest/gtest.h>

#include <cstring>

#include <Fw/FPrimeBasicTypes.hpp>
#include <Fw/Types/Serializable.hpp>

#include "Rust/RustCodec.h"

namespace {

constexpr size_t CAPACITY = 64;

const Fw::Endianness MODES[] = {Fw::Endianness::BIG, Fw::Endianness::LITTLE};

int32_t modeCode(Fw::Endianness mode) {
    return (mode == Fw::Endianness::BIG) ? 0 : 1;
}

// Serialize `value` with both codecs; expect identical status, length, bytes.
// On success, cross-deserialize and expect the original value back from both.
template <typename T, typename SerFn, typename DeserFn>
void checkRoundTrip(T value, SerFn rustSer, DeserFn rustDeser) {
    for (Fw::Endianness mode : MODES) {
        // C++ serialize
        U8 cppBytes[CAPACITY] = {0};
        Fw::ExternalSerializeBuffer cppBuf(cppBytes, CAPACITY);
        const Fw::SerializeStatus cppStat = cppBuf.serializeFrom(value, mode);
        ASSERT_EQ(cppStat, Fw::FW_SERIALIZE_OK);

        // Rust serialize
        uint8_t rustBytes[CAPACITY] = {0};
        size_t written = 0;
        const int32_t rustStat = rustSer(value, modeCode(mode), rustBytes, CAPACITY, &written);
        ASSERT_EQ(rustStat, static_cast<int32_t>(cppStat));
        ASSERT_EQ(written, static_cast<size_t>(cppBuf.getSize()));
        ASSERT_EQ(0, memcmp(cppBytes, rustBytes, written)) << "byte mismatch, mode " << modeCode(mode);

        // Rust deserializes the C++ bytes
        T rustValue{};
        size_t consumed = 0;
        ASSERT_EQ(0, rustDeser(cppBytes, written, modeCode(mode), &rustValue, &consumed));
        ASSERT_EQ(consumed, written);
        ASSERT_EQ(rustValue, value);

        // C++ deserializes the Rust bytes
        Fw::ExternalSerializeBuffer readBuf(rustBytes, CAPACITY);
        ASSERT_EQ(readBuf.setBuffLen(static_cast<Fw::Serializable::SizeType>(written)), Fw::FW_SERIALIZE_OK);
        T cppValue{};
        ASSERT_EQ(readBuf.deserializeTo(cppValue, mode), Fw::FW_SERIALIZE_OK);
        ASSERT_EQ(cppValue, value);
    }
}

}  // namespace

TEST(RustCodec, AbiVersion) {
    ASSERT_EQ(fprime_rs_abi_version(), 1u);
}

TEST(RustCodec, UnsignedIntegers) {
    for (U8 v : {U8(0), U8(1), U8(0x7F), U8(0xFF)}) {
        checkRoundTrip(v, fprime_rs_ser_u8, fprime_rs_deser_u8);
    }
    for (U16 v : {U16(0), U16(0x0102), U16(0xFFFF)}) {
        checkRoundTrip(v, fprime_rs_ser_u16, fprime_rs_deser_u16);
    }
    for (U32 v : {U32(0), U32(0x01020304), U32(0xFFFFFFFF)}) {
        checkRoundTrip(v, fprime_rs_ser_u32, fprime_rs_deser_u32);
    }
    for (U64 v : {U64(0), U64(0x0102030405060708ULL), U64(0xFFFFFFFFFFFFFFFFULL)}) {
        checkRoundTrip(v, fprime_rs_ser_u64, fprime_rs_deser_u64);
    }
}

TEST(RustCodec, SignedIntegers) {
    for (I8 v : {I8(0), I8(-1), I8(127), I8(-128)}) {
        checkRoundTrip(v, fprime_rs_ser_i8, fprime_rs_deser_i8);
    }
    for (I16 v : {I16(0), I16(-2), I16(32767), I16(-32768)}) {
        checkRoundTrip(v, fprime_rs_ser_i16, fprime_rs_deser_i16);
    }
    for (I32 v : {I32(0), I32(-3), I32(0x7FFFFFFF), I32(-0x7FFFFFFF - 1)}) {
        checkRoundTrip(v, fprime_rs_ser_i32, fprime_rs_deser_i32);
    }
    for (I64 v : {I64(0), I64(-4), I64(0x7FFFFFFFFFFFFFFFLL), I64(-0x7FFFFFFFFFFFFFFFLL - 1)}) {
        checkRoundTrip(v, fprime_rs_ser_i64, fprime_rs_deser_i64);
    }
}

TEST(RustCodec, Floats) {
    for (F32 v : {F32(0.0F), F32(-1.5F), F32(3.14159F)}) {
        checkRoundTrip(v, fprime_rs_ser_f32, fprime_rs_deser_f32);
    }
    for (F64 v : {F64(0.0), F64(-2.5), F64(2.718281828459045)}) {
        checkRoundTrip(v, fprime_rs_ser_f64, fprime_rs_deser_f64);
    }
}

TEST(RustCodec, Bool) {
    checkRoundTrip(true, fprime_rs_ser_bool, fprime_rs_deser_bool);
    checkRoundTrip(false, fprime_rs_ser_bool, fprime_rs_deser_bool);

    // Wire encoding is pinned by config: 0xFF / 0x00
    uint8_t out[CAPACITY];
    size_t written = 0;
    ASSERT_EQ(0, fprime_rs_ser_bool(true, 0, out, CAPACITY, &written));
    ASSERT_EQ(written, 1u);
    ASSERT_EQ(out[0], 0xFF);
    ASSERT_EQ(0, fprime_rs_ser_bool(false, 0, out, CAPACITY, &written));
    ASSERT_EQ(out[0], 0x00);

    // Both sides reject a non-canonical byte with FORMAT_ERROR
    const uint8_t bad[] = {0x42};
    bool value = false;
    size_t consumed = 0;
    const int32_t rustStat = fprime_rs_deser_bool(bad, 1, 0, &value, &consumed);

    U8 cppBad[] = {0x42};
    Fw::ExternalSerializeBuffer cppBuf(cppBad, 1);
    ASSERT_EQ(cppBuf.setBuffLen(1), Fw::FW_SERIALIZE_OK);
    bool cppValue = false;
    const Fw::SerializeStatus cppStat = cppBuf.deserializeTo(cppValue, Fw::Endianness::BIG);
    ASSERT_EQ(rustStat, static_cast<int32_t>(cppStat));
    ASSERT_EQ(cppStat, Fw::FW_DESERIALIZE_FORMAT_ERROR);
}

TEST(RustCodec, LengthPrefixedBytes) {
    const uint8_t payload[] = {0xAA, 0xBB, 0xCC, 0x01, 0x00};
    for (Fw::Endianness mode : MODES) {
        // C++ serialize with INCLUDE_LENGTH
        U8 cppBytes[CAPACITY] = {0};
        Fw::ExternalSerializeBuffer cppBuf(cppBytes, CAPACITY);
        ASSERT_EQ(cppBuf.serializeFrom(payload, static_cast<FwSizeType>(sizeof payload),
                                       Fw::Serialization::INCLUDE_LENGTH, mode),
                  Fw::FW_SERIALIZE_OK);

        // Rust serialize
        uint8_t rustBytes[CAPACITY] = {0};
        size_t written = 0;
        ASSERT_EQ(0, fprime_rs_ser_bytes(payload, sizeof payload, modeCode(mode), rustBytes, CAPACITY, &written));
        ASSERT_EQ(written, static_cast<size_t>(cppBuf.getSize()));
        ASSERT_EQ(0, memcmp(cppBytes, rustBytes, written));

        // Rust deserializes the C++ bytes
        uint8_t out[CAPACITY] = {0};
        size_t outLen = 0;
        ASSERT_EQ(0, fprime_rs_deser_bytes(cppBytes, written, modeCode(mode), out, CAPACITY, &outLen));
        ASSERT_EQ(outLen, sizeof payload);
        ASSERT_EQ(0, memcmp(out, payload, sizeof payload));
    }
}

TEST(RustCodec, LargeSourceBufferHasNoCeiling) {
    // The C++ deserializer has no source-length limit; the Rust FFI must not
    // impose one either (regression: an earlier draft capped sources at 64).
    U8 big[1024] = {0};
    big[0] = 0x01;
    big[1] = 0x02;
    big[2] = 0x03;
    big[3] = 0x04;

    Fw::ExternalSerializeBuffer cppBuf(big, sizeof big);
    ASSERT_EQ(cppBuf.setBuffLen(sizeof big), Fw::FW_SERIALIZE_OK);
    U32 cppValue = 0;
    ASSERT_EQ(cppBuf.deserializeTo(cppValue, Fw::Endianness::BIG), Fw::FW_SERIALIZE_OK);

    uint32_t rustValue = 0;
    size_t consumed = 0;
    ASSERT_EQ(0, fprime_rs_deser_u32(big, sizeof big, 0, &rustValue, &consumed));
    ASSERT_EQ(rustValue, cppValue);
    ASSERT_EQ(consumed, 4u);
}

TEST(RustCodec, BoolFormatErrorLeavesCursorUntouched) {
    // Pins the C++ cursor semantics the Rust codec mirrors: a non-canonical
    // bool byte returns FORMAT_ERROR without advancing, so a second attempt
    // fails identically instead of reading the byte behind it. (The Rust-side
    // twin of this test lives in fprime-core's unit tests; the FFI is
    // stateless per call so the sequence is pinned here on the C++ side.)
    U8 bytes[] = {0x42, 0xFF};
    Fw::ExternalSerializeBuffer cppBuf(bytes, sizeof bytes);
    ASSERT_EQ(cppBuf.setBuffLen(sizeof bytes), Fw::FW_SERIALIZE_OK);
    bool value = false;
    ASSERT_EQ(cppBuf.deserializeTo(value, Fw::Endianness::BIG), Fw::FW_DESERIALIZE_FORMAT_ERROR);
    ASSERT_EQ(cppBuf.deserializeTo(value, Fw::Endianness::BIG), Fw::FW_DESERIALIZE_FORMAT_ERROR);
}

TEST(RustCodec, ErrorStatusParity) {
    // NO_ROOM_LEFT: a U32 cannot fit in 3 bytes
    {
        U8 cppBytes[3];
        Fw::ExternalSerializeBuffer cppBuf(cppBytes, 3);
        const Fw::SerializeStatus cppStat = cppBuf.serializeFrom(U32(1), Fw::Endianness::BIG);

        uint8_t rustBytes[3];
        size_t written = 0;
        const int32_t rustStat = fprime_rs_ser_u32(1, 0, rustBytes, 3, &written);
        ASSERT_EQ(rustStat, static_cast<int32_t>(cppStat));
        ASSERT_EQ(cppStat, Fw::FW_SERIALIZE_NO_ROOM_LEFT);
    }
    // BUFFER_EMPTY: deserialize from a drained buffer
    {
        U8 cppBytes[8];
        Fw::ExternalSerializeBuffer cppBuf(cppBytes, 8);
        U32 cppValue = 0;
        const Fw::SerializeStatus cppStat = cppBuf.deserializeTo(cppValue, Fw::Endianness::BIG);

        uint32_t rustValue = 0;
        size_t consumed = 0;
        const int32_t rustStat = fprime_rs_deser_u32(cppBytes, 0, 0, &rustValue, &consumed);
        ASSERT_EQ(rustStat, static_cast<int32_t>(cppStat));
        ASSERT_EQ(cppStat, Fw::FW_DESERIALIZE_BUFFER_EMPTY);
    }
    // SIZE_MISMATCH: data present but short of a full U32
    {
        U8 cppBytes[8] = {1, 2};
        Fw::ExternalSerializeBuffer cppBuf(cppBytes, 8);
        ASSERT_EQ(cppBuf.setBuffLen(2), Fw::FW_SERIALIZE_OK);
        U32 cppValue = 0;
        const Fw::SerializeStatus cppStat = cppBuf.deserializeTo(cppValue, Fw::Endianness::BIG);

        uint32_t rustValue = 0;
        size_t consumed = 0;
        const int32_t rustStat = fprime_rs_deser_u32(cppBytes, 2, 0, &rustValue, &consumed);
        ASSERT_EQ(rustStat, static_cast<int32_t>(cppStat));
        ASSERT_EQ(cppStat, Fw::FW_DESERIALIZE_SIZE_MISMATCH);
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
