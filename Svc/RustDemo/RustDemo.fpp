# ======================================================================
# RustDemo.fpp
#
# A component that exists to demonstrate fprime-rs Phase 7: every line of
# glue connecting this component to its Rust implementation is GENERATED
# from this model by tools/fprime-rs/fpp_to_rust.py --component. Nothing
# between the autocoded C++ base and the Rust trait is hand-written.
#
# Its ports are deliberately primitive-only, which is the shape the glue
# generator supports; ports carrying Fw::Buffer or user types still need
# the hand-written marshalling the earlier phases established.
# ======================================================================

module Svc {

    @ Accumulates values and reports the running total
    passive component RustDemo {

        @ Add a value to the running total
        sync input port addIn: Svc.RustDemoAdd

        @ Reset the running total
        sync input port resetIn: Svc.RustDemoReset

        @ Report the running total
        output port totalOut: Svc.RustDemoTotal

    }

}
