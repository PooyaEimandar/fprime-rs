# ======================================================================
# RustDemo.fpp
#
# A component that exists to demonstrate fprime-rs Phase 7: every line of
# glue connecting this component to its Rust implementation is GENERATED
# from this model by tools/fprime-rs/fpp_to_rust.py --component. Nothing
# between the autocoded C++ base and the Rust trait is hand-written.
#
# Its ports cover what the generator can carry: primitives, bools, values
# returned across the boundary, parameters whose type is an FPP enum, and
# emitting an event from Rust through the generated Env. Ports carrying
# Fw::Buffer, structs or arrays still need the hand-written marshalling the
# earlier phases established.
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
