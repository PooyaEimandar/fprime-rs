// ----------------------------------------------------------------------
// Main.cpp
// ----------------------------------------------------------------------

#include "gtest/gtest.h"

#include "CFDP/Checksum/Checksum.hpp"

using namespace CFDP;

const U8 data[] = {0, 1, 2, 3, 4, 5, 6, 7, 8};

const U32 expectedValue = (data[0] << 3 * 8) + (data[1] << 2 * 8) + (data[2] << 1 * 8) + data[3] + (data[4] << 3 * 8) +
                          (data[5] << 2 * 8) + (data[6] << 1 * 8) + data[7];

TEST(Checksum, OnePacket) {
    Checksum checksum;
    checksum.update(data, 0, 8);
    ASSERT_EQ(expectedValue, checksum.getValue());
}

TEST(Checksum, TwoPacketsAligned) {
    Checksum checksum;
    checksum.update(&data[0], 0, 4);
    checksum.update(&data[4], 4, 4);
    ASSERT_EQ(expectedValue, checksum.getValue());
}

TEST(Checksum, TwoPacketsUnaligned1) {
    Checksum checksum;
    checksum.update(&data[0], 0, 3);
    checksum.update(&data[3], 3, 5);
    ASSERT_EQ(expectedValue, checksum.getValue());
}

TEST(Checksum, TwoPacketsUnaligned2) {
    Checksum checksum;
    checksum.update(&data[0], 0, 5);
    checksum.update(&data[5], 5, 3);
    ASSERT_EQ(expectedValue, checksum.getValue());
}

TEST(Checksum, ThreePackets) {
    Checksum checksum;
    checksum.update(&data[0], 0, 2);
    checksum.update(&data[2], 2, 3);
    checksum.update(&data[5], 5, 3);
    ASSERT_EQ(expectedValue, checksum.getValue());
}

TEST(Checksum, ByteAtATime) {
    // Any partition of the data must sum identically, down to single bytes
    Checksum checksum;
    for (U32 i = 0; i < 8; i++) {
        checksum.update(&data[i], i, 1);
    }
    ASSERT_EQ(expectedValue, checksum.getValue());
}

TEST(Checksum, OffsetDeterminesBytePosition) {
    // The same byte contributes differently depending on its file offset:
    // offset 0 is the most significant byte of a word, offset 3 the least
    Checksum atZero;
    atZero.update(&data[1], 0, 1);  // data[1] == 1
    ASSERT_EQ(static_cast<U32>(1) << 24, atZero.getValue());

    Checksum atThree;
    atThree.update(&data[1], 3, 1);
    ASSERT_EQ(static_cast<U32>(1), atThree.getValue());
}

TEST(Checksum, WrappingAddition) {
    // The CFDP checksum is a modular 32-bit sum: overflow wraps silently
    const U8 ones[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    Checksum checksum(0xFFFFFFFFu);
    checksum.update(ones, 0, 4);
    ASSERT_EQ(0xFFFFFFFEu, checksum.getValue());
}

TEST(Checksum, ShortFirstWordAtUnalignedOffset) {
    // length smaller than the distance to the next word boundary
    Checksum checksum;
    checksum.update(&data[1], 1, 1);  // one byte at offset 1: position 1 of a word
    ASSERT_EQ(static_cast<U32>(data[1]) << 16, checksum.getValue());
}

TEST(Checksum, ConstructorsAndComparison) {
    Checksum a(42);
    Checksum b(a);
    Checksum c;
    ASSERT_EQ(a.getValue(), 42u);
    ASSERT_TRUE(a == b);
    ASSERT_TRUE(a != c);
    c = a;
    ASSERT_TRUE(a == c);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
