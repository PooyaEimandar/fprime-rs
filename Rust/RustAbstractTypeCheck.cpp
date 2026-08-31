// ======================================================================
// \title  RustAbstractTypeCheck.cpp
// \brief  Cross-language check for FPP abstract types mirrored in Rust
//
// An FPP `type Foo` with no body is declared in the model and implemented in
// hand-written C++, so the model does not describe its serialization. Where
// Rust mirrors one anyway, the mirror rests on facts read out of the C++ by a
// person, and nothing else would notice if those facts changed.
//
// That is what this file is for. It is the same idea as RustConfigCheck.cpp,
// except that these widths cannot be generated from the model -- which is
// precisely why they need asserting by hand.
// ======================================================================
#include <Fw/Buffer/Buffer.hpp>
#include <Fw/FPrimeBasicTypes.hpp>

// Fw::Buffer serializes its wrapped pointer as a PlatformPointerCastType,
// which is declared in cmake/platform/<platform>/Platform/PlatformTypes.h and
// NOT in FPP. Rust/fprime-core/src/fw_buffer.rs assumes it is 8 bytes; on a
// target where it is not, that mirror is wrong and this stops the build
// rather than letting a 4-byte field be read as 8.
static_assert(sizeof(PlatformPointerCastType) == 8,
              "PlatformPointerCastType is not 8 bytes on this target, so the Rust FwBuffer mirror "
              "(Rust/fprime-core/src/fw_buffer.rs) does not match Fw::Buffer. It assumes a 64-bit "
              "pointer; a 32-bit target needs the mirror parameterised before it can be used.");

// The size the Rust mirror reports, against the C++ constant it mirrors.
// This is the assertion that actually matters: it holds whatever the
// individual field widths are, so a change to any of them lands here.
static_assert(static_cast<FwSizeType>(Fw::Buffer::SERIALIZED_SIZE) == 36,
              "Fw::Buffer::SERIALIZED_SIZE changed; update FwBuffer::SERIALIZED_SIZE in "
              "Rust/fprime-core/src/fw_buffer.rs to match");

// The field widths the mirror declares, each named so a failure says which.
static_assert(sizeof(FwSizeType) == 8, "FwBuffer::size/offset/capacity assume a 8-byte FwSizeType");
static_assert(sizeof(U32) == 4, "FwBuffer::context assumes a 4-byte U32");
