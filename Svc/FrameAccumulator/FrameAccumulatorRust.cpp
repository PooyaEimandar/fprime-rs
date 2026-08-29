// ======================================================================
// \title  FrameAccumulatorRust.cpp
// \brief  Rust-backed implementation of the FrameAccumulator component
//
// fprime-rs Phase 3: the stateful accumulate/detect/extract orchestration
// runs in Rust (Rust/fprime-svc/src/accumulator.rs), driving the component
// through a trampoline table: the accumulation ring (whose byte engine is
// itself Rust under FPRIME_RUST_IMPL_UTILS_TYPES), the polymorphic
// FrameDetector, the bufferAllocate returning port, frame emission, and
// events. Selected instead of FrameAccumulator.cpp via
// FPRIME_RUST_IMPL_SVC_FRAME_ACCUMULATOR; behavior is identical, enforced by
// the existing unit-test suite.
//
// Note: the private processBuffer/processRing members declared in the header
// are intentionally left undefined here — the orchestration lives in Rust,
// and they are never odr-used.
// ======================================================================

#include "Svc/FrameAccumulator/FrameAccumulator.hpp"
#include "Fw/FPrimeBasicTypes.hpp"
#include "Fw/Types/Assert.hpp"

#include "Rust/RustSvc.h"

// The Rust orchestration pins the FrameDetector::Status values
static_assert(Svc::FrameDetector::FRAME_DETECTED == 0, "FrameDetector::Status values changed: update fprime-svc");
static_assert(Svc::FrameDetector::NO_FRAME_DETECTED == 1, "FrameDetector::Status values changed: update fprime-svc");
static_assert(Svc::FrameDetector::MORE_DATA_NEEDED == 2, "FrameDetector::Status values changed: update fprime-svc");

namespace Svc {

// ----------------------------------------------------------------------
// Component construction and destruction
// ----------------------------------------------------------------------

FrameAccumulator ::FrameAccumulator(const char* const compName)
    : FrameAccumulatorComponentBase(compName),
      m_detector(nullptr),
      m_memoryAllocator(nullptr),
      m_memory(nullptr),
      m_allocatorId(0) {}

FrameAccumulator ::~FrameAccumulator() {}

void FrameAccumulator ::configure(const FrameDetector& detector,
                                  FwEnumStoreType allocationId,
                                  Fw::MemAllocator& allocator,
                                  FwSizeType store_size) {
    bool recoverable = false;
    U8* const data = static_cast<U8*>(allocator.allocate(allocationId, store_size, recoverable));
    FW_ASSERT(data != nullptr);
    m_inRing.setup(data, store_size);

    this->m_detector = &detector;
    this->m_allocatorId = allocationId;
    this->m_memoryAllocator = &allocator;
    this->m_memory = data;
}

void FrameAccumulator ::cleanup() {
    // If configuration happened, we must deallocate
    if (this->m_memoryAllocator != nullptr) {
        this->m_memoryAllocator->deallocate(this->m_allocatorId, this->m_memory);
        this->m_memory = nullptr;
    }
}

// ----------------------------------------------------------------------
// Handler implementations for user-defined typed input ports
// ----------------------------------------------------------------------

void FrameAccumulator ::dataIn_handler(FwIndexType portNum, Fw::Buffer& buffer, const ComCfg::FrameContext& context) {
    // Check whether there is data to process
    if (buffer.isValid()) {
        FW_ASSERT(this->m_detector != nullptr);
        // Trampoline context: the Rust orchestration drives the component
        // through these captureless lambdas (member scope grants access to
        // the protected ports and private members). The extracted-frame
        // Fw::Buffer lives here so this handler owns it on every path.
        struct OpsContext {
            FrameAccumulator* component;
            const ComCfg::FrameContext* context;
            Fw::Buffer frame;
        };
        OpsContext opsContext{this, &context, Fw::Buffer()};

        FprimeRsAccumulatorOps ops;
        ops.ctx = &opsContext;
        ops.ringFree = [](void* raw) -> FwSizeType {
            return static_cast<OpsContext*>(raw)->component->m_inRing.get_free_size();
        };
        ops.ringAllocated = [](void* raw) -> FwSizeType {
            return static_cast<OpsContext*>(raw)->component->m_inRing.get_allocated_size();
        };
        ops.ringCapacity = [](void* raw) -> FwSizeType {
            return static_cast<OpsContext*>(raw)->component->m_inRing.get_capacity();
        };
        ops.ringSerialize = [](void* raw, const uint8_t* bytes, FwSizeType size) -> int32_t {
            return static_cast<int32_t>(static_cast<OpsContext*>(raw)->component->m_inRing.serialize(bytes, size));
        };
        ops.ringRotate = [](void* raw, FwSizeType amount) -> int32_t {
            return static_cast<int32_t>(static_cast<OpsContext*>(raw)->component->m_inRing.rotate(amount));
        };
        ops.detect = [](void* raw, FwSizeType* sizeOut) -> int32_t {
            OpsContext* ctx = static_cast<OpsContext*>(raw);
            FwSizeType size = 0;
            const FrameDetector::Status status = ctx->component->m_detector->detect(ctx->component->m_inRing, size);
            *sizeOut = size;
            return static_cast<int32_t>(status);
        };
        ops.allocateFrame = [](void* raw, FwSizeType size) -> uint8_t {
            OpsContext* ctx = static_cast<OpsContext*>(raw);
            ctx->frame = ctx->component->bufferAllocate_out(0, size);
            return ctx->frame.isValid() ? 1 : 0;
        };
        ops.peekIntoFrame = [](void* raw, FwSizeType size) -> int32_t {
            OpsContext* ctx = static_cast<OpsContext*>(raw);
            const Fw::SerializeStatus status = ctx->component->m_inRing.peek(ctx->frame.getData(), size);
            ctx->frame.setSize(size);
            return static_cast<int32_t>(status);
        };
        ops.emitFrame = [](void* raw) {
            OpsContext* ctx = static_cast<OpsContext*>(raw);
            ctx->component->dataOut_out(0, ctx->frame, *ctx->context);
        };
        ops.logFrameSizeError = [](void* raw, FwSizeType size) {
            static_cast<OpsContext*>(raw)->component->log_WARNING_HI_FrameDetectionSizeError(size);
        };
        ops.logNoBuffer = [](void* raw) {
            static_cast<OpsContext*>(raw)->component->log_WARNING_HI_NoBufferAvailable();
        };
        ops.logFrameDropped = [](void* raw) {
            static_cast<OpsContext*>(raw)->component->log_WARNING_HI_FrameDetectionValidFrameDropped();
        };

        fprime_rs_accumulator_process(&ops, buffer.getData(), buffer.getSize());
    }
    // Return ownership of the incoming data
    this->dataReturnOut_out(0, buffer, context);
}

void FrameAccumulator ::dataReturnIn_handler(FwIndexType portNum,
                                             Fw::Buffer& fwBuffer,
                                             const ComCfg::FrameContext& context) {
    // Frame buffer ownership is returned to the component. Component had allocated with a buffer manager,
    // so we return it to the buffer manager for deallocation
    this->bufferDeallocate_out(0, fwBuffer);
}

}  // namespace Svc
