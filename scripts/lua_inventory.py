#!/usr/bin/env python3
"""The repository's shipped Lua, enumerated statically. One list, two consumers.

WHY THIS MODULE EXISTS
----------------------
`scripts/coverage/coverage_report.py` needs to know what the Lua coverage
DENOMINATOR is, and `scripts/check_lua_statement_lines.py` needs to know what
to lint. Those two answers used to be computed separately, and they disagreed:
the lint scanned `docs/modding/examples` and the coverage denominator did not.
A file that one tool calls shipped game logic and the other does not see at all
is a hole in whichever tool is smaller, so both now import this.

THE PROPERTY THIS FILE EXISTS TO GUARANTEE
------------------------------------------
The inventory is a **pure function of the repository contents**. It is computed
with zero tests run and is identical no matter which tests run — and identical
no matter which binaries happen to be built or on PATH, which is why nothing
in this module shells out to `luac` or to any built tool. That is not a
tidiness preference — it is the whole gate. The previous denominator was partly
runtime-derived: a script entered it only if some test had mounted it, which
meant

  * a new example pack nobody exercised left the report byte-identical, because
    an unmeasured file was an ABSENT file rather than a file at 0%;
  * deleting a test DELETED its scripts from the denominator, so dropping the
    109 lines of `court.lua` boss logic pushed the combined number UP;
  * the same bytes mounted under N chunk names counted N times in BOTH halves,
    so re-mounting an already-covered pack manufactured headroom out of nothing.

Runtime dumps now supply only the NUMERATOR, attributed back to these entries
by content hash.

WHAT COUNTS AS REPOSITORY CONTENT
---------------------------------
`git ls-files --cached --others --exclude-standard`: everything committed, plus
everything untracked that is not ignored. So a pack script you have written but
not yet `git add`-ed is measured (it would otherwise be a way to add unmeasured
game logic), while `build/`, `_deps/`, `node_modules/` and the rest of the
ignored tree is not.

There is deliberately NO non-git fallback. The old directory-walk answered the
same question differently: the git listing follows a symlink to a file, the
walk skipped symlinks, so one tree produced two denominators depending on how
it happened to be enumerated. A denominator that depends on anything besides
repository contents is the exact bug this module exists to prevent, so when
git cannot answer, `repository_files()` refuses loudly instead of guessing.
(A coverage gate outside a git checkout is not a real deployment — an exported
tarball has no tracked/untracked/ignored distinction left to enumerate.)

Symlink semantics are therefore git's, and only git's: a listed symlink to a
file is read through (the repository offers Lua at that path); a symlink to a
directory is the link object itself, never descended into.

HOW GIT ITSELF IS FOUND
-----------------------
By explicit handoff when the caller has one, and only otherwise from PATH.
"git is installed" and "this process can spawn the bare name `git`" are
different facts on the Windows CI runner: the MSYS2 shell that configures
the build sees git through POSIX-style PATH entries, while CMake's
FindPython3 resolves a NATIVE (registry) interpreter — and when that
interpreter spawns `git` at BUILD time, Windows process creation cannot use
those entries, so a toolchain that had just finished a git-driven
FetchContent clone died with WinError 2 in the middle of the build. The
CMake check targets therefore pass the git that find_package(Git) resolved
at configure time through the OG_GIT_EXECUTABLE environment variable
(--git-exe on the CLIs writes the same variable), and `git_executable()`
treats a broken explicit value as a hard error rather than falling back to
PATH: a typo'd override must not make the enumeration quietly
environment-dependent again. With no override and no git on PATH, the
refusal is this module's own SystemExit naming both levers — never an
ENOENT/WinError traceback from inside subprocess.

WHERE LUA HIDES
---------------
Three places, because "a .lua file in the tree" is not the whole story:

  1. `*.lua` files under the SHIPPED ROOTS — packs/ and docs/ — at any
     depth, tracked or untracked. Those are the trees the product ships: the
     pack scripts the engine mounts, and the modding examples the docs
     distribute. Within a shipped root there is deliberately no narrower
     pattern (no `packs/*/scripts` glob): any `.lua` there is game logic,
     and enumerating a subset just moves the hole somewhere a glob does not
     look.
  2. `*.lua` entries inside `*.glad` campaign archives, wherever the archive
     sits — `.glad` itself matched exactly and case-sensitively, like every
     other spelling the engine tests. The engine mounts campaign packs
     straight out of the archive, and one whole boss script ships only that
     way.
  3. Lua inside C++ raw string literals delimited `R"LUA( ... )LUA"`, in a
     file that `scripts/coverage/embedded_lua.txt` declares `shipped`. The
     showcase pack is written into a generated `.glad` from one of these and
     never exists as a file at all.

In the first two, `.lua` is matched exactly and CASE-SENSITIVELY — the
engine's own membership test (src/resources/packs.cpp compares the literal
bytes ".lua") — and a case-variant (`PROBE.LUA`) under a shipped root, or as
an archive member, is a collected PROBLEM: the engine can never load it, so
admitting it (as a lowercasing enumeration once did) made an unfixable 0%
denominator entry, and skipping it silently would hide the typo.

PRECOMPILED BYTECODE IS A PROBLEM, NOT A SOURCE
-----------------------------------------------
A blob in ANY of the three places whose bytes begin with Lua's binary-chunk
signature byte (LUA_SIGNATURE, "\\x1bLua" — matched on the FIRST byte, which
is the classification the loader itself performs), with or without a UTF-8
BOM pasted in front, is a collected problem and never a denominator entry:
precompiled Lua is not shipped source; commit the .lua text. Two reasons,
either sufficient. The engine refuses binary chunks outright — ScriptHost
compiles text-only, because a crafted binary chunk is the canonical
arbitrary-code vector through the sandbox (lundump does no consistency
checking) — so the blob can never load and would sit as an unfixable red,
like the case-variants above. And a STRIPPED chunk still carries its
function spans while its line info is gone, so ADMITTING it as an entry
would erase the file's line denominator while leaving its function bar
coverable: the same logic scored against fewer lines, a gate FAIL flipping
to PASS with nothing tested (P8-A). The oracle (og_lua_lines /
source_facts) rejects the same prefix with the same named error, so even a
blob that slipped this enumeration cannot mint a truncated grid.

The same engine-exact rule covers every OTHER case-sensitive spelling on the
way to a script, each a collected problem rather than a silent admit or a
silent skip:

  * the `.glad` suffix itself (`PROBE.GLAD`): the engine's campaign filter
    and builtin restore (src/resources/io/platform_io_common.cpp) match
    ".glad" byte-for-byte and every mount path is built as `<id>.glad`, so
    a case-variant archive's members used to enter the denominator as
    entries nothing could ever load — an unfixable red;
  * a top-level directory that case-folds to a shipped root without
    equaling it (`Packs/`): correctly OUT of the denominator — PhysFS on
    the gate platform is case-sensitive, the engine cannot mount it — but a
    case-insensitive dev filesystem (macOS/Windows) WOULD load it while
    this gate never measures it, so the spelling cannot stay silent;
  * a case-variant `scripts/` segment inside a pack (`packs/x/Scripts/`):
    still a denominator entry (any `.lua` under a shipped root is — see
    below), but the engine mounts only the literal lowercase
    `packs/<pack>/scripts/`, so on the gate platform that entry is an
    uncoverable red a case-insensitive dev filesystem hides.

THE FIRST ONE HAS A DELIBERATE BOUNDARY
---------------------------------------
`tests/**`, `scripts/**` and every location outside the shipped roots never
enter the denominator, and are never even read. The alternative was a
false-failure generator: a four-line Lua helper fixture dropped in
tests/data/ became "shipped game logic", entered the denominator at 0%,
failed the 100% function bar, and — because this module also backs the
og_gameplay build lint — broke every build on the machine. Symmetric with
the raw-string policy below: what does not ship cannot carry shipped logic.

The exemption cannot hide game logic, for the same reason the raw-string
exemption cannot. A `.lua` outside the shipped roots that nothing loads
ships nothing; the moment a test MOUNTS one at runtime, the report's
content-digest poison pills own it — an engine-loaded pack chunk whose
bytes are not shipped content and not a listed runtime-only fixture is a
hard gate error (see scripts/coverage/coverage_report.py, attribute_dumps).

ENUMERATION FAILURES ARE PROBLEMS, NOT SKIPS
--------------------------------------------
A read failure on a shipped path fails closed. An unreadable `.lua` under a
shipped root, an unopenable or corrupt `.glad`, an unreadable product C++
file — each used to be silently `continue`d past, which SHRANK the
denominator: the one lever this module exists to keep out of runtime's
hands was available to a chmod. Every such failure is now a problem entry,
so the lint lists it and `inventory()` refuses on it.

THE THIRD ONE FAILS CLOSED, IN BOTH DIRECTIONS
----------------------------------------------
`R"LUA(` used to be a pure convention, opt-in from both ends, and it leaked
both ways: pack Lua under any OTHER delimiter (`R"MODLUA( ... )MODLUA"`) was
invisible — no denominator, no error — and a test fixture that innocently
reached for `R"LUA(` entered the denominator as shipped game logic at 0%, a
false-failure generator. So the declaration is explicit and two-sided now.

The scan covers every raw string literal, ANY delimiter, in the PRODUCT
directories — src/, tools/, include/ — across every suffix a C++ compiler
might be handed. In a product file:

  * `R"LUA(` outside a file declared `shipped`            -> hard failure;
  * a literal under any other delimiter that could compile as Lua
    (`could_be_lua`) AND references the pack API (`og.` / `register_hooks`),
    in a file with no declaration                          -> hard failure;
  * the same, in a file declared `shipped`                 -> hard failure
    (shipped pack Lua must use the `LUA` delimiter, where it is collected);
  * a raw-string opener INSIDE a matched literal's body    -> hard failure
    (an opener quoted in a comment makes the match swallow the real literal
    below it — the one way to hide a literal from this scan, so ambiguity
    is itself an error rather than a guess);
  * a stale declaration — file gone, or its literals gone  -> hard failure.

`tests/` (and every other non-product location) is EXEMPT from must-declare,
and its raw strings never enter the shipped denominator — silently or
otherwise; a declaration naming a non-product path is itself an error, and
so is any `fixture` declaration at all. `fixture` used to silence the
sniffer for one product file per line — the only lever that could hide live
product Lua behind a single reviewable line — so the disposition is
forbidden: product literals are `shipped`, full stop, and a test-only
literal belongs in tests/. The boundary is safe by construction: a raw
string that never ships cannot carry shipped logic, and the moment a test
MOUNTS Lua at runtime the report's own poison pills take over — recorded
pack bytes that are not repository content, or a pack chunk with no
declared source, hard-fail the gate
(see scripts/coverage/coverage_report.py, attribute_dumps).

`could_be_lua` is a deliberate one-sided approximation of `luac -p`, built on
the same lexer the statement lint uses: real Lua always lexes and always
balances its blocks and brackets, so a REJECTED body is proof the text is not
Lua, while over-acceptance merely widens must-declare — the fail-closed
direction. It shells out to nothing, so the answer cannot depend on what is
built or installed. Everything that actually enters the denominator is then
compiled by the real Lua (og_lua_lines) inside the coverage gate, which
hard-fails on a source Lua will not compile.

Problems are never advisory. `scan()` returns them; `inventory()` REFUSES —
raises `SystemExit` — when any exist, so a consumer cannot take the sources
and drop the problems on the floor. (Both consumers used to do exactly that:
the sniffer ran, filled `problems`, and nothing read it.)

IDENTITY IS THE CONTENT HASH
----------------------------
Entries are deduplicated by sha256 of the bytes. Identical bytes are ONE entry
however many paths, archives or mounts expose them — that is what stops a copy
of a covered pack from buying free coverage, and it is why `emberwisp.lua` is
counted once rather than once per place it appears.

The price is stated plainly in `scripts/coverage/README.md`: `cp big_orc.lua
big_orc_elite.lua` adds no entry, so a second family registered against
duplicated bytes ships with its own dispatch unexercised and the gate says
nothing. That is the deliberate trade for making a copied pack worth zero.

The bytes hashed are the bytes the LINUX gate platform checks out — LF,
since the repository writes no eol-rewriting .gitattributes and the
coverage gate runs on Linux CI. A Windows checkout under core.autocrlf=true
materializes CRLF `.lua` files, so its digests — and its dedup grouping
against `.glad` archive members, whose stored bytes are never rewritten —
legitimately diverge from the gate's; nothing on Windows consumes them.
The one duty this module performs there is the per-build statement lint,
and lint verdicts are line-ending-independent: the lexer treats \r as
whitespace, and C++ files are read in text mode, which normalizes newlines
before an embedded literal is extracted. Digest parity across platforms is
neither needed nor claimed, and the CRLF difference is deliberately NOT
normalized away: the coverage report attributes runtime hits by hashing
the exact bytes the engine mounted, so the inventory must hash exact bytes
too — and the platform whose bytes could disagree is exactly the platform
where no digest is ever used.

TRACKED vs UNTRACKED
--------------------
`include_untracked=True` (the default, and what the coverage denominator uses)
counts untracked-but-not-ignored files, so uncommitted game logic cannot dodge
the gate. `include_untracked=False` is the per-build lint's mode
(`check_lua_statement_lines.py --tracked-only`, a build dependency of
og_gameplay): a scratch or junk untracked file must not break every ninja
build. The junk still cannot hide — the coverage-gate path always runs the
FULL inventory (the `check_lua_statement_lines_full` target gating
`coverage_report`, and `inventory()` inside coverage_report.py itself) and
fails loudly there, naming the repository-side path.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import pathlib
import re
import shutil
import subprocess
import zipfile
import zlib
from dataclasses import dataclass
from typing import Dict, Iterable, List, Optional, Sequence, Tuple

REPO = pathlib.Path(__file__).resolve().parents[1]

# Pack Lua embedded in C++. `R"LUA(` declares "these bytes are shipped pack
# Lua" — but only in a file EMBEDDED_LUA_FILE marks `shipped`; see the module
# docstring.
LUA_LITERAL = re.compile(r'R"LUA\((.*?)\)LUA"', re.S)

# Every raw string literal, whatever its delimiter, so an undeclared one
# carrying Lua can be caught instead of ignored. C++ allows up to 16
# delimiter characters from the basic source set minus parens/backslash/space.
ANY_RAW_LITERAL = re.compile(r'R"([^()\\\s]{0,16})\((.*?)\)\1"', re.S)

# Deliberately generous. The narrow set ({.cpp,.cc,.cxx,.h,.hh,.hpp}) silently
# excused `.ipp` and `.inc` — and this tree ships fifteen `.inc` translation
# units — so anything a C++ compiler might be handed is scanned.
CXX_SUFFIXES = {
    ".c", ".c++", ".cc", ".cp", ".cpp", ".cppm", ".cxx", ".h", ".h++", ".hh",
    ".hpp", ".hxx", ".inc", ".inl", ".ipp", ".ixx", ".m", ".mm", ".tcc",
}

# Where shipped C++ lives, and therefore the only place a C++ literal can
# carry shipped Lua. tests/ (and scripts/, docs/, ...) raw strings never enter
# the denominator and are exempt from must-declare: they do not ship, and any
# Lua a test mounts at RUNTIME is owned by the report's content-hash poison
# pills instead. See the module docstring.
PRODUCT_DIRS = ("include", "src", "tools")

# Where shipped .lua FILES live: the pack tree the engine mounts, and the
# modding examples the docs distribute. A .lua anywhere else — tests/,
# scripts/, a scratch file at the repo root — does not ship and never enters
# the denominator; if a test mounts one at runtime, the report's content-hash
# poison pills own it (bytes the engine loaded that are not shipped content
# hard-fail the gate). Symmetric with PRODUCT_DIRS for raw strings, and for
# the same reason: without the boundary, a four-line helper fixture in
# tests/data/ was "shipped game logic" at 0% and broke every build through
# the og_gameplay lint. See the module docstring.
SHIPPED_LUA_ROOTS = ("docs", "packs")

# "Looks like pack logic": the API root every pack script talks through, or
# the registration entry point. A Lua blob that touches neither cannot
# register a hook and cannot call the engine.
PACK_API_REFERENCE = re.compile(r"\bog\s*\.|\bregister_hooks\b")

# A raw-string OPENER. Finding one INSIDE a matched literal's body means the
# regex scan is ambiguous for this file: the classic shape is `R"X(` quoted in
# a comment above a real `R"X( ... )X"` literal, which makes the match start
# at the comment and swallow the real literal — the one way to hide an
# embedded literal from the sniffer. So ambiguity is itself a hard failure.
RAW_OPENER = re.compile(r'R"[^()\\\s]{0,16}\(')

# Lua's binary-chunk classification, byte-for-byte: the loader decides
# text-vs-precompiled on the FIRST byte alone (LUA_SIGNATURE[0] == 0x1b;
# lundump then verifies the full "\x1bLua" signature). The inventory matches
# the classification, not the full signature, for the same engine-exact
# reason the suffix rules match packs.cpp: whatever the engine would refuse
# as "a binary chunk" is precompiled here, valid signature or corrupt.
LUA_SIGNATURE_BYTE = b"\x1b"
UTF8_BOM = b"\xef\xbb\xbf"

# Which C++ files may contain Lua. One live disposition:
#   shipped  the file's R"LUA( ... )LUA" literals ARE pack Lua: measured by
#            the coverage gate and linted by check_lua_statement_lines.py.
# The historical `fixture` disposition is still parsed — so a line using it
# can be REJECTED loudly rather than ignored — but it is forbidden: product
# literals are `shipped`, full stop; a test-only literal belongs in tests/.
EMBEDDED_LUA_FILE = (
    pathlib.Path(__file__).resolve().parent / "coverage" / "embedded_lua.txt"
)

# Canonical-label preference, ordered by how directly a human can go and EDIT
# the bytes: a real .lua file, then a C++ literal (a text location with a line
# number), then an archive member — which is a binary blob generated from one
# of the other two, so naming it in a diagnostic sends the reader nowhere.
KIND_FILE, KIND_LITERAL, KIND_ARCHIVE = 0, 1, 2


# ---------------------------------------------------------------------------
# Lua lexing — shared with scripts/check_lua_statement_lines.py
# ---------------------------------------------------------------------------

KEYWORDS = {
    "and", "break", "do", "else", "elseif", "end", "false", "for", "function",
    "goto", "if", "in", "local", "nil", "not", "or", "repeat", "return",
    "then", "true", "until", "while",
}


@dataclass
class Token:
    kind: str  # name | number | string | keyword | op
    text: str
    line: int


class LuaLexError(Exception):
    pass


def tokenize(source: str) -> List[Token]:
    """Enough of the Lua 5.4 lexer to know what is code and what is not."""
    tokens: List[Token] = []
    i, line, n = 0, 1, len(source)

    def long_bracket(start: int) -> Optional[Tuple[int, int]]:
        """Match `[==[` at `start`; return (level, index after the opener)."""
        if source[start] != "[":
            return None
        j = start + 1
        level = 0
        while j < n and source[j] == "=":
            level += 1
            j += 1
        if j < n and source[j] == "[":
            return level, j + 1
        return None

    def skip_long(start: int, level: int) -> Tuple[int, int]:
        close = "]" + "=" * level + "]"
        end = source.find(close, start)
        if end < 0:
            raise LuaLexError(f"unterminated long bracket opened at line {line}")
        return end + len(close), source.count("\n", start, end)

    while i < n:
        c = source[i]
        if c == "\n":
            line += 1
            i += 1
            continue
        if c in " \t\r":
            i += 1
            continue
        if source.startswith("--", i):
            i += 2
            opened = long_bracket(i) if i < n and source[i] == "[" else None
            if opened is not None:
                level, body = opened
                i, newlines = skip_long(body, level)
                line += newlines
            else:
                end = source.find("\n", i)
                i = n if end < 0 else end
            continue
        if c in "'\"":
            quote, j = c, i + 1
            while j < n:
                if source[j] == "\\":
                    if j + 1 < n and source[j + 1] == "\n":
                        line += 1
                    j += 2
                    continue
                if source[j] == quote:
                    break
                if source[j] == "\n":
                    raise LuaLexError(f"unterminated string at line {line}")
                j += 1
            if j >= n:
                raise LuaLexError(f"unterminated string at line {line}")
            tokens.append(Token("string", source[i : j + 1], line))
            i = j + 1
            continue
        opened = long_bracket(i)
        if opened is not None:
            level, body = opened
            start_line = line
            i, newlines = skip_long(body, level)
            line += newlines
            tokens.append(Token("string", "[[...]]", start_line))
            continue
        if c.isdigit() or (c == "." and i + 1 < n and source[i + 1].isdigit()):
            j = i
            while j < n and (
                source[j].isalnum() or source[j] == "." or
                (source[j] in "+-" and source[j - 1] in "eEpP")
            ):
                j += 1
            tokens.append(Token("number", source[i:j], line))
            i = j
            continue
        if c.isalpha() or c == "_":
            j = i
            while j < n and (source[j].isalnum() or source[j] == "_"):
                j += 1
            word = source[i:j]
            tokens.append(
                Token("keyword" if word in KEYWORDS else "name", word, line)
            )
            i = j
            continue
        for op in ("...", "..", "::", "==", "~=", "<=", ">=", "//", "<<", ">>"):
            if source.startswith(op, i):
                tokens.append(Token("op", op, line))
                i += len(op)
                break
        else:
            tokens.append(Token("op", c, line))
            i += 1
    return tokens


def could_be_lua(body: str) -> bool:
    """Could `luac -p` conceivably accept this text? One-sided, deliberately.

    Real Lua always lexes (strings and long brackets terminate) and always
    balances: every `end` closes exactly one open `function`/`if`/`do`, every
    `until` closes a `repeat`, and (), [], {} nest. So a body this function
    REJECTS is proof the text is not Lua — a C++ snippet trips the apostrophe
    in a comment or an `if` with no `end`; prose trips its own punctuation.
    The reverse is not a proof, and does not need to be: over-acceptance only
    widens must-declare, which is the fail-closed direction, and everything
    that actually enters the denominator is afterwards compiled by the REAL
    Lua (og_lua_lines) inside the coverage gate.

    Why not shell out to `luac -p` here: the inventory must be a pure function
    of repository contents — the same answer whether or not any tool is built
    or installed. This module backs a lint that runs as a BUILD dependency,
    before the vendored Lua exists as a binary.
    """
    if not body.strip():
        return False
    try:
        tokens = tokenize(body)
    except LuaLexError:
        return False
    blocks: List[str] = []
    brackets = 0
    for token in tokens:
        if token.kind == "keyword":
            if token.text in ("function", "if", "do", "repeat"):
                blocks.append(token.text)
            elif token.text == "end":
                if not blocks or blocks[-1] == "repeat":
                    return False
                blocks.pop()
            elif token.text == "until":
                if not blocks or blocks[-1] != "repeat":
                    return False
                blocks.pop()
        elif token.kind == "op" and len(token.text) == 1:
            if token.text in "([{":
                brackets += 1
            elif token.text in ")]}":
                brackets -= 1
                if brackets < 0:
                    return False
    return not blocks and brackets == 0


def references_pack_api(body: str) -> bool:
    """Does this text touch the pack API (`og.` / `register_hooks`)?"""
    return bool(PACK_API_REFERENCE.search(body))


def precompiled_lua_problem(label: str, data: bytes) -> Optional[str]:
    """The problem text for a precompiled-Lua blob, or None for source text.

    Applied to every collected blob — a .lua on disk, a .glad member, an
    embedded literal — before it can become a denominator entry. A blob
    whose bytes begin with the binary-chunk signature byte (with or without
    a UTF-8 BOM pasted in front) is named and refused: the engine will not
    load it (ScriptHost compiles text-only — a crafted binary chunk is the
    canonical arbitrary-code vector through the sandbox), and admitting it
    would let a STRIPPED chunk erase its line denominator while its function
    spans stayed coverable — a gate FAIL flipping to PASS on identical
    logic (P8-A). See the module docstring.
    """
    body = data
    disguise = ""
    if body.startswith(UTF8_BOM):
        body = body[len(UTF8_BOM):]
        disguise = " behind a UTF-8 BOM"
    if not body.startswith(LUA_SIGNATURE_BYTE):
        return None
    return (
        f"{label}: precompiled Lua bytecode (begins with the binary-chunk "
        f"signature byte LUA_SIGNATURE[0]{disguise}). Precompiled Lua is "
        "not shipped source; commit the .lua text. The engine refuses "
        "binary chunks outright (ScriptHost compiles text-only: crafted "
        "bytecode is an arbitrary-code vector through the sandbox), and a "
        "stripped chunk's empty line grid would otherwise erase this "
        "file's line denominator while its functions stayed coverable"
    )


@dataclass(frozen=True)
class LuaSource:
    """One distinct blob of shipped Lua, whatever exposes it."""

    digest: str          # sha256 of `data`; the entry's identity
    path: str            # canonical label, used as the lcov SF: record
    aliases: Tuple[str, ...]   # every other label carrying the same bytes
    data: bytes

    @property
    def text(self) -> str:
        return self.data.decode("utf-8", errors="replace")


# How a build system hands this module a git it has ALREADY resolved. The
# CMake check targets set it to find_package(Git)'s configure-time result
# (see og_add_python_check_target in CMakeLists.txt); --git-exe on the CLIs
# writes the same variable, so one spelling reaches every consumer —
# including scripts/coverage/coverage_report.py, which imports this module.
GIT_ENV_VAR = "OG_GIT_EXECUTABLE"


def git_executable() -> str:
    """The git to spawn: explicit handoff first, PATH second, never a guess.

    Resolution order (HOW GIT ITSELF IS FOUND in the module docstring):

      1. $OG_GIT_EXECUTABLE — the CMake-plumbed (or --git-exe-supplied)
         executable. Validated, and a value that does not name an executable
         is a hard error rather than a fall-through to PATH: the override
         exists to carry a known-good git, and a typo'd one that silently
         degraded to PATH lookup would reintroduce the exact
         environment-dependence it removes.
      2. shutil.which("git") — plus which("git.exe") on Windows, in case
         PATHEXT is stripped there (which() relies on it for the suffix).
      3. SystemExit naming both levers — the same clear refusal style as a
         failed enumeration, never an ENOENT/WinError traceback from inside
         subprocess.

    which() validates both branches: given a bare name it searches PATH;
    given anything with a directory component it checks that file directly,
    applying PATHEXT on Windows so a suffixless CMake-style `.../git`
    spelling still resolves to `git.exe`.
    """
    explicit = os.environ.get(GIT_ENV_VAR)
    if explicit:
        resolved = shutil.which(explicit)
        if resolved is None:
            raise SystemExit(
                f"scripts/lua_inventory.py: {GIT_ENV_VAR}={explicit!r} does "
                "not name an executable git. The variable carries a git the "
                "build system already resolved (CMake's find_package(Git) "
                "result, or a --git-exe argument), so a broken value is an "
                "error, never a silent fall-through to PATH. Fix or unset "
                "it."
            )
        return resolved
    for name in ("git", "git.exe") if os.name == "nt" else ("git",):
        resolved = shutil.which(name)
        if resolved is not None:
            return resolved
    raise SystemExit(
        "scripts/lua_inventory.py: no git executable found. The shipped-Lua "
        "inventory is defined over `git ls-files` and has no fallback "
        "enumeration (see the module docstring). Install git and put it on "
        f"PATH, or point {GIT_ENV_VAR} (or --git-exe on the CLIs) at the "
        "executable."
    )


def _git_files(
    repo_root: pathlib.Path, include_untracked: bool = True
) -> List[pathlib.Path]:
    """Committed (plus, by default, untracked-but-not-ignored) paths.

    Raises SystemExit when git cannot answer: the inventory is DEFINED over
    the git enumeration, and no walk of the directory tree answers the same
    question (see the module docstring — the old fallback disagreed with git
    about symlinks, which meant one tree, two denominators).
    """
    argv = [git_executable(), "ls-files", "--cached", "-z"]
    if include_untracked:
        argv[2:2] = ["--others", "--exclude-standard"]
    try:
        proc = subprocess.run(
            argv, cwd=repo_root, capture_output=True, check=False,
        )
    except OSError as exc:
        raise SystemExit(
            f"scripts/lua_inventory.py: cannot run {argv[0]} ({exc}). The "
            "shipped-Lua inventory is defined over `git ls-files` and has no "
            "fallback enumeration: a coverage gate outside a git checkout is "
            "not a real deployment. Run it from inside the repository, with "
            "a working git."
        )
    if proc.returncode != 0:
        stderr = proc.stderr.decode("utf-8", errors="replace").strip()
        raise SystemExit(
            f"scripts/lua_inventory.py: `git ls-files` failed in {repo_root} "
            f"({stderr or f'exit {proc.returncode}'}). The shipped-Lua "
            "inventory is defined over the git enumeration and has no "
            "fallback: a coverage gate outside a git checkout is not a real "
            "deployment."
        )
    out = []
    for name in proc.stdout.split(b"\0"):
        if not name:
            continue
        path = repo_root / name.decode("utf-8", errors="surrogateescape")
        if path.is_file():
            out.append(path)
    return out


def repository_files(
    repo_root: pathlib.Path = REPO, include_untracked: bool = True
) -> List[pathlib.Path]:
    return sorted(_git_files(repo_root, include_untracked))


# ---------------------------------------------------------------------------
# Embedded Lua: declared dispositions and the undeclared-Lua sniffer
# ---------------------------------------------------------------------------


def embedded_lua_dispositions(
    path: pathlib.Path = EMBEDDED_LUA_FILE,
) -> Dict[str, str]:
    """repo-relative C++ path -> "shipped" | "fixture".

    "fixture" is parsed but FORBIDDEN: `_declaration_problems` turns every
    such line into a hard problem. Parsing it anyway is what makes the ban
    loud — an unparsed line would be silently ignored, and the file it names
    would merely fall back to the undeclared-literal message instead of the
    one that says why the disposition no longer exists.
    """
    out: Dict[str, str] = {}
    if not path.exists():
        return out
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        disposition, _, name = line.partition(" ")
        name = name.strip()
        if name and disposition in ("shipped", "fixture"):
            out[name] = disposition
    return out


def _rel(repo_root: pathlib.Path, path: pathlib.Path) -> str:
    try:
        return path.relative_to(repo_root).as_posix()
    except ValueError:
        return path.as_posix()


def _scan_cxx(
    rel: str,
    text: str,
    disposition: Optional[str],
    found: List[Tuple[int, str, bytes]],
    problems: List[str],
) -> Tuple[int, int]:
    """Classify every raw string literal in one PRODUCT C++ file.

    Returns (count of R"LUA( literals, count of sniffer-positive literals) so
    the caller can flag stale declarations. Every literal lands in one of
    three buckets — collected as shipped pack Lua, silenced by a `fixture`
    declaration, or a problem — and there is no fourth. Silence is not an
    option any more: it is what let `R"MODLUA(` ship a family's dispatch with
    no denominator.
    """
    lua_literals = 0
    sniffed = 0
    for match in ANY_RAW_LITERAL.finditer(text):
        delimiter, body = match.group(1), match.group(2)
        line = text.count("\n", 0, match.start(2)) + 1
        inner = RAW_OPENER.search(body)
        if inner is not None:
            inner_line = line + body.count("\n", 0, inner.start())
            problems.append(
                f"{rel}:{inner_line}: raw-string opener "
                f"`{inner.group(0)}` inside another raw string's matched "
                f'body (the literal starting at line {line}, delimiter '
                f'"{delimiter}"). The scan cannot tell where one literal '
                "ends and the next begins — the classic cause is quoting an "
                "opener in a comment above a real literal, which swallows "
                "the literal and hides it from the sniffer. Reword the text "
                "so no opener appears inside a literal body."
            )
            continue
        if delimiter == "LUA":
            lua_literals += 1
            if disposition == "shipped":
                found.append(
                    (KIND_LITERAL, f'{rel}:R"LUA"@{line}',
                     body.encode("utf-8")))
            else:
                problems.append(
                    f'{rel}:{line}: R"LUA( ... )LUA" in a file that '
                    f"{EMBEDDED_LUA_FILE.name} does not mark `shipped`. That "
                    "delimiter means the bytes are pack Lua and go into the "
                    "coverage denominator. If they are, add "
                    f"`shipped {rel}`; if they are test-only, the literal "
                    "belongs under tests/ (exempt by construction), not in "
                    "a product file."
                )
            continue
        if not (could_be_lua(body) and references_pack_api(body)):
            continue
        sniffed += 1
        if disposition == "shipped":
            problems.append(
                f'{rel}:{line}: raw string R"{delimiter}( ... ){delimiter}" '
                "could compile as Lua and references the pack API, but this "
                "file's `shipped` declaration only covers R\"LUA( ... )LUA\" "
                "literals, so nothing measures it. Shipped pack Lua must use "
                'the LUA delimiter; a test fixture belongs in tests/.'
            )
        else:
            problems.append(
                f'{rel}:{line}: raw string R"{delimiter}( ... ){delimiter}" '
                "could compile as Lua and references the pack API "
                f"(og./register_hooks), but {EMBEDDED_LUA_FILE.name} does "
                "not mark this file `shipped`, so nothing measures it. If "
                'it is shipped pack Lua, move it to R"LUA( ... )LUA" and '
                f"add `shipped {rel}`; if it is test-only, move it under "
                "tests/, which is exempt by construction — `fixture` "
                "declarations are forbidden in product directories."
            )
    return lua_literals, sniffed


def _declaration_problems(
    repo_root: pathlib.Path,
    dispositions: Dict[str, str],
    counts: Dict[str, Tuple[int, int]],
    include_untracked: bool,
    problems: List[str],
) -> None:
    """A declaration must be a live, permitted claim, or it is a failure.

    A `fixture` line is forbidden outright — it silenced the sniffer for one
    product file per line, the only lever that could hide live product Lua
    behind a single reviewable diff line. A stale `shipped` line claims a
    denominator that no longer exists; a declaration outside the product
    directories claims shipped status for something that does not ship.
    """
    for rel in sorted(dispositions):
        disposition = dispositions[rel]
        top, _, _ = rel.partition("/")
        if top not in PRODUCT_DIRS:
            problems.append(
                f"{EMBEDDED_LUA_FILE.name} declares `{disposition} {rel}`, "
                "which is outside the product directories "
                f"({', '.join(d + '/' for d in PRODUCT_DIRS)}). Only product "
                "C++ can carry shipped embedded Lua; tests/ and the rest are "
                "exempt by construction. Drop the line."
            )
            continue
        if disposition == "fixture":
            problems.append(
                f"{EMBEDDED_LUA_FILE.name} declares `fixture {rel}`: "
                "`fixture` is forbidden for literals in src/, tools/ and "
                "include/ — product literals are `shipped`, full stop, and "
                "a test-only literal belongs in tests/. One `fixture` line "
                "was the only lever that could silence live product Lua, so "
                "the disposition no longer exists. Mark the file `shipped` "
                "or move the literal under tests/."
            )
            continue
        if rel not in counts:
            # In tracked-only mode an untracked-but-present declared file is
            # simply not scanned here; the full (coverage-path) scan owns it.
            if include_untracked or not (repo_root / rel).is_file():
                problems.append(
                    f"{EMBEDDED_LUA_FILE.name} declares "
                    f"`{disposition} {rel}` but no such C++ file is in the "
                    "repository enumeration (deleted, ignored, or not a "
                    "scanned suffix); drop the line"
                )
        else:
            lua_literals, _ = counts[rel]
            if lua_literals == 0:
                problems.append(
                    f'{rel}: declared `shipped` in {EMBEDDED_LUA_FILE.name} '
                    'but no R"LUA( ... )LUA" literal exists there any more; '
                    "drop the line"
                )


def _case_variant_shipped_root(top: str) -> Optional[str]:
    """The shipped root `top` is a case-variant of, or None.

    Exact membership stays the caller's test (byte equality against
    SHIPPED_LUA_ROOTS — PhysFS on the gate platform is case-sensitive and so
    is the git enumeration). This answers the follow-up question for a
    top-level directory that is NOT a shipped root: would it have been one
    under case folding? Such a directory is correctly absent from the
    denominator — the engine cannot mount it on the gate platform — but a
    case-insensitive dev filesystem (macOS/Windows) WOULD load it while this
    gate never measures it, so the spelling is a collected problem, never a
    silent exclusion.
    """
    if top in SHIPPED_LUA_ROOTS:
        return None
    fold = top.casefold()
    for root in SHIPPED_LUA_ROOTS:
        if fold == root.casefold():
            return root
    return None


def _shipped_root_case_problem(rel: str, top: str, root: str) -> str:
    return (
        f"{rel}: will not ship on the gate platform — shipped roots are "
        f"matched case-sensitively and '{root}/' is the real one (PhysFS "
        "and the git enumeration both distinguish case), so the engine "
        f"never mounts '{top}/' there. A case-insensitive dev filesystem "
        "(macOS/Windows) WOULD load it, hiding the typo while this gate "
        "never measures the file: rename the directory"
    )


def _case_variant_scripts_segment(rel: str) -> Optional[str]:
    """The case-variant `scripts` path segment in `rel`, or None.

    The engine mounts pack scripts from the literal lowercase
    `packs/<pack>/scripts/` only (src/resources/packs.cpp builds that path
    byte-for-byte). Any `.lua` under a shipped root still enters the
    denominator — the deliberate no-narrower-pattern policy in the module
    docstring — so a `packs/x/Scripts/` file would sit there as an entry no
    test on the gate platform could ever cover, while a case-insensitive dev
    filesystem loads it happily. That spelling is a collected problem; the
    entry itself stays, because problems already stop the gate.
    """
    parts = rel.split("/")
    if len(parts) >= 4 and parts[0] == "packs":
        seg = parts[2]
        if seg != "scripts" and seg.casefold() == "scripts":
            return seg
    return None


def _collect(
    repo_root: pathlib.Path,
    files: Sequence[pathlib.Path],
    include_untracked: bool,
    problems: List[str],
) -> List[Tuple[int, str, bytes]]:
    """(kind, label, bytes) for every blob of shipped Lua in `files`.

    Every read failure on a shipped path is a PROBLEM, never a skip: a
    `continue` here shrinks the denominator, and an unreadable file is the
    cheapest possible way to make game logic unmeasured (the demonstration
    was a chmod-000 pack script — the inventory dropped it and the gate
    said nothing). Junk outside the shipped roots is the opposite case: it
    is not read at all, so it can be neither a denominator entry nor a
    failure.
    """
    found: List[Tuple[int, str, bytes]] = []
    dispositions = embedded_lua_dispositions()
    counts: Dict[str, Tuple[int, int]] = {}
    for path in files:
        rel = _rel(repo_root, path)
        suffix = path.suffix
        top, _, _ = rel.partition("/")
        # ".lua" is matched CASE-SENSITIVELY, because that is the engine's
        # own test (src/resources/packs.cpp compares the last four bytes
        # against ".lua", exactly): a PROBE.LUA under a shipped root can
        # never be loaded by anything. Lowercasing here once put such a file
        # into the denominator — a 0% entry no test could ever cover, an
        # unfixable red. Matching the engine and SAYING NOTHING would be the
        # opposite failure: a typo'd suffix silently ships nothing. So the
        # membership test is the engine's, and the case-variant is a
        # collected problem.
        if suffix == ".lua":
            if top not in SHIPPED_LUA_ROOTS:
                variant_root = _case_variant_shipped_root(top)
                if variant_root is not None:
                    problems.append(
                        _shipped_root_case_problem(rel, top, variant_root))
                # tests/, scripts/, and everywhere else: not shipped, never
                # read. A runtime mount of such a file is owned by the
                # report's content-hash poison pills. See the docstring.
                continue
            seg = _case_variant_scripts_segment(rel)
            if seg is not None:
                problems.append(
                    f"{rel}: the engine mounts pack scripts only from the "
                    "literal lowercase 'packs/<pack>/scripts/' "
                    f"(src/resources/packs.cpp), so '{seg}/' never loads on "
                    "the gate platform and this denominator entry can never "
                    "be covered there — while a case-insensitive dev "
                    "filesystem WOULD load it, hiding the typo: rename the "
                    "directory to 'scripts'"
                )
            try:
                found.append((KIND_FILE, rel, path.read_bytes()))
            except OSError as exc:
                problems.append(
                    f"{rel}: cannot read shipped Lua ({exc}); an unreadable "
                    "file under a shipped root would otherwise silently "
                    "shrink the coverage denominator"
                )
        elif suffix.lower() == ".lua":
            if top in SHIPPED_LUA_ROOTS:
                problems.append(
                    f"{rel}: will never load — pack scripts require a "
                    "lowercase '.lua' suffix (the engine's test in "
                    "src/resources/packs.cpp is case-sensitive). A "
                    "case-variant under a shipped root is a typo, not a "
                    "denominator entry and not a silent skip: rename it to "
                    "'.lua' or delete it"
                )
            else:
                variant_root = _case_variant_shipped_root(top)
                if variant_root is not None:
                    problems.append(
                        _shipped_root_case_problem(rel, top, variant_root))
            # Outside the shipped roots the file does not ship under ANY
            # spelling; it stays unread, exactly like its lowercase twin —
            # except that a case-variant ROOT is itself named above, since a
            # case-insensitive dev filesystem would ship it.
        elif suffix == ".glad":
            try:
                with zipfile.ZipFile(path) as archive:
                    for entry in sorted(archive.namelist()):
                        # Same engine-exact suffix rule as on disk: the
                        # archive is mounted and its members walk through
                        # the identical case-sensitive test.
                        if entry.endswith(".lua"):
                            found.append(
                                (KIND_ARCHIVE, f"{rel}!{entry}",
                                 archive.read(entry)))
                        elif entry.lower().endswith(".lua"):
                            problems.append(
                                f"{rel}!{entry}: will never load — pack "
                                "scripts require a lowercase '.lua' suffix "
                                "(the engine's test is case-sensitive). A "
                                "case-variant member is a typo the gate "
                                "refuses to hide: rebuild the archive with "
                                "the member renamed"
                            )
            except (OSError, zipfile.BadZipFile, KeyError, zlib.error) as exc:
                problems.append(
                    f"{rel}: cannot enumerate campaign archive "
                    f"({exc}); its .lua members are shipped game logic, and "
                    "an unopenable or corrupt archive would otherwise "
                    "silently shrink the coverage denominator"
                )
        elif suffix.lower() == ".glad":
            # The exact asymmetry the .lua rule above closed, closed for the
            # archive suffix too: membership is the ENGINE'S — ".glad"
            # byte-for-byte (the campaign filter and builtin restore in
            # src/resources/io/platform_io_common.cpp are case-sensitive,
            # and every mount path is built as "<id>.glad") — so a
            # case-variant archive's members are NOT denominator entries
            # (a lowercasing match once admitted them: unloadable, therefore
            # uncoverable, an unfixable red), and the archive itself is a
            # named problem rather than a silent skip.
            problems.append(
                f"{rel}: will never load — campaign archives require a "
                "lowercase '.glad' suffix (the engine's campaign filter and "
                "builtin restore in src/resources/io/platform_io_common.cpp "
                "are case-sensitive). A case-variant archive is a typo, not "
                "a source of denominator entries and not a silent skip: "
                "rename it to '.glad' or delete it"
            )
        elif suffix.lower() in CXX_SUFFIXES:
            if top not in PRODUCT_DIRS:
                continue  # tests/ etc.: exempt by construction, see docstring
            disposition = dispositions.get(rel)
            try:
                text = path.read_text(encoding="utf-8", errors="replace")
            except OSError as exc:
                problems.append(
                    f"{rel}: cannot read product C++ file while scanning "
                    f"for embedded Lua ({exc}); a literal there would be "
                    "invisible to the coverage denominator"
                )
                continue
            if 'R"' not in text and disposition is None:
                continue
            counts[rel] = _scan_cxx(rel, text, disposition, found, problems)

    _declaration_problems(
        repo_root, dispositions, counts, include_untracked, problems)
    return found


@dataclass(frozen=True)
class Scan:
    sources: List[LuaSource]
    problems: List[str]


def scan(
    repo_root: pathlib.Path = REPO, include_untracked: bool = True
) -> Scan:
    """The inventory plus every enumeration problem found building it.

    `problems` is never advisory. The lint prints them and exits non-zero;
    `inventory()` refuses outright: an undeclared blob of embedded Lua is
    either game logic with no denominator or a fixture inflating one, and
    neither should be discoverable only by reading a warning.
    """
    problems: List[str] = []
    by_digest: Dict[str, List[Tuple[int, str, bytes]]] = {}
    for kind, label, data in _collect(
        repo_root, repository_files(repo_root, include_untracked),
        include_untracked, problems
    ):
        # One choke point for all three kinds. A precompiled blob is a
        # problem and NOT an entry — unlike the case-variant problems above
        # (real text a case-insensitive filesystem could still load), a
        # binary chunk is never admissible source, and an entry for it would
        # carry the exact truncated grid this check exists to keep out of
        # the denominator. The problem already stops the gate and the lint.
        precompiled = precompiled_lua_problem(label, data)
        if precompiled is not None:
            problems.append(precompiled)
            continue
        by_digest.setdefault(hashlib.sha256(data).hexdigest(), []).append(
            (kind, label, data))

    sources: List[LuaSource] = []
    for digest, group in by_digest.items():
        group.sort(key=lambda item: (item[0], len(item[1]), item[1]))
        canonical = group[0]
        sources.append(LuaSource(
            digest=digest,
            path=canonical[1],
            aliases=tuple(label for _, label, _ in group[1:]),
            data=canonical[2],
        ))
    sources.sort(key=lambda s: s.path)
    return Scan(sources, problems)


def inventory(
    repo_root: pathlib.Path = REPO, include_untracked: bool = True
) -> List[LuaSource]:
    """Every distinct blob of shipped Lua, deduplicated by content hash.

    Sorted by canonical label so the report, the tracefile and the lint all
    iterate in one reproducible order.

    FAIL-CLOSED: raises SystemExit when the scan found enumeration problems.
    A sources list built over a tree with an undeclared or stale embedded-Lua
    literal is not a denominator, and handing it out anyway is how the
    sniffer spent a whole audit cycle as dead code — both consumers took the
    sources and dropped the problems. Callers that want to present the
    problems themselves use scan().
    """
    result = scan(repo_root, include_untracked)
    if result.problems:
        raise SystemExit(
            f"{len(result.problems)} problem(s) enumerating the repository's "
            "shipped Lua — the coverage denominator cannot be trusted until "
            "they are fixed:\n"
            + "\n".join(f"  ERROR: {p}" for p in result.problems)
        )
    return result.sources


def by_digest(sources: Iterable[LuaSource]) -> Dict[str, LuaSource]:
    return {s.digest: s for s in sources}


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Enumerate the repository's shipped Lua (the coverage "
        "denominator) and every enumeration problem."
    )
    parser.add_argument(
        "--tracked-only", action="store_true",
        help="enumerate git-tracked files only (the per-build lint's view); "
        "the default is the coverage denominator's view, which includes "
        "untracked-but-not-ignored files",
    )
    parser.add_argument(
        "--git-exe", metavar="PATH",
        help=f"git executable for the enumeration; sets {GIT_ENV_VAR}, the "
        "same override the CMake check targets pass through the environment "
        "on hosts where the build-time PATH cannot resolve the bare name "
        "(the Windows CI runner). Default: that variable if set, else PATH.",
    )
    args = parser.parse_args()
    if args.git_exe:
        os.environ[GIT_ENV_VAR] = args.git_exe
    result = scan(include_untracked=not args.tracked_only)
    for entry in result.sources:
        extra = f"  (+{len(entry.aliases)} alias)" if entry.aliases else ""
        print(f"{entry.digest[:12]}  {len(entry.data):7}  {entry.path}{extra}")
        for alias in entry.aliases:
            print(f"{'':12}  {'':7}    = {alias}")
    print(f"{len(result.sources)} distinct Lua source(s)")
    for problem in result.problems:
        print(f"  ERROR: {problem}")
    return 1 if result.problems else 0


if __name__ == "__main__":
    raise SystemExit(main())
