//
// Created by mstarch on 11/13/20.
//
#include <gtest/gtest.h>
#include <Fw/FPrimeBasicTypes.hpp>
#include "Fw/Buffer/Buffer.hpp"
#include "Fw/Types/test/ut/LinearBufferBaseTester.hpp"

namespace Fw {
class BufferTester {
  public:
    // ----------------------------------------------------------------------
    // Construction and destruction
    // ----------------------------------------------------------------------
    BufferTester() {}

    ~BufferTester() {}

    // ----------------------------------------------------------------------
    // Tests
    // ----------------------------------------------------------------------
    void test_basic() {
        U8 data[100];
        U8 faux[100];
        Fw::Buffer buffer;
        // Check basic guarantees
        ASSERT_EQ(buffer.m_context, Fw::Buffer::NO_CONTEXT);
        buffer.set(data, sizeof(data));
        buffer.setContext(1234);
        ASSERT_EQ(buffer.getData(), data);
        ASSERT_EQ(buffer.getOriginalData(), data);
        ASSERT_EQ(buffer.getSize(), sizeof(data));
        ASSERT_EQ(buffer.getCapacity(), sizeof(data));
        ASSERT_EQ(buffer.getOffset(), 0);
        ASSERT_EQ(buffer.getContext(), 1234);

        // Test set method is equivalent
        Fw::Buffer buffer_set;
        buffer_set.set(data, sizeof(data), 1234);
        ASSERT_EQ(buffer_set, buffer);

        // Check constructors and assignments
        Fw::Buffer buffer_new(buffer);
        ASSERT_EQ(buffer_new.getData(), data);
        ASSERT_EQ(buffer_new.getSize(), sizeof(data));
        ASSERT_EQ(buffer_new.getContext(), 1234);
        ASSERT_EQ(buffer, buffer_new);

        // Creating empty buffer
        Fw::Buffer testBuffer(nullptr, 0);
        ASSERT_EQ(testBuffer.getData(), nullptr);
        ASSERT_EQ(testBuffer.getSize(), 0);

        // Assignment operator with transitivity
        Fw::Buffer buffer_assignment1, buffer_assignment2;
        ASSERT_NE(buffer_assignment1.getData(), data);
        ASSERT_NE(buffer_assignment1.getSize(), sizeof(data));
        ASSERT_NE(buffer_assignment1.getContext(), 1234);
        ASSERT_NE(buffer_assignment2.getData(), data);
        ASSERT_NE(buffer_assignment2.getSize(), sizeof(data));
        ASSERT_NE(buffer_assignment2.getContext(), 1234);
        buffer_assignment1 = buffer_assignment2 = buffer;
        ASSERT_EQ(buffer_assignment1.getData(), data);
        ASSERT_EQ(buffer_assignment1.getSize(), sizeof(data));
        ASSERT_EQ(buffer_assignment1.getContext(), 1234);
        ASSERT_EQ(buffer_assignment2.getData(), data);
        ASSERT_EQ(buffer_assignment2.getSize(), sizeof(data));
        ASSERT_EQ(buffer_assignment2.getContext(), 1234);

        // Check modifying the copies does not destroy
        buffer_new.set(faux, 0);
        buffer_new.setContext(22222);
        buffer_assignment1.set(faux, 0);
        buffer_assignment1.setContext(22222);
        buffer_assignment2.set(faux, 0);
        buffer_assignment2.setContext(22222);

        ASSERT_EQ(buffer.getData(), data);
        ASSERT_EQ(buffer.getSize(), sizeof(data));
        ASSERT_EQ(buffer.getContext(), 1234);
    }

    void test_advance() {
        U8 data[100];
        Fw::Buffer buffer(data, sizeof(data), 1234);

        // Advance forward: offset grows, size shrinks, end fixed, original recoverable
        buffer.advance(10);
        ASSERT_EQ(buffer.getData(), data + 10);
        ASSERT_EQ(buffer.getOriginalData(), data);
        ASSERT_EQ(buffer.getOffset(), 10);
        ASSERT_EQ(buffer.getSize(), sizeof(data) - 10);
        ASSERT_EQ(buffer.getCapacity(), sizeof(data));

        // Advance backward restores
        buffer.advance(-10);
        ASSERT_EQ(buffer.getData(), data);
        ASSERT_EQ(buffer.getOffset(), 0);
        ASSERT_EQ(buffer.getSize(), sizeof(data));

        // setData within the original allocation updates the offset
        buffer.setData(data + 25);
        ASSERT_EQ(buffer.getData(), data + 25);
        ASSERT_EQ(buffer.getOriginalData(), data);
        ASSERT_EQ(buffer.getOffset(), 25);
        ASSERT_EQ(buffer.getSize(), sizeof(data) - 25);

        // setSize is checked against offset + capacity
        buffer.setSize(sizeof(data) - 25);
        ASSERT_EQ(buffer.getSize(), sizeof(data) - 25);

        // Copies preserve offset and capacity
        Fw::Buffer copy(buffer);
        ASSERT_EQ(copy.getOriginalData(), data);
        ASSERT_EQ(copy.getOffset(), 25);
        ASSERT_EQ(copy.getCapacity(), sizeof(data));
        ASSERT_EQ(copy, buffer);

        // Out-of-bounds operations assert
        U8* unrelated = new U8[100];
        ASSERT_DEATH_IF_SUPPORTED(buffer.setData(unrelated), "");
        delete[] unrelated;
        ASSERT_DEATH_IF_SUPPORTED(buffer.setSize(sizeof(data) - 25 + 1), "");
        ASSERT_DEATH_IF_SUPPORTED(buffer.advance(-26), "");
        ASSERT_DEATH_IF_SUPPORTED(buffer.advance(static_cast<FwSignedSizeType>(sizeof(data))), "");
    }

    void test_equality() {
        // The rest of this suite only ever asserts that equal buffers compare
        // EQUAL, so a constant-true operator== passes all of it. Each case
        // below must compare UNEQUAL, which is what actually pins the
        // operator.
        U8 data[100];
        U8 other[100];

        const Fw::Buffer base(data, sizeof(data), 1234);
        const Fw::Buffer same(data, sizeof(data), 1234);
        ASSERT_EQ(base, same);

        const Fw::Buffer elsewhere(other, sizeof(other), 1234);
        ASSERT_FALSE(base == elsewhere);
        const Fw::Buffer otherContext(data, sizeof(data), 5678);
        ASSERT_FALSE(base == otherContext);
        // Size ALONE. Constructing with a different size would also change
        // the capacity, since the constructor sets one from the other, so
        // both must start from the same allocation and be trimmed.
        Fw::Buffer half(data, sizeof(data), 1234);
        half.setSize(50);
        Fw::Buffer threeFifths(data, sizeof(data), 1234);
        threeFifths.setSize(60);
        ASSERT_EQ(half.getCapacity(), threeFifths.getCapacity());
        ASSERT_EQ(half.getOffset(), threeFifths.getOffset());
        ASSERT_NE(half.getSize(), threeFifths.getSize());
        ASSERT_FALSE(half == threeFifths);

        // Offset. This is the member the header used to omit -- it described
        // equality as pointer, size and context, and the implementation has
        // always also compared offset and capacity. The comment is corrected
        // now; this pins the behaviour so the two cannot drift apart again,
        // and it is what a reimplementation written from the old comment
        // would have got wrong.
        Fw::Buffer advanced(data, sizeof(data), 1234);
        advanced.advance(1);
        Fw::Buffer trimmed(data, sizeof(data), 1234);
        trimmed.setSize(sizeof(data) - 1);
        ASSERT_EQ(advanced.getSize(), trimmed.getSize());
        ASSERT_EQ(advanced.getCapacity(), trimmed.getCapacity());
        ASSERT_NE(advanced.getOffset(), trimmed.getOffset());
        ASSERT_FALSE(advanced == trimmed);

        // Capacity, with every other field equal.
        Fw::Buffer wide(data, sizeof(data), 1234);
        wide.setSize(50);
        const Fw::Buffer narrow(data, 50, 1234);
        ASSERT_EQ(wide.getSize(), narrow.getSize());
        ASSERT_EQ(wide.getOffset(), narrow.getOffset());
        ASSERT_NE(wide.getCapacity(), narrow.getCapacity());
        ASSERT_FALSE(wide == narrow);
    }

    void test_partial_extent() {
        // Every other buffer this suite builds satisfies
        // offset + size == capacity, which hides both behaviours below.
        U8 data[100];
        Fw::Buffer buffer(data, sizeof(data), 1234);
        buffer.setSize(50);
        ASSERT_EQ(buffer.getOffset(), 0);
        ASSERT_EQ(buffer.getSize(), 50);
        ASSERT_EQ(buffer.getCapacity(), sizeof(data));

        // setData bounds the new pointer against CAPACITY and then delegates
        // to advance, which bounds the resulting delta against SIZE. Those
        // two bounds differ exactly here: data + 60 is inside the allocation
        // and setData's own guard accepts it, but it is past the represented
        // extent, so advance rejects it. The tighter bound wins silently.
        ASSERT_DEATH_IF_SUPPORTED(buffer.setData(data + 60), "");

        // At the end of the represented extent it is accepted, and leaves a
        // buffer that is not valid even though its data pointer is not null:
        // getData() and isValid() disagree about a zero-size buffer.
        buffer.setData(data + 50);
        ASSERT_EQ(buffer.getOffset(), 50);
        ASSERT_EQ(buffer.getSize(), 0);
        ASSERT_EQ(buffer.getData(), data + 50);
        ASSERT_FALSE(buffer.isValid());
    }

    void test_wire_format() {
        // Fw::Buffer::SERIALIZED_SIZE is load-bearing outside this module --
        // Svc/ComQueue sizes its queue entries from it -- and the field order
        // on the wire (data, size, context, offset, capacity) is NOT the
        // declaration order in Buffer.hpp. Until now both were pinned only by
        // a round trip through this same pair of functions, which cannot fail
        // if serializeTo and deserializeFrom are changed together.
        U8 data[100];
        U8 wire[Fw::Buffer::SERIALIZED_SIZE];

        Fw::Buffer buffer(data, sizeof(data), 0xABCD1234);
        buffer.advance(7);
        buffer.setSize(11);

        Fw::ExternalSerializeBuffer out(wire, sizeof(wire));
        ASSERT_EQ(out.serializeFrom(buffer), Fw::FW_SERIALIZE_OK);
        ASSERT_EQ(out.getSize(), static_cast<FwSizeType>(Fw::Buffer::SERIALIZED_SIZE));

        // Read the five fields back individually, in the order the wire
        // actually carries them.
        Fw::ExternalSerializeBuffer in(wire, sizeof(wire));
        ASSERT_EQ(in.setBuffLen(sizeof(wire)), Fw::FW_SERIALIZE_OK);
        PlatformPointerCastType pointer = 0;
        FwSizeType size = 0;
        U32 context = 0;
        FwSizeType offset = 0;
        FwSizeType capacity = 0;
        ASSERT_EQ(in.deserializeTo(pointer), Fw::FW_SERIALIZE_OK);
        ASSERT_EQ(in.deserializeTo(size), Fw::FW_SERIALIZE_OK);
        ASSERT_EQ(in.deserializeTo(context), Fw::FW_SERIALIZE_OK);
        ASSERT_EQ(in.deserializeTo(offset), Fw::FW_SERIALIZE_OK);
        ASSERT_EQ(in.deserializeTo(capacity), Fw::FW_SERIALIZE_OK);

        ASSERT_EQ(reinterpret_cast<U8*>(pointer), data);
        ASSERT_EQ(size, 11);
        ASSERT_EQ(context, 0xABCD1234u);
        ASSERT_EQ(offset, 7);
        ASSERT_EQ(capacity, sizeof(data));
        // Nothing left over: those five fields are the whole encoding.
        ASSERT_EQ(in.getDeserializeSizeLeft(), 0);
    }

    void test_representations() {
        U8 data[100];
        Fw::Buffer buffer(data, sizeof(data), 1234);

        // Test serialization and that it stops before overflowing
        auto serializer = buffer.getSerializer();
        for (U32 i = 0; i < sizeof(data) / 4; i++) {
            ASSERT_EQ(serializer.serializeFrom(i), Fw::FW_SERIALIZE_OK);
        }
        Fw::SerializeStatus stat = serializer.serializeFrom(100);
        ASSERT_NE(stat, Fw::FW_SERIALIZE_OK);

        // And that another call to repr resets it
        serializer.resetSer();
        ASSERT_EQ(serializer.serializeFrom(0), Fw::FW_SERIALIZE_OK);

        // Now deserialize all the things
        auto deserializer = buffer.getDeserializer();
        U32 out;
        for (U32 i = 0; i < sizeof(data) / 4; i++) {
            ASSERT_EQ(deserializer.deserializeTo(out), Fw::FW_SERIALIZE_OK);
            ASSERT_EQ(i, out);
        }
        ASSERT_NE(deserializer.deserializeTo(out), Fw::FW_SERIALIZE_OK);
        deserializer.setBuffLen(buffer.getSize());
        ASSERT_EQ(deserializer.deserializeTo(out), Fw::FW_SERIALIZE_OK);
        ASSERT_EQ(0, out);
    }

    void test_serialization() {
        U8 data[100];
        U8 wire[100];

        Fw::Buffer buffer(data, sizeof(data), 1234);
        buffer.advance(7);

        Fw::ExternalSerializeBuffer externalSerializeBuffer(wire, sizeof(wire));
        externalSerializeBuffer.serializeFrom(buffer);
        Fw::LinearBufferBaseTester::verifySerLocLT(externalSerializeBuffer, sizeof(data));

        Fw::Buffer buffer_new;
        externalSerializeBuffer.deserializeTo(buffer_new);
        ASSERT_EQ(buffer_new, buffer);
        ASSERT_EQ(buffer_new.getOriginalData(), data);
        ASSERT_EQ(buffer_new.getData(), data + 7);
        ASSERT_EQ(buffer_new.getOffset(), 7);
        ASSERT_EQ(buffer_new.getCapacity(), sizeof(data));
    }
};
}  // namespace Fw

TEST(Nominal, BasicBuffer) {
    Fw::BufferTester tester;
    tester.test_basic();
}

TEST(Nominal, Advance) {
    Fw::BufferTester tester;
    tester.test_advance();
}

TEST(Nominal, Equality) {
    Fw::BufferTester tester;
    tester.test_equality();
}

TEST(Nominal, PartialExtent) {
    Fw::BufferTester tester;
    tester.test_partial_extent();
}

TEST(Nominal, WireFormat) {
    Fw::BufferTester tester;
    tester.test_wire_format();
}

TEST(Nominal, Representations) {
    Fw::BufferTester tester;
    tester.test_representations();
}

TEST(Nominal, Serialization) {
    Fw::BufferTester tester;
    tester.test_serialization();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
