#!/usr/bin/env python3
"""Generate the simulation semantics manifest from the review's JSON files.

    gen_manifest.py [--repo DIR] <review_dir> <manifest_dir>

Reads every *.json in <review_dir> (each a list of rows, one per semantic or
per non-rule branch, as the simulation review wrote them) and writes one
manifest file per source file into <manifest_dir>:

    {"path": ..., "rules": [...], "other": [...]}

rows sorted by first line.  `lines` keeps only the branch lines the
check's grep sees, plus the anchor line (the review sometimes lists the
function's opening brace or a statement between two arms as well).  A rule
row keeps id, lines, kinds, function, anchor, anchor_index (when not 0),
condition, semantic, arms, setting, status, tests, missing_arms, issue (when
the review gave one) and note (free text that cites no test).  An other row
keeps id, lines, kinds, kind, function (when the review gave one), anchor,
anchor_index and note.

`kinds` holds, for each of `lines`, the branch keyword the grep matched
there ("if", "else", "case", "ternary", Lua's "elseif", "and", ...; "" for
an anchor line that is no branch line), so that the check can tell when a
line a row claims has become a different branch.

`anchor` is the trimmed source text of one of the row's lines in the tree
at --repo (default: this script's repository), which is what
check_manifest.py anchors on: the first line that is a branch line with text
of its own (not a bare `else`, `{`, `default:`), else the first line with
text of its own, else the first line.  When that is not lines[0] the row
also carries `anchor_index`, the position of the anchor line in `lines`.
The review's own words for the condition, a paraphrase that is often not
literal source text, are kept as `condition`.

Test citations are normalised from the review's free text:
    "tests/unit/x.cpp: TEST_F(Suite, name) (remark)"
        -> {"file": "tests/unit/x.cpp", "test": "Suite.name", "note": "remark"}
    "parity: some_row_scen99 / Fact(...)"
        -> {"parity": "some_row_scen99", "fact": "Fact(...)"}
A TEST() with no file of its own takes the last file named earlier in the
same row.  A citation that names neither, and every "GAP" / "NOT credited"
remark, goes to the row's note list.

Deterministic: the same inputs give byte-identical files; manifest files the
review no longer produces are removed from <manifest_dir>.
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import re
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_manifest as cm  # noqa: E402  (the scope, the anchor rules, the writer)

TEST_RX = re.compile(r"\bTEST(?:_F|_P)?\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)")
FILE_RX = re.compile(r"\b(tests/[\w./-]+\.(?:cpp|cc|h))\b")
PARITY_ID_RX = re.compile(r"\b([A-Za-z]\w*_scen\d+)\b")
NOT_A_CITATION_RX = re.compile(r"^\s*(GAP|NOT credited|gap|UNASSERTED|unasserted)\b")
ANCHOR_MAX = 160


def clean(text: str) -> str:
    """Trim the punctuation a citation leaves around its remark."""
    text = text.strip()
    text = re.sub(r"^[\s:;,.\-]+", "", text)
    text = re.sub(r"[\s;,]+$", "", text)
    if text.startswith("(") and closing_paren(text) == len(text) - 1:
        text = text[1:-1].strip()
    return text


def closing_paren(text: str) -> int:
    """Index of the parenthesis that closes text[0], or -1."""
    depth = 0
    for i, ch in enumerate(text):
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0:
                return i
    return -1


def normalise_where(where: list[str]) -> tuple[list[dict], list[str]]:
    tests: list[dict] = []
    notes: list[str] = []
    last_file: str | None = None
    for raw in where:
        text = raw.strip()
        if NOT_A_CITATION_RX.match(text):
            notes.append(text)
            continue
        m = re.match(r"^\s*parity\s*:\s*(.*)$", text, re.DOTALL)
        if m:
            body = m.group(1)
            ids_part, sep, fact = body.partition(" / ")
            ids = list(dict.fromkeys(PARITY_ID_RX.findall(ids_part)))
            if not ids:
                notes.append(text)
                continue
            leftover = clean(PARITY_ID_RX.sub("", ids_part).replace(",", " ").replace("/", " "))
            for pid in ids:
                entry: dict = {"parity": pid, "fact": fact.strip() if sep else ""}
                tests.append(entry)
            if leftover:
                tests[-1]["note"] = leftover
            continue
        hits = list(TEST_RX.finditer(text))
        if not hits:
            files = FILE_RX.findall(text)
            if files:
                last_file = files[-1]
            notes.append(text)
            continue
        produced = 0
        for i, hit in enumerate(hits):
            files_before = FILE_RX.findall(text[: hit.start()])
            if files_before:
                last_file = files_before[-1]
            if last_file is None:
                notes.append(text)
                break
            end = hits[i + 1].start() if i + 1 < len(hits) else len(text)
            tail = text[hit.end(): end]
            tail = FILE_RX.sub("", tail)
            entry = {"file": last_file, "test": f"{hit.group(1)}.{hit.group(2)}"}
            remark = clean(tail)
            if remark and remark not in ("and", "or"):
                entry["note"] = remark
            tests.append(entry)
            produced += 1
        if produced and hits[0].start() > 0:
            lead = clean(FILE_RX.sub("", text[: hits[0].start()]))
            if lead:
                tests[-produced].setdefault("note", lead)
    return tests, notes


def line_text(src: list[str], line: int) -> str:
    if 1 <= line <= len(src):
        return src[line - 1].strip()[:ANCHOR_MAX]
    return ""


def pick_anchor(path: str, src: list[str], lines: list[int]) -> tuple[str, int]:
    """(anchor text, index into lines) for a row: the first of its lines that
    is a candidate branch line with text of its own, else the first line with
    text of its own, else the first line.  A row the review starts on a bare
    `{` or `else` is so anchored on the branch that names it."""
    def own(n: int) -> bool:
        return not cm.is_generic(line_text(src, n))

    for pred in (lambda n: own(n) and 1 <= n <= len(src) and cm.is_candidate(path, src[n - 1]),
                 own):
        for k, n in enumerate(lines):
            if pred(n):
                return line_text(src, n), k
    return line_text(src, lines[0]), 0


def branch_lines(path: str, src: list[str], lines: list[int]) -> tuple[list[int], dict]:
    """The row's lines as the manifest keeps them, and its anchor fields.

    The review sometimes lists context lines with a row (the function's
    opening brace, a statement between two arms).  The manifest keeps only
    the branch lines the grep sees, plus the anchor line, so that every line
    below a row's anchor is a branch line; check_manifest.py --fix relies on
    that to re-place an arm an edit has moved."""
    anchor, k = pick_anchor(path, src, lines)
    at = lines[k]
    kept = [n for n in lines
            if n == at or (1 <= n <= len(src) and cm.is_candidate(path, src[n - 1]))]
    k = kept.index(at)
    kinds = [cm.branch_kind(path, src[n - 1]) if 1 <= n <= len(src) else "" for n in kept]
    return kept, {"kinds": kinds, "anchor": anchor, **({"anchor_index": k} if k else {})}


def line_list(row: dict) -> list[int]:
    lines = sorted({int(n) for n in row.get("lines", [])})
    if not lines:
        # The review left a handful of rows with no lines of their own (a
        # semantic whose branch the grep does not see); its id names the line.
        tail = str(row["id"]).rsplit(":", 1)[-1]
        if tail.isdigit():
            lines = [int(tail)]
    return lines


def rule_row(row: dict, path: str, src: list[str]) -> dict:
    a = row.get("assertion", {})
    tests, notes = normalise_where(list(a.get("where", [])))
    if a.get("note"):
        notes.append(str(a["note"]))
    for arm in a.get("unasserted_arms", []) or []:
        notes.append(f"unasserted: {arm}")
    lines, anchor = branch_lines(path, src, line_list(row))
    out: dict = {
        "id": row["id"],
        "lines": lines,
        "kinds": anchor.pop("kinds"),
        "function": row.get("function", ""),
        **anchor,
        "condition": str(row.get("condition", "")).strip(),
        "semantic": row.get("semantic", ""),
        "arms": row.get("arms", []),
        "setting": row.get("setting", "none"),
        "status": a.get("status", "none"),
        "tests": tests,
        "missing_arms": list(a.get("missing_arms", []) or []),
    }
    issue = a.get("issue", row.get("issue"))
    if issue is not None:
        out["issue"] = issue
    if notes:
        out["note"] = notes
    return out


def other_row(row: dict, path: str, src: list[str]) -> dict:
    lines, anchor = branch_lines(path, src, line_list(row))
    out: dict = {"id": row["id"], "lines": lines, "kinds": anchor.pop("kinds"), "kind": row["kind"]}
    if row.get("function"):
        out["function"] = row["function"]
    out.update(anchor)
    out["note"] = row.get("note", "")
    return out


def generate(repo: str, review_dir: str, manifest_dir: str) -> int:
    rows: list[dict] = []
    for name in sorted(glob.glob(os.path.join(review_dir, "*.json"))):
        with open(name, encoding="utf-8") as fh:
            data = json.load(fh)
        if not isinstance(data, list):
            print(f"gen_manifest: {name}: not a list of rows", file=sys.stderr)
            return 2
        rows.extend(data)
    scope = set(cm.in_scope_files(repo))
    seen: dict[str, str] = {}
    by_path: dict[str, list[dict]] = {}
    for row in rows:
        rid, path = row.get("id"), row.get("path")
        if path not in scope:
            print(f"gen_manifest: row {rid}: {path} is not an in-scope file", file=sys.stderr)
            return 2
        if rid in seen:
            print(f"gen_manifest: row id {rid} appears twice", file=sys.stderr)
            return 2
        if not line_list(row):
            print(f"gen_manifest: row {rid}: no lines and no line number in its id", file=sys.stderr)
            return 2
        seen[rid] = path
        by_path.setdefault(path, []).append(row)

    names = [os.path.basename(p) for p in scope]
    clashes = {n for n in names if names.count(n) > 1}
    os.makedirs(manifest_dir, exist_ok=True)
    written: set[str] = set()
    for path in sorted(by_path):
        src = cm.read_lines(repo, path) or []
        rules, other = [], []
        for row in by_path[path]:
            if row.get("kind") == "rule":
                rules.append(rule_row(row, path, src))
            else:
                other.append(other_row(row, path, src))
        rules.sort(key=lambda r: (r["lines"][0], r["id"]))
        other.sort(key=lambda r: (r["lines"][0], r["id"]))
        name = cm.manifest_name(path, clashes)
        with open(os.path.join(manifest_dir, name), "w", encoding="utf-8", newline="\n") as fh:
            fh.write(cm.dump_manifest({"path": path, "rules": rules, "other": other}))
        written.add(name)
    for stale in sorted(os.listdir(manifest_dir)):
        if stale.endswith(".json") and stale not in written:
            os.remove(os.path.join(manifest_dir, stale))
    n_rules = sum(1 for r in rows if r.get("kind") == "rule")
    print(f"gen_manifest: {len(written)} files, {n_rules} rule rows, {len(rows) - n_rules} other rows")
    return 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--repo", default=os.path.dirname(os.path.dirname(
        os.path.dirname(os.path.abspath(__file__)))))
    ap.add_argument("review_dir")
    ap.add_argument("manifest_dir")
    args = ap.parse_args(argv)
    return generate(os.path.abspath(args.repo), args.review_dir, args.manifest_dir)


if __name__ == "__main__":
    sys.exit(main())
