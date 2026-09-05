// ======================================================================
// \title  RustDemoTestMain.cpp
// \brief  End-to-end proof for fprime-rs Phase 7
//
// Every layer between these port calls and the Rust code that answers them is
// GENERATED from Svc/RustDemo/RustDemo.fpp:
//
//   invoke_to_addIn (autocoded tester)
//     -> RustDemo::addIn_handler   (generated shim, RustDemoShim.cpp)
//     -> fprime_rs_rustdemo_addIn  (generated stub, rust_demo_glue.rs)
//     -> RustDemo::add_in          (hand-written Rust, rust_demo.rs)
//
// Nothing in that chain was written by hand except the FPP model, the
// component header, and the Rust behavior itself.
// ======================================================================

#include <gtest/gtest.h>

#include "Rust/RustDemoRegistration.h"
#include "RustDemoTester.hpp"

namespace {

//! Start each test from a known total. The tester installs the Rust instance
//! on the component at construction, before any port is connected.
void resetComponent(Svc::RustDemoTester& tester) {
    tester.reset(0, true);
}

}  // namespace

TEST(RustDemo, PortCallReachesRustAndAccumulates) {
    Svc::RustDemoTester tester;
    resetComponent(tester);

    tester.add(5);
    ASSERT_EQ(fprime_rs_rustdemo_total(), 5u) << "the generated glue did not reach Rust";

    tester.add(7);
    ASSERT_EQ(fprime_rs_rustdemo_total(), 12u);
}

TEST(RustDemo, MultipleParametersCrossTheGeneratedBoundary) {
    // resetIn carries a U32 and a bool, so this pins that the generated
    // signatures agree across FPP, C++ and Rust for a mixed parameter list.
    Svc::RustDemoTester tester;
    resetComponent(tester);

    tester.add(9);
    tester.reset(100, false);
    ASSERT_EQ(fprime_rs_rustdemo_total(), 9u) << "a disabled reset must leave the total alone";

    tester.reset(100, true);
    ASSERT_EQ(fprime_rs_rustdemo_total(), 100u);
}

TEST(RustDemo, BehaviorMatchesTheRustUnitTests) {
    // The same wrapping behavior the Rust unit tests assert, observed here
    // through the real component ports.
    Svc::RustDemoTester tester;
    resetComponent(tester);

    tester.reset(0xFFFFFFFFu, true);
    tester.add(2);
    ASSERT_EQ(fprime_rs_rustdemo_total(), 1u);
}

TEST(RustDemo, ReturnValueAndEnumParameterCrossTheGeneratedBoundary) {
    // Phase 10: the glue generator can now carry a value back across the FFI
    // and a parameter whose type is a user-defined FPP enum. Neither was
    // expressible before, and between them they are what a real framework
    // component's ports look like.
    Svc::RustDemoTester tester;
    resetComponent(tester);

    tester.add(4);
    tester.add(6);
    ASSERT_EQ(tester.query(Svc::RustDemoKind::TOTAL), 10u) << "the return value did not come back";
    ASSERT_EQ(tester.query(Svc::RustDemoKind::COUNT), 2u) << "the enum parameter did not select";
}

TEST(RustDemo, EnumReturnValueCrossesTheGeneratedBoundary) {
    // Phase 12: an enum RETURN value. The representation crosses extern "C"
    // and the generated shim rebuilds Svc::RustDemoKind from it -- which is
    // the shape of every status-returning port in the framework
    // (Drv::I2cStatus, Fw::Success, Fw::TlmValid, ...).
    Svc::RustDemoTester tester;
    resetComponent(tester);

    ASSERT_EQ(tester.classify(0), Svc::RustDemoKind::TOTAL);
    ASSERT_EQ(tester.classify(1), Svc::RustDemoKind::COUNT);
    // A raw value the enumeration does not have. Rust clamps it, so what
    // comes back is a valid object rather than a fault -- and the object is
    // a real RustDemoKind, not an integer that merely compares equal.
    const Svc::RustDemoKind answered = tester.classify(200);
    ASSERT_EQ(answered, Svc::RustDemoKind::TOTAL);
    ASSERT_TRUE(answered.isValid()) << "the shim handed back a value outside the enumeration";
}

TEST(RustDemo, RustEmitsAnEventThroughTheGeneratedEnv) {
    // The one thing Rust cannot do alone: the autocoded base owns the time
    // port and the event buffer, so emitting goes back out through the Env
    // the shim builds. Observed here as a real event on the component.
    Svc::RustDemoTester tester;
    resetComponent(tester);

    tester.reset(10, true);
    tester.add(5);  // total 15, past the seed of 10

    tester.assertEventCounts(1, 1);
    tester.assertExceededSeed(0, 15, 10);
}

TEST(RustDemo, NoEventWhenTheSeedIsNotPassed) {
    Svc::RustDemoTester tester;
    resetComponent(tester);

    tester.reset(100, true);
    tester.add(5);  // total 105... which IS past 100

    tester.assertEventCounts(1, 1);

    Svc::RustDemoTester quiet;
    resetComponent(quiet);
    quiet.add(5);  // never reset, so there is no seed to have passed
    quiet.assertEventCounts(0, 0);
}

TEST(RustDemo, RefParameterComesBackChanged) {
    // Phase 12: a `ref` parameter crosses as a pointer and the caller sees
    // what Rust wrote. 20 of the framework's 79 port parameters are `ref`.
    Svc::RustDemoTester tester;
    resetComponent(tester);

    ASSERT_EQ(tester.scale(21u), 42u);
    ASSERT_EQ(tester.scale(0u), 0u);
    // Saturating rather than wrapping: a port has no channel to report an
    // overflow on.
    ASSERT_EQ(tester.scale(0xFFFFFFFFu), 0xFFFFFFFFu);
}

TEST(RustDemo, BufferCrossesAndIsWrittenBack) {
    // Phase 12: Fw::Buffer is the most common non-primitive port parameter in
    // the framework (10 of 79). It cannot cross as an object -- it derives
    // Fw::Serializable and so has a vtable -- so the shim marshals the
    // (data, size, context) triple and writes size and context back.
    Svc::RustDemoTester tester;
    resetComponent(tester);

    U8 payload[] = {1, 2, 3, 4, 5};
    Fw::Buffer after = tester.consume(payload, sizeof(payload));

    ASSERT_EQ(fprime_rs_rustdemo_total(), 15u) << "Rust did not see the bytes";
    ASSERT_EQ(after.getSize(), 0u) << "the shrunk size was not written back";
    ASSERT_EQ(after.getContext(), sizeof(payload)) << "the context was not written back";
}

TEST(RustDemo, EmptyBufferIsHandled) {
    Svc::RustDemoTester tester;
    resetComponent(tester);

    U8 unused = 0;
    Fw::Buffer after = tester.consume(&unused, 0);
    ASSERT_EQ(fprime_rs_rustdemo_total(), 0u);
    ASSERT_EQ(after.getContext(), 0u);
}

TEST(RustDemo, RustAnswersACommandAndWritesTelemetry) {
    // Phase 13: a command is what made a generated shim abstract -- the
    // autocoded base leaves *_cmdHandler pure virtual. Rust now implements it,
    // answers the dispatcher, and writes a channel, all through the Env.
    Svc::RustDemoTester tester;
    resetComponent(tester);

    tester.sendSetTotal(500u);

    ASSERT_EQ(fprime_rs_rustdemo_total(), 500u) << "the command did not reach Rust";
    tester.expectCmdResponse(Fw::CmdResponse::OK);
    tester.assertTotalChannel(1, 500u);
}

TEST(RustDemo, RustCanRejectACommand) {
    // The status a command returns is a decision, and it is Rust's: the
    // dispatcher acts on whatever comes back through the Env.
    Svc::RustDemoTester tester;
    resetComponent(tester);

    tester.sendSetTotal(2000000u);  // over the limit Rust enforces

    tester.expectCmdResponse(Fw::CmdResponse::VALIDATION_ERROR);
    tester.assertTotalChannel(0, 0u);
    ASSERT_EQ(fprime_rs_rustdemo_total(), 0u) << "a rejected command must change nothing";
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
