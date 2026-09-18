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
the history knows the age.** PR #201's rewrite deleted sixteen 2013 Dearborn
comments (`// Print authors`, `// Here are the browser variables`,
`// Rating stars`, …). A careful human-style review of the deleted lines
waved every one of them through as disposable narration, because 2013
narration is indistinguishable from 2026 narration on sight. The blame
audit caught all sixteen in one pass. Never classify by eye — and never by
`git blame` alone either, which PR #292 showed finds only half of them.

## Heritage cutoff

Treat any comment whose blame predates **2020** as heritage. In this repo
that currently means two eras: the 2002 initial revision (the GPL-era DOS
port) and Jonathan Dearborn's 2013 openglad work. The 2026 modernization
(any author, including past Claude sessions) is not heritage. When a line
is borderline or blame is ambiguous (e.g. a reflowed line re-attributed by
a formatting commit), keep the comment — restoring one comment too many
costs nothing; losing one loses history.

## The audit

Run this over any branch that rewrote or moved commented code, before opening
or squash-merging its PR:

```bash
python3 scripts/heritage_audit.py --base <merge-base> --paths src include tests
```

`--base` is the ref the branch diverged from (`git merge-base origin/master
HEAD`); `--head` defaults to HEAD, `--paths` to the whole tree, `--cutoff` to
2020-01-01. It reads history and prints a report — it changes nothing and
always exits 0. Expect minutes on a branch-sized diff, and note it needs the
real history: a shallow clone cannot blame.

The script collects every comment line the diff deleted (a *reworded* comment is
in that set too — a unified diff shows it as its deleted half), blames each one
on the base tree with `git blame -C -C -C`, runs the two text-identity passes
below, then looks for each line in the head tree. One row per unique
(file, text) pair:

| column | meaning |
|---|---|
| text | the deleted line, verbatim |
| origin file:line | where it was at `--base` |
| author / date | who wrote it, per the pass that found it |
| class | `heritage (blame)`, `heritage (pre-cutoff tree)`, `heritage (pre-cutoff history …)` — the mechanism that dated it |
| status at HEAD | `survives`, `survives as prefix`, or `gone`, with the file:line |
| note | the commit that wrote the line, or where the identity pass found it |

Under the table, every **gone** heritage row is printed again with its birth
commit and the commit on this branch that retired it — that block is the
ledger a restoration commit body needs. The last line is the count:
`N unique deleted comment lines; H pre-cutoff; S survive; G gone`.

Deleted is not lost, and prefix survival counts: a rewrite that folded
`// Prev page` into `// Prev page (geometry single-sourced in picker_common …)`
preserved it, and the row says `survives as prefix`. Only `gone` rows need
action.

Work the gone rows, in this order: is there a live successor site (restore
verbatim there), is the rule itself gone (the comment goes with it, ledgered),
or does a live twin already carry the text (nothing to do)?

### Why blame alone is not enough

`git blame -C -C -C` on PR #292's deleted comment lines reported **19** pre-2020
lines. The true number was **36** — out of 353 unique deleted comment lines under
`src/`-and-friends and 1566 under `tests/`, the counts that PR's restoration
commit records as 354 and 1627 (it had not yet applied the four-character floor,
and it counted `#include` in a `.cpp` file as a comment). Blame under-reported by
17, in two ways, and the recipe that preceded this script lost a third set of
lines before blame ever saw them:

- **Reflow / move re-attribution (15 lines).** A 2026 commit that reindented or
  relocated the line owns the blame. `// Get current number of foes` blames to
  2026 and is verbatim `glad.cpp:601` from the 2002 tree. The script catches
  this by comparing the comment **fragment** (the text from `//` onward, so a
  changed indent does not matter) against the last pre-cutoff tree.
- **Delete-and-restore re-attribution (2 lines).** Jonathan Dearborn wrote
  `// FIXME: SDL does not have keyboard customization, so we can't make newlines
  with RETURN.` and `// I need to either modify SDL or add click/touch text
  navigation.` in 2013 (`03882de0`), deleted them himself later that year
  (`3102863c`), and a 2026 commit restored them (`19d7eeb3`). Blame says 2026
  and the lines are in no pre-cutoff *tree*, so the script also compares against
  the union of every comment fragment that ever existed pre-cutoff, built from
  the patch text of the whole pre-cutoff history.
- **Deleted files.** A deleted file's hunks carry `+++ /dev/null`, so file
  attribution must come from `--- a/`. Keying off `+++ b/` alone files every
  one of its lines under whichever file preceded it in the diff — on that PR,
  every comment line of the three deleted test files, blamed against a file
  that never held them and so reported as born in the branch.

The two identity passes are deliberately over-inclusive, and the `class` column
is why: a row that says `heritage (pre-cutoff tree)` is a candidate you
adjudicate, not a verdict. A short fragment collides with old vocabulary —
`// charge`, `// right`, `// eat corpse` are 2026 trailing comments on test
lines, and every one of them is also a comment somewhere in the 2013 tree. That
is 30 rows on PR #292's `tests/` half, all of them declined, against zero
heritage lines missed. Restoring one comment too many costs nothing.

Those three classes, plus a comment containing a `|`, are the four rows of
`python3 scripts/heritage_audit.py --self-test` (ctest: `heritage_audit_selftest`),
which is what keeps a pass from silently degrading into a shorter report.

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
- The successor site is the live implementation of the same rule **in any
  language**: the retired touch arms' comments belong over the web overlay's
  handlers in `web/shell.html`, not nowhere, because that is where the rule
  lives now.
- **Mechanism identity decides, not topic**: `// Treat KEY_SHIFTER as an action
  instead of a modifier` has no successor over a sticky-modifier latch, because
  a latch is the opposite mechanism, however close the subject matter.
- When the feature is gone for good, its comment is ledgered **line by line in
  the restoration commit body** (text, author, date, origin file:line, retiring
  commit, why no successor exists) — never a docs ledger file, which is an
  orphan with a filename.
- A **2026** comment that narrates DOS-era lineage is not heritage by age, but
  its durable fact may be kept once as a plain gloss under the surviving
  heritage line it explains — never behind that author's signature prefix
  (`//buffers:`) and never naming a symbol the branch deleted.

Reference restorations: commit `c3b300bb` (post-#201) — sixteen comments, ten
restored at successor sites, six verified as survivals; and commit `097407ba`
(PR #292, `X4-R5-restore-heritage-sites`) — nine lines restored across four
files and two languages, eight ledgered as gone with their retired feature, one
covered by its live twin.
