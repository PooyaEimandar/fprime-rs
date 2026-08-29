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

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
