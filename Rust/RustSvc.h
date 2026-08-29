// ======================================================================
// \title  RustSvc.h
// \brief  C declarations for the fprime-rs service-component FFI (Phase 2)
//
// These must match the #[no_mangle] extern "C" definitions in
// Rust/fprime-ffi/src/lib.rs exactly. The component shims
// (Svc/FprimeDeframer/FprimeDeframerRust.cpp,
// Svc/FprimeFramer/FprimeFramerRust.cpp) own all FW_ASSERT preconditions,
// event emission, context handling, and port calls; Rust owns the protocol
// logic.
// ======================================================================
#ifndef FPRIME_RUST_SVC_H
#define FPRIME_RUST_SVC_H

#include <stdint.h>

#include <Fw/FPrimeBasicTypes.h>

#ifdef __cplusplus
extern "C" {
#endif

// Mirrors fprime-ffi DeframeResultFfi
typedef struct {
    // 0 = valid frame; 1 = buffer too short (InvalidBufferReceived);
    // 2 = bad start word (InvalidStartWord); 3 = length mismatch
    // (InvalidLengthReceived); 4 = CRC mismatch (InvalidChecksum)
    int32_t outcome;
    // 1 when the PayloadTooShort WARNING_LO applies (outcomes 0 and 4)
    uint8_t payloadTooShort;
    // 1 when descriptor holds a packet descriptor read from the payload
    uint8_t hasDescriptor;
    FwPacketDescriptorType descriptor;
} FprimeRsDeframeResult;

void fprime_rs_deframer_deframe(const uint8_t* data, FwSizeType len, FprimeRsDeframeResult* result);

// Mirrors fprime-ffi ByteViewFfi
typedef struct {
    uint8_t* ptr;
    FwSizeType len;
} FprimeRsByteView;

#ifdef __cplusplus
}
#endif

// The allocator trampoline type intentionally has C++ language linkage: the
// shim converts a captureless lambda (which has C++ linkage) to this type.
// The calling convention is identical to extern "C" on all supported
// platforms/compilers (gcc/clang, Itanium/ARM ABIs).
//
// The trampoline must NOT throw or otherwise unwind: it is called from a
// panic=abort extern "C" Rust frame, and unwinding through it is undefined
// behavior. A project assert hook that throws from doAssert() (instead of
// aborting) is incompatible with the Rust-backed framer.
typedef FprimeRsByteView (*FprimeRsAllocFn)(void* ctx, FwSizeType size);

#ifdef __cplusplus
extern "C" {
#endif

int32_t fprime_rs_framer_frame(const uint8_t* payload,
                               FwSizeType payloadLen,
                               FprimeRsAllocFn alloc,
                               void* ctx,
                               FwSizeType* written);

#ifdef __cplusplus
}
#endif

// ---------------------------------------------------------------------------
// FrameAccumulator (Phase 3)
// ---------------------------------------------------------------------------
// Trampoline table driving the Rust orchestration of processBuffer/processRing.
// All function pointers intentionally have C++ language linkage (captureless
// lambdas; same calling convention as extern "C" on gcc/clang) and must NOT
// throw or otherwise unwind: they are called from a panic=abort extern "C"
// Rust frame. Layout must match AccumulatorOpsFfi in Rust/fprime-ffi.
typedef struct {
    void* ctx;
    FwSizeType (*ringFree)(void*);
    FwSizeType (*ringAllocated)(void*);
    FwSizeType (*ringCapacity)(void*);
    int32_t (*ringSerialize)(void*, const uint8_t*, FwSizeType);
    int32_t (*ringRotate)(void*, FwSizeType);
    int32_t (*detect)(void*, FwSizeType* sizeOut);
    uint8_t (*allocateFrame)(void*, FwSizeType);
    int32_t (*peekIntoFrame)(void*, FwSizeType);
    void (*emitFrame)(void*);
    void (*logFrameSizeError)(void*, FwSizeType);
    void (*logNoBuffer)(void*);
    void (*logFrameDropped)(void*);
} FprimeRsAccumulatorOps;

#ifdef __cplusplus
extern "C" {
#endif

void fprime_rs_accumulator_process(const FprimeRsAccumulatorOps* ops, const uint8_t* data, FwSizeType len);

// FprimeRouter buffer-context table (Phase 3): Rust owns the slot state
// machine over the C++-resident entry array (strided view: address of entry
// 0's state/key fields + stride). Returns the slot index or -1.
int64_t fprime_rs_router_table_insert(uint8_t* state0,
                                      uint8_t* key0,
                                      FwSizeType stride,
                                      FwSizeType count,
                                      int32_t active,
                                      int32_t inactive,
                                      const uint8_t* key);

int64_t fprime_rs_router_table_take(uint8_t* state0,
                                    uint8_t* key0,
                                    FwSizeType stride,
                                    FwSizeType count,
                                    int32_t active,
                                    int32_t inactive,
                                    const uint8_t* key);

#ifdef __cplusplus
}
#endif

#endif  // FPRIME_RUST_SVC_H
