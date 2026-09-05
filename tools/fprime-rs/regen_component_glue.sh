#!/usr/bin/env bash
#
# Regenerate the checked-in component glue for Svc/RustDemo, or verify that
# what is checked in still matches the model.
#
# The type-mode generator is wired into the build (cmake/autocoder/rust.cmake
# regenerates and compares on every configure), but component mode is not: its
# outputs are a C++ source file and a C header that the build has to compile,
# so they are checked in. Without this script the command that produced them
# lived nowhere, and the dependency list -- which is not obvious, PlatformCfg
# and ComCfg are both needed to resolve FpConstants -- had to be rediscovered.
#
#   tools/fprime-rs/regen_component_glue.sh          # rewrite in place
#   tools/fprime-rs/regen_component_glue.sh --check  # fail if stale
#
# Needs fpp-to-json on PATH (activate fprime-venv).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

if ! command -v fpp-to-json >/dev/null 2>&1; then
    echo "regen_component_glue: fpp-to-json not on PATH (activate fprime-venv)" >&2
    exit 1
fi

CHECK=0
if [ "${1:-}" = "--check" ]; then
    CHECK=1
elif [ $# -ne 0 ]; then
    echo "usage: $0 [--check]" >&2
    exit 2
fi

# The model, then everything it needs to resolve. Order does not matter to
# fpp-to-json, but every transitive definition has to be present: the
# component's standard command/event/telemetry ports pull in Fw.Cmd, Fw.Log,
# Fw.Tlm and Fw.Time, and those pull in the platform and config constants.
DEPS=(
    Svc/RustDemo/RustDemo.fpp
    Svc/RustDemo/RustDemoPorts.fpp
    Fw/Buffer/Buffer.fpp
    Fw/Cmd/Cmd.fpp
    Fw/Log/Log.fpp
    Fw/Tlm/Tlm.fpp
    Fw/Time/Time.fpp
    Fw/Types/Types.fpp
    cmake/platform/unix/Platform/PlatformTypes.fpp
    default/config/FpConfig.fpp
    default/config/ComCfg.fpp
    default/config/AcConstants.fpp
    default/config/FpConstants.fpp
)

RS=Rust/fprime-svc/src/generated/rust_demo_glue.rs
HDR=Rust/RustDemoGlue.h
SHIM=Svc/RustDemo/RustDemoShim.cpp
HPP=Svc/RustDemo/RustDemo.hpp

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

python3 tools/fprime-rs/fpp_to_rust.py \
    --module-name "Svc/RustDemo/RustDemo.fpp" \
    --component RustDemo \
    --out "$WORK/$(basename "$RS")" \
    --out-header "$WORK/$(basename "$HDR")" \
    --out-shim "$WORK/$(basename "$SHIM")" \
    --out-component-header "$WORK/$(basename "$HPP")" \
    --shim-include "Svc/RustDemo/RustDemo.hpp" \
    --glue-include "Rust/RustDemoGlue.h" \
    "${DEPS[@]}"

status=0
for pair in "$RS" "$HDR" "$SHIM" "$HPP"; do
    fresh="$WORK/$(basename "$pair")"
    if [ "$CHECK" = 1 ]; then
        if ! diff -u "$pair" "$fresh"; then
            echo "regen_component_glue: $pair is stale" >&2
            status=1
        fi
    else
        cp "$fresh" "$pair"
    fi
done

if [ "$CHECK" = 1 ] && [ "$status" = 0 ]; then
    echo "regen_component_glue: checked-in glue matches the model"
fi
exit "$status"
