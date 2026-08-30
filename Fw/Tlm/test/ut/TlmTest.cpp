#include <gtest/gtest.h>
#include <Fw/Com/ComBuffer.hpp>
#include <Fw/Tlm/TlmPacket.hpp>

TEST(FwTlmTest, TlmPacketSerializeSingle) {
    // Serialize data

    Fw::TlmPacket pktIn;
    Fw::TlmBuffer buffIn;
    ASSERT_EQ(Fw::FW_SERIALIZE_OK, buffIn.serializeFrom(static_cast<U32>(12)));
    Fw::Time timeIn(TimeBase::TB_WORKSTATION_TIME, 10, 11);
    FwChanIdType id = 10;

    ASSERT_EQ(Fw::FW_SERIALIZE_OK, pktIn.resetPktSer());
    ASSERT_EQ(Fw::FW_SERIALIZE_OK, pktIn.addValue(id, timeIn, buffIn));

    Fw::ComBuffer comBuff = pktIn.getBuffer();

    // Deserialize data
    Fw::TlmPacket pktOut;
    Fw::TlmBuffer buffOut;
    Fw::Time timeOut(TimeBase::TB_WORKSTATION_TIME, 10, 11);

    pktOut.setBuffer(comBuff);
    ASSERT_EQ(Fw::FW_SERIALIZE_OK, pktOut.resetPktDeser());
    // extract values
    id = 0;
    ASSERT_EQ(Fw::FW_SERIALIZE_OK, pktOut.extractValue(id, timeOut, buffOut, sizeof(U32)));

    ASSERT_EQ(10u, id);
    U32 valOut = 0;
    buffOut.resetDeser();
    ASSERT_EQ(Fw::FW_SERIALIZE_OK, buffOut.deserializeTo(valOut));
    ASSERT_EQ(valOut, 12u);
}

TEST(FwTlmTest, TlmPacketDeserializeOversizeRejected) {
    // A payload larger than the telemetry buffer capacity must be rejected, not copied
    Fw::TlmPacket pktIn;
    Fw::TlmBuffer buffIn;
    ASSERT_EQ(Fw::FW_SERIALIZE_OK, buffIn.serializeFrom(static_cast<U32>(12)));
    ASSERT_EQ(Fw::FW_SERIALIZE_OK, pktIn.resetPktSer());
    Fw::Time timeIn(TimeBase::TB_WORKSTATION_TIME, 10, 11);
    ASSERT_EQ(Fw::FW_SERIALIZE_OK, pktIn.addValue(10, timeIn, buffIn));

    U8 data[FW_COM_BUFFER_MAX_SIZE + 128];
    Fw::ExternalSerializeBuffer bigBuff(data, sizeof(data));
    ASSERT_EQ(Fw::FW_SERIALIZE_OK, bigBuff.serializeFrom(pktIn));
    // Append filler so the remaining payload exceeds the TlmBuffer capacity
    U8 filler[FW_TLM_BUFFER_MAX_SIZE] = {};
    ASSERT_EQ(Fw::FW_SERIALIZE_OK, bigBuff.serializeFrom(filler, sizeof(filler), Fw::Serialization::OMIT_LENGTH));

    Fw::TlmPacket pktOut;
    ASSERT_EQ(Fw::FW_DESERIALIZE_SIZE_MISMATCH, bigBuff.deserializeTo(pktOut));
}

// TlmPacket::addValue admits a value when `needed > left` is false, so a value
// that exactly fills the packet is accepted. TlmPacketSerializeFill does not
// distinguish `>` from `>=`: its entries are a fixed size, so it stops with a
// leftover equal to (capacity - descriptor) mod entry-size, and the boundary
// case arises only when that happens to be zero. With this repository's
// defaults it is 16 against a 19-byte stride, so it never arises.
//
// Rather than fill and hope the remainder lands on the boundary -- which
// depends on FW_COM_BUFFER_MAX_SIZE, FwChanIdType and Time::SERIALIZED_SIZE,
// all of which a deployment retunes -- these drive the packet to an exact
// amount of free space directly. That holds for any configuration, so neither
// test can quietly become vacuous.
namespace {

// Leave the packet with exactly `left` bytes free.
void setRemaining(Fw::TlmPacket& packet, FwSizeType left) {
    Fw::ComBuffer com;
    ASSERT_LE(left, com.getCapacity());
    // The descriptor first, so the buffer still looks like a telemetry packet;
    // addValue does not inspect it, but a nonsense packet would be a poor
    // thing to leave behind for whoever reads this next.
    ASSERT_EQ(Fw::FW_SERIALIZE_OK,
              com.serializeFrom(static_cast<FwPacketDescriptorType>(Fw::ComPacketType::FW_PACKET_TELEM)));
    for (FwSizeType i = com.getSize(); i < com.getCapacity() - left; i++) {
        ASSERT_EQ(Fw::FW_SERIALIZE_OK, com.serializeFrom(static_cast<U8>(i)));
    }
    packet.setBuffer(com);
    ASSERT_EQ(packet.getBuffer().getCapacity() - packet.getBuffer().getSize(), left);
}

// A value buffer holding exactly `bytes` bytes of content.
Fw::TlmBuffer valueOfSize(FwSizeType bytes) {
    Fw::TlmBuffer buffer;
    EXPECT_LE(bytes, buffer.getCapacity());
    for (FwSizeType i = 0; i < bytes; i++) {
        EXPECT_EQ(Fw::FW_SERIALIZE_OK, buffer.serializeFrom(static_cast<U8>(i)));
    }
    return buffer;
}

const FwSizeType ENTRY_OVERHEAD = sizeof(FwChanIdType) + Fw::Time::SERIALIZED_SIZE;
const FwSizeType VALUE_BYTES = 4;

}  // namespace

TEST(FwTlmTest, TlmPacketAddValueExactFillIsAccepted) {
    Fw::TlmPacket pktIn;
    // needed == left exactly. `>` accepts this; `>=` would not.
    setRemaining(pktIn, ENTRY_OVERHEAD + VALUE_BYTES);
    Fw::TlmBuffer exact = valueOfSize(VALUE_BYTES);
    Fw::Time timeIn(TimeBase::TB_WORKSTATION_TIME, 3, 4);

    ASSERT_EQ(Fw::FW_SERIALIZE_OK, pktIn.addValue(9, timeIn, exact));
    ASSERT_EQ(pktIn.getBuffer().getSize(), pktIn.getBuffer().getCapacity()) << "the packet is now exactly full";
}

TEST(FwTlmTest, TlmPacketAddValueOneByteOverIsRejected) {
    Fw::TlmPacket pktIn;
    // needed == left + 1: the first size that must be refused.
    setRemaining(pktIn, ENTRY_OVERHEAD + VALUE_BYTES);
    const FwSizeType before = pktIn.getBuffer().getSize();
    Fw::TlmBuffer oversize = valueOfSize(VALUE_BYTES + 1);
    Fw::Time timeIn(TimeBase::TB_WORKSTATION_TIME, 3, 4);

    ASSERT_EQ(Fw::FW_SERIALIZE_NO_ROOM_LEFT, pktIn.addValue(9, timeIn, oversize));
    ASSERT_EQ(pktIn.getBuffer().getSize(), before) << "a refused value must not partially serialize";
}

TEST(FwTlmTest, TlmPacketSerializeFill) {
    // compute a single entry size assuming for the test that the value of the telemetry channel
    // is a U32
    static const FwSizeType SIZE_OF_ENTRY = sizeof(FwChanIdType) + Fw::Time::SERIALIZED_SIZE + sizeof(U32);

    // compute the number of entries that should fit - will equal rounded down value of
    // ComBuffer size - size of telemetry packet id / size of an entry
    static const FwSizeType NUM_ENTRIES = (FW_COM_BUFFER_MAX_SIZE - sizeof(FwPacketDescriptorType)) / SIZE_OF_ENTRY;

    Fw::TlmPacket pktIn;
    ASSERT_EQ(Fw::FW_SERIALIZE_OK, pktIn.resetPktSer());

    // fill a telemetry packet

    for (U32 entry = 0; entry < NUM_ENTRIES; entry++) {
        // Serialize data

        Fw::TlmBuffer buffIn;
        ASSERT_EQ(Fw::FW_SERIALIZE_OK, buffIn.serializeFrom(static_cast<U32>(entry)));
        Fw::Time timeIn(TimeBase::TB_WORKSTATION_TIME, entry + 1, entry + 2);
        FwChanIdType id = static_cast<FwChanIdType>(NUM_ENTRIES - entry);

        ASSERT_EQ(Fw::FW_SERIALIZE_OK, pktIn.addValue(id, timeIn, buffIn));
    }

    // Next one should fail because it's full
    {
        Fw::TlmBuffer buffIn;
        ASSERT_EQ(Fw::FW_SERIALIZE_OK, buffIn.serializeFrom(static_cast<U32>(12)));
        Fw::Time timeIn(TimeBase::TB_WORKSTATION_TIME, 10, 11);
        FwChanIdType id = 10;

        ASSERT_EQ(Fw::FW_SERIALIZE_NO_ROOM_LEFT, pktIn.addValue(id, timeIn, buffIn));
    }

    // Create a new packet from the ComBuffer

    Fw::ComBuffer comBuff = pktIn.getBuffer();
    Fw::TlmPacket pktOut;
    pktOut.setBuffer(comBuff);
    ASSERT_EQ(Fw::FW_SERIALIZE_OK, pktOut.resetPktDeser());

    // empty the packet of entries
    for (U32 entry = 0; entry < NUM_ENTRIES; entry++) {
        // Deserialize data
        Fw::TlmBuffer buffOut;
        Fw::Time timeOut;
        FwChanIdType id = 0;
        ASSERT_EQ(Fw::FW_SERIALIZE_OK, pktOut.extractValue(id, timeOut, buffOut, sizeof(U32)));
        ASSERT_EQ(NUM_ENTRIES - entry, id);
        Fw::Time expTime(TimeBase::TB_WORKSTATION_TIME, entry + 1, entry + 2);
        ASSERT_EQ(expTime, timeOut);
        U32 val = 0;
        ASSERT_EQ(Fw::FW_SERIALIZE_OK, buffOut.deserializeTo(val));
        ASSERT_EQ(entry, val);
    }

    // try to extract one more, should fail
    {
        Fw::TlmBuffer buffOut;
        Fw::Time timeOut;
        FwChanIdType id = 0;
        ASSERT_EQ(Fw::FW_DESERIALIZE_BUFFER_EMPTY, pktOut.extractValue(id, timeOut, buffOut, sizeof(U32)));
    }
}

int main(int argc, char* argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
