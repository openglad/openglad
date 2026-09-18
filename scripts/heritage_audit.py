#!/usr/bin/env python3
"""Heritage-comment audit: which pre-cutoff comment lines did this branch delete?

The rule this script serves lives in .claude/skills/openglad-heritage-comments:
when code moves, is rewritten or is ported, its comment moves with it verbatim,
and a comment whose blame predates the cutoff may not be dropped silently.  In
this repo that cutoff is 2020: the 2002 GPL-era import and Jonathan Dearborn's
2013 maintainership are heritage, the 2026 modernization is not.

``git blame`` alone cannot answer the question.  Three classes of heritage line
hide from it, and all three were live on PR #292:

  1. Reflow / move re-attribution.  A 2026 commit that reindented or relocated
     the line owns the blame.  ``// Get current number of foes`` blames to 2026
     and is verbatim ``glad.cpp:601`` from the 2002 tree.
  2. Delete-and-restore re-attribution.  Jonathan Dearborn wrote the two RETURN
     FIXME lines in 2013 (03882de0), deleted them himself later that year
     (3102863c), and a 2026 commit restored them (19d7eeb3).  Blame says 2026,
     and the lines are absent from every pre-cutoff *tree*, so only the union of
     every pre-cutoff comment fragment ever written finds them.
  3. Deleted files.  A deleted file's hunks carry ``+++ /dev/null``, so file
     attribution must come from ``--- a/``.

So the pipeline is: collect -> blame -> pre-cutoff tree identity -> pre-cutoff
history identity -> survival at HEAD -> ledger.

Usage:

    scripts/heritage_audit.py --base <merge-base> [--head HEAD]
                              [--paths src include tests ...]
                              [--cutoff 2020-01-01] [--json out.json]

The report is a report, not a gate: it always exits 0.  ``--self-test`` runs the
whole pipeline over a synthetic four-commit repository with one row per failure
class and exits 1 on the first mismatch.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
from datetime import datetime, timezone

CUTOFF_DEFAULT = "2020-01-01"

# Comment syntax by file type.  Extensions not listed here are skipped: Markdown
# and docs/scen.txt are prose, not comment lines, and are reviewed by hand.
C_LIKE = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".inc", ".js", ".html"}
LUA_LIKE = {".lua"}
HASH_LIKE = {".sh", ".bash", ".py", ".yml", ".yaml", ".cmake"}
HASH_NAMES = {"CMakeLists.txt"}

MIN_TEXT = 4     # a bare `//` separator is not a comment worth tracking
MIN_FRAG = 8     # fragments shorter than this match everything


def _style(path):
    ext = os.path.splitext(path)[1]
    base = os.path.basename(path)
    if ext in C_LIKE:
        return "c"
    if ext in LUA_LIKE:
        return "lua"
    if ext in HASH_LIKE or base in HASH_NAMES:
        return "hash"
    return None


def is_comment(path, text):
    """True when `text`, a line of `path`, carries a comment."""
    s = text.strip()
    if not s:
        return False
    style = _style(path)
    if style == "c":
        return "//" in s or s.startswith("/*") or s.startswith("*")
    if style == "lua":
        return s.startswith("--")
    if style == "hash":
        return s.startswith("#") or "  #" in s
    return False


def fragment(path, text, style=None):
    """The comment itself: the text from its marker onward, right-trimmed.

    Fragment granularity is what makes a reindented or relocated heritage line
    recognisable — the leading whitespace a 2026 reflow changed sits outside the
    fragment, so the fragment still matches the pre-cutoff tree verbatim.
    """
    style = style or _style(path)
    s = text.rstrip()
    if style == "c":
        i = s.find("//")
        if i >= 0:
            return s[i:]
        t = s.strip()
        return t if (t.startswith("/*") or t.startswith("*")) else None
    if style == "lua":
        i = s.find("--")
        return s[i:] if i >= 0 else None
    if style == "hash":
        i = s.find("#")
        return s[i:] if i >= 0 else None
    return None


def any_fragment(text):
    """Marker-agnostic fragment, for scanning raw patch text of unknown files."""
    s = text.rstrip()
    i = s.find("//")
    if i >= 0:
        return s[i:]
    t = s.strip()
    for marker in ("/*", "*", "--", "#"):
        if t.startswith(marker):
            return t
    return None


def normalize(s):
    return re.sub(r"\s+", " ", s.strip())


# --- git plumbing -----------------------------------------------------------

class Git:
    """Every git call in one place, with a hermetic environment.

    quotepath=false keeps non-ASCII paths readable; the config overrides keep a
    developer's global git config (diff drivers, external diff, pagers) out of
    the audit so the answer does not depend on whose machine it runs on.
    """

    def __init__(self, repo):
        self.repo = repo
        self.env = dict(os.environ)
        self.env["GIT_PAGER"] = "cat"
        self.env["GIT_TERMINAL_PROMPT"] = "0"

    def run(self, args, check=True, ok=(0,)):
        cmd = ["git", "-c", "core.quotepath=false", "-c", "core.pager=cat"] + args
        r = subprocess.run(cmd, cwd=self.repo, capture_output=True, text=True,
                           errors="replace", env=self.env)
        if check and r.returncode not in ok:
            raise RuntimeError(
                "git %s failed (%d): %s" % (" ".join(args[:3]), r.returncode,
                                            r.stderr.strip()[:400]))
        return r

    def out(self, args, **kw):
        return self.run(args, **kw).stdout


# --- (a) collect ------------------------------------------------------------

def collect(git, base, head, paths):
    """Every deleted (or modified-away) comment line in base..head.

    A modified comment appears in a unified diff as its deleted half, so
    modifications land in this set without a second pass.

    File attribution comes from ``--- a/`` and falls back to ``+++ b/`` only for
    an added file.  Keying off ``+++ b/`` alone — the recipe this script
    replaces — files every hunk of a DELETED file (``+++ /dev/null``) under
    whichever file preceded it in the diff.
    """
    args = ["diff", "--no-color", "--no-ext-diff", "--find-renames", base, head]
    if paths:
        args += ["--"] + list(paths)
    diff = git.out(args).splitlines()

    rows = []
    seen = set()
    old_path = new_path = cur = None
    oldln = 0
    for line in diff:
        if line.startswith("diff --git "):
            old_path = new_path = cur = None
        elif line.startswith("--- "):
            p = line[4:]
            old_path = None if p == "/dev/null" else re.sub(r"^a/", "", p)
        elif line.startswith("+++ "):
            p = line[4:]
            new_path = None if p == "/dev/null" else re.sub(r"^b/", "", p)
            cur = old_path or new_path
        elif line.startswith("@@"):
            m = re.match(r"@@ -(\d+)(?:,\d+)? \+", line)
            oldln = int(m.group(1)) if m else 0
        elif line.startswith("-") and not line.startswith("---"):
            text = line[1:]
            if cur and is_comment(cur, text):
                text_s = text.strip()
                # A tuple key, never a joined string: a comment containing the
                # join character (`around == (TO_LEFT | TO_RIGHT)`) is truncated
                # by a `file|text` key and then fails to blame.
                key = (cur, text_s)
                if len(text_s) >= MIN_TEXT and key not in seen:
                    seen.add(key)
                    rows.append({
                        "file": cur,
                        "line": oldln,
                        "text": text,
                        "text_s": text_s,
                        "frag": fragment(cur, text),
                    })
            oldln += 1
        elif line.startswith(" "):
            oldln += 1
    return rows


# --- (b) blame --------------------------------------------------------------

_BLAME_HEAD = re.compile(r"^([0-9a-f]{40}) (\d+) (\d+)(?: (\d+))?$")


def blame_file(git, rev, path):
    """(stripped text) -> [(line, sha, author, author-time, summary, origfile)].

    ``-C -C -C`` so a line that moved between files in the same commit, or came
    from any file in any earlier commit, keeps its real author.  One whole-file
    blame per file, not one per line: same answer, a fraction of the processes.
    """
    r = git.run(["blame", "-C", "-C", "-C", "--line-porcelain", rev, "--", path],
                check=False)
    if r.returncode != 0:
        return {}
    index = {}
    lines = r.stdout.splitlines()
    i = 0
    while i < len(lines):
        m = _BLAME_HEAD.match(lines[i])
        if not m:
            i += 1
            continue
        sha = m.group(1)
        final = int(m.group(3))
        meta = {}
        i += 1
        while i < len(lines) and not lines[i].startswith("\t"):
            k, _, v = lines[i].partition(" ")
            meta.setdefault(k, v)
            i += 1
        content = lines[i][1:] if i < len(lines) else ""
        i += 1
        index.setdefault(content.strip(), []).append((
            final, sha, meta.get("author"), int(meta.get("author-time") or 0),
            meta.get("summary"), meta.get("filename")))
    return index


def blame_rows(git, rows, base):
    for path in sorted({r["file"] for r in rows}):
        index = blame_file(git, base, path)
        for row in rows:
            if row["file"] != path:
                continue
            hits = index.get(row["text_s"], [])
            if not hits:
                row.update(sha=None, author=None, atime=None, summary=None,
                           origfile=None, blame_line=None)
                continue
            # Earliest authorship wins when the same text occurs several times
            # in one file: the oldest copy is the one with the history.
            ln, sha, author, atime, summary, origfile = sorted(
                hits, key=lambda h: h[3])[0]
            row.update(sha=sha[:8], author=author, atime=atime, summary=summary,
                       origfile=origfile, blame_line=ln)


# --- grep helpers -----------------------------------------------------------

_GREP_LINE = re.compile(r"^(?P<rev>[^:]*):(?P<path>[^:]*):(?P<line>\d+):(?P<text>.*)$")


def grep_rev(git, rev, frags, chunk=200):
    """frag -> [(path, line, text)] for every fragment found in `rev`.

    One git grep per chunk of patterns rather than one per fragment: the pattern
    that matched is recovered in Python, which is far cheaper than the process.
    """
    frags = [f for f in frags if f]
    hits = {f: [] for f in frags}
    for start in range(0, len(frags), chunk):
        batch = frags[start:start + chunk]
        args = ["grep", "--no-color", "-n", "-F"]
        for f in batch:
            args += ["-e", f]
        args += [rev]
        r = git.run(args, check=False, ok=(0, 1))
        if r.returncode not in (0, 1):
            continue
        for line in r.stdout.splitlines():
            m = _GREP_LINE.match(line)
            if not m or m.group("rev") != rev:
                continue
            path, ln, text = m.group("path"), int(m.group("line")), m.group("text")
            for f in batch:
                if f in text:
                    hits[f].append((path, ln, text))
    return hits


# --- (c) pre-cutoff tree identity -------------------------------------------

def pass_tree(git, rows, base, cutoff_iso):
    """Re-class rows whose comment fragment is verbatim in the last pre-cutoff tree.

    This is the reflow/move class: the 2026 commit that reindented the line owns
    the blame, but the text itself is older than the cutoff.
    """
    rev = git.out(["rev-list", "-1", "--before=" + cutoff_iso, base]).strip()
    if not rev:
        return None
    todo = [r for r in rows if r["class"] == "modern" and r["frag"]
            and len(r["frag"]) >= MIN_FRAG]
    hits = grep_rev(git, rev, sorted({r["frag"] for r in todo}))
    for row in todo:
        # Identity, not containment: git grep is a substring search, and a short
        # fragment (`// rename`, `// namespace`) is inside some longer comment in
        # every old tree.  The old line must BE this comment, not contain it.
        found = [h for h in (hits.get(row["frag"]) or [])
                 if normalize(fragment(h[0], h[2]) or "") == normalize(row["frag"])]
        if found:
            path, ln, _ = found[0]
            row["class"] = "heritage"
            row["origin"] = "pre-cutoff tree"
            row["origin_at"] = "%s:%d@%s" % (path, ln, rev[:8])
    return rev


# --- (d) pre-cutoff history identity ----------------------------------------

def pre_cutoff_fragments(git, base, cutoff_iso):
    """Every comment fragment that ever existed before the cutoff.

    Built from the patch text of the whole pre-cutoff history, both the `+` and
    the `-` sides, so a comment that was written and then deleted before the
    cutoff is still in the union.  That is the only pass that can see a
    delete-and-restore: the line is in no pre-cutoff tree at all.
    """
    r = git.run(["log", "--before=" + cutoff_iso, "-p", "--no-renames",
                 "--no-color", "--no-ext-diff", "--format=", base], check=False)
    union = set()
    for line in r.stdout.splitlines():
        if not line or line[0] not in "+- ":
            continue
        if line.startswith("+++") or line.startswith("---"):
            continue
        f = any_fragment(line[1:])
        if f and len(f) >= MIN_FRAG:
            union.add(normalize(f))
    return union


def pass_history(git, rows, base, cutoff_iso):
    todo = [r for r in rows if r["class"] == "modern" and r["frag"]
            and len(r["frag"]) >= MIN_FRAG]
    if not todo:
        return 0
    union = pre_cutoff_fragments(git, base, cutoff_iso)
    for row in todo:
        if normalize(row["frag"]) in union:
            row["class"] = "heritage"
            row["origin"] = "pre-cutoff history (deleted and restored later)"
    return len(union)


# --- (e) survival at HEAD ---------------------------------------------------

def survival(git, rows, head):
    """Deleted is not lost: a rewrite often re-creates the comment elsewhere.

    Exact survival is the same fragment as a whole comment; prefix survival is
    the fragment inside a longer comment (`// Buttons` inside
    `// Buttons (geometry single-sourced in picker_common ...)`), which the
    skill counts as preserved.
    """
    todo = [r for r in rows if r["frag"] and len(r["frag"]) >= MIN_FRAG]
    hits = grep_rev(git, head, sorted({r["frag"] for r in todo}))
    for row in rows:
        row["status"] = "gone"
        row["status_at"] = ""
        if not row["frag"] or len(row["frag"]) < MIN_FRAG:
            row["status"] = "not checked (fragment too short)"
            continue
        found = hits.get(row["frag"]) or []
        exact = [h for h in found
                 if (fragment(h[0], h[2]) or "").rstrip() == row["frag"].rstrip()]
        if exact:
            row["status"] = "survives"
            row["status_at"] = "%s:%d" % (exact[0][0], exact[0][1])
        elif found:
            row["status"] = "survives as prefix"
            row["status_at"] = "%s:%d" % (found[0][0], found[0][1])


def retiring_commit(git, row, base, head):
    """The commit in base..head that removed the line, for the ledger."""
    if not row["frag"]:
        return ""
    r = git.run(["log", "--no-color", "--format=%h %ad %s", "--date=short",
                 "-S", row["frag"], "%s..%s" % (base, head)], check=False)
    lines = [l for l in r.stdout.splitlines() if l.strip()]
    return lines[0] if lines else ""


# --- report -----------------------------------------------------------------

def _cell(s):
    return (s or "").replace("|", "\\|")


def _date(ts):
    if not ts:
        return "?"
    return datetime.fromtimestamp(ts, timezone.utc).strftime("%Y-%m-%d")


def audit(git, base, head, paths, cutoff_iso):
    rows = collect(git, base, head, paths)
    blame_rows(git, rows, base)
    cutoff_ts = int(datetime.strptime(cutoff_iso, "%Y-%m-%d")
                    .replace(tzinfo=timezone.utc).timestamp())
    for row in rows:
        if row["atime"] and row["atime"] < cutoff_ts:
            row["class"] = "heritage"
            row["origin"] = "blame"
        else:
            row["class"] = "modern"
            row["origin"] = ""
        row["origin_at"] = ""
    pass_tree(git, rows, base, cutoff_iso)
    pass_history(git, rows, base, cutoff_iso)
    survival(git, rows, head)
    return rows


def report(git, rows, base, head, cutoff_iso):
    heritage = [r for r in rows if r["class"] == "heritage"]
    survive = [r for r in heritage if r["status"].startswith("survives")]
    gone = [r for r in heritage if not r["status"].startswith("survives")]

    print("# Heritage comment audit %s..%s (cutoff %s)\n" % (base, head, cutoff_iso))
    print("| text | origin file:line | author | date | class | status at HEAD | note |")
    print("|---|---|---|---|---|---|---|")
    for row in sorted(heritage, key=lambda r: (r["atime"] or 0, r["file"], r["line"])):
        klass = row["class"]
        if row["origin"]:
            klass += " (%s)" % row["origin"]
        status = row["status"]
        if row["status_at"]:
            status += " " + row["status_at"]
        note = row["origin_at"] or (row["summary"] or "")
        print("| `%s` | %s:%d | %s | %s | %s | %s | %s |" % (
            _cell(row["text_s"]), _cell(row["file"]), row["line"],
            _cell(row["author"] or "?"), _date(row["atime"]),
            _cell(klass), _cell(status), _cell(note)))

    if gone:
        print("\n## Gone — each needs a successor site or a ledger line\n")
        for row in sorted(gone, key=lambda r: (r["atime"] or 0, r["file"])):
            print("- `%s`" % row["text_s"])
            print("  | %s %s | %s:%d" % (row["author"] or "?", _date(row["atime"]),
                                         row["file"], row["line"]))
            if row["sha"]:
                print("  | born %s %s" % (row["sha"], row["summary"] or ""))
            if row["origin"] and row["origin"] != "blame":
                print("  | found by %s %s" % (row["origin"], row["origin_at"]))
            retired = retiring_commit(git, row, base, head)
            if retired:
                print("  | retired by %s" % retired)

    print("\n%d unique deleted comment lines; %d pre-cutoff; %d survive; %d gone"
          % (len(rows), len(heritage), len(survive), len(gone)))


# --- self-test --------------------------------------------------------------

_ST_ENV = {
    "GIT_CONFIG_GLOBAL": os.devnull,
    "GIT_CONFIG_SYSTEM": os.devnull,
    "GIT_CONFIG_NOSYSTEM": "1",
    "GIT_ATTR_NOSYSTEM": "1",
}


def _st_git(repo, args, when=None, who=None, env_extra=None):
    env = dict(os.environ)
    env.update(_ST_ENV)
    if when:
        env["GIT_AUTHOR_DATE"] = when
        env["GIT_COMMITTER_DATE"] = when
    if who:
        env["GIT_AUTHOR_NAME"] = who[0]
        env["GIT_AUTHOR_EMAIL"] = who[1]
        env["GIT_COMMITTER_NAME"] = who[0]
        env["GIT_COMMITTER_EMAIL"] = who[1]
    if env_extra:
        env.update(env_extra)
    r = subprocess.run(["git"] + args, cwd=repo, capture_output=True, text=True,
                       errors="replace", env=env)
    if r.returncode != 0:
        raise RuntimeError("self-test git %s failed: %s" % (args[:2], r.stderr[:300]))
    return r.stdout


def _st_write(repo, name, lines):
    with open(os.path.join(repo, name), "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines) + "\n")


DEARBORN = ("Jonathan Dearborn", "jd@example.invalid")
YAN = ("Yan", "yan@example.invalid")


def _st_build(repo):
    """Four commits, one per failure class the blame-only recipe missed."""
    _st_git(repo, ["init", "-q", "-b", "main"])
    _st_git(repo, ["config", "user.name", "t"])
    _st_git(repo, ["config", "user.email", "t@t"])

    # A (2013): the heritage tree.  gone.cpp exists only here.
    _st_write(repo, "a.cpp", ["int x;", "// heritage line", "// has | pipe",
                              "// fixme line"])
    _st_write(repo, "gone.cpp", ["int gone_marker;", "// in deleted file",
                                 "int gone_tail;"])
    _st_git(repo, ["add", "a.cpp", "gone.cpp"])
    _st_git(repo, ["commit", "-q", "-m", "A: heritage"],
            when="2013-07-28T12:00:00+0000", who=DEARBORN)

    # B (2013): the author deletes his own FIXME.  The last pre-cutoff TREE
    # therefore does not contain it — only the pre-cutoff HISTORY does.
    _st_write(repo, "a.cpp", ["int x;", "// heritage line", "// has | pipe"])
    _st_git(repo, ["add", "a.cpp"])
    _st_git(repo, ["commit", "-q", "-m", "B: drop fixme"],
            when="2013-10-19T12:00:00+0000", who=DEARBORN)

    # C (2026) = BASE: reindents the heritage line (blame moves to 2026) and
    # restores the FIXME (blame moves to 2026, and it is in no pre-cutoff tree).
    _st_write(repo, "a.cpp", ["int x;", "    // heritage line", "// has | pipe",
                              "// fixme line"])
    _st_git(repo, ["add", "a.cpp"])
    _st_git(repo, ["commit", "-q", "-m", "C: reflow and restore"],
            when="2026-01-05T12:00:00+0000", who=YAN)
    base = _st_git(repo, ["rev-parse", "HEAD"]).strip()

    # D (2026) = HEAD: the branch under audit.  a.cpp loses every comment,
    # gone.cpp is deleted outright, and the pipe comment moves to b.cpp.
    _st_write(repo, "a.cpp", ["int x;"])
    _st_write(repo, "b.cpp", ["// has | pipe"])
    _st_git(repo, ["rm", "-q", "gone.cpp"])
    _st_git(repo, ["add", "a.cpp", "b.cpp"])
    _st_git(repo, ["commit", "-q", "-m", "D: the branch under audit"],
            when="2026-02-01T12:00:00+0000", who=YAN)
    head = _st_git(repo, ["rev-parse", "HEAD"]).strip()
    return base, head


_ST_EXPECTED = [
    # The reflow class: blame says 2026, the pre-cutoff tree says otherwise.
    {"file": "a.cpp", "text": "// heritage line", "class": "heritage",
     "origin": "pre-cutoff tree", "author": "Yan", "year": "2026",
     "status": "gone", "at": ""},
    # The `|` class: a joined file|text key truncates this row out of existence.
    {"file": "a.cpp", "text": "// has | pipe", "class": "heritage",
     "origin": "blame", "author": "Jonathan Dearborn", "year": "2013",
     "status": "survives", "at": "b.cpp:1"},
    # The delete-and-restore class: in no pre-cutoff tree, only in the history.
    {"file": "a.cpp", "text": "// fixme line", "class": "heritage",
     "origin": "pre-cutoff history (deleted and restored later)",
     "author": "Yan", "year": "2026", "status": "gone", "at": ""},
    # The deleted-file class: `+++ /dev/null` must not file this under a.cpp.
    {"file": "gone.cpp", "text": "// in deleted file", "class": "heritage",
     "origin": "blame", "author": "Jonathan Dearborn", "year": "2013",
     "status": "gone", "at": ""},
]


def _st_fail(msg):
    print("FAIL self-test: %s" % msg)
    return 1


def self_test():
    with tempfile.TemporaryDirectory(prefix="heritage_audit_selftest_") as tmp:
        repo = os.path.join(tmp, "repo")
        os.makedirs(repo)
        base, head = _st_build(repo)
        git = Git(repo)
        git.env.update(_ST_ENV)
        rows = audit(git, base, head, None, CUTOFF_DEFAULT)

        if len(rows) != len(_ST_EXPECTED):
            got = ", ".join("%s|%s" % (r["file"], r["text_s"]) for r in rows)
            return _st_fail("expected %d rows got %d [%s]"
                            % (len(_ST_EXPECTED), len(rows), got))
        for exp in _ST_EXPECTED:
            name = "row %s|%s" % (exp["file"], exp["text"])
            match = [r for r in rows if r["text_s"] == exp["text"]]
            if not match:
                return _st_fail("%s missing" % name)
            if len(match) > 1:
                return _st_fail("%s matched %d rows" % (name, len(match)))
            row = match[0]
            if row["file"] != exp["file"]:
                return _st_fail("%s expected file %s got %s"
                                % (name, exp["file"], row["file"]))
            if row["class"] != exp["class"]:
                return _st_fail("%s expected class %s got %s"
                                % (name, exp["class"], row["class"]))
            if row["origin"] != exp["origin"]:
                return _st_fail("%s expected origin '%s' got '%s'"
                                % (name, exp["origin"], row["origin"]))
            if (row["author"] or "") != exp["author"]:
                return _st_fail("%s expected author %s got %s"
                                % (name, exp["author"], row["author"]))
            if _date(row["atime"])[:4] != exp["year"]:
                return _st_fail("%s expected blame year %s got %s"
                                % (name, exp["year"], _date(row["atime"])))
            if row["status"] != exp["status"]:
                return _st_fail("%s expected status %s got %s"
                                % (name, exp["status"], row["status"]))
            if row["status_at"] != exp["at"]:
                return _st_fail("%s expected survival site '%s' got '%s'"
                                % (name, exp["at"], row["status_at"]))
    print("PASS self-test (%d/%d rows)" % (len(_ST_EXPECTED), len(_ST_EXPECTED)))
    return 0


# --- main -------------------------------------------------------------------

def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--base", help="the ref the branch diverged from (merge base)")
    ap.add_argument("--head", default="HEAD", help="the branch tip (default HEAD)")
    ap.add_argument("--paths", nargs="+", default=None,
                    help="limit the audit to these paths (default: whole tree)")
    ap.add_argument("--cutoff", default=CUTOFF_DEFAULT,
                    help="heritage cutoff date, YYYY-MM-DD (default %s)" % CUTOFF_DEFAULT)
    ap.add_argument("--repo", default=".", help="repository root (default cwd)")
    ap.add_argument("--json", dest="json_out", default=None,
                    help="also dump every row to this JSON file")
    ap.add_argument("--self-test", action="store_true",
                    help="run the pipeline over a synthetic repository and exit")
    args = ap.parse_args(argv)

    if args.self_test:
        return self_test()
    if not args.base:
        ap.error("--base is required (or --self-test)")

    git = Git(args.repo)
    rows = audit(git, args.base, args.head, args.paths, args.cutoff)
    report(git, rows, args.base, args.head, args.cutoff)
    if args.json_out:
        with open(args.json_out, "w", encoding="utf-8") as fh:
            json.dump(rows, fh, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
