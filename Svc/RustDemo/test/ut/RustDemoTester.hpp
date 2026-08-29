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

  private:
    //! Declared here, DEFINED by the autocoded helpers (UT_AUTO_HELPERS)
    //! against the `component` member below
    void connectPorts();
    void initComponents();

    RustDemo component;
};

}  // namespace Svc

#endif
