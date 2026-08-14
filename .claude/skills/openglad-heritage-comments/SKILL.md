---
name: openglad-heritage-comments
description: Catching and restoring heritage code comments — the narrations, signed initials, dated notes, and jokes inherited from the 2002 GPL import and Jonathan Dearborn's 2013 openglad era. Use this skill whenever a change deletes, moves, rewrites, or ports commented code (including C++ → Lua), whenever a pre-merge sweep reviews deleted comment lines, and whenever the task mentions old, nostalgic, signed, legacy, or lost comments. Style judgment cannot identify heritage — a 2013 comment reads exactly like disposable narration — so any diff that touches old files needs this skill's blame audit, even when the deleted comments "obviously" look expendable.
---

# Heritage comments

Original comments are load-bearing history: the 2002 import and Jonathan
Dearborn's 2013 maintainership are the game's written record, and the repo
rule (see openglad-pr-workflow, pre-merge sweep) is that when code moves,
is rewritten, or is ported, its comment moves with it verbatim.

The lesson this skill exists to encode: **age decides, not style — and only
`git blame` knows the age.** PR #201's rewrite deleted sixteen 2013 Dearborn
comments (`// Print authors`, `// Here are the browser variables`,
`// Rating stars`, …). A careful human-style review of the deleted lines
waved every one of them through as disposable narration, because 2013
narration is indistinguishable from 2026 narration on sight. The blame
audit caught all sixteen in one pass. Never classify by eye.

## Heritage cutoff

Treat any comment whose blame predates **2020** as heritage. In this repo
that currently means two eras: the 2002 initial revision (the GPL-era DOS
port) and Jonathan Dearborn's 2013 openglad work. The 2026 modernization
(any author, including past Claude sessions) is not heritage. When a line
is borderline or blame is ambiguous (e.g. a reflowed line re-attributed by
a formatting commit), keep the comment — restoring one comment too many
costs nothing; losing one loses history.

## The audit

Run this over any branch that rewrote or moved commented code, before
opening or squash-merging its PR. `BASE` is the comparison ref (usually
`master`).

Step 1 — collect every deleted comment line, line and block styles, with
file attribution:

```bash
BASE=master
git diff $BASE...HEAD -- '*.cpp' '*.h' '*.inc' '*.lua' | awk '
/^\+\+\+ b\// { file=$2; sub("^b/","",file) }
/^-/ && !/^---/ {
  line=$0; sub("^-","",line)
  if (line ~ /\/\// || line ~ /^\s*\/\*/ || line ~ /^\s*\*/) print file "|" line
}' > /tmp/deleted_comments.txt
```

Step 2 — blame every unique (file, text) pair on the pre-change tree and
flag pre-2020 authorship. Match by exact text in the same file: a line the
diff deleted but that never existed on `BASE` was born inside the branch
and cannot be heritage.

```bash
python3 - <<'EOF'
import subprocess, re
BASE = 'master'
lines = open('/tmp/deleted_comments.txt').read().splitlines()
seen = set()
for entry in lines:
    f, _, txt = entry.partition('|')
    txt = txt.strip()
    if len(txt) < 4 or (f, txt) in seen: continue
    seen.add((f, txt))
    r = subprocess.run(['git', 'grep', '-nF', txt, BASE, '--', f],
                      capture_output=True, text=True)
    if not r.stdout: continue
    ln = r.stdout.splitlines()[0].split(':')[2]
    b = subprocess.run(['git', 'blame', f'-L{ln},{ln}', '--porcelain', BASE, '--', f],
                       capture_output=True, text=True).stdout
    a = re.search(r'author (.+)', b); t = re.search(r'author-time (\d+)', b)
    if t and int(t.group(1)) < 1577836800:   # 2020-01-01
        print(f"{f} | {a.group(1)} | {txt}")
print(f"({len(seen)} unique deleted comment lines checked)")
EOF
```

Step 3 — deleted is not lost. A rewrite often re-creates a comment
verbatim or as a prefix of a longer line, and those survivals need no
action. `grep -cF` each flagged text in the current tree first.
Prefix survival counts: `// Prev page` preserves `// Prev`, and
`// Buttons (geometry single-sourced in picker_common ...)` preserves
`// Buttons`. Only lines absent from the new tree get restored.

## Restoring

- Place the comment **verbatim** at its successor site — the code that now
  does what the commented code did. `// Print authors` goes above whatever
  now prints the authors, wherever that moved to.
- When the rewrite's replacement comment carries real new information (a
  pixel constraint, an issue reference), keep it as a follow-on line under
  the restored heritage line. Both survive; the heritage line leads. A
  merged single line is fine when the heritage text remains its verbatim
  prefix (`// Print contributors, on the MORE row left of the button.`).
- If the commented feature was **removed** rather than moved, the comment
  goes with it. No orphan restorations — a comment above code that no
  longer does that thing is worse than the loss.
- Restore in one dedicated commit whose subject names the author/era
  (`Restore Jonathan Dearborn's 2013 campaign-browser comments ...`) and
  whose body states the audit scope ("blame audit of all N unique deleted
  comment lines found no other pre-2020 loss"). That sentence is the
  reviewer's proof the sweep was exhaustive, not spot-checked.

Reference restoration: commit `c3b300bb` (post-#201) — sixteen comments,
ten restored at successor sites, six verified as survivals.
