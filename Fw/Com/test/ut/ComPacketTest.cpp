// ======================================================================
// \title  ComPacketTest.cpp
// \brief  Unit tests for Fw::ComPacket's descriptor field
//
// Fw/Com had no test directory. That is a gap worth naming: every packet
// the framework exchanges begins with the descriptor these two functions
// write and read, and on an uplink that descriptor is attacker-controlled.
// The derived packet suites (Fw/Cmd, Fw/Tlm, Fw/Log) all exercise the base
// through their own round trips, but none of them ever feeds it a value
// that is not a valid Apid, which is the only interesting input.
// ======================================================================
#include <gtest/gtest.h>
#include <Fw/Com/ComBuffer.hpp>
#include <Fw/Com/ComPacket.hpp>
#include <Fw/FPrimeBasicTypes.hpp>

namespace Fw {

// serializeBase/deserializeBase are protected, and ComPacket is abstract
// because it leaves Serializable's two pure virtuals alone. This is the
// smallest concrete subclass that reaches them.
class TestPacket : public ComPacket {
  public:
    SerializeStatus serializeTo(SerialBufferBase& buffer, Endianness mode = Endianness::BIG) const override {
        (void)mode;
        return this->serializeBase(buffer);
    }
    SerializeStatus deserializeFrom(SerialBufferBase& buffer, Endianness mode = Endianness::BIG) override {
        (void)mode;
        return this->deserializeBase(buffer);
    }
    ComPacketType type() const { return this->m_type; }
    void setType(ComPacketType type) { this->m_type = type; }
};

// A packet starts as FW_PACKET_UNKNOWN rather than as the enum's own default.
TEST(ComPacket, DefaultsToUnknown) {
    TestPacket packet;
    ASSERT_EQ(packet.type(), ComPacketType::FW_PACKET_UNKNOWN);
}

// Every descriptor the configuration defines survives a round trip, and
// occupies exactly FwPacketDescriptorType bytes on the wire.
//
// The valid set is derived from the generated enum rather than listed here.
// config/ComCfg.fpp is a *default* that a deployment replaces, so a hardcoded
// list would be testing this repository's configuration rather than the code,
// and would fail for any project that adds an APID.
TEST(ComPacket, EveryValidDescriptorRoundTrips) {
    FwSizeType checked = 0;
    for (U32 candidate = 0; candidate <= 0xFFFF; candidate++) {
        const FwPacketDescriptorType descriptor = static_cast<FwPacketDescriptorType>(candidate);
        if (!ComCfg::Apid::isValid(descriptor)) {
            continue;
        }
        checked++;
        ComBuffer wire;
        TestPacket out;
        out.setType(static_cast<ComPacketType>(descriptor));
        ASSERT_EQ(out.serializeTo(wire), FW_SERIALIZE_OK);
        ASSERT_EQ(wire.getSize(), static_cast<FwSizeType>(sizeof(FwPacketDescriptorType)));

        TestPacket in;
        ASSERT_EQ(in.deserializeFrom(wire), FW_SERIALIZE_OK) << "descriptor " << descriptor << " was rejected";
        ASSERT_EQ(static_cast<FwPacketDescriptorType>(in.type()), descriptor);
    }
    ASSERT_GT(checked, 0u) << "no valid descriptor found; the test is not exercising anything";
}

// The case that motivated this file. A descriptor that is not a configured
// APID must be rejected rather than cast: ComPacketType is a plain enum, so a
// value outside its range is undefined behaviour, and the value arrives from
// the wire.
//
// This scans the WHOLE descriptor space rather than a sample. A sample taken
// from one end would leave the guard's predicate unpinned -- narrowing it to,
// say, `serVal > 0x0FFF` would still reject everything a top-of-range sample
// looks at, while quietly re-admitting the thousands of in-range values that
// are not enumerators. Those are the values a real uplink is most likely to
// carry.
TEST(ComPacket, EveryInvalidDescriptorIsRejected) {
    FwSizeType checked = 0;
    for (U32 candidate = 0; candidate <= 0xFFFF; candidate++) {
        const FwPacketDescriptorType descriptor = static_cast<FwPacketDescriptorType>(candidate);
        if (ComCfg::Apid::isValid(descriptor)) {
            continue;
        }
        ComBuffer wire;
        ASSERT_EQ(wire.serializeFrom(descriptor), FW_SERIALIZE_OK);

        TestPacket in;
        in.setType(ComPacketType::FW_PACKET_TELEM);
        ASSERT_EQ(in.deserializeFrom(wire), FW_DESERIALIZE_TYPE_MISMATCH)
            << "descriptor " << descriptor << " was accepted";
        // Rejection leaves the packet's own type alone rather than half-set.
        ASSERT_EQ(in.type(), ComPacketType::FW_PACKET_TELEM);
        checked++;
    }
    ASSERT_GT(checked, 0u) << "every descriptor is valid; there is nothing to reject";
}

// A buffer too short to hold a whole descriptor fails on length, and the
// failure comes back from the buffer rather than being masked by the
// validity check added above.
TEST(ComPacket, ATruncatedDescriptorFailsOnLength) {
    ComBuffer wire;
    TestPacket in;
    in.setType(ComPacketType::FW_PACKET_LOG);
    ASSERT_NE(in.deserializeFrom(wire), FW_SERIALIZE_OK);
    ASSERT_EQ(in.type(), ComPacketType::FW_PACKET_LOG);
}

}  // namespace Fw

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
