// ======================================================================
// \title  RustDemoTester.cpp
// \brief  Test harness for the RustDemo component
// ======================================================================

#include "RustDemoTester.hpp"

#include "Rust/RustDemoRegistration.h"

namespace Svc {

RustDemoTester ::RustDemoTester() : RustDemoGTestBase("RustDemoTester", MAX_HISTORY_SIZE), component("RustDemo") {
    // Install the Rust instance before the ports are connected, which is the
    // ordering the generated dispatch requires.
    this->component.setRustHandlers(fprime_rs_rustdemo_instance());
    this->initComponents();
    this->connectPorts();
}

RustDemoTester ::~RustDemoTester() {}

void RustDemoTester ::add(U32 value) {
    this->invoke_to_addIn(0, value);
}

void RustDemoTester ::reset(U32 seed, bool enabled) {
    this->invoke_to_resetIn(0, seed, enabled);
}

}  // namespace Svc
