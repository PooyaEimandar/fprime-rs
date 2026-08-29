// ======================================================================
// \title  RustDemo.hpp
// \brief  hpp file for RustDemo component implementation class
//
// fprime-rs Phase 7 demonstration. This header and the FPP model are the
// only hand-written C++ artifacts for this component: the implementation
// file (RustDemoShim.cpp), the C declarations (Rust/RustDemoGlue.h) and
// the Rust dispatch layer are all generated from the model.
// ======================================================================

#ifndef Svc_RustDemo_HPP
#define Svc_RustDemo_HPP

#include "Svc/RustDemo/RustDemoComponentAc.hpp"

namespace Svc {

class RustDemo final : public RustDemoComponentBase {
  public:
    //! Construct RustDemo object
    RustDemo(const char* const compName);

    //! Destroy RustDemo object
    ~RustDemo();

    //! Install the Rust instance that answers this component's ports.
    //! DEFINED by the generated shim. Call once during initialization,
    //! before the topology connects ports.
    void setRustHandlers(const void* handlers);

  private:
    //! Handler for input port addIn
    void addIn_handler(FwIndexType portNum, uint32_t value) override;

    //! Handler for input port resetIn
    void resetIn_handler(FwIndexType portNum, uint32_t seed, bool enabled) override;

    //! The Rust instance, installed by setRustHandlers
    const void* m_rustHandlers;
};

}  // namespace Svc

#endif
