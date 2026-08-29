// ======================================================================
// \title  FprimeRouterRust.cpp
// \brief  Rust-backed implementation of the FprimeRouter component
//
// fprime-rs Phase 3: the stateful piece of this component — the
// buffer-to-context association table — runs in Rust
// (Rust/fprime-svc/src/router_table.rs), managing the C++-resident entry
// array through a strided view; the shim copies the context object at the
// slot index Rust returns. The APID routing switch (config-defined enum +
// port plumbing) stays C++. Selected instead of FprimeRouter.cpp via
// FPRIME_RUST_IMPL_SVC_FPRIME_ROUTER; behavior is identical, enforced by the
// existing unit-test suite.
// ======================================================================

#include "Svc/FprimeRouter/FprimeRouter.hpp"
#include "Fw/Com/ComPacket.hpp"
#include "Fw/FPrimeBasicTypes.hpp"
#include "Fw/Logger/Logger.hpp"
#include "config/ApidEnumAc.hpp"

#include "Rust/RustSvc.h"

// The Rust table logic reads/writes the state's raw enum storage (the public
// `e` member of the FPP enum class; the object itself also carries a vtable)
static_assert(sizeof(Fw::Active::T) == sizeof(int32_t),
              "Fw::Active enum storage changed: update fprime-svc router_table");

namespace Svc {

// ----------------------------------------------------------------------
// Component construction and destruction
// ----------------------------------------------------------------------

FprimeRouter ::FprimeRouter(const char* const compName) : FprimeRouterComponentBase(compName) {
    // Mark every table entry unused
    for (FwSizeType i = 0; i < FW_NUM_ARRAY_ELEMENTS(this->m_bufferContextTable); i++) {
        this->m_bufferContextTable[i].state = Fw::Active::INACTIVE;
    }
}

FprimeRouter ::~FprimeRouter() {}

// ----------------------------------------------------------------------
// Handler implementations for user-defined typed input ports
// ----------------------------------------------------------------------

void FprimeRouter ::dataIn_handler(FwIndexType portNum, Fw::Buffer& packetBuffer, const ComCfg::FrameContext& context) {
    Fw::SerializeStatus status;
    Fw::ComPacketType packetType = context.get_apid();
    // Route based on received APID (packet type)
    switch (packetType) {
        // Handle a command packet
        case Fw::ComPacketType::FW_PACKET_COMMAND: {
            // Allocate a com buffer on the stack
            Fw::ComBuffer com;
            // Copy the contents of the packet buffer into the com buffer
            status = com.setBuff(packetBuffer.getData(), packetBuffer.getSize());
            if (status == Fw::FW_SERIALIZE_OK) {
                // Send the com buffer - critical functionality so it is considered an error not to
                // have the port connected. This is why we don't check isConnected() before sending.
                this->commandOut_out(0, com, 0);
            } else {
                this->log_WARNING_HI_SerializationError(status);
            }
            // The command buffer was copied into the com buffer above, so ownership of the
            // incoming packetBuffer is returned immediately with the context it was received with.
            this->dataReturnOut_out(0, packetBuffer, context);
            break;
        }
        // Handle a file packet
        case Fw::ComPacketType::FW_PACKET_FILE: {
            // If the file uplink output port is connected, send the file packet directly.
            if (this->isConnected_fileOut_OutputPort(0)) {
                if (this->insertContext(packetBuffer, context) == Fw::Success::FAILURE) {
                    this->log_WARNING_HI_FileOutContextTableFull();
                }
                this->fileOut_out(0, packetBuffer);
            } else {
                // Port not connected, return the buffer immediately with its context
                this->dataReturnOut_out(0, packetBuffer, context);
            }
            break;
        }
        default: {
            // Packet type is not known to the F Prime protocol. If the unknownDataOut port is
            // connected, forward packet and context for further processing.
            if (this->isConnected_unknownDataOut_OutputPort(0)) {
                if (this->insertContext(packetBuffer, context) == Fw::Success::FAILURE) {
                    this->log_WARNING_HI_UnknownDataOutContextTableFull();
                }
                this->unknownDataOut_out(0, packetBuffer, context);
            } else {
                // Port not connected, return the buffer immediately with its context
                this->dataReturnOut_out(0, packetBuffer, context);
            }
            break;
        }
    }
}

void FprimeRouter ::cmdResponseIn_handler(FwIndexType portNum,
                                          FwOpcodeType opcode,
                                          U32 cmdSeq,
                                          const Fw::CmdResponse& response) {
    // Nothing to do
}

void FprimeRouter ::fileBufferReturnIn_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) {
    // Restore the context that was saved when this buffer was handed off on
    // fileOut/unknownDataOut
    ComCfg::FrameContext context;
    if (this->takeContext(fwBuffer, context) == Fw::Success::FAILURE) {
        // Buffer not found in the table: return with an empty context (already default)
        this->log_WARNING_HI_BufferContextNotFound();
    }
    this->dataReturnOut_out(0, fwBuffer, context);
}

// ----------------------------------------------------------------------
// Buffer-to-context association table helpers (slot logic in Rust)
// ----------------------------------------------------------------------

Fw::Success FprimeRouter ::insertContext(const Fw::Buffer& buffer, const ComCfg::FrameContext& context) {
    const int64_t slot = fprime_rs_router_table_insert(
        reinterpret_cast<uint8_t*>(&this->m_bufferContextTable[0].state.e),
        reinterpret_cast<uint8_t*>(&this->m_bufferContextTable[0].key), sizeof(this->m_bufferContextTable[0]),
        FW_NUM_ARRAY_ELEMENTS(this->m_bufferContextTable), static_cast<int32_t>(Fw::Active::ACTIVE),
        static_cast<int32_t>(Fw::Active::INACTIVE), buffer.getData());
    if (slot < 0) {
        return Fw::Success::FAILURE;
    }
    // Rust claimed the slot and recorded the key; store the context payload
    this->m_bufferContextTable[slot].context = context;
    return Fw::Success::SUCCESS;
}

Fw::Success FprimeRouter ::takeContext(const Fw::Buffer& buffer, ComCfg::FrameContext& context) {
    const int64_t slot = fprime_rs_router_table_take(
        reinterpret_cast<uint8_t*>(&this->m_bufferContextTable[0].state.e),
        reinterpret_cast<uint8_t*>(&this->m_bufferContextTable[0].key), sizeof(this->m_bufferContextTable[0]),
        FW_NUM_ARRAY_ELEMENTS(this->m_bufferContextTable), static_cast<int32_t>(Fw::Active::ACTIVE),
        static_cast<int32_t>(Fw::Active::INACTIVE), buffer.getData());
    if (slot < 0) {
        return Fw::Success::FAILURE;
    }
    context = this->m_bufferContextTable[slot].context;
    return Fw::Success::SUCCESS;
}

}  // namespace Svc
