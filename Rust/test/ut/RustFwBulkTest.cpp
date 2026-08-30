// ======================================================================
// \title  RustFwBulkTest.cpp
// \brief  Pins the Fw serialization kernel's BULK byte paths
//
// Phase 8 routed the bulk transfers -- the paths that carry the payloads:
// strings, Fw::Buffer contents, ComBuffer, DP containers -- through Rust.
// Four of the six had NO coverage in Fw/Types/test/ut, which the README calls
// the kernel's oracle, and the other two were covered only on their success
// path. A flip with no test is a claim, not a result.
//
// These tests use only the public Fw API, so they compile and pass against
// EITHER implementation. That is the point: they are a differential oracle,
// and CI runs them in both switch positions. What they pin is the behavior
// the flip had to preserve exactly -- which status comes back on each path,
// in which order the bounds checks and the asserts are consulted, what the
// cursors read afterwards, and what is left behind on a failure.
// ======================================================================

#include <gtest/gtest.h>

#include <Fw/FPrimeBasicTypes.hpp>
#include <Fw/Types/Serializable.hpp>

#include <cstring>

namespace {

//! A buffer of `N` bytes with its own storage, for brevity below.
template <FwSizeType N>
class Buf : public Fw::ExternalSerializeBuffer {
  public:
    Buf() : Fw::ExternalSerializeBuffer(m_store, N) {}

  private:
    U8 m_store[N];
};

// ----------------------------------------------------------------------
// serializeFrom(const U8*, length, lengthMode, endianness)
// ----------------------------------------------------------------------

TEST(FwBulk, SerializeOmitLengthWritesExactlyTheBytes) {
    Buf<16> buf;
    const U8 payload[] = {1, 2, 3, 4};
    ASSERT_EQ(buf.serializeFrom(payload, sizeof(payload), Fw::Serialization::OMIT_LENGTH), Fw::FW_SERIALIZE_OK);
    ASSERT_EQ(buf.getSize(), sizeof(payload));
    ASSERT_EQ(std::memcmp(buf.getBuffAddr(), payload, sizeof(payload)), 0);
}

TEST(FwBulk, SerializeIncludeLengthPrefixesTheLengthToken) {
    Buf<16> buf;
    const U8 payload[] = {9, 8, 7};
    ASSERT_EQ(buf.serializeFrom(payload, sizeof(payload), Fw::Serialization::INCLUDE_LENGTH), Fw::FW_SERIALIZE_OK);
    // The token plus the body: the token's width is configuration, so assert
    // the relationship rather than a hard number.
    ASSERT_EQ(buf.getSize(), sizeof(FwSizeStoreType) + sizeof(payload));
}

TEST(FwBulk, SerializeRefusesWhenTheBodyDoesNotFit) {
    Buf<4> buf;
    const U8 payload[] = {1, 2, 3, 4, 5};
    ASSERT_EQ(buf.serializeFrom(payload, sizeof(payload), Fw::Serialization::OMIT_LENGTH),
              Fw::FW_SERIALIZE_NO_ROOM_LEFT);
    // A refused transfer must not have moved the write cursor.
    ASSERT_EQ(buf.getSize(), 0u);
}

TEST(FwBulk, SerializeFitsExactlyToCapacity) {
    Buf<4> buf;
    const U8 payload[] = {1, 2, 3, 4};
    ASSERT_EQ(buf.serializeFrom(payload, sizeof(payload), Fw::Serialization::OMIT_LENGTH), Fw::FW_SERIALIZE_OK);
    ASSERT_EQ(buf.getSize(), 4u);
}

TEST(FwBulk, SerializeZeroLengthSucceedsAndMovesNothing) {
    Buf<8> buf;
    const U8 payload[] = {1};
    ASSERT_EQ(buf.serializeFrom(payload, 0, Fw::Serialization::OMIT_LENGTH), Fw::FW_SERIALIZE_OK);
    ASSERT_EQ(buf.getSize(), 0u);
}

// ----------------------------------------------------------------------
// deserializeTo(U8*, length&, lengthMode, endianness)
// ----------------------------------------------------------------------

TEST(FwBulk, RoundTripsThroughTheLengthToken) {
    Buf<32> buf;
    const U8 payload[] = {4, 5, 6, 7, 8};
    ASSERT_EQ(buf.serializeFrom(payload, sizeof(payload), Fw::Serialization::INCLUDE_LENGTH), Fw::FW_SERIALIZE_OK);

    U8 out[8] = {};
    Fw::Serializable::SizeType length = sizeof(out);
    ASSERT_EQ(buf.deserializeTo(out, length, Fw::Serialization::INCLUDE_LENGTH), Fw::FW_SERIALIZE_OK);
    ASSERT_EQ(length, sizeof(payload));
    ASSERT_EQ(std::memcmp(out, payload, sizeof(payload)), 0);
}

TEST(FwBulk, DeserializeReportsMismatchWhenTheStoredLengthExceedsTheCallersBuffer) {
    Buf<32> buf;
    const U8 payload[] = {1, 2, 3, 4, 5, 6};
    ASSERT_EQ(buf.serializeFrom(payload, sizeof(payload), Fw::Serialization::INCLUDE_LENGTH), Fw::FW_SERIALIZE_OK);

    U8 out[2] = {};
    Fw::Serializable::SizeType length = sizeof(out);
    // A status, not a fault: the stored length is larger than the caller can
    // take. This is the ordering the flip had to preserve -- the bounds test
    // must be consulted before the destination pointer is asserted on.
    ASSERT_EQ(buf.deserializeTo(out, length, Fw::Serialization::INCLUDE_LENGTH), Fw::FW_DESERIALIZE_SIZE_MISMATCH);
}

TEST(FwBulk, DeserializeReportsMismatchWhenMoreIsAskedForThanWasWritten) {
    Buf<32> buf;
    const U8 payload[] = {1, 2, 3};
    ASSERT_EQ(buf.serializeFrom(payload, sizeof(payload), Fw::Serialization::OMIT_LENGTH), Fw::FW_SERIALIZE_OK);

    U8 out[8] = {};
    Fw::Serializable::SizeType length = sizeof(out);
    ASSERT_EQ(buf.deserializeTo(out, length, Fw::Serialization::OMIT_LENGTH), Fw::FW_DESERIALIZE_SIZE_MISMATCH);
}

TEST(FwBulk, DeserializeOnAnEmptyBufferReportsBufferEmpty) {
    Buf<32> buf;
    U8 out[4] = {};
    Fw::Serializable::SizeType length = sizeof(out);
    ASSERT_EQ(buf.deserializeTo(out, length, Fw::Serialization::INCLUDE_LENGTH), Fw::FW_DESERIALIZE_BUFFER_EMPTY);
}

TEST(FwBulk, ConsecutiveOmitLengthReadsAdvanceTheCursor) {
    Buf<32> buf;
    const U8 payload[] = {10, 20, 30, 40};
    ASSERT_EQ(buf.serializeFrom(payload, sizeof(payload), Fw::Serialization::OMIT_LENGTH), Fw::FW_SERIALIZE_OK);

    U8 first[2] = {};
    U8 second[2] = {};
    Fw::Serializable::SizeType n = 2;
    ASSERT_EQ(buf.deserializeTo(first, n, Fw::Serialization::OMIT_LENGTH), Fw::FW_SERIALIZE_OK);
    n = 2;
    ASSERT_EQ(buf.deserializeTo(second, n, Fw::Serialization::OMIT_LENGTH), Fw::FW_SERIALIZE_OK);
    ASSERT_EQ(first[0], 10);
    ASSERT_EQ(second[0], 30);
    // Drained: a third read has nothing left.
    n = 1;
    ASSERT_EQ(buf.deserializeTo(first, n, Fw::Serialization::OMIT_LENGTH), Fw::FW_DESERIALIZE_SIZE_MISMATCH);
}

// ----------------------------------------------------------------------
// serializeFrom / deserializeTo (LinearBufferBase&)
// ----------------------------------------------------------------------

TEST(FwBulk, RoundTripsAnEmbeddedBuffer) {
    Buf<64> outer;
    Buf<16> inner;
    const U8 payload[] = {3, 1, 4, 1, 5};
    ASSERT_EQ(inner.serializeFrom(payload, sizeof(payload), Fw::Serialization::OMIT_LENGTH), Fw::FW_SERIALIZE_OK);
    ASSERT_EQ(outer.serializeFrom(inner), Fw::FW_SERIALIZE_OK);

    Buf<16> restored;
    ASSERT_EQ(outer.deserializeTo(restored), Fw::FW_SERIALIZE_OK);
    ASSERT_EQ(restored.getSize(), sizeof(payload));
    ASSERT_EQ(std::memcmp(restored.getBuffAddr(), payload, sizeof(payload)), 0);
}

TEST(FwBulk, EmbeddedBufferRefusesADestinationThatIsTooSmall) {
    Buf<64> outer;
    Buf<16> inner;
    const U8 payload[] = {1, 2, 3, 4, 5, 6, 7, 8};
    ASSERT_EQ(inner.serializeFrom(payload, sizeof(payload), Fw::Serialization::OMIT_LENGTH), Fw::FW_SERIALIZE_OK);
    ASSERT_EQ(outer.serializeFrom(inner), Fw::FW_SERIALIZE_OK);

    Buf<4> tooSmall;
    ASSERT_EQ(outer.deserializeTo(tooSmall), Fw::FW_DESERIALIZE_SIZE_MISMATCH);
}

TEST(FwBulk, EmbeddedSerializeRefusesWhenTokenAndBodyDoNotBothFit) {
    Buf<4> outer;
    Buf<16> inner;
    const U8 payload[] = {1, 2, 3, 4};
    ASSERT_EQ(inner.serializeFrom(payload, sizeof(payload), Fw::Serialization::OMIT_LENGTH), Fw::FW_SERIALIZE_OK);
    // The body alone would fit; the length token is what does not.
    ASSERT_EQ(outer.serializeFrom(inner), Fw::FW_SERIALIZE_NO_ROOM_LEFT);
    ASSERT_EQ(outer.getSize(), 0u);
}

// ----------------------------------------------------------------------
// setBuff, copyFrom, copyRaw, copyRawOffset
// ----------------------------------------------------------------------

TEST(FwBulk, SetBuffReplacesContentsAndResetsBothCursors) {
    Buf<16> buf;
    const U8 first[] = {1, 2, 3, 4, 5, 6};
    ASSERT_EQ(buf.serializeFrom(first, sizeof(first), Fw::Serialization::OMIT_LENGTH), Fw::FW_SERIALIZE_OK);
    U8 drain[3] = {};
    Fw::Serializable::SizeType n = 3;
    ASSERT_EQ(buf.deserializeTo(drain, n, Fw::Serialization::OMIT_LENGTH), Fw::FW_SERIALIZE_OK);

    const U8 second[] = {7, 8};
    ASSERT_EQ(buf.setBuff(second, sizeof(second)), Fw::FW_SERIALIZE_OK);
    ASSERT_EQ(buf.getSize(), sizeof(second));
    // The read cursor was rewound, so the whole new content is readable.
    ASSERT_EQ(buf.getDeserializeSizeLeft(), sizeof(second));
}

TEST(FwBulk, SetBuffRefusesMoreThanCapacity) {
    Buf<4> buf;
    const U8 payload[] = {1, 2, 3, 4, 5};
    ASSERT_EQ(buf.setBuff(payload, sizeof(payload)), Fw::FW_SERIALIZE_NO_ROOM_LEFT);
}

TEST(FwBulk, CopyFromTakesContentsAndBothCursors) {
    Buf<16> src;
    const U8 payload[] = {2, 4, 6, 8};
    ASSERT_EQ(src.serializeFrom(payload, sizeof(payload), Fw::Serialization::OMIT_LENGTH), Fw::FW_SERIALIZE_OK);
    U8 drain[2] = {};
    Fw::Serializable::SizeType n = 2;
    ASSERT_EQ(src.deserializeTo(drain, n, Fw::Serialization::OMIT_LENGTH), Fw::FW_SERIALIZE_OK);

    Buf<16> dst;
    dst = src;  // operator= delegates to copyFrom
    ASSERT_EQ(dst.getSize(), sizeof(payload));
    ASSERT_EQ(std::memcmp(dst.getBuffAddr(), payload, sizeof(payload)), 0);
    // The read cursor came across too, so the same two bytes remain.
    ASSERT_EQ(dst.getDeserializeSizeLeft(), sizeof(payload) - 2);
}

TEST(FwBulk, CopyRawMovesTheRemainderAndAdvancesTheSource) {
    Buf<32> src;
    const U8 payload[] = {1, 2, 3, 4, 5, 6};
    ASSERT_EQ(src.serializeFrom(payload, sizeof(payload), Fw::Serialization::OMIT_LENGTH), Fw::FW_SERIALIZE_OK);

    Buf<32> dst;
    ASSERT_EQ(src.copyRaw(dst, 4), Fw::FW_SERIALIZE_OK);
    ASSERT_EQ(dst.getSize(), 4u);
    ASSERT_EQ(std::memcmp(dst.getBuffAddr(), payload, 4), 0);
    ASSERT_EQ(src.getDeserializeSizeLeft(), 2u);
}

TEST(FwBulk, CopyRawRefusesMoreThanRemains) {
    Buf<32> src;
    const U8 payload[] = {1, 2};
    ASSERT_EQ(src.serializeFrom(payload, sizeof(payload), Fw::Serialization::OMIT_LENGTH), Fw::FW_SERIALIZE_OK);

    Buf<32> dst;
    ASSERT_EQ(src.copyRaw(dst, 4), Fw::FW_DESERIALIZE_SIZE_MISMATCH);
    ASSERT_EQ(src.getDeserializeSizeLeft(), 2u);
}

TEST(FwBulk, CopyRawOffsetAppendsWithoutALengthToken) {
    Buf<32> src;
    const U8 payload[] = {1, 2, 3, 4};
    ASSERT_EQ(src.serializeFrom(payload, sizeof(payload), Fw::Serialization::OMIT_LENGTH), Fw::FW_SERIALIZE_OK);

    Buf<32> dst;
    const U8 existing[] = {9};
    ASSERT_EQ(dst.serializeFrom(existing, sizeof(existing), Fw::Serialization::OMIT_LENGTH), Fw::FW_SERIALIZE_OK);
    ASSERT_EQ(src.copyRawOffset(dst, 3), Fw::FW_SERIALIZE_OK);
    // Appended after what was already there, no token in between.
    ASSERT_EQ(dst.getSize(), 1u + 3u);
    ASSERT_EQ(dst.getBuffAddr()[0], 9);
    ASSERT_EQ(dst.getBuffAddr()[1], 1);
}

TEST(FwBulk, CopyRawOffsetRefusesWhenTheDestinationCannotTakeIt) {
    Buf<32> src;
    const U8 payload[] = {1, 2, 3, 4};
    ASSERT_EQ(src.serializeFrom(payload, sizeof(payload), Fw::Serialization::OMIT_LENGTH), Fw::FW_SERIALIZE_OK);

    Buf<2> dst;
    ASSERT_EQ(src.copyRawOffset(dst, 4), Fw::FW_SERIALIZE_NO_ROOM_LEFT);
}

}  // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
