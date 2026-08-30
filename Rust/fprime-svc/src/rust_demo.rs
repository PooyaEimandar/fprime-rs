//! The RustDemo component's behavior, in Rust (fprime-rs Phase 7).
//!
//! This is the *only* hand-written artifact for the component besides its FPP
//! model and its header: the handler trait, the dispatch stubs, the C
//! declarations and the C++ shim are all generated from the model by
//! `tools/fprime-rs/fpp_to_rust.py --component`.
//!
//! The component keeps a running total. Handlers take `&self` because the
//! generated dispatch holds a `'static` reference and sync ports may be
//! entered from a caller's thread, so mutable state lives behind an atomic
//! rather than `&mut self` — the concurrency contract established in the
//! interop design.

use core::sync::atomic::{AtomicU32, Ordering};

use crate::generated::rust_demo_glue::{cmd_response, Buffer, Env, RustDemoHandlers};

/// Mirrors the FPP enum `Svc.RustDemoKind`, which crosses as its `U8` repr.
const KIND_TOTAL: u8 = 0;
/// The largest total SET_TOTAL accepts; anything above is a validation error.
const MAX_SETTABLE_TOTAL: u32 = 1_000_000;
const KIND_COUNT: u8 = 1;

/// Running total, shared across port calls.
pub struct RustDemo {
    total: AtomicU32,
    count: AtomicU32,
    seed: AtomicU32,
}

impl RustDemo {
    pub const fn new() -> Self {
        Self {
            total: AtomicU32::new(0),
            count: AtomicU32::new(0),
            seed: AtomicU32::new(0),
        }
    }

    /// The current running total.
    pub fn total(&self) -> u32 {
        self.total.load(Ordering::Relaxed)
    }

    /// How many values have been added.
    pub fn count(&self) -> u32 {
        self.count.load(Ordering::Relaxed)
    }
}

impl Default for RustDemo {
    fn default() -> Self {
        Self::new()
    }
}

impl RustDemoHandlers for RustDemo {
    fn add_in(&self, _port_num: i32, env: &Env, value: u32) {
        // Wrapping is the specified behavior: the total is a counter, and a
        // panic on overflow would take down the component.
        let mut current = self.total.load(Ordering::Relaxed);
        loop {
            let next = current.wrapping_add(value);
            match self.total.compare_exchange_weak(
                current,
                next,
                Ordering::Relaxed,
                Ordering::Relaxed,
            ) {
                Ok(_) => break,
                Err(observed) => current = observed,
            }
        }
        self.count.fetch_add(1, Ordering::Relaxed);

        // Emitting an event is the one thing Rust cannot do on its own: the
        // autocoded base owns the time port and the event buffer, so the call
        // goes back out through the generated Env.
        let seed = self.seed.load(Ordering::Relaxed);
        let total = self.total.load(Ordering::Relaxed);
        if seed != 0 && total > seed {
            env.exceeded_seed(total, seed);
        }
    }

    fn reset_in(&self, _port_num: i32, _env: &Env, seed: u32, enabled: bool) {
        // `enabled` false leaves the total untouched, so a reset can be wired
        // in and disabled by configuration without rewiring the topology.
        if enabled {
            self.total.store(seed, Ordering::Relaxed);
            self.seed.store(seed, Ordering::Relaxed);
            // The count is part of the running state, so a reset clears it
            // too -- otherwise it accumulates across resets and COUNT stops
            // meaning "values added since the last reset".
            self.count.store(0, Ordering::Relaxed);
        }
    }

    fn scale_in(&self, _port_num: i32, _env: &Env, value: &mut u32) {
        // A `ref` parameter: the caller sees what we write back. Saturating,
        // because a port has no way to report an overflow.
        *value = value.saturating_mul(2);
    }

    fn consume_in(&self, _port_num: i32, _env: &Env, data: &mut Buffer) {
        // SAFETY: the buffer belongs to the C++ Fw::Buffer for the duration
        // of this call, which is exactly this scope.
        let bytes = unsafe { data.as_slice() };
        let sum: u32 = bytes.iter().map(|b| u32::from(*b)).sum();
        let consumed = bytes.len();
        self.add_in(0, _env, sum);
        // Report consumption by shrinking the buffer, which the shim writes
        // back to the C++ object.
        data.size = 0;
        data.context = consumed as u32;
    }

    fn cmd_set_total(&self, env: &Env, opcode: u32, cmd_seq: u32, value: u32) {
        // A command is the one place Rust decides a status the framework
        // acts on, so the validation and the response both live here.
        if value > MAX_SETTABLE_TOTAL {
            env.respond(opcode, cmd_seq, cmd_response::VALIDATION_ERROR);
            return;
        }
        self.total.store(value, Ordering::Relaxed);
        self.count.store(0, Ordering::Relaxed);
        // Telemetry also goes back out through the Env: the autocoded base
        // owns the channel ID and the time tag.
        env.tlm_total(value);
        env.respond(opcode, cmd_seq, cmd_response::OK);
    }

    fn query_in(&self, _port_num: i32, _env: &Env, kind: u8) -> u32 {
        match kind {
            KIND_TOTAL => self.total(),
            KIND_COUNT => self.count(),
            // The enum has no other members, and a port answering a query has
            // no channel to report one: fall back to the total.
            _ => self.total(),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// An Env with no callbacks installed: every emitter is a no-op, which is
    /// what the generated `Env` methods do for a `None` entry. It lets these
    /// tests exercise the handlers without a C++ component behind them.
    fn quiet_env() -> Env {
        Env {
            component: core::ptr::null_mut(),
            exceeded_seed: None,
            tlm_total: None,
            cmd_response: None,
        }
    }

    #[test]
    fn accumulates_values() {
        let component = RustDemo::new();
        component.add_in(0, &quiet_env(), 5);
        component.add_in(0, &quiet_env(), 7);
        assert_eq!(component.total(), 12);
    }

    #[test]
    fn reset_seeds_only_when_enabled() {
        let component = RustDemo::new();
        component.add_in(0, &quiet_env(), 9);
        component.reset_in(0, &quiet_env(), 100, false);
        assert_eq!(
            component.total(),
            9,
            "a disabled reset must not change the total"
        );
        component.reset_in(0, &quiet_env(), 100, true);
        assert_eq!(component.total(), 100);
    }

    #[test]
    fn query_answers_by_kind() {
        let component = RustDemo::new();
        component.add_in(0, &quiet_env(), 4);
        component.add_in(0, &quiet_env(), 6);
        assert_eq!(component.query_in(0, &quiet_env(), KIND_TOTAL), 10);
        assert_eq!(component.query_in(0, &quiet_env(), KIND_COUNT), 2);
        assert_eq!(
            component.query_in(0, &quiet_env(), 200),
            10,
            "an out-of-range kind falls back to the total rather than faulting"
        );
    }

    #[test]
    fn event_fires_only_once_the_seed_is_passed() {
        use core::sync::atomic::AtomicUsize;
        static FIRED: AtomicUsize = AtomicUsize::new(0);
        unsafe extern "C" fn record(_component: *mut core::ffi::c_void, _total: u32, _seed: u32) {
            FIRED.fetch_add(1, Ordering::Relaxed);
        }
        let env = Env {
            component: core::ptr::null_mut(),
            exceeded_seed: Some(record),
            tlm_total: None,
            cmd_response: None,
        };
        let component = RustDemo::new();
        component.reset_in(0, &env, 10, true);
        component.add_in(0, &env, 5); // total 15 > seed 10
        assert_eq!(FIRED.load(Ordering::Relaxed), 1);
        component.add_in(0, &env, 1); // still above the seed
        assert_eq!(FIRED.load(Ordering::Relaxed), 2);
    }

    #[test]
    fn no_event_without_a_seed() {
        let component = RustDemo::new();
        // seed 0 means "never reset", so nothing to have passed.
        component.add_in(0, &quiet_env(), 1000);
        assert_eq!(component.total(), 1000);
    }

    #[test]
    fn addition_wraps_rather_than_panicking() {
        let component = RustDemo::new();
        component.reset_in(0, &quiet_env(), u32::MAX, true);
        component.add_in(0, &quiet_env(), 2);
        assert_eq!(component.total(), 1);
    }
}
