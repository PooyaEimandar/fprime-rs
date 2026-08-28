// Copyright 2026 California Institute of Technology
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// SPDX-License-Identifier: Apache-2.0

/*
 * Rust-backed implementation of crc32_ieee802_3_update (fprime-rs Phase 1).
 * Delegates to Rust/fprime-utils/src/crc32.rs, which computes the identical
 * IEEE 802.3 reflected CRC (poly 0xEDB88320, raw register, no complements).
 * Selected instead of Crc32.cpp via FPRIME_RUST_IMPL_UTILS_HASH; equivalence
 * is enforced by the existing Crc32 unit-test suite.
 */

#include "Crc32.hpp"
#include "Fw/FPrimeBasicTypes.hpp"
#include "Fw/Types/Assert.hpp"
#include "Rust/RustUtils.h"

namespace Utils {

U32 crc32_ieee802_3_update(const U8* data, FwSizeType length, U32 crc) {
    FW_ASSERT(data != nullptr, FwAssertArgType(length));
    return fprime_rs_crc32_update(data, length, crc);
}

}  // namespace Utils
