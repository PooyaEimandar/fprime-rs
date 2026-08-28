// ======================================================================
// \title  CFDP/Checksum/ChecksumRust.cpp
// \brief  Rust-backed implementation of the CFDP checksum (fprime-rs Phase 1)
//
// The update algorithm delegates to Rust/fprime-utils/src/checksum.rs,
// which mirrors Checksum.cpp exactly. Selected instead of Checksum.cpp via
// FPRIME_RUST_IMPL_CFDP_CHECKSUM; equivalence is enforced by the unit tests
// in test/ut/ChecksumMain.cpp.
// ======================================================================

#include "CFDP/Checksum/Checksum.hpp"
#include "Fw/Types/Assert.hpp"
#include "Rust/RustUtils.h"

// Note: the private addWordAligned/addWordUnaligned/addByteAtOffset members
// declared in Checksum.hpp are intentionally left undefined here — they are
// never odr-used (the algorithm lives in Rust), so no definition is required.

namespace CFDP {

Checksum ::Checksum() : m_value(0) {}

Checksum ::Checksum(const U32 value) : m_value(value) {}

Checksum ::Checksum(const Checksum& original) {
    this->m_value = original.getValue();
}

Checksum ::~Checksum() {}

Checksum& Checksum ::operator=(const Checksum& checksum) {
    this->m_value = checksum.m_value;
    return *this;
}

bool Checksum ::operator==(const Checksum& checksum) const {
    return this->m_value == checksum.m_value;
}

bool Checksum ::operator!=(const Checksum& checksum) const {
    return not(*this == checksum);
}

U32 Checksum ::getValue() const {
    return this->m_value;
}

void Checksum ::update(const U8* const data, const U32 offset, const U32 length) {
    FW_ASSERT(data != nullptr);
    fprime_rs_cfdp_checksum_update(&this->m_value, data, offset, length);
}

}  // namespace CFDP
