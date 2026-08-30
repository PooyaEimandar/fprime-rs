#!/usr/bin/env python3
"""Generate a Rust-backed C++ implementation file from its C++ original.

Several fprime-rs shims are *near-copies* of an upstream file with a handful
of method bodies rerouted into Rust. Written by hand, each such copy is a
silent-divergence hazard: upstream changes the original, nothing reaches the
Rust build, and the two implementations drift apart with nothing to notice.

So they are generated, and the generator doubles as the drift check:

  <gen>.py           regenerate (and tidy with clang-format, if available)
  <gen>.py --check   verify, exit 1 on drift

The check compares *content*, not formatting: whitespace is collapsed and
spaces adjacent to punctuation are dropped, so reformatting either file can
never put the repository's format check and this drift check in conflict.

Bodies that are replaced would otherwise be a blind spot -- the generator
overwrites those regions, so an upstream edit inside one leaves the output
unchanged. Each replaced body's original text is therefore fingerprinted into
the generated file, which turns that blind spot into a diff.

A caller supplies a `Shim`; `main()` does the rest.
"""

import argparse
import difflib
import hashlib
import pathlib
import re
import shutil
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]


class GeneratorError(Exception):
    """The upstream file no longer has the shape the shim expects."""


class Shim:
    """One generated file: where it comes from, and what to reroute.

    `replacements` is a list of (signature, body) pairs. The signature is
    matched with whitespace collapsed, so a wrapped declaration matches
    regardless of its continuation indent. The body replaces everything
    between the method's braces.

    `edits` is a list of (find, replace, required) applied to the whole text
    after the bodies, for banner and include surgery.
    """

    def __init__(
        self,
        source,
        target,
        banner,
        replacements,
        edits=(),
        preamble=None,
        removals=(),
    ):
        self.source = source
        self.target = target
        self.banner = banner
        self.replacements = list(replacements)
        self.edits = list(edits)
        self.preamble = preamble  # (anchor, text) inserted before `anchor`
        # Signatures whose whole definition is deleted -- for helpers that
        # only existed to serve a body that has moved to Rust. Leaving them
        # would put unreachable code in a flight file.
        self.removals = list(removals)


def find_body(text, open_brace):
    """Return the index just past the brace matching the one at open_brace.

    Skips braces inside comments and character/string literals. The bodies
    being replaced are plain code, but a mis-matched brace here would corrupt
    the generated file silently, so the scan is careful.
    """
    if text[open_brace] != "{":
        raise GeneratorError("expected an opening brace")
    depth = 0
    i = open_brace
    end = len(text)
    while i < end:
        c = text[i]
        if c == "/" and i + 1 < end and text[i + 1] == "/":
            i = text.find("\n", i)
            if i == -1:
                break
            continue
        if c == "/" and i + 1 < end and text[i + 1] == "*":
            i = text.find("*/", i + 2)
            if i == -1:
                raise GeneratorError("unterminated block comment")
            i += 2
            continue
        if c in "\"'":
            quote = c
            i += 1
            while i < end and text[i] != quote:
                i += 2 if text[i] == "\\" else 1
            i += 1
            continue
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    raise GeneratorError("unbalanced braces")


def normalized(text):
    """Content with whitespace collapsed and punctuation spacing removed.

    `--check` compares on this so that reformatting is invisible to it. Spaces
    between two identifiers survive, so `const U8` stays distinct from
    `constU8`; a line break clang-format inserts after `(` does not.
    """
    # clang-format sorts each contiguous run of #include lines; that ordering
    # is formatting. Sort *within* runs only -- hoisting every include out of
    # its position would also erase which ones sit inside a preprocessor
    # conditional, and that is content. (Serializable.cpp has such a block.)
    out = []
    run = []
    for line in text.splitlines():
        if line.lstrip().startswith("#include"):
            run.append(line.strip())
            continue
        if run:
            out.extend(sorted(run))
            run = []
        out.append(line)
    out.extend(sorted(run))
    text = "\n".join(out)

    collapsed = re.sub(r"\s+", " ", text)
    return re.sub(r" ?([^\w\s]) ?", r"\1", collapsed).strip()


def short_label(signature):
    """`serializeFrom(const U8*, FwSizeType, Serialization::t, Endianness)`.

    Parameter names are dropped: the label identifies the overload, and the
    names are noise that would push the fingerprint table past the margin.
    """
    sig = " ".join(signature.split())
    # Strip the return type, which is whatever precedes the qualified name --
    # anchored, so a parameter of the same type is not eaten too.
    sig = re.sub(r"^[\w:<>,\s*&]*?(?=\b\w+::)", "", sig)
    sig = re.sub(r"\b\w+::(?=\w+\()", "", sig, count=1)
    name, _, rest = sig.partition("(")
    types = [re.sub(r"\s+\w+$", "", p.strip()) for p in rest.rstrip(")").split(",")]
    return f"{name}({', '.join(types)})"


def fingerprint_block(fingerprints):
    """The provenance table that makes replaced bodies visible to `--check`."""
    width = max(len(name) for name, _ in fingerprints)
    listing = "".join(
        f"//   {name.ljust(width)}  {digest}\n" for name, digest in fingerprints
    )
    return (
        "// clang-format off\n"
        "// Fingerprints of the upstream bodies this generator replaced or\n"
        "// removed. The generated file does not contain them, so nothing else\n"
        "// would notice if upstream changed one -- a mismatch here means the\n"
        "// C++ that the Rust implementation mirrors has moved and needs\n"
        "// re-reviewing. The `removed:` entries are the helper functions the\n"
        "// Rust reimplements, and are the reason this table exists at all.\n"
        "//\n" + listing + "// clang-format on\n\n"
    )


def generate(shim):
    """Produce the generated file's text from the upstream original."""
    text = shim.source.read_text()
    fingerprints = []

    for signature, body in shim.replacements:
        pattern = re.compile(r"\s+".join(re.escape(w) for w in signature.split()))
        found = pattern.findall(text)
        if len(found) != 1:
            raise GeneratorError(
                f"{shim.source.relative_to(REPO)}: expected exactly one "
                f"'{signature}', found {len(found)}. The upstream file changed "
                f"shape; update the generator."
            )
        match = pattern.search(text)
        open_brace = text.index("{", match.end())
        close = find_body(text, open_brace)
        original = normalized(text[open_brace + 1 : close - 1])
        fingerprints.append(
            (short_label(signature), hashlib.sha256(original.encode()).hexdigest()[:12])
        )
        text = text[: open_brace + 1] + "\n" + body + "}" + text[close:]

    for signature in shim.removals:
        pattern = re.compile(r"\s+".join(re.escape(w) for w in signature.split()))
        if len(pattern.findall(text)) != 1:
            raise GeneratorError(
                f"{shim.source.relative_to(REPO)}: expected exactly one "
                f"'{signature}' to remove. The upstream file changed shape; "
                f"update the generator."
            )
        match = pattern.search(text)
        open_brace = text.index("{", match.end())
        close = find_body(text, open_brace)
        # A removed body is a *larger* blind spot than a replaced one: the
        # generated file keeps no trace of it at all, so its content cannot
        # affect the output no matter what it says. These are exactly the
        # helpers the Rust mirrors, so fingerprint them too.
        original = normalized(text[open_brace + 1 : close - 1])
        fingerprints.append(
            (
                "removed: " + short_label(signature),
                hashlib.sha256(original.encode()).hexdigest()[:12],
            )
        )
        # Take any comment block immediately above the definition with it.
        start = match.start()
        while True:
            line_start = text.rfind("\n", 0, start - 1)
            preceding = text[line_start + 1 : start]
            if preceding.lstrip().startswith("//"):
                start = line_start + 1
                continue
            break
        text = text[:start] + text[close:].lstrip("\n")

    # Replace the original banner (everything up to the first #include).
    text = shim.banner + text[text.index("#include") :]

    for find, replace, required in shim.edits:
        if find not in text:
            if required:
                raise GeneratorError(f"could not locate {find!r} to edit")
            continue
        text = text.replace(find, replace, 1)

    if shim.preamble:
        anchor, extra = shim.preamble
        if text.count(anchor) != 1:
            raise GeneratorError(f"expected exactly one {anchor!r}")
        text = text.replace(anchor, fingerprint_block(fingerprints) + extra + anchor, 1)
    else:
        raise GeneratorError("a shim must define where its preamble goes")

    return text


def clang_format(path):
    """Run clang-format in place if it can be found; report whether it ran."""
    candidates = [
        REPO / "fprime-venv" / "bin" / "clang-format",
        REPO / ".venv" / "bin" / "clang-format",
    ]
    exe = next((str(c) for c in candidates if c.exists()), None) or shutil.which(
        "clang-format"
    )
    if exe is None:
        return False
    subprocess.run(
        [exe, f"--style=file:{REPO / '.clang-format'}", "-i", str(path)], check=True
    )
    return True


def is_up_to_date(shim, generated=None):
    """Whether the checked-in file still matches what the shim produces.

    Factored out of `main` so the drift check itself can be tested (see
    tools/fprime-rs/test_shimgen.py) rather than only ever exercised on a
    pristine tree, where it can only ever pass.
    """
    if generated is None:
        generated = generate(shim)
    if not shim.target.exists():
        return False
    return normalized(shim.target.read_text()) == normalized(generated)


def main(shim, doc=None):
    """CLI shared by every shim generator: regenerate, or `--check`."""
    parser = argparse.ArgumentParser(description=doc or __doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="verify the checked-in file matches; exit 1 on drift",
    )
    args = parser.parse_args()

    try:
        generated = generate(shim)
    except GeneratorError as error:
        print(error, file=sys.stderr)
        return 1

    rel = shim.target.relative_to(REPO)
    if not args.check:
        shim.target.write_text(generated)
        formatted = clang_format(shim.target)
        note = (
            "" if formatted else " (clang-format not found; run it before committing)"
        )
        print(f"wrote {rel}{note}")
        return 0

    if not shim.target.exists():
        print(f"{rel} is missing; run without --check", file=sys.stderr)
        return 1
    current = shim.target.read_text()
    if is_up_to_date(shim, generated):
        print(f"{rel} is up to date")
        return 0

    sys.stdout.writelines(
        difflib.unified_diff(
            current.splitlines(keepends=True),
            generated.splitlines(keepends=True),
            fromfile=f"{rel} (checked in)",
            tofile=f"{rel} (regenerated)",
        )
    )
    print(
        f"\n{rel} is stale with respect to {shim.source.relative_to(REPO)}.\n"
        f"Regenerate it, and re-review the Rust side if a fingerprint changed.",
        file=sys.stderr,
    )
    return 1
