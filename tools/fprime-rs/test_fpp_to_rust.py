#!/usr/bin/env python3
"""Tests for the FPP-to-Rust type generator.

fpp_to_rust.py is ~2,500 lines that everything downstream of Phase 4 depends
on, and it had no tests. The cases here are deliberately not a survey of the
generator: they pin the things that were actually wrong, plus the invariants
whose failure would be silent.

Silence is the theme. The generator's worst failure mode is not an exception
-- it is emitting a file that looks complete and is not. It did exactly that
for every type defined inside a component body, reporting "0 arrays" for
modules that define several. So one test asserts the guard against that fires.

    python3 tools/fprime-rs/test_fpp_to_rust.py

One trap when adding a case: FPP has more reserved words than it looks, and
they collide with obvious member names -- `at` and `id` are both keywords, and
using either yields a bare "} expected" pointing at the member. The harness
prints fpp-to-json's own diagnostics so that reads as a model error rather
than a generator one.

Needs fpp-to-json on PATH (activate the fprime venv); without it the suite
skips, unless `--require-tools` is given, which CI passes so that a broken
venv fails loudly instead of quietly passing. Exits non-zero on failure.
"""

import pathlib
import shutil
import subprocess
import sys
import tempfile

import fpp_to_rust as G


def build(source, modules=None):
    """Compile one FPP source and return the model."""
    with tempfile.TemporaryDirectory() as raw:
        work = pathlib.Path(raw)
        model_fpp = work / "model.fpp"
        model_fpp.write_text(source)
        try:
            ast, analysis = G.run_fpp_to_json([model_fpp], work)
        except subprocess.CalledProcessError as exc:
            # Without this, a typo in a test model above reads as a generator
            # failure rather than as the FPP compile error it is.
            raise AssertionError(
                "fpp-to-json rejected the test model:\n"
                + exc.stderr.decode(errors="replace")
            ) from exc
        return G.build_model(ast, analysis, modules)


def array_named(model, name):
    for path, array_name, elt, count, defaults in model.arrays:
        if array_name == name:
            return (path, elt, count, defaults)
    raise AssertionError(f"no array named {name!r} in {[a[1] for a in model.arrays]}")


CASES = []


def case(fn):
    CASES.append(fn)
    return fn


@case
def a_named_constant_sizes_an_array():
    """Almost no real array is sized by a literal."""
    model = build(
        """
        module M {
          constant Depth = 4
          array Sizes = [Depth] U32
        }
        """
    )
    _, elt, count, _ = array_named(model, "Sizes")
    assert count == 4, count
    assert elt[0] == "prim", elt


@case
def an_enum_constant_sizes_an_array():
    """`array X = [Section.COUNT] ...`, as Svc/TlmPacketizer writes it."""
    model = build(
        """
        module M {
          enum Section { A = 0, B = 1, COUNT = 3 }
          array Sizes = [Section.COUNT] U32
        }
        """
    )
    _, _, count, _ = array_named(model, "Sizes")
    assert count == 3, count


@case
def an_expression_sizes_an_array():
    """FPP folds it; the generator must not re-implement the arithmetic."""
    model = build(
        """
        module M {
          constant Base = 5
          array Sizes = [Base * 2] U32
        }
        """
    )
    _, _, count, _ = array_named(model, "Sizes")
    assert count == 10, count


@case
def a_type_inside_a_component_is_emitted():
    """The omission that motivated this file.

    fpp-to-cpp emits this as `M::C_Inner`; the generator flattens the scope
    chain the same way it flattens modules, so the component name is part of
    the Rust name rather than being dropped along with the type.
    """
    model = build(
        """
        module M {
          constant Ports = 3
          passive component C {
            array Inner = [Ports] U32
          }
        }
        """
    )
    path, _, count, _ = array_named(model, "Inner")
    assert count == 3, count
    assert path == ("M", "C"), path
    assert G.rust_name(path, "Inner") == "MCInner"


@case
def an_unwalked_scope_is_an_error_not_an_omission():
    """The guard that turns a silent drop into a loud failure.

    Simulated by forgetting how to walk a component, which is precisely the
    bug that existed: every type inside one vanished and the generator
    reported success.
    """
    saved = G.SCOPE_KINDS
    G.SCOPE_KINDS = ("DefModule",)
    try:
        build(
            """
            module M {
              passive component C {
                array Inner = [3] U32
              }
            }
            """
        )
    except G.GeneratorError as exc:
        assert "reachable" in str(exc), exc
    else:
        raise AssertionError("a type inside an unwalked scope was dropped silently")
    finally:
        G.SCOPE_KINDS = saved


@case
def a_scalar_default_fills_every_element():
    model = build(
        """
        module M {
          array Sizes = [3] U32 default 7
        }
        """
    )
    _, _, count, defaults = array_named(model, "Sizes")
    assert count == 3, count
    assert defaults is not None and len(defaults) == 3, defaults


# There is deliberately no test for build_array's "default has N elements,
# array holds M" error. FPP rejects that model itself -- "cannot convert [2]
# Integer to array Sizes" -- so the generator's check is unreachable through
# fpp-to-json and exists only as a guard against a future AST shape. A test
# that cannot run is worse than none.


@case
def a_flattened_name_collision_is_refused():
    """Module separators are erased, so `A.BC` and `AB.C` would share a type."""
    try:
        build(
            """
            module AB { enum C { X = 0 } }
            module A { enum BC { X = 0 } }
            """
        )
    except G.GeneratorError as exc:
        assert "collision" in str(exc), exc
    else:
        raise AssertionError("two types flattening to one Rust name were accepted")


@case
def a_string_typed_array_is_refused_by_name():
    """Refusing is fine; refusing silently would not be."""
    try:
        build(
            """
            type FwSizeStoreType = U16
            constant FW_FIXED_LENGTH_STRING_SIZE = 80
            module M {
              array Names = [2] string
            }
            """
        )
    except G.GeneratorError as exc:
        assert "string" in str(exc), exc
    else:
        raise AssertionError("an array of strings was accepted")


@case
def an_abstract_type_serializes_as_its_fpp_stand_in():
    """`type Foo` is C++-implemented, so the model does not describe it.

    Where the C++ delegates serialization wholesale to an FPP type -- as
    Fw::Time does to Fw::TimeValue -- the abstract type IS that type on the
    wire, and a field of it must resolve to the stand-in.
    """
    model = build(
        """
        module Fw {
          struct TimeValue {
            seconds: U32
            useconds: U32
          }
          type Time
        }
        module M {
          struct Stamped {
            stamp: Fw.Time
            count: U32
          }
        }
        """
    )
    fields = next(f for path, name, f, _ in model.structs if name == "Stamped")
    kinds = {name: desc[0] for name, desc, _ in fields}
    assert kinds["stamp"] == "named", kinds
    assert kinds["count"] == "prim", kinds
    # It resolves to TimeValue itself, not to some placeholder.
    stamp = next(desc for name, desc, _ in fields if name == "stamp")
    assert model.by_node_id[stamp[1]][1] == "TimeValue", model.by_node_id[stamp[1]]


@case
def an_abstract_type_with_a_hand_written_mirror_resolves():
    """`Fw.Buffer` has no FPP stand-in -- it serializes by hand, and one field
    is a C++-only PlatformPointerCastType -- so it is mirrored in Rust and the
    generated file imports that mirror."""
    model = build(
        """
        module Fw {
          type Buffer
        }
        module M {
          struct Holder {
            payload: Fw.Buffer
            tag: U32
          }
        }
        """
    )
    fields = next(f for _, name, f, _ in model.structs if name == "Holder")
    payload = next(desc for name, desc, _ in fields if name == "payload")
    assert payload == ("mirror", "FwBuffer", None), payload
    assert "fprime_core::fw_buffer::FwBuffer" in model.abs_imports, model.abs_imports
    emitted = G.generate(model, "test")
    assert "use fprime_core::fw_buffer::FwBuffer;" in emitted
    assert "pub payload: FwBuffer," in emitted
    assert "FwBuffer::SERIALIZED_SIZE" in emitted


@case
def a_model_without_mirrors_imports_none():
    """The import is emitted only on use, which is what keeps every
    already-checked-in generated file byte-identical."""
    model = build(
        """
        module M {
          struct Plain {
            n: U32
          }
        }
        """
    )
    assert model.abs_imports == set(), model.abs_imports
    assert "fw_buffer" not in G.generate(model, "test")


@case
def an_unmapped_abstract_type_is_refused_by_name():
    """Refusing loudly is the point; guessing a layout would be worse."""
    try:
        build(
            """
            module Fw {
              type PolyType
            }
            module M {
              struct Holder {
                p: Fw.PolyType
              }
            }
            """
        )
    except G.GeneratorError as exc:
        assert "Fw.PolyType" in str(exc), exc
        assert "no Rust representation" in str(exc), exc
    else:
        raise AssertionError("a field of an unmapped abstract type was accepted")


@case
def an_unused_abstract_type_does_not_block_generation():
    """Fw/Types/Types.fpp declares `type String` and `type PolyType` for other
    models to refer to. Declaring one must not stop a model generating."""
    model = build(
        """
        module Fw {
          type PolyType
          type String
        }
        module M {
          struct Plain {
            n: U32
          }
        }
        """
    )
    assert [n for _, n, *_ in model.structs] == ["Plain"], model.structs


def main():
    # `--require-tools` is what CI passes. Without it a missing fpp-to-json is
    # a skip, which is convenient locally and would be dangerous in CI: a
    # broken venv would turn every check below into a silent pass.
    require = "--require-tools" in sys.argv[1:]
    if shutil.which("fpp-to-json") is None:
        if require:
            print("FAIL  fpp-to-json not on PATH and --require-tools was given")
            return 1
        print("SKIP  fpp-to-json not on PATH (activate the fprime venv)")
        return 0
    failures = 0
    for fn in CASES:
        try:
            fn()
        except Exception as exc:  # noqa: BLE001 -- the report is the point
            print(f"FAIL  {fn.__name__}: {exc}")
            failures += 1
        else:
            print(f"ok    {fn.__name__}")
    print(f"\n{len(CASES) - failures}/{len(CASES)} checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
