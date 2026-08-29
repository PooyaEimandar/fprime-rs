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

use crate::generated::rust_demo_glue::RustDemoHandlers;

/// Running total, shared across port calls.
pub struct RustDemo {
    total: AtomicU32,
}

impl RustDemo {
    pub const fn new() -> Self {
        Self {
            total: AtomicU32::new(0),
        }
    }

    /// The current running total.
    pub fn total(&self) -> u32 {
        self.total.load(Ordering::Relaxed)
    }
}

impl Default for RustDemo {
    fn default() -> Self {
        Self::new()
    }
}

impl RustDemoHandlers for RustDemo {
    fn add_in(&self, _port_num: i32, value: u32) {
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
    }

    fn reset_in(&self, _port_num: i32, seed: u32, enabled: bool) {
        // `enabled` false leaves the total untouched, so a reset can be wired
        // in and disabled by configuration without rewiring the topology.
        if enabled {
            self.total.store(seed, Ordering::Relaxed);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn accumulates_values() {
        let component = RustDemo::new();
        component.add_in(0, 5);
        component.add_in(0, 7);
        assert_eq!(component.total(), 12);
    }

    #[test]
    fn reset_seeds_only_when_enabled() {
        let component = RustDemo::new();
        component.add_in(0, 9);
        component.reset_in(0, 100, false);
        assert_eq!(
            component.total(),
            9,
            "a disabled reset must not change the total"
        );
        component.reset_in(0, 100, true);
        assert_eq!(component.total(), 100);
    }

    #[test]
    fn addition_wraps_rather_than_panicking() {
        let component = RustDemo::new();
        component.reset_in(0, u32::MAX, true);
        component.add_in(0, 2);
        assert_eq!(component.total(), 1);
    }
}
