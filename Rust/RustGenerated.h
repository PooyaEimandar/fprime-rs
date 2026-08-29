// ======================================================================
// \title  RustGenerated.h
// \brief  C declarations for the fpp-to-rust GENERATED type FFI (Phase 4)
//
// These expose types produced by tools/fprime-rs/fpp_to_rust.py from the
// FPP models, so the differential unit test can prove their serialization
// is byte-identical to the C++ code FPP autocodes from the same model.
// Statuses are Fw::SerializeStatus integer values; mode is Fw::Endianness
// (0 = BIG, 1 = LITTLE).
// ======================================================================
#ifndef FPRIME_RUST_GENERATED_H
#define FPRIME_RUST_GENERATED_H

#include <stdint.h>

#include <Fw/FPrimeBasicTypes.h>

#ifdef __cplusplus
extern "C" {
#endif

int32_t fprime_rs_gen_frameheader_ser(uint32_t startWord,
                                      uint32_t lengthField,
                                      int32_t mode,
                                      uint8_t* out,
                                      FwSizeType capacity,
                                      FwSizeType* written);

int32_t fprime_rs_gen_frameheader_deser(const uint8_t* bytes,
                                        FwSizeType len,
                                        int32_t mode,
                                        uint32_t* startWord,
                                        uint32_t* lengthField);

int32_t fprime_rs_gen_frametrailer_ser(uint32_t crcField,
                                       int32_t mode,
                                       uint8_t* out,
                                       FwSizeType capacity,
                                       FwSizeType* written);

// Type ids for the multi-construct differential, driven by the real in-tree
// config model default/config/ComCfg.fpp
#define FPRIME_RS_GEN_APID 0
#define FPRIME_RS_GEN_PVN 1
#define FPRIME_RS_GEN_FRAMECONTEXT 2
#define FPRIME_RS_GEN_TIMEBASE 3

//! Serialize a default-constructed value of the selected generated type
int32_t fprime_rs_gen_test_default(int32_t typeId,
                                   int32_t mode,
                                   uint8_t* out,
                                   FwSizeType capacity,
                                   FwSizeType* written);

//! Deserialize bytes into the selected generated type and re-serialize them
int32_t fprime_rs_gen_test_roundtrip(int32_t typeId,
                                     const uint8_t* bytes,
                                     FwSizeType len,
                                     int32_t mode,
                                     uint8_t* out,
                                     FwSizeType capacity,
                                     FwSizeType* written);

//! The generated SERIALIZED_SIZE of the selected type
FwSizeType fprime_rs_gen_test_serialized_size(int32_t typeId);

#ifdef __cplusplus
}
#endif

#endif  // FPRIME_RUST_GENERATED_H
