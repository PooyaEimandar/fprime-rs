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


U32 RustDemoTester::query(RustDemoKind kind) {
    return this->invoke_to_queryIn(0, kind);
}

void RustDemoTester::assertExceededSeed(U32 index, U32 total, U32 seed) {
    this->assertEvents_ExceededSeed(__FILE__, __LINE__, index, total, seed);
}

void RustDemoTester::assertEventCounts(U32 all, U32 exceededSeed) {
    this->assertEvents_size(__FILE__, __LINE__, all);
    this->assertEvents_ExceededSeed_size(__FILE__, __LINE__, exceededSeed);
}

U32 RustDemoTester::scale(U32 value) {
    this->invoke_to_scaleIn(0, value);
    return value;
}

Fw::Buffer RustDemoTester::consume(U8* data, FwSizeType size) {
    Fw::Buffer buffer(data, size);
    this->invoke_to_consumeIn(0, buffer);
    return buffer;
}

void RustDemoTester::sendSetTotal(U32 value) {
    this->sendCmd_SET_TOTAL(0, 10, value);
}

void RustDemoTester::expectCmdResponse(Fw::CmdResponse expected) {
    this->assertCmdResponse_size(__FILE__, __LINE__, 1);
    this->assertCmdResponse(__FILE__, __LINE__, 0, RustDemoComponentBase::OPCODE_SET_TOTAL, 10, expected);
}

void RustDemoTester::assertTotalChannel(U32 count, U32 value) {
    this->assertTlm_Total_size(__FILE__, __LINE__, count);
    if (count > 0) {
        this->assertTlm_Total(__FILE__, __LINE__, 0, value);
    }
}

}  // namespace Svc
