// ======================================================================
// \title  RustPanicHook.cpp
// \brief  Routes a Rust panic into the FW_ASSERT fault path
//
// The Rust workspace builds with panic=abort and a #[panic_handler] that
// calls this hook (see Rust/fprime-ffi/src/lib.rs). A Rust panic is a
// contract violation of the same class as a failed FW_ASSERT, so it takes
// the same fault path: registered assert hooks fire, then execution halts.
// ======================================================================

#include <cstdlib>

#include <Fw/FPrimeBasicTypes.hpp>
#include <Fw/Types/Assert.hpp>

extern "C" [[noreturn]] void fprime_rs_panic_hook(const unsigned char* file, size_t fileLen, unsigned int line) {
    (void)file;
    (void)fileLen;
    FW_ASSERT(0, static_cast<FwAssertArgType>(line));
    // FW_ASSERT can return in configurations where assertions are disabled.
    // Continuing past a Rust panic is never sound; abort unconditionally.
    // (An empty infinite loop would be removable UB pre-C++26.)
    abort();
}
