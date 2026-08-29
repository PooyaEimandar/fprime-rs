// ======================================================================
// \title  FprimeDeframerRust.cpp
// \brief  Rust-backed implementation of the FprimeDeframer component
//
// fprime-rs Phase 2: the frame-validation logic (parsing untrusted uplink
// input) runs in Rust (Rust/fprime-svc/src/deframer.rs); this shim keeps the
// autocoded component base, event emission, config-defined APID validation,
// buffer adjustment, and port calls. Selected instead of FprimeDeframer.cpp
// via FPRIME_RUST_IMPL_SVC_FPRIME_DEFRAMER; behavior is identical, enforced
// by the existing unit-test suite.
// ======================================================================

#include "Svc/FprimeDeframer/FprimeDeframer.hpp"
#include "Fw/FPrimeBasicTypes.hpp"
#include "Fw/Types/Assert.hpp"

#include "Svc/FprimeProtocol/FrameHeaderSerializableAc.hpp"
#include "Svc/FprimeProtocol/FrameTrailerSerializableAc.hpp"

#include "Rust/RustSvc.h"

// Pin the C++ protocol shapes to the constants the Rust logic was written
// against (Rust/fprime-svc/src/lib.rs protocol module)
static_assert(Svc::FprimeProtocol::FrameHeader::SERIALIZED_SIZE == 8,
              "FrameHeader size changed: update Rust/fprime-svc protocol constants");
static_assert(Svc::FprimeProtocol::FrameTrailer::SERIALIZED_SIZE == 4,
              "FrameTrailer size changed: update Rust/fprime-svc protocol constants");
// The Rust logic and the FprimeRsDeframeResult layout use the generated
// fprime-config width for the packet descriptor; pin the C++ side to it
// (RustConfigCheck.cpp also asserts this via the generated battery)
static_assert(sizeof(FwPacketDescriptorType) == 2,
              "FwPacketDescriptorType width changed: regenerate Rust/fprime-config");

namespace Svc {

// ----------------------------------------------------------------------
// Component construction and destruction
// ----------------------------------------------------------------------

FprimeDeframer ::FprimeDeframer(const char* const compName) : FprimeDeframerComponentBase(compName) {
    // The Rust logic hardcodes the protocol start word; verify the FPP
    // default matches (it is data, not a compile-time constant, in C++)
    FW_ASSERT(FprimeProtocol::FrameHeader().get_startWord() == 0xdeadbeefU);
}

FprimeDeframer ::~FprimeDeframer() {}

// ----------------------------------------------------------------------
// Handler implementations for user-defined typed input ports
// ----------------------------------------------------------------------

void FprimeDeframer ::dataIn_handler(FwIndexType portNum, Fw::Buffer& data, const ComCfg::FrameContext& context) {
    // A null-data buffer with a nonzero size is a contract violation; the
    // original C++ FW_ASSERTs on it deterministically (via a failed header
    // deserialize) — fail the same way rather than handing Rust a null slice
    FW_ASSERT((data.getSize() == 0) || (data.getData() != nullptr));
    FprimeRsDeframeResult result;
    fprime_rs_deframer_deframe(data.getData(), data.getSize(), &result);

    switch (result.outcome) {
        case 1:  // buffer cannot hold header + trailer
            this->log_WARNING_HI_InvalidBufferReceived();
            this->dataReturnOut_out(0, data, context);  // drop the frame
            return;
        case 2:  // start word mismatch
            this->log_WARNING_HI_InvalidStartWord();
            this->dataReturnOut_out(0, data, context);  // drop the frame
            return;
        case 3:  // frame size does not match the header length field
            this->log_WARNING_HI_InvalidLengthReceived();
            this->dataReturnOut_out(0, data, context);  // drop the frame
            return;
        default:
            break;  // outcomes 0 (valid) and 4 (bad CRC) continue below
    }

    // APID handling, before trailer validation as in the original: the
    // config-defined validity check stays in C++
    ComCfg::FrameContext contextCopy = context;
    if (result.payloadTooShort) {
        this->log_WARNING_LO_PayloadTooShort();
    } else {
        FW_ASSERT(result.hasDescriptor);
        if (ComCfg::Apid::isValid(result.descriptor)) {
            contextCopy.set_apid(static_cast<ComCfg::Apid::T>(result.descriptor));
        } else {
            contextCopy.set_apid(ComCfg::Apid::INVALID_UNINITIALIZED);
        }
    }

    if (result.outcome == 4) {  // trailer CRC mismatch
        this->log_WARNING_HI_InvalidChecksum();
        this->dataReturnOut_out(0, data, context);  // drop the frame
        return;
    }

    // Valid frame: strip header and trailer, emit the payload
    data.advance(FprimeProtocol::FrameHeader::SERIALIZED_SIZE);
    data.setSize(data.getSize() - FprimeProtocol::FrameTrailer::SERIALIZED_SIZE);
    this->dataOut_out(0, data, contextCopy);
}

void FprimeDeframer ::dataReturnIn_handler(FwIndexType portNum,
                                           Fw::Buffer& fwBuffer,
                                           const ComCfg::FrameContext& context) {
    this->dataReturnOut_out(0, fwBuffer, context);
}

}  // namespace Svc
