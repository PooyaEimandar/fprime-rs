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


def build_ports(source, component="C"):
    """Compile one FPP source and describe `component`'s input ports.

    Component mode reads the raw AST and analysis rather than the Model, so it
    needs its own entry point: the type generator's `build_model` is not on
    this path at all, which is how the two halves drifted apart.
    """
    with tempfile.TemporaryDirectory() as raw:
        work = pathlib.Path(raw)
        model_fpp = work / "model.fpp"
        model_fpp.write_text(source)
        try:
            ast, analysis = G.run_fpp_to_json([model_fpp], work)
        except subprocess.CalledProcessError as exc:
            raise AssertionError(
                "fpp-to-json rejected the test model:\n"
                + exc.stderr.decode(errors="replace")
            ) from exc
        return ast, analysis, G.collect_ports(ast, analysis, component, component)


def build_shim(source, component="C"):
    """The generated C++ shim text for `component`."""
    ast, analysis, ports = build_ports(source, component)
    return G.generate_component_shim(
        G.component_module_path(ast, component),
        component,
        ports,
        [],
        [],
        [],
        "test.fpp",
        "Test/C.hpp",
        "Test/CGlue.h",
    )


def build_hpp(source, component="C"):
    """The generated C++ class declaration for `component`."""
    ast, analysis, ports = build_ports(source, component)
    return G.generate_component_hpp(
        G.component_module_path(ast, component),
        component,
        ports,
        [],
        "test.fpp",
        "Test/Dir/C.hpp",
    )


def refusal(source, component="C"):
    """The message the generator refuses `source` with."""
    try:
        build_ports(source, component)
    except G.GeneratorError as exc:
        return str(exc)
    raise AssertionError("the generator accepted a model it cannot carry")


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


@case
def a_filter_that_hides_a_referenced_type_is_refused():
    """`--modules` filters what is EMITTED, not what is REFERENCED.

    A struct inside the filter can have a member whose type sits outside it,
    and the result is a Rust file naming a type nobody declared. That used to
    surface as a cargo build error against a checked-in artifact, which is the
    wrong place to find out -- the model knew.
    """
    source = """
        module Other {
          struct Payload {
            n: U32
          }
        }
        module Keep {
          struct Holder {
            body: Other.Payload
          }
        }
        """
    try:
        build(source, ["Keep"])
    except G.GeneratorError as exc:
        assert "Other.Payload" in str(exc), exc
        assert "Keep.Holder" in str(exc), exc
    else:
        raise AssertionError("a dangling cross-module reference was emitted")

    # Widening the filter is exactly the fix the message names.
    model = build(source, ["Keep", "Other"])
    assert sorted(n for _, n, *_ in model.structs) == ["Holder", "Payload"], model.structs


@case
def emission_order_does_not_follow_the_source_order():
    """Two orderings of the same sources must produce the same file.

    The AST follows the order sources are passed, so without a deterministic
    sort the same model emits a different file depending on how a caller
    happened to list its dependencies -- which makes a checked-in artifact and
    its CI diff fragile for no reason.
    """
    first = build(
        """
        module Bee {
          enum Second { X = 0 }
        }
        module Ay {
          enum First { X = 0 }
        }
        """
    )
    second = build(
        """
        module Ay {
          enum First { X = 0 }
        }
        module Bee {
          enum Second { X = 0 }
        }
        """
    )
    # The models may hold their definitions in different orders; the emitted
    # file may not.
    assert G.generate(first, "m") == G.generate(second, "m")


@case
def a_type_inside_a_state_machine_is_emitted():
    """The second scope kind, and the one the omission guard found.

    `Svc/ComAggregator` defines a `State` enum inside a state machine, which
    fpp-to-cpp emits as `Svc::AggregationMachine_State`. It is worth its own
    case because a state machine's `members` are an FPP Option rather than the
    plain list a module or component carries -- adding the kind to SCOPE_KINDS
    was not enough on its own, and the guard kept failing until the Option was
    unwrapped.
    """
    model = build(
        """
        module M {
          state machine Machine {
            enum Phase: U8 { IDLE = 0, BUSY = 1 }
            initial enter S
            state S
          }
        }
        """
    )
    names = {(path, name) for path, name, *_ in model.enums}
    assert (("M", "Machine"), "Phase") in names, names
    assert G.rust_name(("M", "Machine"), "Phase") == "MMachinePhase"


@case
def a_sized_string_member_carries_its_capacity():
    """`string size N` is a length token plus up to N characters.

    The capacity is part of the Rust type so SERIALIZED_SIZE stays a compile
    time constant, matching Fw::StringBase::STATIC_SERIALIZED_SIZE(N).
    """
    model = build(
        """
        type FwSizeStoreType = U16
        module M {
          struct Named {
            label: string size 80
          }
        }
        """
    )
    fields = next(f for _, name, f, _ in model.structs if name == "Named")
    kind, capacity, size = next(desc for n, desc, _ in fields if n == "label")
    assert kind == "string", kind
    assert capacity == 80, capacity
    assert size == 2 + 80, size


@case
def a_string_sized_by_a_constant_resolves():
    """Same as every array bound in the framework: a named constant, not a
    literal."""
    model = build(
        """
        type FwSizeStoreType = U16
        module M {
          constant Width = 24
          struct Named {
            label: string size Width
          }
        }
        """
    )
    fields = next(f for _, name, f, _ in model.structs if name == "Named")
    _, capacity, _ = next(desc for n, desc, _ in fields if n == "label")
    assert capacity == 24, capacity


@case
def a_struct_literal_default_is_folded():
    """`default [ { a = 1, b = 2 } ]` -- FPP folds the whole literal, so the
    members arrive as resolved values rather than as AST nodes."""
    model = build(
        """
        module M {
          struct Entry {
            first: U32
            second: U8
          }
          array Entries = [2] Entry default [
            { first = 7, second = 1 },
            { first = 9, second = 2 }
          ]
        }
        """
    )
    _, _, _, count, defaults = next(
        a for a in model.arrays if a[1] == "Entries"
    )
    assert count == 2, count
    assert defaults is not None and len(defaults) == 2, defaults
    rendered = G.generate(model, "m")
    assert "MEntry {" in rendered, rendered[:400]
    assert "first: 7" in rendered and "second: 2" in rendered


@case
def an_array_default_named_by_a_constant_is_not_broadcast():
    """The case that needed the valueMap.

    A default written as a named constant and a scalar default meant for every
    element are the same shape in the AST -- one non-literal node. Only FPP's
    folded value distinguishes them, and getting it wrong silently fills every
    element with the whole array.
    """
    model = build(
        """
        module M {
          array Row = [2] U32
          constant ROWS = [ [1, 2], [3, 4] ]
          array Table = [2] Row default ROWS
        }
        """
    )
    _, _, _, count, defaults = next(a for a in model.arrays if a[1] == "Table")
    assert count == 2, count
    assert len(defaults) == 2, defaults
    rendered = G.generate(model, "m")
    # Element-wise: two distinct rows, not the same value twice.
    assert "MRow([1, 2])" in rendered, rendered[rendered.find("impl Default for MTable"):][:300]
    assert "MRow([3, 4])" in rendered


@case
def a_serial_buffer_abstract_type_takes_its_capacity_from_a_constant():
    """`Fw.LogBuffer` and friends are abstract, but their capacity is not.

    Six FPP abstract types are all one shape -- an Fw::LinearBufferBase
    subclass, a length token plus up to N bytes -- and each one's N is an FPP
    constant. So unlike Fw::Buffer, whose size depends on a C++-only pointer
    type, these ARE resolvable from the model.
    """
    model = build(
        """
        type FwSizeStoreType = U16
        constant FW_LOG_BUFFER_MAX_SIZE = 506
        module Fw {
          type LogBuffer
        }
        module M {
          struct Holder {
            payload: Fw.LogBuffer
          }
        }
        """
    )
    fields = next(f for _, name, f, _ in model.structs if name == "Holder")
    kind, rust, _ = next(desc for n, desc, _ in fields if n == "payload")
    assert kind == "mirror", kind
    assert rust == "FwSerialBuffer<506>", rust
    rendered = G.generate(model, "m")
    # Type position keeps the plain form; expression position needs a
    # turbofish, and getting that wrong is a syntax error in the output.
    assert "pub payload: FwSerialBuffer<506>" in rendered, rendered[:600]
    assert "FwSerialBuffer::<506>::default()" in rendered
    assert "FwSerialBuffer::<506>::SERIALIZED_SIZE" in rendered


@case
def a_missing_capacity_constant_is_refused_by_name():
    try:
        build(
            """
            type FwSizeStoreType = U16
            module Fw {
              type LogBuffer
            }
            module M {
              struct Holder {
                payload: Fw.LogBuffer
              }
            }
            """
        )
    except G.GeneratorError as exc:
        assert "FW_LOG_BUFFER_MAX_SIZE" in str(exc), exc
    else:
        raise AssertionError("a serial buffer with no capacity constant was accepted")


@case
def a_string_default_is_folded():
    model = build(
        """
        type FwSizeStoreType = U16
        module M {
          struct Named {
            label: string size 8
            count: U32
          } default { label = "hi", count = 0 }
        }
        """
    )
    rendered = G.generate(model, "m")
    assert 'FwString::<8>::from_bytes(b"hi")' in rendered, rendered[rendered.find("impl Default for MNamed"):][:300]


# ---------------------------------------------------------------------------
# Component mode (Phase 7/12): the glue generator
# ---------------------------------------------------------------------------


ENUM_RETURN_MODEL = """
module M {
  enum Status: U8 { OK = 0, ERR = 1 }
  port Ask(which: U8) -> Status
  passive component C {
    sync input port askIn: M.Ask
  }
}
"""


@case
def an_enum_return_crosses_as_its_representation():
    """The port shape most of the framework uses: `... -> Drv.I2cStatus`.

    Two different spellings have to come out of one descriptor: the handler is
    declared returning the generated CLASS (it overrides an autocoded virtual,
    so a mismatch would silently become an overload and never be called), and
    the value crossing extern "C" is the enum's representation.
    """
    _, _, ports = build_ports(ENUM_RETURN_MODEL)
    (port,) = ports
    assert port.ret[0] == "enum", port.ret
    assert G.c_type(port.ret) == "uint8_t", G.c_type(port.ret)
    assert G.cpp_return_type(port.ret) == "Status", G.cpp_return_type(port.ret)
    assert G.rust_param_type(port.ret) == "u8", G.rust_param_type(port.ret)


@case
def an_enum_return_is_rebuilt_through_serial_type():
    """Never `static_cast<Status::T>`: that is undefined behaviour.

    Rust can answer with any integer of the representation. Casting one
    outside the enumeration straight to a plain C++ enum is UB, so the shim
    goes through the generated class's SerialType constructor, which asserts
    first. This pins the cast that must NOT appear as much as the one that
    must.
    """
    shim = build_shim(ENUM_RETURN_MODEL)
    assert "Status askIn_handler" in shim.replace("C ::", ""), shim
    assert "const uint8_t rs_result =" in shim, shim
    assert "return Status(static_cast<Status::SerialType>(rs_result));" in shim, shim
    assert "static_cast<Status::T>" not in shim, shim
    assert "static_cast<enum Status" not in shim, shim


@case
def a_ref_enum_is_written_back_through_serial_type():
    """The same rule on the way out through a `ref` parameter.

    (`state` would read better than `outcome` for the parameter, and is an FPP
    keyword -- the trap this file's header warns about.)
    """
    shim = build_shim(
        """
        module M {
          enum Status: U8 { OK = 0, ERR = 1 }
          port Apply(ref outcome: Status)
          passive component C {
            sync input port applyIn: M.Apply
          }
        }
        """
    )
    assert "outcome = Status(static_cast<Status::SerialType>(rs_outcome));" in shim, shim
    assert "static_cast<Status::T>" not in shim, shim


@case
def a_returned_buffer_is_refused_with_the_alternative():
    """Fw.Buffer crosses in, but a returned one has no allocation behind it."""
    message = refusal(
        """
        module Fw { type Buffer }
        module M {
          port Get(n: U32) -> Fw.Buffer
          passive component C {
            sync input port getIn: M.Get
          }
        }
        """
    )
    assert "Fw.Buffer return types are not generated" in message, message
    assert "`ref` parameter" in message, message


@case
def a_refused_type_is_named():
    """It used to say "got named" for every struct and array in the tree.

    Which told you nothing: not the type, not the parameter. The point of a
    refusal is that a human can act on it.
    """
    message = refusal(
        """
        module M {
          struct Pair { a: U32, b: U32 }
          port Send(p: Pair)
          passive component C {
            sync input port sendIn: M.Send
          }
        }
        """
    )
    assert "got named" not in message, message
    assert "Pair" in message, message
    assert "struct" in message, message
    assert "sendIn" in message, message


@case
def a_refused_string_is_named_as_a_string():
    """A string is a different refusal path than a struct, and was silent."""
    message = refusal(
        """
        type FwSizeStoreType = U16
        module M {
          port Send(label: string size 8)
          passive component C {
            sync input port sendIn: M.Send
          }
        }
        """
    )
    assert "string" in message, message
    assert "sendIn" in message, message


@case
def a_void_port_with_no_ref_parameters_returns_directly():
    """The temporary is for `ref` write-back and enum rebuilding only.

    Without this, a refactor that always introduces `rs_result` would go
    unnoticed -- it compiles, and it changes every generated shim in the tree.
    """
    shim = build_shim(
        """
        module M {
          port Ask(n: U32) -> U32
          passive component C {
            sync input port askIn: M.Ask
          }
        }
        """
    )
    assert "rs_result" not in shim, shim
    assert "return fprime_rs_c_askIn(" in shim, shim


@case
def the_header_and_the_shim_declare_the_same_signatures():
    """A mismatch here does not fail to compile -- it silently misbehaves.

    `foo_handler` declared with one signature in the header and defined with
    another in the shim is two functions: the `override` fails only if the
    header's version does not match the autocoded base, and the shim's
    definition becomes a free-standing overload nothing calls. Generating both
    from one descriptor is the fix; this checks they did not drift anyway.
    """
    model = """
        module M {
          enum Status: U8 { OK = 0, ERR = 1 }
          port Ask(which: U8) -> Status
          port Apply(ref outcome: Status)
          passive component C {
            sync input port askIn: M.Ask
            sync input port applyIn: M.Apply
          }
        }
    """
    hpp = build_hpp(model)
    shim = build_shim(model)
    for signature in (
        "Status askIn_handler(FwIndexType portNum, uint8_t which)",
        "void applyIn_handler(FwIndexType portNum, Status& outcome)",
    ):
        assert f"    {signature} override;" in hpp, hpp
        assert f"{signature} {{" in shim.replace("C ::", ""), shim


@case
def the_component_header_includes_the_autocoded_base_by_directory():
    """An FPP module is a namespace; F Prime's directories are not it.

    Svc.RustDemo lives in Svc/RustDemo/, and Svc.Ccsds.ApidManager lives in
    Svc/Ccsds/ApidManager/. Deriving the include from the module path emits
    `Svc/CComponentAc.hpp`, which does not exist -- and the failure is a
    missing header at build time, far from here.
    """
    hpp = build_hpp(
        """
        module M {
          port Ask(which: U8)
          passive component C {
            sync input port askIn: M.Ask
          }
        }
        """
    )
    assert '#include "Test/Dir/CComponentAc.hpp"' in hpp, hpp


@case
def a_buffer_port_pulls_in_the_buffer_header():
    """And a component without one does not: no unused includes."""
    with_buffer = build_hpp(
        """
        module Fw { type Buffer }
        module M {
          port Take(ref b: Fw.Buffer)
          passive component C {
            sync input port takeIn: M.Take
          }
        }
        """
    )
    assert '#include "Fw/Buffer/Buffer.hpp"' in with_buffer, with_buffer
    without = build_hpp(
        """
        module M {
          port Ask(which: U8)
          passive component C {
            sync input port askIn: M.Ask
          }
        }
        """
    )
    assert "Fw/Buffer/Buffer.hpp" not in without, without


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
