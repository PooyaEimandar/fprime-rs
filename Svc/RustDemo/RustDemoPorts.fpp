module Svc {
    @ Add a value to the running total
    port RustDemoAdd(value: U32)

    @ Reset the running total, optionally seeding it
    port RustDemoReset(seed: U32, enabled: bool)

    @ Report the running total
    port RustDemoTotal(total: U32)
}
