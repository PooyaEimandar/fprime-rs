// ======================================================================
// \title  RustUtils.h
// \brief  C declarations for the fprime-rs utility-library FFI (Phase 1)
//
// These must match the #[no_mangle] extern "C" definitions in
// Rust/fprime-ffi/src/lib.rs exactly. Statuses are Fw::SerializeStatus
// integer values. The fprime_rs_cb_* functions operate on the C++
// Types::CircularBuffer object's own fields, passed by the shim
// (Utils/Types/CircularBufferRust.cpp); preconditions (setup called,
// non-null buffers) are FW_ASSERTed on the C++ side before calling.
//
// Overlap contract: the src/out buffers of fprime_rs_cb_serialize and
// fprime_rs_cb_peek_bytes must not overlap the ring store (Rust slice
// aliasing rules; the shim FW_ASSERTs this). The original C++ per-byte
// copies tolerated overlap; no framework caller relies on it.
// ======================================================================
#ifndef FPRIME_RUST_UTILS_H
#define FPRIME_RUST_UTILS_H

#include <stdint.h>

#include <Fw/FPrimeBasicTypes.h>

#ifdef __cplusplus
extern "C" {
#endif

int32_t fprime_rs_cb_serialize(uint8_t* store,
                               FwSizeType store_size,
                               FwSizeType* head_idx,
                               FwSizeType* allocated_size,
                               FwSizeType* high_water_mark,
                               const uint8_t* src,
                               FwSizeType size);

int32_t fprime_rs_cb_commit(uint8_t* store,
                            FwSizeType store_size,
                            FwSizeType* head_idx,
                            FwSizeType* allocated_size,
                            FwSizeType* high_water_mark,
                            FwSizeType size);

int32_t fprime_rs_cb_rotate(uint8_t* store,
                            FwSizeType store_size,
                            FwSizeType* head_idx,
                            FwSizeType* allocated_size,
                            FwSizeType* high_water_mark,
                            FwSizeType amount);

int32_t fprime_rs_cb_trim(uint8_t* store,
                          FwSizeType store_size,
                          FwSizeType* head_idx,
                          FwSizeType* allocated_size,
                          FwSizeType* high_water_mark,
                          FwSizeType amount);

int32_t fprime_rs_cb_peek_bytes(const uint8_t* store,
                                FwSizeType store_size,
                                FwSizeType head_idx,
                                FwSizeType allocated_size,
                                FwSizeType offset,
                                uint8_t* out,
                                FwSizeType size);

int32_t fprime_rs_cb_peek_u8(const uint8_t* store,
                             FwSizeType store_size,
                             FwSizeType head_idx,
                             FwSizeType allocated_size,
                             FwSizeType offset,
                             uint8_t* value);

int32_t fprime_rs_cb_peek_u32(const uint8_t* store,
                              FwSizeType store_size,
                              FwSizeType head_idx,
                              FwSizeType allocated_size,
                              FwSizeType offset,
                              uint32_t* value);

void fprime_rs_cfdp_checksum_update(uint32_t* value, const uint8_t* data, uint32_t offset, uint32_t length);

uint32_t fprime_rs_crc32_update(const uint8_t* data, FwSizeType length, uint32_t crc);

#ifdef __cplusplus
}
#endif

#endif  // FPRIME_RUST_UTILS_H
