# ======================================================================
# RustDemo.fpp
#
# A component that exists to demonstrate fprime-rs Phase 7: every line of C++
# for this component is GENERATED from this model by
# tools/fprime-rs/fpp_to_rust.py --component -- the class declaration
# (RustDemo.hpp) and the shim that defines it (RustDemoShim.cpp) as well as
# the C declarations and the Rust dispatch layer. The model you are reading,
# the Rust implementation and a CMakeLists are the whole component.
#
# Its ports cover what the generator can carry: primitives, bools, `ref`
# parameters written back to the caller, `ref Fw.Buffer`, parameters and
# return values whose type is an FPP enum, and emitting an event, a telemetry
# channel and a command response from Rust through the generated Env. Ports
# carrying structs, arrays or FPP strings are still refused by the generator:
# those would have to cross in their serialized form, which is a different
# design than passing values across extern "C".
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

        @ Query a running quantity, answered by Rust
        sync input port queryIn: Svc.RustDemoQuery

        @ Scale a value in place, through a `ref` parameter
        sync input port scaleIn: Svc.RustDemoScale

        @ Consume a buffer's bytes, through a `ref Fw.Buffer`
        sync input port consumeIn: Svc.RustDemoConsume

        @ Classify a raw kind, answered with an FPP enum
        sync input port classifyIn: Svc.RustDemoClassify

        @ The running total passed the value it was reset to
        event ExceededSeed(total: U32, seed: U32) \
            severity warning low \
            format "Running total {} passed its seed {}"

        @ Set the running total, answered by Rust
        sync command SET_TOTAL(value: U32)

        @ The running total
        telemetry Total: U32

        ###############################################################################
        # Standard AC ports, required once the component declares an event,           #
        # a telemetry channel or a command                                            #
        ###############################################################################
        @ Port for receiving commands
        command recv port cmdIn

        @ Port for sending command registration requests
        command reg port cmdRegOut

        @ Port for sending command responses
        command resp port cmdResponseOut

        @ Port for sending telemetry
        telemetry port tlmOut

        @ Port for requesting the current time
        time get port timeCaller

        @ Port for sending textual representation of events
        text event port logTextOut

        @ Port for sending events to downlink
        event port logOut

    }

}
