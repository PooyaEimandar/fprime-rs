module Svc {
    @ Add a value to the running total
    port RustDemoAdd(value: U32)

    @ Reset the running total, optionally seeding it
    port RustDemoReset(seed: U32, enabled: bool)

    @ Report the running total
    port RustDemoTotal(total: U32)

    @ Which running quantity to report
    enum RustDemoKind: U8 {
        @ The accumulated total
        TOTAL = 0
        @ How many values have been added
        COUNT = 1
    }

    @ Query a running quantity. Exercises two things the glue generator
    @ could not carry before Phase 10: a return value, and a parameter whose
    @ type is a user-defined FPP enum.
    port RustDemoQuery(kind: RustDemoKind) -> U32

    @ Scale the running total in place. Exercises a `ref` parameter: the
    @ handler writes through the reference and the caller sees the change.
    port RustDemoScale(ref value: U32)

    @ Add every byte of a buffer to the running total, then report how many
    @ were consumed by shrinking it. Exercises `ref Fw.Buffer`, the most
    @ common non-primitive parameter shape in the framework.
    port RustDemoConsume(ref data: Fw.Buffer)
}
