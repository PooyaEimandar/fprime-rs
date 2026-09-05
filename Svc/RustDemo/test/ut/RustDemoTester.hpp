// ======================================================================
// \title  RustDemoTester.hpp
// \brief  Test harness for the RustDemo component
// ======================================================================

#ifndef Svc_RustDemoTester_HPP
#define Svc_RustDemoTester_HPP

#include "Svc/RustDemo/RustDemo.hpp"
#include "Svc/RustDemo/RustDemoGTestBase.hpp"

namespace Svc {

class RustDemoTester : public RustDemoGTestBase {
  public:
    //! Constants expected by the autocoded test helpers
    static const FwSizeType MAX_HISTORY_SIZE = 10;
    static const FwEnumStoreType TEST_INSTANCE_ID = 0;

    RustDemoTester();
    ~RustDemoTester();

    //! Drive addIn, which the generated glue forwards into Rust
    void add(U32 value);

    //! Drive resetIn, which the generated glue forwards into Rust
    void reset(U32 seed, bool enabled);

    //! Drive queryIn, whose value comes back across the FFI and whose
    //! parameter is an FPP enum
    U32 query(RustDemoKind kind);

    //! Drive scaleIn; returns what Rust wrote back through the `ref`
    U32 scale(U32 value);

    //! Drive consumeIn with a real buffer; returns the buffer afterwards so
    //! the test can see what Rust wrote back
    Fw::Buffer consume(U8* data, FwSizeType size);

    //! Drive classifyIn, whose RETURN type is an FPP enum: Rust answers with
    //! the U8 representation and the generated shim rebuilds the object
    RustDemoKind classify(U8 kind);

    //! The autocoded GTest base declares its assertion helpers protected, so
    //! surface the two this suite needs.
    void assertExceededSeed(U32 index, U32 total, U32 seed);
    void assertEventCounts(U32 all, U32 exceededSeed);

    //! Send SET_TOTAL and let Rust answer the dispatcher
    void sendSetTotal(U32 value);

    //! Assert the command response Rust produced
    void expectCmdResponse(Fw::CmdResponse expected);

    //! Assert the Total channel Rust wrote
    void assertTotalChannel(U32 count, U32 value);

  private:
    //! Declared here, DEFINED by the autocoded helpers (UT_AUTO_HELPERS)
    //! against the `component` member below
    void connectPorts();
    void initComponents();

    RustDemo component;
};

}  // namespace Svc

#endif
