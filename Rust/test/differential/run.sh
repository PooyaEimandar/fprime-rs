#!/usr/bin/env bash
# Build and run the Phase 14 differential oracles.
#
# Needs two build caches in opposite switch positions, because the point is
# to have both implementations of the same function in one process:
#
#   fprime-util generate --ut
#   fprime-util generate --ut --build-cache "$PWD/build-rust-off" -DFPRIME_ENABLE_RUST=OFF
#   for d in Fw/Types Svc/FprimeProtocol Svc/ComQueue; do
#       (cd "$d" && fprime-util build --ut --build-cache "$PWD/../../build-rust-off")
#   done
#
# GeneratedTypesDiff needs the last two: it compares against the autocoded
# C++ types, which only exist once their modules have been built.
#
# then: Rust/test/differential/run.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
OFF="${FPRIME_OFF_CACHE:-$REPO/build-rust-off}"
ON="${FPRIME_ON_CACHE:-$REPO/build-fprime-automatic-native-ut}"
OUT="${TMPDIR:-/tmp}/fprime-rs-diff"

# The C++ side must come from the switch-OFF cache, or the archive holds the
# shim and both "implementations" are the same one -- a test that cannot fail.
CPP_LIB="$(find "$OFF/lib" -name libFw_Types.a -print -quit 2>/dev/null || true)"
RUST_LIB="$ON/fprime-rust-target/release/libfprime_rust.a"
for lib in "$CPP_LIB" "$RUST_LIB"; do
    if [ -z "$lib" ] || [ ! -f "$lib" ]; then
        echo "missing archive: ${lib:-libFw_Types.a in $OFF/lib}" >&2
        echo "see the header of this script for the two caches it needs" >&2
        exit 2
    fi
done
if grep -q "StringUtilsRust" "$OFF/build.ninja" 2>/dev/null; then
    echo "$OFF was configured with the Rust StringUtils selected; it must be the OFF position" >&2
    exit 2
fi

mkdir -p "$OUT"
LOGGER="$(dirname "$CPP_LIB")/libFw_Logger.a"
OS_LIB="$(dirname "$CPP_LIB")/libOs.a"

for src in "$HERE"/*Diff.cpp; do
    name="$(basename "$src" .cpp)"
    echo "== $name"
    # --coverage because the switch-OFF cache is built with it, and its
    # objects reference the profiling runtime.
    # Link every library the ON-position build produced, so a test that needs
    # an autocoded type (Svc/ComQueue, ComCfg, Svc/FprimeProtocol) finds it.
    EXTRA=""
    for lib in "$(dirname "$CPP_LIB")"/*.a; do
        case "$lib" in *libFw_Types.a) continue;; esac
        EXTRA="$EXTRA $lib"
    done
    # shellcheck disable=SC2086
    ${CXX:-c++} -std=c++14 -g -DBUILD_UT -fsanitize=address,undefined --coverage \
        -I"$REPO" -I"$OFF" -I"$OFF/F-Prime" \
        -I"$OFF/cmake/platform/unix/Platform/.." -I"$OFF/F-Prime/default/config/.." \
        "$src" "$REPO/Fw/Types/snprintf_format.cpp" "$REPO/Rust/RustPanicHook.cpp" \
        -o "$OUT/$name" \
        "$CPP_LIB" "$RUST_LIB" $EXTRA "$LOGGER" "$OS_LIB" "$CPP_LIB"
    (cd "$OUT" && "./$name")
done
