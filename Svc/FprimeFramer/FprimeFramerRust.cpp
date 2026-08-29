// ======================================================================
// \title  FprimeFramerRust.cpp
// \brief  Rust-backed implementation of the FprimeFramer component
//
// fprime-rs Phase 2: the framing logic runs in Rust
// (Rust/fprime-svc/src/framer.rs), which obtains frame storage by calling
// BACK into this component's bufferAllocate output port through a trampoline
// — the synchronous returning-port-call pattern (Fw.BufferGet from inside
// Rust logic). This shim keeps the autocoded base, FW_ASSERT preconditions,
// events, and port sends. Selected instead of FprimeFramer.cpp via
// FPRIME_RUST_IMPL_SVC_FPRIME_FRAMER; behavior is identical, enforced by the
// existing unit-test suite.
// ======================================================================

#include <limits>

#include "Svc/FprimeFramer/FprimeFramer.hpp"
#include "Fw/Types/Assert.hpp"
#include "Svc/FprimeProtocol/FrameHeaderSerializableAc.hpp"
#include "Svc/FprimeProtocol/FrameTrailerSerializableAc.hpp"

#include "Rust/RustSvc.h"

static_assert(Svc::FprimeProtocol::FrameHeader::SERIALIZED_SIZE == 8,
              "FrameHeader size changed: update Rust/fprime-svc protocol constants");
static_assert(Svc::FprimeProtocol::FrameTrailer::SERIALIZED_SIZE == 4,
              "FrameTrailer size changed: update Rust/fprime-svc protocol constants");

namespace Svc {

// ----------------------------------------------------------------------
// Component construction and destruction
// ----------------------------------------------------------------------

FprimeFramer ::FprimeFramer(const char* const compName) : FprimeFramerComponentBase(compName) {
    // The Rust logic hardcodes the protocol start word; verify the FPP default
    FW_ASSERT(FprimeProtocol::FrameHeader().get_startWord() == 0xdeadbeefU);
}

FprimeFramer ::~FprimeFramer() {}

// ----------------------------------------------------------------------
// Handler implementations for typed input ports
// ----------------------------------------------------------------------

void FprimeFramer ::dataIn_handler(FwIndexType portNum, Fw::Buffer& data, const ComCfg::FrameContext& context) {
    // Preconditions, mirroring the original
    const FwSizeType frameSize =
        FprimeProtocol::FrameHeader::SERIALIZED_SIZE + data.getSize() + FprimeProtocol::FrameTrailer::SERIALIZED_SIZE;
    FW_ASSERT(data.getSize() <= std::numeric_limits<FprimeProtocol::TokenType>::max(),
              static_cast<FwAssertArgType>(frameSize));
    FW_ASSERT(frameSize <= std::numeric_limits<Fw::Buffer::SizeType>::max(), static_cast<FwAssertArgType>(frameSize));

    // Trampoline context: the Rust framing logic calls back through
    // allocate() to obtain frame storage from the bufferAllocate output port
    // (a synchronous returning port call made mid-logic). The Fw::Buffer is
    // captured here so this handler retains ownership on every path.
    struct AllocContext {
        FprimeFramer* component;
        Fw::Buffer buffer;
        const U8* payload;
        FwSizeType payloadSize;
    };
    AllocContext allocContext{this, Fw::Buffer(), data.getData(), data.getSize()};
    // Captureless lambda in member scope: may call the protected port method
    const FprimeRsAllocFn allocate = [](void* raw, FwSizeType size) -> FprimeRsByteView {
        AllocContext* ctx = static_cast<AllocContext*>(raw);
        ctx->buffer = ctx->component->bufferAllocate_out(0, size);
        if (ctx->buffer.isValid()) {
            // Rust slice aliasing rules forbid the allocation overlapping the
            // payload (Phase 1 shim convention; a sane allocator never does)
            const U8* alloc_begin = ctx->buffer.getData();
            const U8* alloc_end = alloc_begin + ctx->buffer.getSize();
            FW_ASSERT(ctx->payload + ctx->payloadSize <= alloc_begin || alloc_end <= ctx->payload);
        }
        FprimeRsByteView view;
        view.ptr = ctx->buffer.isValid() ? ctx->buffer.getData() : nullptr;
        view.len = ctx->buffer.isValid() ? ctx->buffer.getSize() : 0;
        return view;
    };

    FwSizeType written = 0;
    const int32_t status = fprime_rs_framer_frame(data.getData(), data.getSize(), allocate, &allocContext, &written);
    if (status != 0) {
        // The allocator returned an invalid or smaller-than-requested buffer
        this->log_WARNING_HI_NoBufferAvailable();
        if (allocContext.buffer.isValid()) {
            this->bufferDeallocate_out(0, allocContext.buffer);
        }
        this->dataReturnOut_out(0, data, context);
        return;
    }
    FW_ASSERT(written == frameSize, static_cast<FwAssertArgType>(written));
    // Trim to actual frame size in case the allocator returned a larger buffer
    allocContext.buffer.setSize(static_cast<Fw::Buffer::SizeType>(written));

    // Send the full frame out - this port shall always be connected
    this->dataOut_out(0, allocContext.buffer, context);
    // Return original (unframed) data buffer ownership back to its sender - always connected
    this->dataReturnOut_out(0, data, context);
}

void FprimeFramer ::comStatusIn_handler(FwIndexType portNum, Fw::Success& condition) {
    if (this->isConnected_comStatusOut_OutputPort(portNum)) {
        this->comStatusOut_out(portNum, condition);
    }
}

void FprimeFramer ::dataReturnIn_handler(FwIndexType portNum,
                                         Fw::Buffer& frameBuffer,
                                         const ComCfg::FrameContext& context) {
    // dataReturnIn is the allocated buffer coming back from the ComManager (e.g. ComStub) component
    this->bufferDeallocate_out(0, frameBuffer);
}

}  // namespace Svc
