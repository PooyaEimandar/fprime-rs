// ======================================================================
// \title  GeneratedTypesDiff.cpp
// \brief  Differential oracle for fpp-to-rust generated types
//
// Every file tools/fprime-rs/fpp_to_rust.py emits carries the line
// "Serialization is byte-identical to the C++ code FPP autocodes for the same
// model; the differential unit test is the proof." This is that test. Before
// it existed the sentence was an intention: the generated types supplied
// SERIALIZED_SIZE constants that C++ static_asserts pinned, but nothing
// compared a single serialized BYTE between the two languages.
//
// It links the autocoded C++ types and the generated Rust into one binary and
// serializes the same values with both. The C++ side comes from the ordinary
// build, so it is the real fpp-to-cpp output, not a transcription.
//
// Covers the three shapes the generator emits, chosen because each has a
// different failure mode: a struct of primitives (field order), an array whose
// length came from a named FPP constant (count and element order), and a
// struct mixing bools and enums (the representation width of an FPP enum,
// which is a vtable-carrying class in C++ and a newtype in Rust).
// ======================================================================
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <config/FrameContextSerializableAc.hpp>
#include <Fw/Types/Serializable.hpp>
#include <Svc/ComQueue/ComQueueDepthArrayAc.hpp>
#include <Svc/FprimeProtocol/FrameHeaderSerializableAc.hpp>

extern "C" {
FwSizeType fprime_rs_diff_frame_header(U32 startWord, U32 lengthField, U8* out, FwSizeType outLen);
FwSizeType fprime_rs_diff_com_queue_depth(const U32* depths, U8* out, FwSizeType outLen);
FwSizeType fprime_rs_diff_frame_context(I16 comQueueIndex,
                                        U16 apid,
                                        bool hasSecHdr,
                                        U8 sequenceFlags,
                                        U16 sequenceCount,
                                        U8 vcId,
                                        U8 pvn,
                                        bool sendNow,
                                        U16 saIndex,
                                        U8* out,
                                        FwSizeType outLen);
}

namespace {

int failures = 0;

//! Serialize a C++ Serializable and return its bytes.
std::vector<U8> cppBytes(const Fw::Serializable& value, FwSizeType capacity) {
    std::vector<U8> storage(static_cast<size_t>(capacity), 0);
    Fw::ExternalSerializeBuffer buffer(storage.data(), capacity);
    if (buffer.serializeFrom(value) != Fw::FW_SERIALIZE_OK) {
        std::printf("FAIL  the C++ side refused to serialize\n");
        failures++;
        return {};
    }
    storage.resize(static_cast<size_t>(buffer.getSize()));
    return storage;
}

//! Report bytes as hex; a wrong byte is unreadable as characters.
std::string hex(const std::vector<U8>& bytes) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (const U8 byte : bytes) {
        out += digits[byte >> 4];
        out += digits[byte & 0xF];
        out += ' ';
    }
    return out;
}

void compare(const char* what, const std::vector<U8>& fromCpp, const std::vector<U8>& fromRust) {
    if (fromCpp.size() != fromRust.size() ||
        std::memcmp(fromCpp.data(), fromRust.data(), fromCpp.size()) != 0) {
        std::printf("FAIL  %s\n        C++:  %s\n        Rust: %s\n", what, hex(fromCpp).c_str(),
                    hex(fromRust).c_str());
        failures++;
        return;
    }
    std::printf("ok    %s (%zu bytes)\n", what, fromCpp.size());
}

//! Drive the Rust side and trim to what it wrote.
std::vector<U8> rustBytes(std::vector<U8> storage, FwSizeType written) {
    storage.resize(static_cast<size_t>(written));
    return storage;
}

// A struct of two primitives. Catches field order, which nothing else would:
// both fields are U32, so a swap keeps the length correct.
void frameHeader() {
    const U32 startWord = 0xDEADBEEF;
    const U32 lengthField = 0x01020304;
    Svc::FprimeProtocol::FrameHeader header(startWord, lengthField);

    std::vector<U8> storage(64, 0);
    const FwSizeType written = fprime_rs_diff_frame_header(startWord, lengthField, storage.data(), 64);
    compare("FrameHeader", cppBytes(header, Svc::FprimeProtocol::FrameHeader::SERIALIZED_SIZE),
            rustBytes(storage, written));
}

// An array whose element count came from a named FPP constant rather than a
// literal -- the capability that unblocked real framework modules.
void comQueueDepth() {
    U32 depths[2] = {0x11223344, 0x55667788};
    Svc::ComQueueDepth depth(depths);

    std::vector<U8> storage(64, 0);
    const FwSizeType written = fprime_rs_diff_com_queue_depth(depths, storage.data(), 64);
    compare("ComQueueDepth", cppBytes(depth, Svc::ComQueueDepth::SERIALIZED_SIZE),
            rustBytes(storage, written));
}

// Bools and enums mixed with primitives. An FPP enum is a vtable-carrying
// class in C++ and a newtype in Rust, so its serialized WIDTH is the thing
// that could differ without either side looking wrong.
void frameContext() {
    ComCfg::FrameContext context;
    context.set_comQueueIndex(-3);
    context.set_apid(ComCfg::Apid::FW_PACKET_TELEM);
    context.set_hasSecHdr(true);
    context.set_sequenceFlags(2);
    context.set_sequenceCount(0xBEEF);
    context.set_vcId(7);
    context.set_pvn(ComCfg::Pvn::SPACE_PACKET_PROTOCOL);
    context.set_sendNow(false);
    context.set_saIndex(0x0102);

    std::vector<U8> storage(128, 0);
    const FwSizeType written = fprime_rs_diff_frame_context(
        -3, static_cast<U16>(ComCfg::Apid::FW_PACKET_TELEM), true, 2, 0xBEEF, 7,
        static_cast<U8>(ComCfg::Pvn::SPACE_PACKET_PROTOCOL), false, 0x0102, storage.data(), 128);
    compare("FrameContext", cppBytes(context, ComCfg::FrameContext::SERIALIZED_SIZE),
            rustBytes(storage, written));
}

}  // namespace

int main() {
    frameHeader();
    comQueueDepth();
    frameContext();
    std::printf("\n%s\n", failures == 0 ? "all generated types match the autocode"
                                        : "generated types DIVERGE from the autocode");
    return failures == 0 ? 0 : 1;
}
