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

#endif  // FPRIME_RUST_SVC_H
