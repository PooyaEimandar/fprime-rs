// ======================================================================
// \title  RustGeneratedTypesTest.cpp
// \brief  Differential test: fpp-to-rust GENERATED types vs C++ autocode
//
// This is the Phase 4 acceptance proof. Both sides are generated from the
// SAME FPP model (Svc/FprimeProtocol/FprimeProtocol.fpp) by different
// generators — fpp-to-cpp for C++, tools/fprime-rs/fpp_to_rust.py for Rust.
// Every case serializes the same values through both and requires identical
// bytes and identical statuses, then cross-deserializes.
//
// If the Rust generator ever drifts from the FPP wire format, this fails.
// ======================================================================

#include <gtest/gtest.h>

#include <cstring>

#include <Fw/FPrimeBasicTypes.hpp>
#include <Fw/Types/Serializable.hpp>

#include "Svc/FprimeProtocol/FrameHeaderSerializableAc.hpp"
#include "Svc/FprimeProtocol/FrameTrailerSerializableAc.hpp"

#include "Rust/RustGenerated.h"

namespace {

constexpr FwSizeType CAPACITY = 64;

const Fw::Endianness MODES[] = {Fw::Endianness::BIG, Fw::Endianness::LITTLE};

int32_t modeCode(Fw::Endianness mode) {
    return (mode == Fw::Endianness::BIG) ? 0 : 1;
}

}  // namespace

TEST(RustGeneratedTypes, FrameHeaderMatchesAutocode) {
    const U32 cases[][2] = {
        {0xdeadbeefU, 0U},           // the protocol default start word, empty body
        {0xdeadbeefU, 0x00000010U},  // typical frame
        {0x00000000U, 0xFFFFFFFFU},  // extremes
        {0x12345678U, 0x9ABCDEF0U},  // arbitrary, catches byte-order errors
    };

    for (Fw::Endianness mode : MODES) {
        for (const auto& values : cases) {
            // C++ side: the FPP-autocoded type
            Svc::FprimeProtocol::FrameHeader cppHeader;
            cppHeader.set_startWord(values[0]);
            cppHeader.set_lengthField(values[1]);
            U8 cppBytes[CAPACITY] = {0};
            Fw::ExternalSerializeBuffer cppBuf(cppBytes, CAPACITY);
            ASSERT_EQ(cppBuf.serializeFrom(cppHeader, mode), Fw::FW_SERIALIZE_OK);

            // Rust side: the fpp-to-rust generated type
            uint8_t rustBytes[CAPACITY] = {0};
            FwSizeType written = 0;
            ASSERT_EQ(0, fprime_rs_gen_frameheader_ser(values[0], values[1], modeCode(mode), rustBytes, CAPACITY,
                                                       &written));

            ASSERT_EQ(written, static_cast<FwSizeType>(cppBuf.getSize()));
            ASSERT_EQ(written, static_cast<FwSizeType>(Svc::FprimeProtocol::FrameHeader::SERIALIZED_SIZE));
            ASSERT_EQ(0, memcmp(cppBytes, rustBytes, static_cast<size_t>(written)))
                << "byte mismatch for startWord=" << values[0] << " lengthField=" << values[1]
                << " mode=" << modeCode(mode);

            // Rust deserializes the C++ bytes back to the same values
            uint32_t startWord = 0;
            uint32_t lengthField = 0;
            ASSERT_EQ(0, fprime_rs_gen_frameheader_deser(cppBytes, written, modeCode(mode), &startWord, &lengthField));
            ASSERT_EQ(startWord, values[0]);
            ASSERT_EQ(lengthField, values[1]);

            // C++ deserializes the Rust bytes back to the same values
            Fw::ExternalSerializeBuffer readBuf(rustBytes, CAPACITY);
            ASSERT_EQ(readBuf.setBuffLen(static_cast<Fw::Serializable::SizeType>(written)), Fw::FW_SERIALIZE_OK);
            Svc::FprimeProtocol::FrameHeader roundTrip;
            ASSERT_EQ(readBuf.deserializeTo(roundTrip, mode), Fw::FW_SERIALIZE_OK);
            ASSERT_EQ(roundTrip.get_startWord(), values[0]);
            ASSERT_EQ(roundTrip.get_lengthField(), values[1]);
        }
    }
}

TEST(RustGeneratedTypes, FrameTrailerMatchesAutocode) {
    const U32 cases[] = {0U, 1U, 0xCBF43926U, 0xFFFFFFFFU};

    for (Fw::Endianness mode : MODES) {
        for (U32 crc : cases) {
            Svc::FprimeProtocol::FrameTrailer cppTrailer;
            cppTrailer.set_crcField(crc);
            U8 cppBytes[CAPACITY] = {0};
            Fw::ExternalSerializeBuffer cppBuf(cppBytes, CAPACITY);
            ASSERT_EQ(cppBuf.serializeFrom(cppTrailer, mode), Fw::FW_SERIALIZE_OK);

            uint8_t rustBytes[CAPACITY] = {0};
            FwSizeType written = 0;
            ASSERT_EQ(0, fprime_rs_gen_frametrailer_ser(crc, modeCode(mode), rustBytes, CAPACITY, &written));

            ASSERT_EQ(written, static_cast<FwSizeType>(cppBuf.getSize()));
            ASSERT_EQ(written, static_cast<FwSizeType>(Svc::FprimeProtocol::FrameTrailer::SERIALIZED_SIZE));
            ASSERT_EQ(0, memcmp(cppBytes, rustBytes, static_cast<size_t>(written)))
                << "byte mismatch for crc=" << crc << " mode=" << modeCode(mode);
        }
    }
}

TEST(RustGeneratedTypes, GeneratedSizesMatchAutocodedSizes) {
    // The generated Rust types must agree with the autocoded SERIALIZED_SIZE,
    // which is what every framing computation in the framework relies on.
    uint8_t bytes[CAPACITY] = {0};
    FwSizeType written = 0;

    ASSERT_EQ(0, fprime_rs_gen_frameheader_ser(0, 0, 0, bytes, CAPACITY, &written));
    ASSERT_EQ(written, static_cast<FwSizeType>(Svc::FprimeProtocol::FrameHeader::SERIALIZED_SIZE));

    ASSERT_EQ(0, fprime_rs_gen_frametrailer_ser(0, 0, bytes, CAPACITY, &written));
    ASSERT_EQ(written, static_cast<FwSizeType>(Svc::FprimeProtocol::FrameTrailer::SERIALIZED_SIZE));
}

TEST(RustGeneratedTypes, NoRoomLeftStatusMatches) {
    // A too-small destination must produce the same status on both sides
    U8 cppBytes[4];
    Svc::FprimeProtocol::FrameHeader cppHeader;
    Fw::ExternalSerializeBuffer cppBuf(cppBytes, 4);
    const Fw::SerializeStatus cppStatus = cppBuf.serializeFrom(cppHeader, Fw::Endianness::BIG);

    uint8_t rustBytes[4];
    FwSizeType written = 0;
    const int32_t rustStatus = fprime_rs_gen_frameheader_ser(0, 0, 0, rustBytes, 4, &written);

    ASSERT_EQ(rustStatus, static_cast<int32_t>(cppStatus));
    ASSERT_EQ(cppStatus, Fw::FW_SERIALIZE_NO_ROOM_LEFT);
}


// ----------------------------------------------------------------------
// Multi-construct coverage, driven by the real in-tree config model
// default/config/ComCfg.fpp
//
// FprimeProtocol exercises only flat U32 structs. ComCfg adds enums with an
// explicit representation and `default` clauses, a nine-member struct whose
// own `default` clause sets five members, nested enum members, bools, and
// several integer widths -- the constructs where a generator is most likely
// to be silently wrong. (Arrays, floats and 64-bit members still have no
// differential coverage; see Rust/README.md.)
// ----------------------------------------------------------------------

#include "config/ApidEnumAc.hpp"
#include "config/FrameContextSerializableAc.hpp"
#include "config/PvnEnumAc.hpp"
#include "config/TimeBaseEnumAc.hpp"

namespace {

//! Require the Rust generated type to agree with the C++ autocode on the
//! default value, on a round trip of populated bytes, and on SERIALIZED_SIZE.
template <typename T>
void checkAgainstRust(int32_t typeId, const T& populated, Fw::Endianness mode) {
    // 1. Default-constructed values must agree byte for byte. This is what
    //    catches a generator that drops the model's `default` clause -- a bug
    //    a round-trip test cannot see, because it never constructs a default.
    T cppDefault;
    U8 cppBytes[CAPACITY] = {0};
    Fw::ExternalSerializeBuffer cppBuf(cppBytes, CAPACITY);
    ASSERT_EQ(cppBuf.serializeFrom(cppDefault, mode), Fw::FW_SERIALIZE_OK);

    uint8_t rustBytes[CAPACITY] = {0};
    FwSizeType written = 0;
    ASSERT_EQ(0, fprime_rs_gen_test_default(typeId, modeCode(mode), rustBytes, CAPACITY, &written));
    ASSERT_EQ(written, static_cast<FwSizeType>(cppBuf.getSize()))
        << "default size mismatch for type " << typeId;
    ASSERT_EQ(0, memcmp(cppBytes, rustBytes, static_cast<size_t>(written)))
        << "default byte mismatch for type " << typeId << " mode " << modeCode(mode);

    // 2. A populated value serialized by C++ must round-trip through the Rust
    //    type unchanged, proving field order, widths, and endianness agree.
    U8 populatedBytes[CAPACITY] = {0};
    Fw::ExternalSerializeBuffer populatedBuf(populatedBytes, CAPACITY);
    ASSERT_EQ(populatedBuf.serializeFrom(populated, mode), Fw::FW_SERIALIZE_OK);
    const FwSizeType populatedSize = static_cast<FwSizeType>(populatedBuf.getSize());

    uint8_t roundTripped[CAPACITY] = {0};
    FwSizeType rtWritten = 0;
    ASSERT_EQ(0, fprime_rs_gen_test_roundtrip(typeId, populatedBytes, populatedSize, modeCode(mode),
                                              roundTripped, CAPACITY, &rtWritten));
    ASSERT_EQ(rtWritten, populatedSize) << "round-trip size mismatch for type " << typeId;
    ASSERT_EQ(0, memcmp(populatedBytes, roundTripped, static_cast<size_t>(rtWritten)))
        << "round-trip byte mismatch for type " << typeId << " mode " << modeCode(mode);

    // 3. The generated SERIALIZED_SIZE must match the autocoded constant
    ASSERT_EQ(fprime_rs_gen_test_serialized_size(typeId), static_cast<FwSizeType>(T::SERIALIZED_SIZE));
}

}  // namespace

TEST(RustGeneratedTypes, EnumsWithDefaultsMatchAutocode) {
    for (Fw::Endianness mode : MODES) {
        checkAgainstRust(FPRIME_RS_GEN_APID, ComCfg::Apid(ComCfg::Apid::FW_PACKET_COMMAND), mode);
        checkAgainstRust(FPRIME_RS_GEN_PVN, ComCfg::Pvn(ComCfg::Pvn::SPACE_PACKET_PROTOCOL), mode);
        checkAgainstRust(FPRIME_RS_GEN_TIMEBASE, TimeBase(TimeBase::TB_WORKSTATION_TIME), mode);
    }
}

TEST(RustGeneratedTypes, StructWithDefaultsAndNestedEnumsMatchesAutocode) {
    for (Fw::Endianness mode : MODES) {
        // Default-constructed: exercises the model's five-member default clause
        checkAgainstRust(FPRIME_RS_GEN_FRAMECONTEXT, ComCfg::FrameContext(), mode);

        // Populated with values distinct from every default
        ComCfg::FrameContext context;
        context.set_comQueueIndex(3);
        context.set_apid(ComCfg::Apid::FW_PACKET_FILE);
        context.set_hasSecHdr(true);
        context.set_sequenceFlags(1);
        context.set_sequenceCount(1234);
        context.set_vcId(2);
        context.set_pvn(ComCfg::Pvn::ENCAPSULATION_PACKET_PROTOCOL);
        context.set_sendNow(true);
        context.set_saIndex(4321);
        checkAgainstRust(FPRIME_RS_GEN_FRAMECONTEXT, context, mode);
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
