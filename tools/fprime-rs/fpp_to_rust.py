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


def literal_int(node, what):
    """Extract an integer literal, refusing anything else by name."""
    data, _ = unwrap(node)
    if not isinstance(data, dict) or "ExprLiteralInt" not in data:
        kind = next(iter(data)) if isinstance(data, dict) else type(data).__name__
        raise GeneratorError(
            f"{what}: only integer literals are supported here, got {kind}"
        )
    return int(data["ExprLiteralInt"]["value"], 0)


def iter_definitions(ast):
    """Collect (module_path, kind, data, node_id) for every type definition."""
    collected = []

    def walk(members, path):
        for entry in members:
            node = entry[1]
            if not isinstance(node, dict):
                continue
            for kind, wrapper in node.items():
                if kind == "DefModule":
                    data, _ = unwrap(wrapper["node"])
                    walk(data["members"], path + [data["name"]])
                elif kind in ("DefStruct", "DefEnum", "DefArray"):
                    data, node_id = unwrap(wrapper["node"])
                    collected.append((tuple(path), kind, data, node_id))

    for unit in ast:
        walk(unit["members"], [])
    return collected


# ---------------------------------------------------------------------------
# Type resolution
# ---------------------------------------------------------------------------


def resolve_type(type_map, node_id):
    entry = type_map.get(str(node_id))
    if entry is None:
        raise GeneratorError(f"no typeMap entry for AST node {node_id}")
    return classify(entry)


def classify(entry):
    """Classify a typeMap entry into ("prim"|"bool"|"string"|"named", ...).

    The JSON vocabulary nests differently per kind: integers appear as
    `Int.PrimitiveInt.kind`, while floats and bools sit behind a `Primitive`
    wrapper (`Primitive.Float.kind`, `Primitive.Boolean`).
    """
    kind = next(iter(entry))
    body = entry[kind]

    if kind == "Primitive":
        return classify(body)
    if kind == "AliasType":
        return classify(body["aliasType"])
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
        return ("string", None, None)
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


def rust_name(path, name):
    """Flatten an FPP qualified name into a Rust type name."""
    return "".join(list(path) + [name])


def build_model(ast, analysis, wanted_modules=None):
    type_map = analysis["typeMap"]
    model = Model()
    model.value_map = analysis.get("valueMap", {})
    defs = iter_definitions(ast)

    # Register every definition first so named references resolve, and reject
    # flattened-name collisions: module separators are erased, so `AB.C` and
    # `A.BC` would otherwise both become `ABC` and silently share a type.
    flattened = {}
    for path, kind, data, node_id in defs:
        model.by_node_id[node_id] = (path, data["name"])
        flat = rust_name(path, data["name"])
        qualified = ".".join(list(path) + [data["name"]])
        if flat in flattened and flattened[flat] != qualified:
            raise GeneratorError(
                f"flattened name collision: '{flattened[flat]}' and '{qualified}' "
                f"both become Rust type '{flat}'"
            )
        flattened[flat] = qualified
        if kind == "DefEnum":
            model.enum_consts[node_id] = collect_enum_constants(data, model.value_map)

    for path, kind, data, node_id in defs:
        if wanted_modules is not None and path and path[0] not in wanted_modules:
            continue
        where = ".".join(list(path) + [data["name"]])
        if kind == "DefStruct":
            model.structs.append(build_struct(model, type_map, path, data, where))
        elif kind == "DefEnum":
            model.enums.append(build_enum(model, type_map, path, data, node_id, where))
        elif kind == "DefArray":
            model.arrays.append(build_array(model, type_map, path, data, where))
    return model


def collect_enum_constants(data, value_map):
    constants = {}
    implicit = 0
    for entry in data["constants"]:
        const, _ = unwrap(entry[1])
        value_node = opt(const["value"])
        if value_node is None:
            value = implicit
        else:
            _, vid = unwrap(value_node)
            resolved = value_map.get(str(vid))
            if resolved is not None and next(iter(resolved)) in (
                "Integer",
                "PrimitiveInt",
            ):
                value = int(resolved[next(iter(resolved))]["value"])
            else:
                value = literal_int(value_node, f"enum constant {const['name']}")
        implicit = value + 1
        constants[const["name"]] = value
    return constants


def build_struct(model, type_map, path, data, where):
    fields = []
    for entry in data["members"]:
        member, _ = unwrap(entry[1])
        _, type_id = unwrap(member["typeName"])
        try:
            desc = resolve_type(type_map, type_id)
        except GeneratorError as exc:
            raise GeneratorError(f"{where}.{member['name']}: {exc}") from exc
        if desc[0] == "string":
            raise GeneratorError(
                f"{where}.{member['name']}: string members are not supported yet "
                "(fixed-capacity FPP strings with length prefixes)"
            )
        size_node = opt(member["size"])
        count = (
            literal_int(size_node, f"{where}.{member['name']} member array size")
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
    count = literal_int(data["size"], f"{where} array size")
    _, elt_id = unwrap(data["eltType"])
    elt = resolve_type(type_map, elt_id)
    if elt[0] == "string":
        raise GeneratorError(f"{where}: arrays of strings are not supported yet")

    default_elems = None
    default_node = opt(data.get("default"))
    if default_node is not None:
        node_data, _ = unwrap(default_node)
        if isinstance(node_data, dict) and "ExprArray" in node_data:
            elts = node_data["ExprArray"]["elts"]
            if len(elts) != count:
                raise GeneratorError(
                    f"{where}: default has {len(elts)} elements, array holds {count}"
                )
            default_elems = elts
        else:
            # FPP also allows a scalar default applied to every element
            default_elems = [default_node] * count
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
    if desc[0] == "named":
        path, name = model.by_node_id[desc[1]]
        ty = rust_name(path, name)
        if model.enum_consts.get(desc[1]) is None:
            raise GeneratorError(
                f"{where}: defaults for nested struct/array members are not supported yet"
            )
        if kind == "EnumConstant":
            return f"{ty}({int(body['value'][1])})"
        if kind in ("Integer", "PrimitiveInt"):
            return f"{ty}({int(body['value'])})"
        raise GeneratorError(f"{where}: unsupported enum default value kind '{kind}'")
    raise GeneratorError(f"{where}: cannot fold default for {desc[0]}")


def zero_value(model, desc, count):
    """The Rust value a member takes when the model gives it no default."""
    if desc[0] == "prim":
        base = "0.0" if desc[1] in ("f32", "f64") else "0"
    elif desc[0] == "bool":
        base = "false"
    elif desc[0] == "named":
        path, name = model.by_node_id[desc[1]]
        base = f"{rust_name(path, name)}::default()"
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
    else:
        raise GeneratorError(f"cannot size type kind: {kind}")
    return f"{base} * {count}" if count else base


def emit_value_ser(desc, expr, indent):
    pad = " " * indent
    if desc[0] == "prim":
        return f"{pad}buf.ser_{desc[1]}({expr}, mode)?;"
    if desc[0] == "bool":
        return f"{pad}buf.ser_bool({expr}, mode)?;"
    if desc[0] == "named":
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
                emit_value_ser(desc, "*item" if desc[0] != "named" else "item", 12)
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
            fold_default(model, elt, node, f"{where}[{i}]")
            for i, node in enumerate(default_elems)
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
    out.append(emit_value_ser(elt, "*item" if elt[0] != "named" else "item", 12))
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
        "",
    ]
    for path, name, repr_rust, constants, default_value in model.enums:
        emit_enum(model, path, name, repr_rust, constants, default_value, out)
    for path, name, elt, count, default_elems in model.arrays:
        emit_array(model, path, name, elt, count, default_elems, out)
    for path, name, fields, defaults in model.structs:
        emit_struct(model, path, name, fields, defaults, out)
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--module-name", required=True, help="label recorded in the generated header"
    )
    ap.add_argument("--out", required=True, type=Path)
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


if __name__ == "__main__":
    main()
