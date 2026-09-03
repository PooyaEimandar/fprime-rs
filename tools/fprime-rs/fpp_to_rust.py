#!/usr/bin/env python3
"""Generate Rust type definitions from FPP models, via fpp-to-json.

This is the fprime-rs Phase 4 code generator. It consumes the JSON model
emitted by `fpp-to-json` (the officially sanctioned extension point -- the FPP
compiler itself is never modified) and emits Rust structs, enums, and arrays
whose serialization is byte-identical to the C++ code FPP autocodes for the
same model.

Design notes that matter for correctness:

* Field order comes from the AST (`ast`), never from `analysis.typeMap`, whose
  member maps are alphabetized. Wire order is declaration order.
* Member types are resolved through `analysis.typeMap`, keyed by the AST node
  id of the member's `typeName`, so aliases (`TokenType`,
  `FwPacketDescriptorType`) collapse to their primitive widths exactly as the
  C++ side sees them.
* FPP `default` clauses are folded into an explicit `impl Default`. This is not
  cosmetic: the C++ constructors initialize members from the same clauses, so
  dropping them makes default-constructed values disagree on the wire.
* Anything the generator cannot represent faithfully is a hard error. Emitting
  plausible-but-wrong code is the one outcome worth avoiding at any cost.

Usage:
  fpp_to_rust.py --module-name <label> --out <types.rs> <model.fpp> [deps.fpp ...]
"""

import argparse
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

PRIMITIVES = {
    "U8": ("u8", 1),
    "U16": ("u16", 2),
    "U32": ("u32", 4),
    "U64": ("u64", 8),
    "I8": ("i8", 1),
    "I16": ("i16", 2),
    "I32": ("i32", 4),
    "I64": ("i64", 8),
    "F32": ("f32", 4),
    "F64": ("f64", 8),
}

# Reserved words that cannot appear as bare Rust identifiers. FPP allows them
# as member names (escaped with `$`), so they are emitted as raw identifiers.
RUST_KEYWORDS = {
    "abstract",
    "as",
    "async",
    "await",
    "become",
    "box",
    "break",
    "const",
    "continue",
    "crate",
    "do",
    "dyn",
    "else",
    "enum",
    "extern",
    "false",
    "final",
    "fn",
    "for",
    "if",
    "impl",
    "in",
    "let",
    "loop",
    "macro",
    "match",
    "mod",
    "move",
    "mut",
    "override",
    "priv",
    "pub",
    "ref",
    "return",
    "self",
    "static",
    "struct",
    "super",
    "trait",
    "true",
    "try",
    "type",
    "typeof",
    "unsafe",
    "unsized",
    "use",
    "virtual",
    "where",
    "while",
    "yield",
}
# A few keywords cannot be raw identifiers even with the r# prefix
NON_RAW_KEYWORDS = {"crate", "self", "super", "Self"}


class GeneratorError(Exception):
    """A model construct this generator cannot represent faithfully."""


def rust_ident(name):
    if name in NON_RAW_KEYWORDS:
        raise GeneratorError(f"member name '{name}' cannot be a Rust identifier")
    return f"r#{name}" if name in RUST_KEYWORDS else name


# ---------------------------------------------------------------------------
# fpp-to-json invocation
# ---------------------------------------------------------------------------


def run_fpp_to_json(sources, workdir: Path):
    """Run fpp-to-json over `sources`, returning (ast, analysis)."""
    if shutil.which("fpp-to-json") is None:
        raise GeneratorError("fpp-to-json not found on PATH (activate the fprime venv)")
    subprocess.run(
        ["fpp-to-json", "-d", str(workdir)] + [str(s) for s in sources],
        check=True,
        capture_output=True,
    )
    ast = json.loads((workdir / "fpp-ast.json").read_text())["ast"]
    analysis = json.loads((workdir / "fpp-analysis.json").read_text())["analysis"]
    return ast, analysis


# ---------------------------------------------------------------------------
# AST helpers
# ---------------------------------------------------------------------------


def unwrap(node):
    """Unwrap an {"AstNode": {"data": ..., "id": N}} wrapper."""
    inner = node["AstNode"]
    return inner["data"], inner["id"]


def opt(value):
    """Unwrap an FPP Option: either "None" or {"Some": ...}."""
    if value == "None" or value is None:
        return None
    return value["Some"]


# `Fw::StringBase::STATIC_SERIALIZED_SIZE(N)` is sizeof(FwSizeStoreType) + N,
# and FwSizeStoreType is a U16 in every configuration this generator supports
# (Rust/RustConfigCheck.cpp static_asserts the width on the C++ side).
STRING_LENGTH_TOKEN_BYTES = 2


def resolve_int_by_id(value_map, node_id, what):
    """As `resolve_int`, for a node the typeMap references by id."""
    entry = value_map.get(str(node_id))
    if entry is None:
        raise GeneratorError(f"{what}: FPP recorded no value for node {node_id}")
    kind = next(iter(entry))
    if kind in ("Integer", "PrimitiveInt"):
        return int(entry[kind]["value"])
    if kind == "EnumConstant":
        return int(entry[kind]["value"][1])
    raise GeneratorError(f"{what}: FPP resolved this to a {kind}, not an integer")


def resolve_int(value_map, node, what):
    """The integer an expression node evaluates to.

    Reads `analysis.valueMap`, which holds FPP's own evaluation of every
    expression node -- the same number `fpp-to-cpp` emits. This matters more
    than it sounds: almost no real array in the framework is sized by a
    literal. They are sized by named constants (`ComQueueComPorts`,
    `TelemetrySection.NUM_SECTIONS`, `PassiveRateGroupOutputPorts`), so
    reading the literal out of the AST refuses essentially every array F Prime
    actually defines.

    The literal fallback is for nodes FPP did not put in the map.
    """
    data, node_id = unwrap(node)
    entry = value_map.get(str(node_id))
    if entry is not None:
        kind = next(iter(entry))
        if kind in ("Integer", "PrimitiveInt"):
            return int(entry[kind]["value"])
        if kind == "EnumConstant":
            # `array X = [TelemetrySection.NUM_SECTIONS] ...` -- an enum
            # constant is an integer here, and FPP records it as the
            # (name, value) pair that fold_default also reads.
            return int(entry[kind]["value"][1])
        raise GeneratorError(f"{what}: FPP resolved this to a {kind}, not an integer")
    if isinstance(data, dict) and "ExprLiteralInt" in data:
        return int(data["ExprLiteralInt"]["value"], 0)
    kind = next(iter(data)) if isinstance(data, dict) else type(data).__name__
    raise GeneratorError(
        f"{what}: FPP recorded no value for this expression and it is not an "
        f"integer literal (got {kind})"
    )


# Abstract FPP types -- `type Foo` with no body -- are declared in the model and
# implemented in hand-written C++, so the model does not describe how they
# serialize. Some of them delegate that wholesale to an FPP struct, and for
# those the mapping is exact and checkable against the C++: `Fw::Time` holds a
# `Fw::TimeValue m_val` (Fw/Time/Time.hpp) and its serializeTo is `return
# this->m_val.serializeTo(...)` (Fw/Time/Time.cpp), so on the wire a Time IS a
# TimeValue.
#
# Only that shape belongs here. `Fw::Buffer`, for instance, does not qualify:
# it serializes by hand and one of its fields is a `PlatformPointerCastType`,
# which is declared in C++ (cmake/platform/unix/Platform/PlatformTypes.h) and
# not in FPP at all -- so its serialized size is not a fact the model contains.
# Anything unlisted is refused by name rather than guessed at.
ABSTRACT_AS_FPP_TYPE = {
    "Fw.Time": "Fw.TimeValue",
    "Fw.TimeInterval": "Fw.TimeIntervalValue",
}

# Abstract types with no FPP stand-in, mirrored by hand in Rust instead:
# qualified FPP name -> (Rust type, `use` path).
#
# Each of these rests on facts a person read out of the C++, so each needs a
# static_assert on the C++ side to hold it -- Rust/RustAbstractTypeCheck.cpp,
# compiled into Fw_Buffer. Do not add an entry without one.
#
# Fw::Buffer is here rather than above because it serializes by hand, and one
# of its fields is a PlatformPointerCastType: declared in
# cmake/platform/<platform>/Platform/PlatformTypes.h and not in FPP at all. Its
# serialized size is therefore not a fact the model contains.
ABSTRACT_AS_RUST_TYPE = {
    "Fw.Buffer": ("FwBuffer", "fprime_core::fw_buffer::FwBuffer"),
}

# Abstract types that are all one shape: an Fw::LinearBufferBase subclass, whose
# wire form is a FwSizeStoreType length token followed by up to N bytes. The
# model declares only the name, but each one's capacity IS an FPP constant, so
# the value is in the model even though the layout is not -- which is what makes
# these resolvable where Fw::Buffer was not.
#
# qualified FPP name -> the FPP constant naming its capacity.
ABSTRACT_AS_SERIAL_BUFFER = {
    "Fw.ComBuffer": "FW_COM_BUFFER_MAX_SIZE",
    "Fw.CmdArgBuffer": "FW_CMD_ARG_BUFFER_MAX_SIZE",
    "Fw.LogBuffer": "FW_LOG_BUFFER_MAX_SIZE",
    "Fw.TlmBuffer": "FW_TLM_BUFFER_MAX_SIZE",
    "Fw.ParamBuffer": "FW_PARAM_BUFFER_MAX_SIZE",
    "Fw.StatementArgBuffer": "FW_STATEMENT_ARG_BUFFER_MAX_SIZE",
}

# DefAbsType is collected but never emitted: it contributes a name that other
# types refer to, and counting it keeps the completeness check below honest.
DEFINITION_KINDS = ("DefStruct", "DefEnum", "DefArray", "DefAbsType")
EMITTED_KINDS = ("DefStruct", "DefEnum", "DefArray")

# Node kinds whose body may hold type definitions. A component is one of them:
# `Svc/PassiveRateGroup/PassiveRateGroup.fpp` defines its `CycleTime` array
# inside the component, which `fpp-to-cpp` emits as
# `Svc::PassiveRateGroup_CycleTime`. A state machine is another:
# `Svc/ComAggregator` defines a `State` enum inside `AggregationMachine`, which
# becomes `Svc::AggregationMachine_State`. Both use the same `<Scope>_<Name>`
# shape, so treating the scope as another path segment gives the same identity
# after flattening, and build_model's collision check still protects it.
#
# This list is not guessed at: count_definitions below refuses to emit a file
# when a definition is unreachable from these, which is how DefStateMachine
# was found -- four framework modules failed with a count mismatch rather than
# silently dropping a type.
SCOPE_KINDS = ("DefModule", "DefComponent", "DefStateMachine")


def count_definitions(node):
    """How many type definitions the AST holds, wherever they are.

    Paired with iter_definitions to make omission loud. The walker below only
    descends into the scopes it knows, so a scope kind it has never seen would
    otherwise drop every type inside it and still report success -- which is
    exactly what happened before components were walked: the generator said
    "0 arrays" for modules that define several.
    """
    total = 0
    if isinstance(node, dict):
        for key, value in node.items():
            if key in DEFINITION_KINDS:
                total += 1
            total += count_definitions(value)
    elif isinstance(node, list):
        for value in node:
            total += count_definitions(value)
    return total


def iter_constants(ast, value_map):
    """Every `constant NAME = ...` in the model, resolved to its value.

    Separate from iter_definitions, which walks type definitions and counts
    them. Constants are needed by name rather than by node id: an abstract
    buffer type's capacity is named in ABSTRACT_AS_SERIAL_BUFFER, not pointed
    at by the typeMap.
    """
    constants = {}

    def walk(members):
        for entry in members:
            node = entry[1]
            if not isinstance(node, dict):
                continue
            for kind, wrapper in node.items():
                if kind in SCOPE_KINDS:
                    data, _ = unwrap(wrapper["node"])
                    inner = data.get("members")
                    if not isinstance(inner, list):
                        inner = opt(inner) or []
                    walk(inner)
                elif kind == "DefConstant":
                    data, _ = unwrap(wrapper["node"])
                    _, value_id = unwrap(data["value"])
                    resolved = value_map.get(str(value_id))
                    if resolved is not None:
                        constants[data["name"]] = resolved

    for unit in ast:
        walk(unit["members"])
    return constants


def iter_definitions(ast):
    """Collect (scope_path, kind, data, node_id) for every type definition."""
    collected = []

    def walk(members, path):
        for entry in members:
            node = entry[1]
            if not isinstance(node, dict):
                continue
            for kind, wrapper in node.items():
                if kind in SCOPE_KINDS:
                    data, _ = unwrap(wrapper["node"])
                    # A module's and a component's members are a plain list; a
                    # state machine's are an FPP Option wrapping one. Both are
                    # scopes, so both are walked.
                    members = data.get("members")
                    if not isinstance(members, list):
                        members = opt(members) or []
                    walk(members, path + [data["name"]])
                elif kind in DEFINITION_KINDS:
                    data, node_id = unwrap(wrapper["node"])
                    collected.append((tuple(path), kind, data, node_id))

    for unit in ast:
        walk(unit["members"], [])

    expected = count_definitions(ast)
    if len(collected) != expected:
        raise GeneratorError(
            f"the model holds {expected} type definitions but only "
            f"{len(collected)} are reachable from the scopes this generator "
            f"walks ({', '.join(SCOPE_KINDS)}). Some type is nested in a scope "
            f"kind that is not handled; emitting the rest would silently drop "
            f"it. Add the scope kind to SCOPE_KINDS."
        )
    return collected


# ---------------------------------------------------------------------------
# Type resolution
# ---------------------------------------------------------------------------


def resolve_type(model, type_map, node_id):
    entry = type_map.get(str(node_id))
    if entry is None:
        raise GeneratorError(f"no typeMap entry for AST node {node_id}")
    if isinstance(entry, dict) and "AbsType" in entry:
        return resolve_abstract(model, type_map, entry["AbsType"]["node"]["astNodeId"])
    return classify(model, entry)


def resolve_abstract(model, type_map, decl_id):
    """Resolve `type Foo` to whatever carries its serialization.

    A model that merely declares an abstract type it never uses still
    generates -- Fw/Types/Types.fpp declares `type String` and `type PolyType`
    for the benefit of other models -- so this is reached only when a field
    actually has that type.
    """
    target_id = model.abs_alias.get(decl_id) if model is not None else None
    if target_id is not None:
        return classify(model, type_map[str(target_id)])
    qualified = (
        model.abs_types.get(decl_id, f"AST node {decl_id}")
        if model is not None
        else f"AST node {decl_id}"
    )
    mirror = ABSTRACT_AS_RUST_TYPE.get(qualified)
    if mirror is not None and model is not None:
        rust, use_path = mirror
        model.abs_imports.add(use_path)
        return ("mirror", rust, None)
    capacity_constant = ABSTRACT_AS_SERIAL_BUFFER.get(qualified)
    if capacity_constant is not None and model is not None:
        entry = model.constants.get(capacity_constant)
        if entry is None:
            raise GeneratorError(
                f"abstract type '{qualified}' takes its capacity from the FPP "
                f"constant '{capacity_constant}', which is not in this model; "
                f"add the file that defines it to the sources"
            )
        kind = next(iter(entry))
        if kind not in ("Integer", "PrimitiveInt"):
            raise GeneratorError(
                f"'{capacity_constant}' resolved to a {kind}, not an integer"
            )
        capacity = int(entry[kind]["value"])
        model.abs_imports.add("fprime_core::serial_buffer::FwSerialBuffer")
        return ("mirror", f"FwSerialBuffer<{capacity}>", None)
    raise GeneratorError(
        f"abstract type '{qualified}' has no Rust representation. It is "
        f"declared in FPP and implemented in C++, so the model does not say "
        f"how it serializes. If its C++ serializeTo delegates to an FPP type, "
        f"add that mapping to ABSTRACT_AS_FPP_TYPE; otherwise it needs a "
        f"hand-written Rust mirror, and its wire format must be pinned by a "
        f"C++ test first"
    )


def classify(model, entry):
    """Classify a typeMap entry into ("prim"|"bool"|"string"|"named", ...).

    The JSON vocabulary nests differently per kind: integers appear as
    `Int.PrimitiveInt.kind`, while floats and bools sit behind a `Primitive`
    wrapper (`Primitive.Float.kind`, `Primitive.Boolean`).
    """
    kind = next(iter(entry))
    body = entry[kind]

    if kind == "Primitive":
        return classify(model, body)
    if kind == "AliasType":
        return classify(model, body["aliasType"])
    if kind == "Int":
        if "PrimitiveInt" in body:
            name = next(iter(body["PrimitiveInt"]["kind"]))
            rust, size = PRIMITIVES[name]
            return ("prim", rust, size)
        raise GeneratorError("unsized integer type (model-only) cannot be generated")
    if kind == "Float":
        name = next(iter(body["kind"]))
        rust, size = PRIMITIVES[name]
        return ("prim", rust, size)
    if kind == "Boolean":
        return ("bool", None, 1)
    if kind == "String":
        # An FPP `string size N` serializes as a FwSizeStoreType length token
        # followed by up to N characters, so its capacity is part of its type.
        # The size is an expression node, which means it can be a named
        # constant like every array bound in the framework.
        size = body.get("size")
        if not isinstance(size, dict) or "Some" not in size:
            raise GeneratorError(
                "a string with no declared size needs the framework's default "
                "capacity, which this generator does not carry; give the "
                "member an explicit `string size N`"
            )
        node_id = size["Some"]["astNodeId"]
        if model is None:
            raise GeneratorError("string capacity cannot be resolved here")
        capacity = resolve_int_by_id(model.value_map, node_id, "string size")
        # Same mechanism the hand-written abstract mirrors use: the import is
        # emitted only when the model actually contains a string.
        model.abs_imports.add("fprime_core::fpp_string::FwString")
        return ("string", capacity, STRING_LENGTH_TOKEN_BYTES + capacity)
    if kind in ("Struct", "Enum", "Array"):
        return ("named", body["node"]["astNodeId"], None)
    raise GeneratorError(f"unsupported type kind: {kind}")


# ---------------------------------------------------------------------------
# Model
# ---------------------------------------------------------------------------


class Model:
    def __init__(self):
        self.structs = []  # (path, name, fields, default_map)
        self.enums = []  # (path, name, repr_rust, constants, default_value)
        self.arrays = []  # (path, name, elt_desc, count, default_elems)
        self.by_node_id = {}  # AST node id -> (path, name)
        self.enum_consts = {}  # AST node id -> {const name: value}
        self.value_map = {}  # AST node id -> FPP-evaluated value
        self.abs_types = {}  # AST node id -> qualified name of a `type Foo`
        self.abs_alias = {}  # abstract node id -> node id that carries its wire form
        self.struct_fields = {}  # AST node id -> [(name, desc, count)]
        self.array_elements = {}  # AST node id -> (element desc, count)
        self.constants = {}  # FPP constant name -> resolved value
        self.abs_imports = set()  # `use` paths for hand-written abstract mirrors


def rust_name(path, name):
    """Flatten an FPP qualified name into a Rust type name."""
    return "".join(list(path) + [name])


def build_model(ast, analysis, wanted_modules=None):
    type_map = analysis["typeMap"]
    model = Model()
    model.value_map = analysis.get("valueMap", {})
    model.constants = iter_constants(ast, model.value_map)
    defs = iter_definitions(ast)

    # Register every definition first so named references resolve, and reject
    # flattened-name collisions: module separators are erased, so `AB.C` and
    # `A.BC` would otherwise both become `ABC` and silently share a type.
    flattened = {}
    by_qualified = {}
    for path, kind, data, node_id in defs:
        model.by_node_id[node_id] = (path, data["name"])
        qualified = ".".join(list(path) + [data["name"]])
        by_qualified[qualified] = node_id
        if kind == "DefAbsType":
            # No Rust type of its own, so it takes part in neither the
            # flattened-name check nor the emit loop.
            model.abs_types[node_id] = qualified
            continue
        flat = rust_name(path, data["name"])
        if flat in flattened and flattened[flat] != qualified:
            raise GeneratorError(
                f"flattened name collision: '{flattened[flat]}' and '{qualified}' "
                f"both become Rust type '{flat}'"
            )
        flattened[flat] = qualified
        if kind == "DefEnum":
            model.enum_consts[node_id] = collect_enum_constants(data, model.value_map)

    # An abstract type stands in for the FPP type that carries its
    # serialization. Resolved here so the target is a plain node id by the
    # time anything asks about a field's type.
    for node_id, qualified in model.abs_types.items():
        target = ABSTRACT_AS_FPP_TYPE.get(qualified)
        if target is None:
            continue
        target_id = by_qualified.get(target)
        if target_id is None:
            raise GeneratorError(
                f"abstract type '{qualified}' is declared to serialize as "
                f"'{target}', which is not in this model; add the file that "
                f"defines it to the sources"
            )
        model.abs_alias[node_id] = target_id

    for path, kind, data, node_id in defs:
        if wanted_modules is not None and path and path[0] not in wanted_modules:
            continue
        where = ".".join(list(path) + [data["name"]])
        if kind == "DefStruct":
            built = build_struct(model, type_map, path, data, where)
            model.structs.append(built)
            model.struct_fields[node_id] = built[2]
        elif kind == "DefEnum":
            model.enums.append(build_enum(model, type_map, path, data, node_id, where))
        elif kind == "DefArray":
            built = build_array(model, type_map, path, data, where)
            model.arrays.append(built)
            model.array_elements[node_id] = (built[2], built[3])
        elif kind == "DefAbsType":
            pass  # registered above; nothing to emit
        else:
            raise GeneratorError(f"{where}: unhandled definition kind '{kind}'")

    check_emitted_types_are_complete(model, wanted_modules)
    return model


def check_emitted_types_are_complete(model, wanted_modules):
    """Refuse to emit a file that references a type it does not define.

    `--modules` filters what is emitted but not what is *referenced*: a struct
    inside the filter can have a member whose type sits outside it, and the
    result is a file that names a Rust type nobody declared. That fails at
    `cargo build` rather than here, which is the wrong place -- the model knew.
    Same rule as the scope walker: an omission is an error, not an output.
    """
    if wanted_modules is None:
        return
    emitted = set()
    for path, name, *_ in model.structs:
        emitted.add(rust_name(path, name))
    for path, name, *_ in model.enums:
        emitted.add(rust_name(path, name))
    for path, name, *_ in model.arrays:
        emitted.add(rust_name(path, name))

    missing = {}
    for group, where_of in (
        (model.structs, lambda entry: ".".join(list(entry[0]) + [entry[1]])),
        (model.arrays, lambda entry: ".".join(list(entry[0]) + [entry[1]])),
    ):
        for entry in group:
            where = where_of(entry)
            # structs carry (path, name, fields, defaults); arrays carry
            # (path, name, elt_desc, count, defaults).
            descs = (
                [desc for _, desc, _ in entry[2]] if group is model.structs else [entry[2]]
            )
            for desc in descs:
                if desc[0] != "named":
                    continue
                referenced_path, referenced_name = model.by_node_id[desc[1]]
                flat = rust_name(referenced_path, referenced_name)
                if flat not in emitted:
                    missing.setdefault(
                        ".".join(list(referenced_path) + [referenced_name]), []
                    ).append(where)
    if missing:
        detail = "; ".join(
            f"{referenced} (used by {', '.join(sorted(set(users)))})"
            for referenced, users in sorted(missing.items())
        )
        modules = sorted({referenced.split(".")[0] for referenced in missing})
        raise GeneratorError(
            f"--modules {' '.join(wanted_modules)} excludes types that the "
            f"emitted ones refer to: {detail}. The generated file would name "
            f"Rust types it does not define. Add {' '.join(modules)} to "
            f"--modules, or drop the filter."
        )


def collect_enum_constants(data, value_map):
    constants = {}
    implicit = 0
    for entry in data["constants"]:
        const, _ = unwrap(entry[1])
        value_node = opt(const["value"])
        if value_node is None:
            value = implicit
        else:
            value = resolve_int(
                value_map, value_node, f"enum constant {const['name']}"
            )
        implicit = value + 1
        constants[const["name"]] = value
    return constants


def build_struct(model, type_map, path, data, where):
    fields = []
    for entry in data["members"]:
        member, _ = unwrap(entry[1])
        _, type_id = unwrap(member["typeName"])
        try:
            desc = resolve_type(model, type_map, type_id)
        except GeneratorError as exc:
            raise GeneratorError(f"{where}.{member['name']}: {exc}") from exc
        size_node = opt(member["size"])
        count = (
            resolve_int(
                model.value_map,
                size_node,
                f"{where}.{member['name']} member array size",
            )
            if size_node
            else None
        )
        fields.append((member["name"], desc, count))

    # Fold the `default { ... }` clause; members without an entry keep the
    # type's zero value, exactly as the C++ constructor leaves them.
    defaults = {}
    default_node = opt(data.get("default"))
    if default_node is not None:
        node_data, _ = unwrap(default_node)
        if not isinstance(node_data, dict) or "ExprStruct" not in node_data:
            raise GeneratorError(f"{where}: unsupported struct default expression")
        for member_node in node_data["ExprStruct"]["members"]:
            member, _ = unwrap(member_node)
            defaults[member["name"]] = member["value"]
    return (path, data["name"], fields, defaults)


def build_enum(model, type_map, path, data, node_id, where):
    entry = type_map.get(str(node_id))
    if entry is None or "Enum" not in entry:
        raise GeneratorError(f"{where}: no resolved Enum entry")
    # The representation comes from the resolved type, not the AST: an enum
    # with no explicit repr resolves to the concrete type behind
    # FwEnumStoreType, which is exactly what fpp emits for C++.
    repr_name = next(iter(entry["Enum"]["repType"]["kind"]))
    repr_rust, _ = PRIMITIVES[repr_name]

    constants = list(model.enum_consts[node_id].items())
    default_value = constants[0][1] if constants else 0
    default_node = opt(data.get("default"))
    if default_node is not None:
        node_data, _ = unwrap(default_node)
        if not isinstance(node_data, dict) or "ExprIdent" not in node_data:
            raise GeneratorError(f"{where}: unsupported enum default expression")
        chosen = node_data["ExprIdent"]["value"]
        if chosen not in model.enum_consts[node_id]:
            raise GeneratorError(f"{where}: default names unknown constant '{chosen}'")
        default_value = model.enum_consts[node_id][chosen]
    return (path, data["name"], repr_rust, constants, default_value)


def build_array(model, type_map, path, data, where):
    count = resolve_int(model.value_map, data["size"], f"{where} array size")
    _, elt_id = unwrap(data["eltType"])
    elt = resolve_type(model, type_map, elt_id)

    # Defaults are carried as resolved VALUES rather than AST nodes. FPP folds
    # the whole default expression, which is the only way to tell an array
    # default written as a named constant -- Svc.TlmPacketizer's
    # TELEMETRY_SECTION_DEFAULTS -- from a scalar meant for every element. The
    # AST alone cannot: both are a single non-literal node.
    default_elems = None
    default_node = opt(data.get("default"))
    if default_node is not None:
        _, default_id = unwrap(default_node)
        entry = model.value_map.get(str(default_id))
        if entry is None:
            raise GeneratorError(f"{where}: no resolved value for the default")
        if next(iter(entry)) == "AnonArray":
            elements = entry["AnonArray"]["elements"]
            if len(elements) != count:
                raise GeneratorError(
                    f"{where}: default has {len(elements)} elements, array "
                    f"holds {count}"
                )
            default_elems = elements
        else:
            # FPP also allows a scalar default applied to every element.
            default_elems = [entry] * count
    return (path, data["name"], elt, count, default_elems)


# ---------------------------------------------------------------------------
# Default-value folding
# ---------------------------------------------------------------------------


def fold_default(model, desc, node, where):
    """Render an FPP default expression as a Rust value of the member's type.

    Values come from `analysis.valueMap`, which holds FPP's own evaluation of
    every expression node -- so constant references, hex literals and enum
    constants all resolve without re-implementing FPP's expression semantics.
    """
    _, node_id = unwrap(node)
    entry = model.value_map.get(str(node_id))
    if entry is None:
        raise GeneratorError(f"{where}: no resolved value for default expression")
    return fold_value(model, desc, entry, where)


def fold_value(model, desc, entry, where):
    """Render an already-resolved FPP value as Rust.

    Split from `fold_default` because a struct literal's members arrive as
    resolved values rather than as AST nodes -- FPP folds the whole literal
    into one `AnonStruct` entry -- so the nested case cannot go back through
    the valueMap by node id.
    """
    kind = next(iter(entry))
    body = entry[kind]

    if desc[0] == "prim":
        if kind == "Integer" or kind == "PrimitiveInt":
            return str(int(body["value"]))
        if kind == "Float":
            return str(float(body["value"]))
        if kind == "EnumConstant":
            # An enum constant used where an integer is expected
            return str(int(body["value"][1]))
        raise GeneratorError(f"{where}: unsupported default value kind '{kind}'")
    if desc[0] == "bool":
        if kind == "Boolean":
            return "true" if body["value"] else "false"
        raise GeneratorError(
            f"{where}: unsupported default value kind '{kind}' for bool"
        )
    if desc[0] == "string":
        if kind != "String":
            raise GeneratorError(
                f"{where}: unsupported default value kind '{kind}' for a string"
            )
        text = body["value"]
        # Rendered as a byte-string literal: the Rust type holds bytes, and a
        # non-ASCII default would otherwise silently change length between the
        # two languages. FPP itself has no escape for one, so refuse rather
        # than guess.
        if any(ord(character) > 0x7E or ord(character) < 0x20 for character in text):
            raise GeneratorError(
                f"{where}: a string default with non-printable-ASCII characters "
                f"is not supported"
            )
        escaped = text.replace("\\", "\\\\").replace('"', '\\"')
        return f'FwString::<{desc[1]}>::from_bytes(b"{escaped}")'
    if desc[0] == "named":
        path, name = model.by_node_id[desc[1]]
        ty = rust_name(path, name)
        if model.enum_consts.get(desc[1]) is not None:
            if kind == "EnumConstant":
                return f"{ty}({int(body['value'][1])})"
            if kind in ("Integer", "PrimitiveInt"):
                return f"{ty}({int(body['value'])})"
            raise GeneratorError(
                f"{where}: unsupported enum default value kind '{kind}'"
            )
        # A struct default, e.g. the `default [ { a = 0, b = X } ]` on
        # Svc.Ccsds.SdlsCfg.SaMap. FPP folds the whole literal, so each member
        # arrives already evaluated and is rendered against its own type.
        if kind == "AnonStruct":
            fields = model.struct_fields.get(desc[1])
            if fields is None:
                raise GeneratorError(
                    f"{where}: a struct default needs the struct's own "
                    f"definition, which is not in this model"
                )
            members = body["members"]
            parts = []
            for member_name, member_desc, member_count in fields:
                if member_count is not None:
                    raise GeneratorError(
                        f"{where}.{member_name}: defaults for a struct member "
                        f"that is itself an array are not supported"
                    )
                if member_name not in members:
                    raise GeneratorError(
                        f"{where}: the default gives no value for member "
                        f"'{member_name}'"
                    )
                parts.append(
                    f"{member_name}: "
                    + fold_value(
                        model,
                        member_desc,
                        members[member_name],
                        f"{where}.{member_name}",
                    )
                )
            return ty + " { " + ", ".join(parts) + " }"
        # An array default whose elements are themselves a named type, e.g.
        # Svc.TlmPacketizer.SectionConfigs, an array of GroupConfigs arrays.
        if kind == "AnonArray":
            element = model.array_elements.get(desc[1])
            if element is None:
                raise GeneratorError(
                    f"{where}: an array default needs the array's own "
                    f"definition, which is not in this model"
                )
            element_desc, count = element
            elements = body["elements"]
            if len(elements) != count:
                raise GeneratorError(
                    f"{where}: the default has {len(elements)} elements, the "
                    f"array holds {count}"
                )
            parts = [
                fold_value(model, element_desc, value, f"{where}[{i}]")
                for i, value in enumerate(elements)
            ]
            return ty + "([" + ", ".join(parts) + "])"
        raise GeneratorError(f"{where}: unsupported default value kind '{kind}'")
    raise GeneratorError(f"{where}: cannot fold default for {desc[0]}")


def expression_form(rust_type):
    """A type name as it must be written in expression position.

    `FwSerialBuffer<506>` is fine as a type but is a chained comparison in an
    expression; Rust wants `FwSerialBuffer::<506>` there. Only the generic
    mirrors need this, and getting it wrong is a syntax error rather than a
    silent one, but it is worth doing in one place.
    """
    index = rust_type.find("<")
    return rust_type if index < 0 else rust_type[:index] + "::" + rust_type[index:]


def zero_value(model, desc, count):
    """The Rust value a member takes when the model gives it no default."""
    if desc[0] == "prim":
        base = "0.0" if desc[1] in ("f32", "f64") else "0"
    elif desc[0] == "bool":
        base = "false"
    elif desc[0] == "named":
        path, name = model.by_node_id[desc[1]]
        base = f"{rust_name(path, name)}::default()"
    elif desc[0] == "mirror":
        base = f"{expression_form(desc[1])}::default()"
    elif desc[0] == "string":
        base = f"FwString::<{desc[1]}>::default()"
    else:
        raise GeneratorError(f"cannot default type kind: {desc[0]}")
    return f"[{base}; {count}]" if count else base


# ---------------------------------------------------------------------------
# Rust emission
# ---------------------------------------------------------------------------


def field_rust_type(model, desc, count):
    kind = desc[0]
    if kind == "prim":
        base = desc[1]
    elif kind == "bool":
        base = "bool"
    elif kind == "named":
        path, name = model.by_node_id[desc[1]]
        base = rust_name(path, name)
    elif kind == "mirror":
        base = desc[1]
    elif kind == "string":
        base = f"FwString<{desc[1]}>"
    else:
        raise GeneratorError(f"unsupported field type in codegen: {kind}")
    return f"[{base}; {count}]" if count else base


def size_term(model, desc, count):
    kind = desc[0]
    if kind == "prim":
        base = str(desc[2])
    elif kind == "bool":
        base = "1"  # FW_SERIALIZE_TRUE_VALUE/FALSE_VALUE occupy one byte
    elif kind == "named":
        path, name = model.by_node_id[desc[1]]
        base = f"{rust_name(path, name)}::SERIALIZED_SIZE"
    elif kind == "mirror":
        base = f"{expression_form(desc[1])}::SERIALIZED_SIZE"
    elif kind == "string":
        base = f"FwString::<{desc[1]}>::SERIALIZED_SIZE"
    else:
        raise GeneratorError(f"cannot size type kind: {kind}")
    return f"{base} * {count}" if count else base


def emit_value_ser(desc, expr, indent):
    pad = " " * indent
    if desc[0] == "prim":
        return f"{pad}buf.ser_{desc[1]}({expr}, mode)?;"
    if desc[0] == "bool":
        return f"{pad}buf.ser_bool({expr}, mode)?;"
    if desc[0] in ("named", "mirror", "string"):
        return f"{pad}{expr}.serialize_to(buf, mode)?;"
    raise GeneratorError(f"unsupported serialize kind: {desc[0]}")


def emit_value_deser(model, desc, target, indent):
    pad = " " * indent
    if desc[0] == "prim":
        return f"{pad}{target} = buf.deser_{desc[1]}(mode)?;"
    if desc[0] == "bool":
        return f"{pad}{target} = buf.deser_bool(mode)?;"
    if desc[0] == "named":
        path, name = model.by_node_id[desc[1]]
        return f"{pad}{target} = {rust_name(path, name)}::deserialize_from(buf, mode)?;"
    if desc[0] == "mirror":
        return (
            f"{pad}{target} = "
            f"{expression_form(desc[1])}::deserialize_from(buf, mode)?;"
        )
    if desc[0] == "string":
        return f"{pad}{target} = FwString::<{desc[1]}>::deserialize_from(buf, mode)?;"
    raise GeneratorError(f"unsupported deserialize kind: {desc[0]}")


def emit_struct(model, path, name, fields, defaults, out):
    ty = rust_name(path, name)
    where = ".".join(list(path) + [name])
    out.append(f"/// FPP `{where}` (struct).")
    # Default is written out rather than derived: the model's `default` clause
    # must be honored, and `[T; N]` has no Default for N > 32.
    out.append("#[derive(Debug, Clone, Copy, PartialEq)]")
    out.append(f"pub struct {ty} {{")
    for fname, desc, count in fields:
        out.append(
            f"    pub {rust_ident(fname)}: {field_rust_type(model, desc, count)},"
        )
    out.append("}")
    out.append("")

    out.append(f"impl Default for {ty} {{")
    out.append("    fn default() -> Self {")
    out.append("        Self {")
    for fname, desc, count in fields:
        if fname in defaults:
            value = fold_default(model, desc, defaults[fname], f"{where}.{fname}")
            if count:
                value = f"[{value}; {count}]"
        else:
            value = zero_value(model, desc, count)
        out.append(f"            {rust_ident(fname)}: {value},")
    out.append("        }")
    out.append("    }")
    out.append("}")
    out.append("")

    out.append(f"impl {ty} {{")
    terms = " + ".join(size_term(model, d, c) for _, d, c in fields) or "0"
    out.append("    /// Matches the autocoded C++ `SERIALIZED_SIZE`.")
    out.append(f"    pub const SERIALIZED_SIZE: usize = {terms};")
    out.append("")
    out.append("    /// Serialize in FPP declaration order, matching the C++ autocode.")
    out.append(
        "    pub fn serialize_to(&self, buf: &mut SerBuffer<'_>, mode: Endianness) -> SerResult {"
    )
    for fname, desc, count in fields:
        ident = rust_ident(fname)
        if count:
            out.append(f"        for item in self.{ident}.iter() {{")
            out.append(
                emit_value_ser(desc, "*item" if desc[0] not in ("named", "mirror", "string") else "item", 12)
            )
            out.append("        }")
        else:
            out.append(emit_value_ser(desc, f"self.{ident}", 8))
    out.append("        Ok(())")
    out.append("    }")
    out.append("")
    out.append(
        "    pub fn deserialize_from(buf: &mut DeserReader<'_>, mode: Endianness) -> SerResult<Self> {"
    )
    out.append("        let mut value = Self::default();")
    for fname, desc, count in fields:
        ident = rust_ident(fname)
        if count:
            out.append(f"        for i in 0..{count} {{")
            out.append(emit_value_deser(model, desc, f"value.{ident}[i]", 12))
            out.append("        }")
        else:
            out.append(emit_value_deser(model, desc, f"value.{ident}", 8))
    out.append("        Ok(value)")
    out.append("    }")
    out.append("}")
    out.append("")


def emit_enum(model, path, name, repr_rust, constants, default_value, out):
    ty = rust_name(path, name)
    where = ".".join(list(path) + [name])
    out.append(f"/// FPP `{where}` (enum), serialized as `{repr_rust}`.")
    out.append("#[derive(Debug, Clone, Copy, PartialEq, Eq)]")
    out.append(f"pub struct {ty}(pub {repr_rust});")
    out.append("")
    out.append(f"impl Default for {ty} {{")
    out.append("    fn default() -> Self {")
    out.append(f"        Self({default_value})")
    out.append("    }")
    out.append("}")
    out.append("")
    out.append(f"impl {ty} {{")
    out.append("    /// Matches the autocoded C++ `SERIALIZED_SIZE`.")
    out.append(
        f"    pub const SERIALIZED_SIZE: usize = core::mem::size_of::<{repr_rust}>();"
    )
    out.append("")
    for cname, value in constants:
        out.append(f"    pub const {cname}: {repr_rust} = {value};")
    out.append("")
    out.append("    /// Mirrors the generated C++ `isValid()`.")
    out.append(f"    pub fn is_valid(value: {repr_rust}) -> bool {{")
    if constants:
        out.append(
            "        " + " || ".join(f"value == Self::{c}" for c, _ in constants)
        )
    else:
        out.append("        false")
    out.append("    }")
    out.append("")
    out.append(
        "    pub fn serialize_to(&self, buf: &mut SerBuffer<'_>, mode: Endianness) -> SerResult {"
    )
    out.append(f"        buf.ser_{repr_rust}(self.0, mode)")
    out.append("    }")
    out.append("")
    out.append("    /// Mirrors the generated C++ deserializer, which rejects a value")
    out.append("    /// outside the constant set with FW_DESERIALIZE_FORMAT_ERROR.")
    out.append(
        "    pub fn deserialize_from(buf: &mut DeserReader<'_>, mode: Endianness) -> SerResult<Self> {"
    )
    out.append(f"        let raw = buf.deser_{repr_rust}(mode)?;")
    out.append("        if !Self::is_valid(raw) {")
    out.append("            return Err(SerError::DeserFormatError);")
    out.append("        }")
    out.append("        Ok(Self(raw))")
    out.append("    }")
    out.append("}")
    out.append("")


def emit_array(model, path, name, elt, count, default_elems, out):
    ty = rust_name(path, name)
    where = ".".join(list(path) + [name])
    elt_ty = field_rust_type(model, elt, None)
    out.append(f"/// FPP `{where}` (array of {count}).")
    out.append("#[derive(Debug, Clone, Copy, PartialEq)]")
    out.append(f"pub struct {ty}(pub [{elt_ty}; {count}]);")
    out.append("")
    out.append(f"impl Default for {ty} {{")
    out.append("    fn default() -> Self {")
    if default_elems is None:
        out.append(f"        Self([{zero_value(model, elt, None)}; {count}])")
    else:
        values = ", ".join(
            fold_value(model, elt, value, f"{where}[{i}]")
            for i, value in enumerate(default_elems)
        )
        out.append(f"        Self([{values}])")
    out.append("    }")
    out.append("}")
    out.append("")
    out.append(f"impl {ty} {{")
    out.append("    /// Matches the autocoded C++ `SERIALIZED_SIZE`.")
    out.append(
        f"    pub const SERIALIZED_SIZE: usize = {size_term(model, elt, count)};"
    )
    out.append("")
    out.append(
        "    pub fn serialize_to(&self, buf: &mut SerBuffer<'_>, mode: Endianness) -> SerResult {"
    )
    out.append("        for item in self.0.iter() {")
    out.append(emit_value_ser(elt, "*item" if elt[0] not in ("named", "mirror", "string") else "item", 12))
    out.append("        }")
    out.append("        Ok(())")
    out.append("    }")
    out.append("")
    out.append(
        "    pub fn deserialize_from(buf: &mut DeserReader<'_>, mode: Endianness) -> SerResult<Self> {"
    )
    out.append("        let mut value = Self::default();")
    out.append(f"        for i in 0..{count} {{")
    out.append(emit_value_deser(model, elt, "value.0[i]", 12))
    out.append("        }")
    out.append("        Ok(value)")
    out.append("    }")
    out.append("}")
    out.append("")


def generate(model, module_name):
    out = [
        f"//! Rust types generated from the FPP model `{module_name}`.",
        "//!",
        "//! GENERATED by tools/fprime-rs/fpp_to_rust.py -- do not edit by hand.",
        "//! Serialization is byte-identical to the C++ code FPP autocodes for",
        "//! the same model; the differential unit test is the proof.",
        "",
        "// Generated code: lints are blanket-allowed rather than hand-tuned.",
        "// FPP member names are preserved verbatim for traceability to the model;",
        "// which lints fire depends on which constructs a given model uses.",
        "#![allow(dead_code)]",
        "#![allow(non_snake_case)]",
        "#![allow(unused_imports)]",
        "#![allow(clippy::derivable_impls)]",
        "#![allow(clippy::field_reassign_with_default)]",
        "#![allow(clippy::identity_op)]",
        "#![allow(clippy::eq_op)]",
        "#![allow(clippy::erasing_op)]",
        "#![allow(clippy::needless_range_loop)]",
        "",
        "use fprime_config::*;",
        "use fprime_core::{DeserReader, Endianness, SerBuffer, SerError, SerResult};",
    ]
    # Only when the model actually uses one: a file that uses none stays
    # byte-identical to what it was before mirrors existed.
    for use_path in sorted(model.abs_imports):
        out.append(f"use {use_path};")
    out.append("")
    # Sorted by qualified name, not left in AST order. The AST order follows
    # the order the sources were passed on the command line, so without this
    # the same model emits a different file depending on how the caller
    # happened to list its dependencies -- which makes a checked-in artifact
    # and its CI diff fragile for no reason. Rust does not care about
    # declaration order, so this costs nothing.
    def qualified(entry):
        return ".".join(list(entry[0]) + [entry[1]])

    for path, name, repr_rust, constants, default_value in sorted(model.enums, key=qualified):
        emit_enum(model, path, name, repr_rust, constants, default_value, out)
    for path, name, elt, count, default_elems in sorted(model.arrays, key=qualified):
        emit_array(model, path, name, elt, count, default_elems, out)
    for path, name, fields, defaults in sorted(model.structs, key=qualified):
        emit_struct(model, path, name, fields, defaults, out)
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--module-name", required=True, help="label recorded in the generated header"
    )
    ap.add_argument(
        "--component",
        help="generate glue for this component instead of type definitions",
    )
    ap.add_argument("--out-header", type=Path, help="component mode: C header path")
    ap.add_argument("--out-shim", type=Path, help="component mode: C++ shim path")
    ap.add_argument(
        "--shim-include",
        default="",
        help="component mode: include path the shim uses for the component header",
    )
    ap.add_argument(
        "--glue-include",
        default="",
        help="component mode: include path the shim uses for the generated C header",
    )
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument(
        "--topology",
        help="emit the resolved topology of this FPP topology as Rust data",
    )
    ap.add_argument(
        "--verify-cpp",
        type=Path,
        help="topology mode: check the resolved facts against this generated "
        "*TopologyAc.cpp",
    )
    ap.add_argument(
        "--modules",
        nargs="*",
        help="restrict generation to these top-level FPP modules",
    )
    ap.add_argument(
        "sources", nargs="+", type=Path, help="model file first, then dependencies"
    )
    args = ap.parse_args()

    try:
        with tempfile.TemporaryDirectory() as tmp:
            ast, analysis = run_fpp_to_json(args.sources, Path(tmp))
        if args.component:
            emit_component(ast, analysis, args)
            return
        if args.topology:
            emit_topology(ast, analysis, args)
            return
        model = build_model(ast, analysis, args.modules)
        code = generate(model, args.module_name)
    except GeneratorError as exc:
        sys.exit(f"fpp-to-rust: {exc}")
    except subprocess.CalledProcessError as exc:
        sys.exit(
            f"fpp-to-rust: fpp-to-json failed:\n{exc.stderr.decode(errors='replace')}"
        )

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(code)

    # Emit rustfmt-clean output so the generated files pass the same
    # `cargo fmt --check` gate as hand-written code. The toolchain is pinned
    # (Rust/rust-toolchain.toml), so this stays deterministic.
    if shutil.which("rustfmt") is not None:
        subprocess.run(["rustfmt", "--edition", "2021", str(args.out)], check=True)
    else:
        print("fpp-to-rust: warning: rustfmt not found; output left unformatted")
    print(
        f"fpp-to-rust: {args.out} "
        f"({len(model.structs)} structs, {len(model.enums)} enums, {len(model.arrays)} arrays)"
    )


# ---------------------------------------------------------------------------
# Component glue generation (Phase 7)
# ---------------------------------------------------------------------------
#
# Phases 2, 3 and 5 each hand-wrote the glue that connects an autocoded C++
# component to its Rust logic: a trait, a set of `extern "C"` stubs, and a
# shim class forwarding handlers across. The shapes stabilized, so they are
# generated here from the same FPP model that produces the C++ base.
#
# Scope, deliberately narrow: input ports whose parameters are all primitives
# or bools passed by value, with no return type. Ports carrying Fw::Buffer,
# user-defined types, `ref` parameters, or return values are refused by name
# -- the marshalling for those is what the hand-written shims still do, and
# guessing at it would produce plausible-but-wrong glue.


PORT_KIND_SYNC = "SyncInput"
PORT_KIND_GUARDED = "GuardedInput"
PORT_KIND_ASYNC = "AsyncInput"
INPUT_KINDS = (PORT_KIND_SYNC, PORT_KIND_GUARDED, PORT_KIND_ASYNC)


class Port:
    """One input port reduced to what the glue needs."""

    def __init__(self, name, kind, params, ret=None):
        self.name = name
        self.kind = kind
        self.params = params  # [(param name, descriptor)]
        self.ret = ret  # descriptor, or None for a void port


class Event:
    """One event the component can emit, which Rust reaches through the Env."""

    def __init__(self, name, severity, params):
        self.name = name
        self.severity = severity
        self.params = params

    @property
    def cpp_method(self):
        """The autocoded base's emitter, e.g. log_WARNING_LO_Foo."""
        return f"log_{SEVERITY_SUFFIX[self.severity]}_{self.name}"


class Channel:
    """One telemetry channel the component can write."""

    def __init__(self, name, desc):
        self.name = name
        self.desc = desc


class Command:
    """One command the component answers."""

    def __init__(self, name, kind, params):
        self.name = name
        self.kind = kind  # Sync, Guarded or Async
        self.params = params


#: FPP severity -> the infix the autocoded event emitter uses.
SEVERITY_SUFFIX = {
    "ActivityHigh": "ACTIVITY_HI",
    "ActivityLow": "ACTIVITY_LO",
    "Command": "COMMAND",
    "Diagnostic": "DIAGNOSTIC",
    "Fatal": "FATAL",
    "WarningHigh": "WARNING_HI",
    "WarningLow": "WARNING_LO",
}


def collect_ports(ast, analysis, component_name, where):
    """Find `component_name` in the analysis and describe its input ports."""
    components = analysis.get("componentMap", {})
    port_defs = {}
    for node in iter_port_definitions(ast):
        port_defs[node[0]] = node[1]

    chosen = None
    for entry in components.values():
        node_id = entry["aNode"]["astNodeId"]
        name = component_ast_name(ast, node_id)
        if name == component_name:
            chosen = entry
            break
    if chosen is None:
        raise GeneratorError(
            f"{where}: no component named '{component_name}' in the model"
        )

    ports = []
    for port_name, spec in chosen["portInterface"]["portMap"].items():
        general = spec.get("General")
        if general is None:
            continue  # special ports (command/event/telemetry) are not glued here
        kind = general["kind"]
        if kind not in INPUT_KINDS:
            continue  # output ports are called from the shim, not dispatched to
        port_symbol = general["ty"].get("DefPort", {}).get("symbol", {}).get("Port")
        if port_symbol is None:
            raise GeneratorError(f"{where}.{port_name}: unsupported port type")
        definition = port_defs.get(port_symbol["nodeId"])
        if definition is None:
            raise GeneratorError(f"{where}.{port_name}: port definition not found")
        params, ret = port_signature(definition, analysis, f"{where}.{port_name}")
        ports.append(Port(port_name, kind, params, ret))
    ports.sort(key=lambda port: port.name)
    return ports


def collect_events(chosen_node_id, ast, analysis, where):
    """Describe the events the component declares.

    Rust reaches these through a generated Env vtable rather than by owning
    the component: emitting an event needs the autocoded base's time port and
    its event buffer, which stay on the C++ side.
    """
    events = []

    def walk(node, inside):
        if isinstance(node, dict):
            if "DefComponent" in node:
                data, this_id = unwrap(node["DefComponent"]["node"])
                walk(data, this_id == chosen_node_id)
                return
            if inside and "SpecEvent" in node:
                data, _ = unwrap(node["SpecEvent"]["node"])
                severity = (
                    next(iter(data["severity"]))
                    if isinstance(data["severity"], dict)
                    else data["severity"]
                )
                if severity not in SEVERITY_SUFFIX:
                    raise GeneratorError(
                        f"{where}.{data['name']}: unknown event severity {severity}"
                    )
                events.append(
                    Event(
                        data["name"],
                        severity,
                        spec_params(
                            data["params"],
                            analysis["typeMap"],
                            f"{where}.{data['name']}",
                        ),
                    )
                )
                return
            for value in node.values():
                walk(value, inside)
        elif isinstance(node, list):
            for item in node:
                walk(item, inside)

    walk(ast, False)
    events.sort(key=lambda event: event.name)
    return events


def collect_component_specs(chosen_node_id, ast, analysis, where):
    """Events, telemetry channels and commands declared by one component.

    All three reach Rust the same way as events do -- through the generated
    Env -- because all three need the autocoded base: the time port and event
    buffer for an event, the channel ID and time tag for telemetry, and the
    command-response port for a command.
    """
    events, channels, commands = [], [], []
    type_map = analysis["typeMap"]

    def walk(node, inside):
        if isinstance(node, dict):
            if "DefComponent" in node:
                data, this_id = unwrap(node["DefComponent"]["node"])
                walk(data, this_id == chosen_node_id)
                return
            if inside and "SpecEvent" in node:
                data, _ = unwrap(node["SpecEvent"]["node"])
                severity = (
                    next(iter(data["severity"]))
                    if isinstance(data["severity"], dict)
                    else data["severity"]
                )
                if severity not in SEVERITY_SUFFIX:
                    raise GeneratorError(
                        f"{where}.{data['name']}: unknown event severity {severity}"
                    )
                events.append(
                    Event(
                        data["name"],
                        severity,
                        spec_params(
                            data["params"], type_map, f"{where}.{data['name']}"
                        ),
                    )
                )
                return
            if inside and "SpecTlmChannel" in node:
                data, _ = unwrap(node["SpecTlmChannel"]["node"])
                _, type_id = unwrap(data["typeName"])
                channels.append(
                    Channel(
                        data["name"],
                        described_type(
                            type_map,
                            type_id,
                            data["typeName"],
                            f"{where}.{data['name']}",
                        ),
                    )
                )
                return
            if inside and "SpecCommand" in node:
                data, _ = unwrap(node["SpecCommand"]["node"])
                kind = (
                    next(iter(data["kind"]))
                    if isinstance(data["kind"], dict)
                    else data["kind"]
                )
                commands.append(
                    Command(
                        data["name"],
                        kind,
                        spec_params(
                            data["params"], type_map, f"{where}.{data['name']}"
                        ),
                    )
                )
                return
            for value in node.values():
                walk(value, inside)
        elif isinstance(node, list):
            for item in node:
                walk(item, inside)

    walk(ast, False)
    events.sort(key=lambda e: e.name)
    channels.sort(key=lambda c: c.name)
    commands.sort(key=lambda c: c.name)
    return events, channels, commands


def iter_port_definitions(ast):
    """Yield (ast node id, DefPort data) for every port definition."""
    found = []

    def walk(node):
        if isinstance(node, dict):
            if "DefPort" in node:
                data, node_id = unwrap(node["DefPort"]["node"])
                found.append((node_id, data))
            for value in node.values():
                walk(value)
        elif isinstance(node, list):
            for item in node:
                walk(item)

    walk(ast)
    return found


def component_ast_name(ast, node_id):
    """Recover a component's declared name from its AST node id."""
    result = []

    def walk(node):
        if isinstance(node, dict):
            if "DefComponent" in node:
                data, this_id = unwrap(node["DefComponent"]["node"])
                if this_id == node_id:
                    result.append(data["name"])
            for value in node.values():
                walk(value)
        elif isinstance(node, list):
            for item in node:
                walk(item)

    walk(ast)
    return result[0] if result else None


def qualified_cpp(type_name_node):
    """The C++ spelling of a `TypeNameQualIdent`, e.g. `ComCfg::Apid`."""
    data, _ = unwrap(type_name_node)
    ident = data.get("TypeNameQualIdent")
    if ident is None:
        return None
    parts = []

    def walk(node):
        inner, _ = unwrap(node)
        if isinstance(inner, str):
            parts.append(inner)
            return
        if "Unqualified" in inner:
            parts.append(inner["Unqualified"]["name"])
            return
        if "Qualified" in inner:
            walk(inner["Qualified"]["qualifier"])
            walk(inner["Qualified"]["name"])
            return
        raise GeneratorError(f"unsupported qualified identifier: {list(inner)}")

    walk(ident["name"])
    return "::".join(parts)


def described_type(type_map, type_id, type_name_node, where):
    """Resolve one parameter or return type into a descriptor the glue can carry.

    Primitives and bools pass straight through. An FPP *enum* is carried as its
    representation type: `fpp-to-cpp` emits enums as a class deriving
    `Fw::Serializable`, so the object itself has a vtable and no stable ABI --
    but its `.e` member is exactly the repr, and that is what crosses.
    Structs, arrays and strings are still refused: they would need the whole
    serialized form to cross, which is a different design.
    """
    # Checked before resolve_type: Fw.Buffer is an FPP *abstract* type, which
    # the type resolver has no representation for.
    cpp = qualified_cpp(type_name_node)
    if cpp == "Fw::Buffer":
        # Fw::Buffer derives Fw::Serializable, so the object has a vtable and
        # no stable ABI -- but it is really (data, size, context), and those
        # three cross fine as a repr(C) triple.
        return ("buffer", None, None, cpp)
    # None: the component/FFI path carries values across extern "C" rather
    # than serializing them, so an abstract type's wire form is not what it
    # would need even if one were known.
    desc = resolve_type(None, type_map, type_id)
    if desc[0] in ("prim", "bool"):
        return desc
    entry = type_map.get(str(type_id))
    if isinstance(entry, dict) and "Enum" in entry:
        repr_name = next(iter(entry["Enum"]["repType"]["kind"]))
        repr_rust, size = PRIMITIVES[repr_name]
        cpp = qualified_cpp(type_name_node)
        if cpp is None:
            raise GeneratorError(f"{where}: enum type has no qualified name")
        return ("enum", repr_rust, size, cpp)
    raise GeneratorError(
        f"{where}: only primitive, bool and enum types are generated yet, "
        f"got {desc[0]}"
    )


class Param:
    """One formal parameter of a port or an event."""

    def __init__(self, name, desc, by_ref):
        self.name = name
        self.desc = desc
        # FPP `ref`: the C++ handler takes a non-const reference, so the value
        # can come back changed. Crosses as a pointer.
        self.by_ref = by_ref

    def __iter__(self):
        # Older call sites unpack these as (name, desc).
        return iter((self.name, self.desc))


def spec_params(params, type_map, where):
    """Describe the formal parameters of a port or an event."""
    out = []
    for entry in params:
        data, _ = unwrap(entry[1])
        kind = (
            next(iter(data["kind"])) if isinstance(data["kind"], dict) else data["kind"]
        )
        if kind not in ("Value", "Ref"):
            raise GeneratorError(f"{where}.{data['name']}: unsupported kind {kind}")
        _, type_id = unwrap(data["typeName"])
        desc = described_type(
            type_map, type_id, data["typeName"], f"{where}.{data['name']}"
        )
        out.append(Param(data["name"], desc, kind == "Ref"))
    return out


def port_signature(definition, analysis, where):
    """A port's parameters and return type, refusing shapes the glue cannot carry."""
    type_map = analysis["typeMap"]
    ret = None
    return_node = opt(definition.get("returnType"))
    if return_node is not None:
        _, ret_id = unwrap(return_node)
        ret = described_type(type_map, ret_id, return_node, f"{where}: return type")
        if ret[0] == "enum":
            # A returned enum would have to be reconstructed as a C++ object on
            # the way back, which is more than passing `.e` across.
            raise GeneratorError(f"{where}: enum return types are not generated yet")
    return spec_params(definition["params"], type_map, where), ret


#: Rust primitive spelling -> the C spelling that crosses the boundary.
C_PRIMITIVES = {
    "u8": "uint8_t",
    "u16": "uint16_t",
    "u32": "uint32_t",
    "u64": "uint64_t",
    "i8": "int8_t",
    "i16": "int16_t",
    "i32": "int32_t",
    "i64": "int64_t",
    "f32": "float",
    "f64": "double",
}


def c_type(desc):
    """The C spelling of a parameter type. Enums cross as their repr."""
    if desc[0] == "bool":
        return "bool"
    if desc[0] == "buffer":
        return "FprimeRsBuffer"
    return C_PRIMITIVES[desc[1]]


def c_param_decl(param):
    """How a parameter appears in the C declaration and the shim's call.

    A buffer always crosses as a pointer, by-value or not: the shim marshals
    the C++ object into a local triple and passes its address either way.
    Only a `ref` buffer is mutable, and only it is written back.
    """
    ty = c_type(param.desc)
    if param.desc[0] == "buffer":
        return f"{ty}* {param.name}" if param.by_ref else f"const {ty}* {param.name}"
    return f"{ty}* {param.name}" if param.by_ref else f"{ty} {param.name}"


def rust_param_type(desc):
    """The Rust spelling. An enum crosses as its representation type."""
    if desc[0] == "bool":
        return "bool"
    if desc[0] == "buffer":
        return "Buffer"
    return desc[1]


def rust_param_decl(param):
    """How a parameter appears in a trait method signature."""
    ty = rust_param_type(param.desc)
    if param.desc[0] == "buffer":
        # A by-value buffer is still borrowed: the payload belongs to the C++
        # object for the call, and copying the triple would not copy the bytes.
        return f"{param.name}: &mut {ty}" if param.by_ref else f"{param.name}: &{ty}"
    return f"{param.name}: &mut {ty}" if param.by_ref else f"{param.name}: {ty}"


def rust_stub_decl(param):
    """How a parameter appears on the `extern "C"` stub."""
    ty = rust_param_type(param.desc)
    if param.desc[0] == "buffer":
        pointer = "*mut" if param.by_ref else "*const"
        return f"{param.name}: {pointer} {ty}"
    return f"{param.name}: *mut {ty}" if param.by_ref else f"{param.name}: {ty}"


def cpp_param_type(param):
    """The C++ spelling in the autocoded handler's signature.

    fpp-to-cpp passes user-defined types by reference -- `const T&` for a
    value parameter, `T&` for a `ref` one -- so an enum or a buffer is
    declared that way even where FPP calls it a value.
    """
    desc, name = param.desc, param.name
    if desc[0] in ("enum", "buffer"):
        qualifier = "" if param.by_ref else "const "
        return f"{qualifier}{desc[3]}& {name}"
    if param.by_ref:
        return f"{c_type(desc)}& {name}"
    return f"{c_type(desc)} {name}"


def cpp_prologue(param):
    """Statements the shim runs before the call, to marshal a parameter in."""
    desc, name = param.desc, param.name
    if desc[0] == "buffer":
        return [
            f"    FprimeRsBuffer rs_{name} = {{{name}.getData(), {name}.getSize(), "
            f"{name}.getContext()}};"
        ]
    if param.by_ref and desc[0] == "enum":
        # The enum object cannot cross; its value can.
        return [
            f"    {c_type(desc)} rs_{name} = static_cast<{c_type(desc)}>({name}.e);"
        ]
    return []


def cpp_argument(param):
    """How the shim hands a parameter across the boundary."""
    desc, name = param.desc, param.name
    if desc[0] == "buffer":
        return f"&rs_{name}"
    if desc[0] == "enum":
        if param.by_ref:
            return f"&rs_{name}"
        # `.e` is the underlying value; the object itself has a vtable.
        return f"static_cast<{c_type(desc)}>({name}.e)"
    return f"&{name}" if param.by_ref else name


def cpp_epilogue(param):
    """Statements the shim runs after the call, to marshal a parameter back."""
    desc, name = param.desc, param.name
    if desc[0] == "buffer" and param.by_ref:
        # Only size and context are written back. `setData` and `setSize` are
        # bounds-checked against the original allocation in C++, and letting
        # Rust move the data pointer would be a different contract than the
        # one Fw::Buffer documents.
        return [
            f"    {name}.setSize(rs_{name}.size);",
            f"    {name}.setContext(rs_{name}.context);",
        ]
    if param.by_ref and desc[0] == "enum":
        return [f"    {name} = static_cast<{desc[3]}::T>(rs_{name});"]
    return []


def snake(name):
    """FPP port names are camelCase; trait methods read better in snake_case.

    Command names are conventionally SCREAMING_SNAKE_CASE already, and running
    the camelCase rule over those produces `s_e_t__t_o_t_a_l`, so they are
    simply lowercased.
    """
    if name.upper() == name:
        return name.lower()
    out = []
    for index, char in enumerate(name):
        if char.isupper() and index > 0:
            out.append("_")
        out.append(char.lower())
    return "".join(out)


def generate_component_rust(
    module_path, component, ports, events, channels, commands, model_label
):
    """The Rust side: a handler trait plus the extern "C" dispatch stubs.

    The dispatch carries the component INSTANCE, not a global: F Prime
    topologies routinely instantiate a component type more than once, and a
    per-type global would silently share one Rust object (and its state)
    across every instance. It also avoids a `static mut` that every port call
    would read unsynchronized, which is a data race whatever the calling
    convention.

    The instance travels as a thin `*const c_void` pointing at a
    `&'static dyn Trait` -- a pointer to the fat pointer -- because a trait
    object cannot be squeezed into one pointer width.
    """
    prefix = f"fprime_rs_{component.lower()}"
    trait = f"{component}Handlers"
    # The Env exists if the component can call anything back: emit an event,
    # write telemetry, or answer a command.
    has_env = bool(events or channels or commands)
    out = [
        f"//! Rust glue for the FPP component `{'.'.join(list(module_path) + [component])}`.",
        "//!",
        "//! GENERATED by tools/fprime-rs/fpp_to_rust.py --component -- do not edit.",
        f"//! Model: `{model_label}`",
        "//!",
        "//! Implement [`" + trait + "`] on a `'static` value, hand its",
        "//! [`handler_ptr`] to the component's `setRustHandlers` before the",
        "//! topology connects, and every port call arrives at that instance.",
        "#![allow(dead_code)]",
        "#![allow(non_snake_case)]",
        "",
        "use core::ffi::c_void;",
        "",
    ]
    if any(p.desc[0] == "buffer" for port in ports for p in port.params):
        out += [
            "use fprime_config::FwSizeType;",
            "",
            "/// An `Fw::Buffer` as it crosses the boundary: the triple the C++",
            "/// object really is. A `ref` parameter's `size` and `context` are",
            "/// written back to the C++ object after the call; `data` is not,",
            "/// because `Fw::Buffer::setData` is bounds-checked against the",
            "/// original allocation and moving it is a different contract.",
            "#[repr(C)]",
            "pub struct Buffer {",
            "    /// Start of the payload",
            "    pub data: *mut u8,",
            "    /// Bytes available at `data`",
            "    pub size: FwSizeType,",
            "    /// Caller-defined tag carried with the buffer",
            "    pub context: u32,",
            "}",
            "",
            "impl Buffer {",
            "    /// The payload as a slice.",
            "    ///",
            "    /// # Safety",
            "    /// Valid only while the C++ `Fw::Buffer` this came from is alive",
            "    /// and its allocation is unchanged -- i.e. during the port call.",
            "    pub unsafe fn as_slice(&self) -> &[u8] {",
            "        if self.size == 0 || self.data.is_null() {",
            "            &[]",
            "        } else {",
            "            // SAFETY: the caller guarantees the buffer outlives this.",
            "            unsafe { core::slice::from_raw_parts(self.data, self.size as usize) }",
            "        }",
            "    }",
            "}",
            "",
        ]
    out += [
        "/// Handlers for each input port of the component.",
        "///",
        "/// Methods take `&self`: sync input ports run on the caller's thread, so",
        "/// two ports of one instance can be entered concurrently. Mutable state",
        "/// belongs behind interior mutability.",
        f"pub trait {trait}: Sync {{",
    ]
    for port in ports:
        args = "".join(f", {rust_param_decl(p)}" for p in port.params)
        ret = f" -> {rust_param_type(port.ret)}" if port.ret else ""
        env = ", env: &Env" if has_env else ""
        out.append(f"    /// `{port.name}` ({port.kind})")
        out.append(f"    fn {snake(port.name)}(&self, port_num: i32{env}{args}){ret};")
    for command in commands:
        args = "".join(f", {rust_param_decl(p)}" for p in command.params)
        out.append(f"    /// `{command.name}` command ({command.kind})")
        out.append("    ///")
        out.append("    /// Must answer exactly once with `env.respond(...)`:")
        out.append("    /// the dispatcher is waiting on it.")
        out.append(
            f"    fn cmd_{snake(command.name)}"
            f"(&self, env: &Env, opcode: u32, cmd_seq: u32{args});"
        )
    out.append("}")
    if has_env:
        out.append("")
        out += generate_env_rust(events, channels, commands)
    out.append("")
    out.append("/// The opaque instance pointer to hand to `setRustHandlers`.")
    out.append("///")
    out.append("/// Takes a reference to a `'static` trait object reference, so the")
    out.append("/// resulting thin pointer stays valid for the life of the program and")
    out.append("/// nothing needs an allocator.")
    out.append(
        f"pub fn handler_ptr(handlers: &'static &'static dyn {trait}) -> *const c_void {{"
    )
    out.append("    handlers as *const _ as *const c_void")
    out.append("}")
    out.append("")
    out.append(f"/// Recover the instance a stub was handed.")
    out.append("///")
    out.append("/// # Safety")
    out.append("/// `handlers` must be a pointer produced by [`handler_ptr`].")
    out.append(
        f"unsafe fn handlers_from(handlers: *const c_void) -> Option<&'static dyn {trait}> {{"
    )
    out.append("    if handlers.is_null() {")
    out.append("        return None;")
    out.append("    }")
    out.append(
        "    // SAFETY: the caller guarantees this came from `handler_ptr`, which"
    )
    out.append("    // produces a pointer to a `'static` trait-object reference.")
    out.append(f"    Some(*unsafe {{ &*(handlers as *const &'static dyn {trait}) }})")
    out.append("}")
    out.append("")
    for port in ports:
        c_args = "".join(f", {rust_stub_decl(p)}" for p in port.params)
        call_args = "".join(f", {p.name}" for p in port.params)
        # `ref` parameters are bound before the call so the unsafe block can
        # carry its own SAFETY comment, which the workspace lints require.
        borrowed = [p for p in port.params if p.by_ref or p.desc[0] == "buffer"]
        ref_binds = (
            [
                "        // SAFETY: these arrive as pointers the C++ side owns",
                "        // for the duration of this call.",
            ]
            if borrowed
            else []
        )
        ref_binds += [
            (
                f"        let {p.name} = unsafe {{ &mut *{p.name} }};"
                if p.by_ref
                else f"        let {p.name} = unsafe {{ &*{p.name} }};"
            )
            for p in borrowed
        ]
        env_param = ", env: *const Env" if has_env else ""
        env_arg = ", env" if has_env else ""
        ret_ty = rust_param_type(port.ret) if port.ret else None
        ret = f" -> {ret_ty}" if ret_ty else ""
        out.append(
            f"/// Dispatch stub for `{port.name}`, called by the generated C++ shim."
        )
        out.append("///")
        out.append("/// # Safety")
        out.append(
            "/// `handlers` must be a pointer produced by [`handler_ptr`], or null."
        )
        if has_env:
            out.append("/// `env` must point to a live Env for the calling instance.")
        if ret_ty:
            out.append("///")
            out.append(
                f"/// A null `handlers` yields `{ret_ty}::default()`: a component "
                "whose"
            )
            out.append(
                "/// handlers were never installed answers with a zero rather than"
            )
            out.append("/// dereferencing.")
        out.append("#[no_mangle]")
        out.append(
            f'pub unsafe extern "C" fn {prefix}_{port.name}('
            f"handlers: *const c_void, port_num: i32{env_param}{c_args}){ret} {{"
        )
        out.append(
            "    // SAFETY: forwarded unchanged from the shim, which stores only"
        )
        out.append("    // pointers produced by `handler_ptr`.")
        out.append("    if let Some(handlers) = unsafe { handlers_from(handlers) } {")
        if has_env:
            out.append("        // SAFETY: the shim builds the Env on its own stack")
            out.append("        // and it outlives this call.")
            out.append("        let env = unsafe { &*env };")
        out += ref_binds
        body = f"handlers.{snake(port.name)}(port_num{env_arg}{call_args})"
        if ret_ty:
            out.append(f"        return {body};")
            out.append("    }")
            out.append(f"    {ret_ty}::default()")
        else:
            out.append(f"        {body};")
            out.append("    }")
        out.append("}")
        out.append("")
    for command in commands:
        c_args = "".join(f", {rust_stub_decl(p)}" for p in command.params)
        call_args = "".join(f", {p.name}" for p in command.params)
        borrowed = [p for p in command.params if p.by_ref or p.desc[0] == "buffer"]
        cmd_binds = (
            [
                "        // SAFETY: these arrive as pointers the C++ side owns",
                "        // for the duration of this call.",
            ]
            if borrowed
            else []
        )
        cmd_binds += [
            (
                f"        let {p.name} = unsafe {{ &mut *{p.name} }};"
                if p.by_ref
                else f"        let {p.name} = unsafe {{ &*{p.name} }};"
            )
            for p in borrowed
        ]
        out += [
            f"/// Dispatch stub for the `{command.name}` command.",
            "///",
            "/// # Safety",
            "/// `handlers` must come from [`handler_ptr`], or be null; `env`",
            "/// must point to a live Env for the calling instance.",
            "#[no_mangle]",
            f'pub unsafe extern "C" fn {prefix}_cmd_{command.name}('
            "handlers: *const c_void, env: *const Env, opcode: u32, "
            f"cmd_seq: u32{c_args}) {{",
            "    // SAFETY: forwarded unchanged from the shim.",
            "    if let Some(handlers) = unsafe { handlers_from(handlers) } {",
            "        // SAFETY: the shim builds the Env on its own stack and it",
            "        // outlives this call.",
            "        let env = unsafe { &*env };",
            *cmd_binds,
            f"        handlers.cmd_{snake(command.name)}"
            f"(env, opcode, cmd_seq{call_args});",
            "    } else {",
            "        // A port call with no handlers installed is a no-op, but a",
            "        // command is not: the dispatcher blocks until it is",
            "        // answered, so an unanswered command hangs it. Fail the",
            "        // command instead.",
            "        // SAFETY: as above.",
            "        unsafe { &*env }.respond(opcode, cmd_seq, cmd_response::EXECUTION_ERROR);",
            "    }",
            "}",
            "",
        ]
    return "\n".join(out)


def generate_env_rust(events, channels=(), commands=()):
    """The Env: what a Rust handler can call back into the component to do.

    Only events today. Each entry is a C function pointer the generated C++
    shim fills with a captureless lambda over the autocoded base's emitter, so
    the base keeps ownership of the time port and the event buffer.
    """
    out = [
        "/// Callbacks into the component instance, for what Rust cannot own.",
        "///",
        "/// Built by the generated shim on its own stack for the duration of one",
        "/// port call. Do not store it.",
        "#[repr(C)]",
        "pub struct Env {",
        "    /// The C++ component instance the callbacks act on",
        "    pub component: *mut c_void,",
    ]
    for event in events:
        args = "".join(f", {rust_param_type(p.desc)}" for p in event.params)
        out.append(f"    /// Emit the `{event.name}` event ({event.severity})")
        out.append(
            f"    pub {snake(event.name)}: "
            f'Option<unsafe extern "C" fn(*mut c_void{args})>,'
        )
    for channel in channels:
        out.append(f"    /// Write the `{channel.name}` telemetry channel")
        out.append(
            f"    pub tlm_{snake(channel.name)}: "
            f'Option<unsafe extern "C" fn(*mut c_void, '
            f"{rust_param_type(channel.desc)})>,"
        )
    if commands:
        out += [
            "    /// Answer a command. Every command handler must call this",
            "    /// exactly once: the dispatcher is waiting on it.",
            "    pub cmd_response: ",
            '        Option<unsafe extern "C" fn(*mut c_void, u32, u32, u32)>,',
        ]
    out += (
        [
            "}",
            "",
            "/// The command responses the dispatcher understands.",
            "pub mod cmd_response {",
            "    /// `Fw::CmdResponse::OK`",
            "    pub const OK: u32 = 0;",
            "    /// `Fw::CmdResponse::INVALID_OPCODE`",
            "    pub const INVALID_OPCODE: u32 = 1;",
            "    /// `Fw::CmdResponse::VALIDATION_ERROR`",
            "    pub const VALIDATION_ERROR: u32 = 2;",
            "    /// `Fw::CmdResponse::FORMAT_ERROR`",
            "    pub const FORMAT_ERROR: u32 = 3;",
            "    /// `Fw::CmdResponse::EXECUTION_ERROR`",
            "    pub const EXECUTION_ERROR: u32 = 4;",
            "    /// `Fw::CmdResponse::BUSY`",
            "    pub const BUSY: u32 = 5;",
            "}",
            "",
            "impl Env {",
        ]
        if commands
        else ["}", "", "impl Env {"]
    )
    for event in events:
        args = "".join(f", {p.name}: {rust_param_type(p.desc)}" for p in event.params)
        call = "".join(f", {p.name}" for p in event.params)
        out += [
            f"    /// Emit `{event.name}`; a no-op if the shim left it unset.",
            f"    pub fn {snake(event.name)}(&self{args}) {{",
            f"        if let Some(emit) = self.{snake(event.name)} {{",
            "            // SAFETY: the shim installs a function valid for the",
            "            // component pointer it paired with it.",
            f"            unsafe {{ emit(self.component{call}) }};",
            "        }",
            "    }",
        ]
    for channel in channels:
        out += [
            f"    /// Write `{channel.name}`; a no-op if the shim left it unset.",
            f"    pub fn tlm_{snake(channel.name)}(&self, value: "
            f"{rust_param_type(channel.desc)}) {{",
            f"        if let Some(write) = self.tlm_{snake(channel.name)} {{",
            "            // SAFETY: the shim installs a function valid for the",
            "            // component pointer it paired with it.",
            "            unsafe { write(self.component, value) };",
            "        }",
            "    }",
        ]
    if commands:
        out += [
            "    /// Answer a command, with one of the `cmd_response` constants.",
            "    pub fn respond(&self, opcode: u32, cmd_seq: u32, response: u32) {",
            "        if let Some(respond) = self.cmd_response {",
            "            // SAFETY: as above.",
            "            unsafe { respond(self.component, opcode, cmd_seq, response) };",
            "        }",
            "    }",
        ]
    out.append("}")
    return out


def generate_component_header(
    module_path, component, ports, events, channels, commands, model_label
):
    """The C declarations the shim calls."""
    guard = f"FPRIME_RS_{component.upper()}_GLUE_H"
    prefix = f"fprime_rs_{component.lower()}"
    has_env = bool(events or channels or commands)
    out = [
        f"// GENERATED by tools/fprime-rs/fpp_to_rust.py --component -- do not edit.",
        f"// Model: {model_label}",
        f"// Component: {'.'.join(list(module_path) + [component])}",
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        "#include <stdbool.h>",
        "#include <stdint.h>",
        "",
        "#include <Fw/FPrimeBasicTypes.h>",
        "",
        "// Each stub carries the component INSTANCE as an opaque pointer, obtained",
        "// from the Rust side's handler_ptr() and installed with the component's",
        "// setRustHandlers(). A null pointer makes the call a no-op, so a component",
        "// that was never given handlers fails quietly rather than dereferencing.",
        "",
        "#ifdef __cplusplus",
        'extern "C" {',
        "#endif",
        "",
    ]
    uses_buffer = any(p.desc[0] == "buffer" for port in ports for p in port.params)
    if uses_buffer:
        out += [
            "//! An Fw::Buffer as it crosses the boundary.",
            "//!",
            "//! Fw::Buffer derives Fw::Serializable, so the object has a vtable",
            '//! and cannot cross extern "C" -- but it is really this triple, and',
            "//! the shim marshals it in and (for a `ref` parameter) back out.",
            "typedef struct {",
            "    uint8_t* data;",
            "    FwSizeType size;",
            "    uint32_t context;",
            "} FprimeRsBuffer;",
            "",
        ]
    if has_env:
        out.append("//! Callbacks into the component, for what Rust cannot own.")
        out.append("//! The shim fills these in for the duration of one port call.")
        out.append(f"typedef struct {prefix}_Env {{")
        out.append("    void* component;")
        for event in events:
            args = "".join(f", {c_type(p.desc)} {p.name}" for p in event.params)
            out.append(f"    void (*{snake(event.name)})(void* component{args});")
        for channel in channels:
            out.append(
                f"    void (*tlm_{snake(channel.name)})"
                f"(void* component, {c_type(channel.desc)} value);"
            )
        if commands:
            out.append(
                "    void (*cmd_response)(void* component, uint32_t opcode, "
                "uint32_t cmdSeq, uint32_t response);"
            )
        out.append(f"}} {prefix}_Env;")
        out.append("")
    for port in ports:
        args = "".join(f", {c_param_decl(p)}" for p in port.params)
        env = f", const {prefix}_Env* env" if has_env else ""
        ret = c_type(port.ret) if port.ret else "void"
        out.append(f"//! Dispatch `{port.name}` ({port.kind}) into the Rust handler")
        out.append(
            f"{ret} {prefix}_{port.name}(const void* handlers, int32_t portNum{env}{args});"
        )
        out.append("")
    for command in commands:
        args = "".join(f", {c_param_decl(p)}" for p in command.params)
        out.append(f"//! Dispatch the `{command.name}` command into Rust")
        out.append(
            f"void {prefix}_cmd_{command.name}(const void* handlers, "
            f"const {prefix}_Env* env, uint32_t opcode, uint32_t cmdSeq{args});"
        )
        out.append("")
    out += ["#ifdef __cplusplus", "}", "#endif", "", f"#endif  // {guard}", ""]
    return "\n".join(out)


def env_construction(component, events, channels, commands, prefix):
    """The Env the shim hands to Rust for one port call.

    Each entry is a *captureless* lambda, so it converts to a plain function
    pointer. Written inside a member function, the lambda's body sits in the
    class's scope, which is what lets it reach the autocoded base's protected
    emitter through the instance it is handed.
    """
    out = [
        f"    {prefix}_Env env = {{}};",
        "    env.component = this;",
    ]
    for event in events:
        params = "".join(f", {c_type(p.desc)} {p.name}" for p in event.params)
        call = "".join(f", {p.name}" for p in event.params)
        out += [
            f"    env.{snake(event.name)} = [](void* component{params}) {{",
            f"        static_cast<{component}*>(component)->{event.cpp_method}({call.lstrip(', ')});",
            "    };",
        ]
    for channel in channels:
        out += [
            f"    env.tlm_{snake(channel.name)} = "
            f"[](void* component, {c_type(channel.desc)} value) {{",
            f"        static_cast<{component}*>(component)->tlmWrite_{channel.name}(value);",
            "    };",
        ]
    if commands:
        out += [
            "    env.cmd_response = [](void* component, uint32_t opcode,",
            "                          uint32_t cmdSeq, uint32_t response) {",
            f"        static_cast<{component}*>(component)->cmdResponse_out(",
            "            static_cast<FwOpcodeType>(opcode), cmdSeq,",
            "            Fw::CmdResponse(static_cast<Fw::CmdResponse::T>(response)));",
            "    };",
        ]
    return out


def generate_component_shim(
    module_path,
    component,
    ports,
    events,
    channels,
    commands,
    model_label,
    include_path,
    glue_include,
):
    """The C++ shim: the component class, forwarding each handler into Rust."""
    # Every segment of the module path, not just the first: components in
    # nested modules (Svc.Ccsds.ApidManager) are the common case in-tree, and
    # emitting only the outer one puts the definitions in the wrong scope.
    namespaces = list(module_path) if module_path else ["Components"]
    out = (
        [
            "// ======================================================================",
            f"// GENERATED by tools/fprime-rs/fpp_to_rust.py --component -- do not edit.",
            f"// Model: {model_label}",
            "//",
            f"// Shim for {'.'.join(list(module_path) + [component])}: the autocoded base",
            "// keeps ownership of ports, threading and the framework plumbing; each",
            "// input port handler forwards into the Rust implementation.",
            "// ======================================================================",
            "",
            f'#include "{include_path}"',
            f'#include "{glue_include}"',
            "",
        ]
        + [f"namespace {name} {{" for name in namespaces]
        + [
            "",
            f"{component} ::{component}(const char* const compName)",
            f"    : {component}ComponentBase(compName), m_rustHandlers(nullptr) {{}}",
            "",
            f"{component} ::~{component}() {{}}",
            "",
            "//! Install the Rust instance that answers this component's ports. Call",
            "//! once during initialization, before the topology connects ports: the",
            "//! pointer is read by every handler and is not synchronized.",
            f"void {component} ::setRustHandlers(const void* handlers) {{",
            "    this->m_rustHandlers = handlers;",
            "}",
            "",
        ]
    )
    prefix = f"fprime_rs_{component.lower()}"
    has_env = bool(events or channels or commands)
    for port in ports:
        args = "".join(f", {cpp_param_type(p)}" for p in port.params)
        call = "".join(f", {cpp_argument(p)}" for p in port.params)
        ret = c_type(port.ret) if port.ret else "void"
        out.append(
            f"{ret} {component} ::{port.name}_handler(FwIndexType portNum{args}) {{"
        )
        for p in port.params:
            out += cpp_prologue(p)
        if has_env:
            out += env_construction(component, events, channels, commands, prefix)
        env_arg = ", &env" if has_env else ""
        invoke = (
            f"{prefix}_{port.name}(this->m_rustHandlers, "
            f"static_cast<int32_t>(portNum){env_arg}{call})"
        )
        epilogue = [line for p in port.params for line in cpp_epilogue(p)]
        if epilogue and port.ret:
            out.append(f"    const {ret} rs_result = {invoke};")
            out += epilogue
            out.append("    return rs_result;")
        else:
            out.append(f"    {'return ' if port.ret else ''}{invoke};")
            out += epilogue
        out.append("}")
        out.append("")
    for command in commands:
        args = "".join(f", {cpp_param_type(p)}" for p in command.params)
        call = "".join(f", {cpp_argument(p)}" for p in command.params)
        out.append(
            f"void {component} ::{command.name}_cmdHandler("
            f"FwOpcodeType opCode, U32 cmdSeq{args}) {{"
        )
        for p in command.params:
            out += cpp_prologue(p)
        out += env_construction(component, events, channels, commands, prefix)
        out.append(
            f"    {prefix}_cmd_{command.name}(this->m_rustHandlers, &env, "
            f"static_cast<uint32_t>(opCode), cmdSeq{call});"
        )
        for p in command.params:
            out += cpp_epilogue(p)
        out.append("}")
        out.append("")
    for name in reversed(namespaces):
        out.append(f"}}  // namespace {name}")
    out.append("")
    return "\n".join(out)


def clang_format_cpp(path):
    """Format generated C++ in place, if clang-format can be found."""
    candidates = [
        Path(__file__).resolve().parents[2] / "fprime-venv" / "bin" / "clang-format",
        Path(__file__).resolve().parents[2] / ".venv" / "bin" / "clang-format",
    ]
    exe = next((str(c) for c in candidates if c.exists()), None) or shutil.which(
        "clang-format"
    )
    if exe is None:
        print("fpp-to-rust: warning: clang-format not found; C++ left unformatted")
        return
    style = Path(__file__).resolve().parents[2] / ".clang-format"
    subprocess.run([exe, f"--style=file:{style}", "-i", str(path)], check=True)


def emit_component(ast, analysis, args):
    """Component mode: write the Rust glue, the C header, and the C++ shim."""
    if args.out_header is None or args.out_shim is None:
        raise GeneratorError("component mode requires --out-header and --out-shim")

    module_path = ()
    for entry in analysis.get("componentMap", {}).values():
        name = component_ast_name(ast, entry["aNode"]["astNodeId"])
        if name == args.component:
            module_path = component_module_path(ast, args.component)
            break

    ports = collect_ports(ast, analysis, args.component, args.component)
    events, channels, commands = [], [], []
    for entry in analysis.get("componentMap", {}).values():
        node_id = entry["aNode"]["astNodeId"]
        if component_ast_name(ast, node_id) == args.component:
            events, channels, commands = collect_component_specs(
                node_id, ast, analysis, args.component
            )
            break
    if not ports:
        raise GeneratorError(
            f"{args.component}: no input ports the generator can carry "
            "(see the scope note in this file)"
        )

    writes = [
        (
            args.out,
            generate_component_rust(
                module_path,
                args.component,
                ports,
                events,
                channels,
                commands,
                args.module_name,
            ),
        ),
        (
            args.out_header,
            generate_component_header(
                module_path,
                args.component,
                ports,
                events,
                channels,
                commands,
                args.module_name,
            ),
        ),
        (
            args.out_shim,
            generate_component_shim(
                module_path,
                args.component,
                ports,
                events,
                channels,
                commands,
                args.module_name,
                args.shim_include,
                args.glue_include,
            ),
        ),
    ]
    for path, text in writes:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)

    # The generated C++ lands under Svc/, which the repository's format check
    # covers, so it has to satisfy clang-format. Emitting it unformatted put
    # the format check and the CI byte-diff of this generator's output in
    # direct conflict -- the same trap gen_fw_kernel.py hit in Phase 8.
    clang_format_cpp(args.out_shim)

    if shutil.which("rustfmt") is not None:
        subprocess.run(["rustfmt", "--edition", "2021", str(args.out)], check=True)
    print(
        f"fpp-to-rust: {args.component} glue -> {args.out}, {args.out_header}, "
        f"{args.out_shim} ({len(ports)} input ports)"
    )


def component_module_path(ast, component_name):
    """The module path enclosing a component definition."""
    found = []

    def walk(members, path):
        for entry in members:
            node = entry[1]
            if not isinstance(node, dict):
                continue
            for kind, wrapper in node.items():
                if kind == "DefModule":
                    data, _ = unwrap(wrapper["node"])
                    walk(data["members"], path + [data["name"]])
                elif kind == "DefComponent":
                    data, _ = unwrap(wrapper["node"])
                    if data["name"] == component_name:
                        found.append(tuple(path))

    for unit in ast:
        walk(unit["members"], [])
    return found[0] if found else ()


# ---------------------------------------------------------------------------
# Topology generation (Phase 11)
# ---------------------------------------------------------------------------
#
# Verification-only, deliberately. `fpp-to-cpp` turns a 582-line topology model
# into 15,530 lines of C++; before anyone writes a Rust runtime to replace
# that, the cheap question to settle is whether the topology is *mechanical* --
# whether everything the generated C++ knows is already derivable from the
# model. `fpp-to-json` exposes the resolved topology, so this emits it as Rust
# data and then checks those facts against the C++ that already exists in the
# build tree. Nothing links and nothing runs; what it produces is an answer.


def qualified(name):
    """`{"qualifier": ["CdhCore"], "base": "cmdDisp"}` -> `CdhCore.cmdDisp`."""
    return ".".join(list(name.get("qualifier", [])) + [name["base"]])


class Instance:
    """One component instance, as the topology resolved it."""

    def __init__(
        self, name, component, base_id, active, priority, stack, queue, cpu, phases
    ):
        self.name = name  # dotted, e.g. CdhCore.cmdDisp
        self.component = component
        self.base_id = base_id
        self.active = active
        self.priority = priority
        self.stack = stack
        self.queue = queue
        self.cpu = cpu
        # [(phase name, literal C++)] -- the model's escape hatch, see below
        self.phases = phases

    @property
    def cpp(self):
        """The C++ spelling the generated topology uses."""
        return self.name.replace(".", "::")


class Connection:
    """One resolved connection: an output port to an input port."""

    def __init__(self, graph, from_inst, from_port, from_num, to_inst, to_port, to_num):
        self.graph = graph
        self.from_inst = from_inst
        self.from_port = from_port
        self.from_num = from_num
        self.to_inst = to_inst
        self.to_port = to_port
        self.to_num = to_num


def component_ast_kind(ast, node_id):
    """`Active`, `Queued` or `Passive` for a component's AST node.

    Only active components get a thread, and so a `start()` call in the
    generated topology -- which is not the same as "has a priority": one of
    Ref's active components leaves its priority to the default.
    """
    found = []

    def walk(node):
        if isinstance(node, dict):
            if "DefComponent" in node:
                data, this_id = unwrap(node["DefComponent"]["node"])
                if this_id == node_id:
                    kind = data.get("kind")
                    found.append(next(iter(kind)) if isinstance(kind, dict) else kind)
            for value in node.values():
                walk(value)
        elif isinstance(node, list):
            for item in node:
                walk(item)

    walk(ast)
    return found[0] if found else None


def collect_phase_blocks(ast, instance_entry):
    """The `phase Fpp.ToCpp.Phases.X """ + '"""..."""' + """` blocks on one instance.

    These are literal C++ pasted into the generated topology, and they are the
    one part of a topology that is *not* mechanical: nothing derives them, and
    a Rust-native topology cannot translate them -- it can only require them
    to be rewritten. Ref uses them to start a passive component
    (`CdhCore::version.start()`), which is why a naive "active components ==
    start() calls" check does not hold.
    """
    specs = instance_entry.get("initSpecifierMap") or {}
    wanted = {entry["aNode"]["astNodeId"] for entry in specs.values()}
    if not wanted:
        return []

    found = []

    def phase_name(node):
        data, _ = unwrap(node)
        if "ExprDot" in data:
            return unwrap(data["ExprDot"]["id"])[0]
        return None

    def walk(node):
        if isinstance(node, dict):
            inner = node.get("AstNode")
            if isinstance(inner, dict) and inner.get("id") in wanted:
                data = inner["data"]
                if "code" in data and "phase" in data:
                    found.append((phase_name(data["phase"]), data["code"]))
            for value in node.values():
                walk(value)
        elif isinstance(node, list):
            for item in node:
                walk(item)

    walk(ast)
    found.sort(key=lambda entry: entry[0] or "")
    return found


def _port_endpoint(end, number):
    """(instance ast id, port name, port number) for one end of a connection."""
    port = end["port"]
    instance = port["interfaceInstance"]
    if "InterfaceComponentInstance" not in instance:
        return None
    instance_id = instance["InterfaceComponentInstance"]["astNodeId"]
    holder = port["portInstance"]
    kind = next(iter(holder))
    name = holder[kind]["specifier"]["name"]
    return instance_id, name, number


def collect_topology(ast, analysis, topology_name):
    """Resolve a topology into its instance and connection tables."""
    topologies = analysis.get("topologyMap", {})
    chosen = None
    for entry in topologies.values():
        if qualified(entry["qualifiedName"]) == topology_name:
            chosen = entry
            break
    if chosen is None:
        available = sorted(qualified(e["qualifiedName"]) for e in topologies.values())
        raise GeneratorError(
            f"no topology named '{topology_name}'; found: {', '.join(available)}"
        )

    by_node = {
        entry["aNode"]["astNodeId"]: entry
        for entry in analysis.get("componentInstanceMap", {}).values()
    }

    # `instanceMap` also carries the imported subtopologies; only the
    # component instances become objects in the generated C++.
    instances = []
    for key, _loc in chosen["instanceMap"]:
        if "InterfaceComponentInstance" not in key:
            continue
        entry = by_node[key["InterfaceComponentInstance"]["astNodeId"]]
        kind = component_ast_kind(ast, entry["component"]["astNodeId"])
        instances.append(
            Instance(
                qualified(entry["qualifiedName"]),
                component_ast_name(ast, entry["component"]["astNodeId"]),
                entry["baseId"],
                kind == "Active",
                opt(entry.get("priority")),
                opt(entry.get("stackSize")),
                opt(entry.get("queueSize")),
                opt(entry.get("cpu")),
                collect_phase_blocks(ast, entry),
            )
        )
    instances.sort(key=lambda i: i.name)

    # Port numbers are resolved per connection, in a parallel list.
    def number_map(entries):
        out = {}
        for connection, number in entries:
            # The port number is a bare integer here, not an Option, unlike
            # the resource fields on an instance.
            out[json.dumps(connection, sort_keys=True)] = (
                number if isinstance(number, int) else opt(number)
            )
        return out

    from_numbers = number_map(chosen["fromPortNumberMap"])
    to_numbers = number_map(chosen["toPortNumberMap"])
    names = {node: qualified(entry["qualifiedName"]) for node, entry in by_node.items()}

    connections = []
    for graph, entries in sorted(chosen["connectionMap"].items()):
        for connection in entries:
            key = json.dumps(connection, sort_keys=True)
            source = _port_endpoint(connection["from"], from_numbers.get(key))
            target = _port_endpoint(connection["to"], to_numbers.get(key))
            if source is None or target is None:
                raise GeneratorError(
                    f"{graph}: connection endpoint is not a component instance"
                )
            connections.append(
                Connection(
                    graph,
                    names[source[0]],
                    source[1],
                    source[2],
                    names[target[0]],
                    target[1],
                    target[2],
                )
            )
    connections.sort(key=lambda c: (c.graph, c.from_inst, c.from_port, c.from_num or 0))
    return instances, connections


def generate_topology_rust(topology_name, instances, connections, model_label):
    """The topology as Rust data: the instance table and the connection table."""
    active = sum(1 for i in instances if i.active)
    out = [
        f"//! The resolved FPP topology `{topology_name}`, as data.",
        "//!",
        "//! GENERATED by tools/fprime-rs/fpp_to_rust.py --topology -- do not edit.",
        f"//! Model: `{model_label}`",
        "//!",
        "//! This is **verification-only** (fprime-rs Phase 11). Nothing here is",
        "//! wired to anything: it exists to show that everything the autocoded",
        "//! C++ topology knows is already derivable from the model, which is the",
        "//! question that decides whether a Rust-native topology is mechanical.",
        "//! `--verify-cpp` checks these facts against the generated C++.",
        "//!",
        f"//! {len(instances)} instances ({active} active), {len(connections)} connections.",
        "#![allow(dead_code)]",
        "",
        "/// One component instance, with the resources the topology assigned it.",
        "#[derive(Debug, Clone, Copy, PartialEq, Eq)]",
        "pub struct Instance {",
        "    /// Dotted instance name, e.g. `CdhCore.cmdDisp`",
        "    pub name: &'static str,",
        "    /// The component type it instantiates",
        "    pub component: &'static str,",
        "    /// First ID of the block reserved for its commands/events/channels",
        "    pub base_id: u32,",
        "    /// Active components get a thread; queued and passive ones do not",
        "    pub active: bool,",
        "    /// Thread priority, where the model set one",
        "    pub priority: Option<u32>,",
        "    /// Thread stack size in bytes, where the model set one",
        "    pub stack_size: Option<u32>,",
        "    /// Message queue depth, for active and queued components",
        "    pub queue_size: Option<u32>,",
        "    /// CPU affinity, where the model set one (-1 means unset)",
        "    pub cpu: Option<i32>,",
        "}",
        "",
        "/// One resolved connection, output port to input port.",
        "#[derive(Debug, Clone, Copy, PartialEq, Eq)]",
        "pub struct Connection {",
        "    /// The connection graph it belongs to, e.g. `Time` or `RateGroups`",
        "    pub graph: &'static str,",
        "    /// Source instance and its output port",
        "    pub from: Endpoint,",
        "    /// Target instance and its input port",
        "    pub to: Endpoint,",
        "}",
        "",
        "/// One end of a connection.",
        "#[derive(Debug, Clone, Copy, PartialEq, Eq)]",
        "pub struct Endpoint {",
        "    /// Dotted instance name",
        "    pub instance: &'static str,",
        "    /// Port name as the component declares it",
        "    pub port: &'static str,",
        "    /// Index into that port's array, where the topology resolved one",
        "    pub number: Option<u32>,",
        "}",
        "",
        f"/// Every component instance in `{topology_name}`.",
        f"pub const INSTANCES: [Instance; {len(instances)}] = [",
    ]
    for i in instances:
        out.append(
            "    Instance { "
            f'name: "{i.name}", component: "{i.component}", base_id: {i.base_id}, '
            f"active: {'true' if i.active else 'false'}, "
            f"priority: {_rust_opt(i.priority)}, stack_size: {_rust_opt(i.stack)}, "
            f"queue_size: {_rust_opt(i.queue)}, cpu: {_rust_opt(i.cpu)} }},"
        )
    out += ["];", "", f"/// Every connection in `{topology_name}`."]
    out.append(f"pub const CONNECTIONS: [Connection; {len(connections)}] = [")
    for c in connections:
        out.append(
            "    Connection { "
            f'graph: "{c.graph}", '
            f'from: Endpoint {{ instance: "{c.from_inst}", port: "{c.from_port}", '
            f"number: {_rust_opt(c.from_num)} }}, "
            f'to: Endpoint {{ instance: "{c.to_inst}", port: "{c.to_port}", '
            f"number: {_rust_opt(c.to_num)} }} }},"
        )
    out += ["];", ""]

    escapes = [(i, name, code) for i in instances for name, code in i.phases]
    out += [
        "/// A `phase Fpp.ToCpp.Phases.X` block: literal C++ the model pastes",
        "/// into the generated topology.",
        "///",
        "/// These are the one part of a topology that is **not** mechanical.",
        "/// Nothing derives them and nothing can translate them; a Rust-native",
        "/// topology would have to have them rewritten by hand. They are listed",
        "/// here so that cost is visible rather than discovered later.",
        "#[derive(Debug, Clone, Copy, PartialEq, Eq)]",
        "pub struct PhaseEscape {",
        "    /// The instance the block is attached to",
        "    pub instance: &'static str,",
        "    /// Which setup or teardown phase it runs in",
        "    pub phase: &'static str,",
        "    /// The literal C++, verbatim",
        "    pub code: &'static str,",
        "}",
        "",
        f"/// The {len(escapes)} hand-written C++ escapes in `{topology_name}`.",
        f"pub const PHASE_ESCAPES: [PhaseEscape; {len(escapes)}] = [",
    ]
    for instance, name, code in escapes:
        out.append(
            "    PhaseEscape { "
            f'instance: "{instance.name}", phase: "{name}", '
            f'code: r#"{code}"# }},'
        )
    out += ["];", ""]
    return "\n".join(out)


def _rust_opt(value):
    return "None" if value is None else f"Some({value})"


def verify_against_cpp(path, instances, connections):
    """Check the resolved facts against the topology `fpp-to-cpp` generated.

    This is the point of the phase. Each assertion is a fact the C++ states
    explicitly, so a mismatch means the model does *not* determine the
    topology on its own -- which would be the finding that matters.
    """
    text = path.read_text()
    active = [i for i in instances if i.active]
    problems = []

    def check(label, expected, actual):
        if expected != actual:
            problems.append(f"{label}: model says {expected}, C++ has {actual}")
        else:
            print(f"  ok  {label}: {expected}")

    check("instances (.init calls)", len(instances), len(re.findall(r"\.init\(", text)))
    check(
        "base IDs (.setIdBase calls)",
        len(instances),
        len(re.findall(r"\.setIdBase\(", text)),
    )
    check(
        "connections (set_*_OutputPort calls)",
        len(connections),
        len(re.findall(r"set_\w+_OutputPort\(", text)),
    )
    # A passive component can still be started by a `phase` block, so the
    # honest expectation is "active components, plus whatever the escapes do".
    escape_starts = sum(
        code.count(".start(")
        for i in instances
        for name, code in i.phases
        if name == "startTasks"
    )
    check(
        "started components (.start calls)",
        len(active) + escape_starts,
        len(re.findall(r"\.start\(", text)),
    )
    if escape_starts:
        print(
            f"  note  {escape_starts} of those come from `phase` blocks, not "
            f"from a component being active"
        )

    # Every instance must actually appear, not merely be counted.
    missing = [i.cpp for i in instances if f"{i.cpp}.init(" not in text]
    if missing:
        problems.append(f"instances absent from the C++: {', '.join(missing[:5])}")
    else:
        print(f"  ok  every instance appears by name in a .init() call")

    missing_start = [i.cpp for i in active if f"{i.cpp}.start(" not in text]
    if missing_start:
        problems.append(
            f"active instances never started: {', '.join(missing_start[:5])}"
        )
    else:
        print(f"  ok  every active instance appears by name in a .start() call")

    # The setup/teardown ordering is a fact a runtime would have to reproduce.
    for name, expected in (
        (
            "setup",
            [
                "initComponents",
                "configComponents",
                "setBaseIds",
                "connectComponents",
                "regCommands",
                "readParameters",
                "loadParameters",
                "startTasks",
            ],
        ),
        (
            "teardown",
            ["stopTasks", "freeThreads", "tearDownComponents", "deinitComponents"],
        ),
    ):
        body = re.search(
            r"void " + name + r"\(const TopologyState& state\) \{(.*?)\n  \}",
            text,
            re.S,
        )
        if body is None:
            problems.append(f"could not find {name}() in the generated C++")
            continue
        found = re.findall(r"(\w+)\(", body.group(1))
        if found != expected:
            problems.append(f"{name}() order: expected {expected}, C++ has {found}")
        else:
            print(f"  ok  {name}() runs {len(expected)} steps in the model's order")

    return problems


def emit_topology(ast, analysis, args):
    """Topology mode: write the tables, and optionally check them against C++."""
    instances, connections = collect_topology(ast, analysis, args.topology)
    code = generate_topology_rust(
        args.topology, instances, connections, args.module_name
    )
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(code)
    if shutil.which("rustfmt") is not None:
        subprocess.run(["rustfmt", "--edition", "2021", str(args.out)], check=True)

    active = sum(1 for i in instances if i.active)
    print(
        f"fpp-to-rust: {args.topology} -> {args.out} "
        f"({len(instances)} instances, {active} active, "
        f"{len(connections)} connections)"
    )

    if args.verify_cpp is not None:
        print(f"verifying against {args.verify_cpp}:")
        problems = verify_against_cpp(args.verify_cpp, instances, connections)
        if problems:
            for problem in problems:
                print(f"  MISMATCH  {problem}", file=sys.stderr)
            sys.exit(
                "fpp-to-rust: the model does not account for the generated "
                "topology; see the mismatches above"
            )
        print("the model accounts for every fact checked in the generated C++")


if __name__ == "__main__":
    main()
