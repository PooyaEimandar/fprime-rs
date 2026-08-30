// ======================================================================
// \title  RustFwKernel.h
// \brief  C declarations for the Rust-backed Fw serialization kernel
//
// fprime-rs Phase 8 -- the flip point. Earlier phases had Rust mirror the
// C++ wire format; here C++ Fw calls Rust for every primitive read and
// write. The C++ LinearBufferBase still owns its storage and cursors and
// passes them in, so the class, its API and its semantics are unchanged.
//
// Only the unsigned integers and bool appear: the C++ kernel routes the
// signed and floating-point overloads through them, so these ten entry
// points carry every primitive SCALAR serialization in the framework. Bulk
// byte movement (serializeFrom(const U8*, ...), deserializeTo(U8*, ...),
// setBuff, copyRaw/copyRawOffset, copyFrom) stays C++ memcpy.
//
// The deserialize entries take a const buffer bounded by serLoc rather than
// capacity: a read can never touch a byte past the write high-water mark, so
// the borrow must not span one either.
//
// Statuses are Fw::SerializeStatus integer values; mode is Fw::Endianness
// (0 = BIG, 1 = LITTLE).
// ======================================================================
#ifndef FPRIME_RUST_FW_KERNEL_H
#define FPRIME_RUST_FW_KERNEL_H

#include <stdbool.h>
#include <stdint.h>

#include <Fw/FPrimeBasicTypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FPRIME_RS_LBB_DECL(name, type)                                                            \
    int32_t fprime_rs_lbb_ser_##name(uint8_t* data, FwSizeType capacity, FwSizeType* serLoc,      \
                                     FwSizeType* deserLoc, type value, int32_t mode);             \
    int32_t fprime_rs_lbb_deser_##name(const uint8_t* data, FwSizeType capacity,                  \
                                       const FwSizeType* serLoc, FwSizeType* deserLoc, type* value, \
                                       int32_t mode);

FPRIME_RS_LBB_DECL(u16, uint16_t)
FPRIME_RS_LBB_DECL(u32, uint32_t)
FPRIME_RS_LBB_DECL(u64, uint64_t)

#undef FPRIME_RS_LBB_DECL

// U8 and bool carry no endianness
int32_t fprime_rs_lbb_ser_u8(uint8_t* data, FwSizeType capacity, FwSizeType* serLoc, FwSizeType* deserLoc,
                             uint8_t value);
int32_t fprime_rs_lbb_deser_u8(const uint8_t* data, FwSizeType capacity, const FwSizeType* serLoc,
                               FwSizeType* deserLoc, uint8_t* value);
int32_t fprime_rs_lbb_ser_bool(uint8_t* data, FwSizeType capacity, FwSizeType* serLoc, FwSizeType* deserLoc,
                               bool value);
int32_t fprime_rs_lbb_deser_bool(const uint8_t* data, FwSizeType capacity, const FwSizeType* serLoc,
                                 FwSizeType* deserLoc, bool* value);

#ifdef __cplusplus
}
#endif

#endif  // FPRIME_RUST_FW_KERNEL_H
