// ======================================================================
// \title  RustCodec.h
// \brief  C declarations for the fprime-rs serialization codec FFI
//
// These must match the #[no_mangle] extern "C" definitions in
// Rust/fprime-ffi/src/lib.rs exactly. Statuses are Fw::SerializeStatus
// integer values; mode is Fw::Endianness (0 = BIG, 1 = LITTLE).
//
// Pointer contract (mirrors Fw::LinearBufferBase): a null pointer is
// accepted wherever its paired length/capacity is 0. Source buffers may be
// any length. fprime_rs_ser_bytes permits its source to overlap the output
// (memmove semantics, as the C++ overload documents); fprime_rs_deser_bytes
// does not (the C++ path is memcpy).
// ======================================================================
#ifndef FPRIME_RUST_CODEC_H
#define FPRIME_RUST_CODEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t fprime_rs_abi_version(void);

#define FPRIME_RS_DECLARE_CODEC(name, type)                                                                         \
    int32_t fprime_rs_ser_##name(type value, int32_t mode, uint8_t* out, size_t capacity, size_t* written);         \
    int32_t fprime_rs_deser_##name(const uint8_t* bytes, size_t len, int32_t mode, type* value, size_t* consumed);

FPRIME_RS_DECLARE_CODEC(u8, uint8_t)
FPRIME_RS_DECLARE_CODEC(i8, int8_t)
FPRIME_RS_DECLARE_CODEC(u16, uint16_t)
FPRIME_RS_DECLARE_CODEC(i16, int16_t)
FPRIME_RS_DECLARE_CODEC(u32, uint32_t)
FPRIME_RS_DECLARE_CODEC(i32, int32_t)
FPRIME_RS_DECLARE_CODEC(u64, uint64_t)
FPRIME_RS_DECLARE_CODEC(i64, int64_t)
FPRIME_RS_DECLARE_CODEC(f32, float)
FPRIME_RS_DECLARE_CODEC(f64, double)
FPRIME_RS_DECLARE_CODEC(bool, bool)

#undef FPRIME_RS_DECLARE_CODEC

int32_t fprime_rs_ser_bytes(const uint8_t* bytes,
                            size_t len,
                            int32_t mode,
                            uint8_t* out,
                            size_t capacity,
                            size_t* written);

int32_t fprime_rs_deser_bytes(const uint8_t* bytes,
                              size_t len,
                              int32_t mode,
                              uint8_t* out,
                              size_t outCapacity,
                              size_t* outLen);

#ifdef __cplusplus
}
#endif

#endif  // FPRIME_RUST_CODEC_H
