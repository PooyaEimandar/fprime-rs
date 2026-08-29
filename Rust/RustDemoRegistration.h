// ======================================================================
// \title  RustDemoRegistration.h
// \brief  Per-deployment registration for the RustDemo component
//
// Hand-written, deliberately SEPARATE from the generated Rust/RustDemoGlue.h:
// the dispatch stubs are derived from the model, but which Rust instance
// backs which component object is a deployment decision the model cannot
// express. Keeping them apart means regenerating the glue never clobbers
// these declarations.
// ======================================================================
#ifndef FPRIME_RS_RUSTDEMO_REGISTRATION_H
#define FPRIME_RS_RUSTDEMO_REGISTRATION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//! The opaque handler pointer for this deployment's RustDemo instance, to be
//! passed to RustDemo::setRustHandlers().
const void* fprime_rs_rustdemo_instance(void);

//! The instance's running total, for tests and telemetry
uint32_t fprime_rs_rustdemo_total(void);

#ifdef __cplusplus
}
#endif

#endif  // FPRIME_RS_RUSTDEMO_REGISTRATION_H
