#!/usr/bin/env python3
"""Measure how much of the framework's port surface the glue generator carries.

The roadmap makes claims like "the generator accepts ten of the 76 components"
and "N of the framework's ports are carried". Those numbers move with every
change to `described_type`, and a number nobody can reproduce is a number that
quietly goes wrong. This recomputes them from the real model.

    python3 tools/fprime-rs/port_coverage.py build-fprime-automatic-native-ut
    python3 tools/fprime-rs/port_coverage.py <build> --why   # refusal reasons

The build directory is used only for its `locs.fpp`, which is the one place
that lists every model file in the framework. Any configured F Prime build
directory works; nothing is compiled.

Needs fpp-to-json on PATH (activate fprime-venv).
"""

import argparse
import collections
import pathlib
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import fpp_to_rust as G  # noqa: E402


def load(build_dir):
    """Compile every model the build knows about and return (ast, analysis)."""
    locs = build_dir / "locs.fpp"
    if not locs.is_file():
        sys.exit(f"port_coverage: no locs.fpp in {build_dir} (configure it first)")
    sources = set()
    for line in locs.read_text().splitlines():
        match = re.search(r'at "([^"]+)"', line)
        if match:
            sources.add((locs.parent / match.group(1)).resolve())
    with tempfile.TemporaryDirectory() as tmp:
        try:
            return G.run_fpp_to_json([locs.resolve()] + sorted(sources), pathlib.Path(tmp))
        except subprocess.CalledProcessError as exc:
            sys.exit(
                "port_coverage: fpp-to-json failed:\n"
                + exc.stderr.decode(errors="replace")
            )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("build_dir", type=pathlib.Path)
    ap.add_argument("--why", action="store_true", help="list every refusal")
    args = ap.parse_args()

    ast, analysis = load(args.build_dir)

    # --- port definitions ---------------------------------------------------
    carried, refused = [], []
    for node_id, definition in G.iter_port_definitions(ast):
        name = definition["name"]
        try:
            G.port_signature(definition, analysis, name)
        except G.GeneratorError as exc:
            refused.append((name, str(exc)))
        else:
            carried.append(name)

    total = len(carried) + len(refused)
    print(f"ports:      {len(carried)}/{total} carried")

    # --- components ---------------------------------------------------------
    whole, partial = [], []
    for entry in analysis.get("componentMap", {}).values():
        component = G.component_ast_name(ast, entry["aNode"]["astNodeId"])
        if component is None:
            continue
        try:
            ports = G.collect_ports(ast, analysis, component, component)
        except G.GeneratorError:
            partial.append(component)
            continue
        # A component with no input ports the generator can carry is not a
        # candidate either: there would be nothing for Rust to answer.
        (whole if ports else partial).append(component)

    print(f"components: {len(whole)}/{len(whole) + len(partial)} fully carried")
    for component in sorted(whole):
        print(f"  + {component}")

    if args.why:
        # Group refusals so the report says which SHAPE is missing, not which
        # forty ports are missing it.
        reasons = collections.Counter()
        for _, message in refused:
            # The location prefix is "Component.port.param: "; the reason is
            # the first sentence after it. Splitting on "." alone would cut
            # "Fw.Buffer" in half and report a reason of "Fw".
            reason = message.split(": ", 1)[-1]
            reasons[reason.split(". ")[0].rstrip(".")] += 1
        print("\nrefusals by reason:")
        for reason, count in reasons.most_common():
            print(f"  {count:3d}  {reason}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
