---
name: openglad-pr-workflow
description: Definition of done for any OpenGlad branch or PR — riding CI to green, completion-report honesty, pre-merge sweeps (nostalgic comments, doc litter), and the web preview delivery paths. Use whenever you push, open a PR, report a task complete, or answer "is CI green" / "which build am I playing".
---

# OpenGlad PR workflow

## Definition of done (non-negotiable)

A turn that pushed code ends only when every check on that push has
CONCLUDED. Block on `gh pr checks <n> --watch` (or poll `gh run list
--commit <sha>`); report the final counts. "CI is running" is not a
completion report.

- Expected wall-clock: ASan+UBSan ~35 min, Coverage ~21–34 min, Campaign
  Regeneration Drift ~2 min. Slower than that is "slow", not "stuck" —
  keep waiting.
- Every push to a PR branch produces DUPLICATE push-triggered and
  pull_request-triggered runs on the same SHA; concurrency rules cancel
  the redundant one. Dedupe by SHA + workflow name. A canceled duplicate
  is NOT a failure — never report it as one.
- Known flake: the wasm-jitter E2E on PR runs. Remedy: `gh run rerun <id>
  --failed`, then PROVE it green. A flake explained away is a red check.
- Report on state change only (first red, final verdict) — not a
  narration of every poll.

## Playtest-report response protocol (paid for by four failed twitcher fixes)

A human report of in-game misbehavior ("units stuck twitching") is an
OBSERVATION, not a diagnosis. The four-round fumble pattern to never
repeat: theorize from code reading -> patch the theory -> build a test
around the same theory -> pass -> declare victory -> user sees the same
bug. Rules:

1. REPRODUCE UNDER OBSERVATION FIRST. Before any fix: dump live entity
   state at the reported spot (openglad_text --protocol `state`), or
   pixel-diff capture frames there. The cheap observability tools find
   in seconds what hypothesis-stacking misses for days.
2. PIN THE OBSERVATION. If the report is ambiguous, ask ONE question up
   front — which units (color/family), where, doing what — instead of
   burning a fix-deploy-playtest round per guess. "Yellow guys sitting
   in open grass ignoring an adjacent enemy" identified in one sentence
   what four patches never touched.
3. THE REGRESSION TEST MUST FAIL FIRST — on the tree the user reported
   against, reproducing THEIR scenario (their position, crew shape,
   timing), not a scenario shaped by your current theory. A net that
   passes pre-fix has proven it does not cover the bug; do not ship the
   fix it "validates". Teeth are red-then-green, nothing less.
4. NO EXTINCTION CLAIMS. Never state a bug class "cannot recur" from
   tests that never reproduced the user's report. Completion reports
   state what the test actually exercises.
5. EPICYCLE ALARM. If a fix needs a fix that needs a fix (posted ->
   re-post cycle -> distance carve-out -> geometry), stop patching and
   map the full state machine you are scripting around — every
   transition and its INPUTS — then verify each input for EVERY unit
   class you touch. Placed and generator-spawned units differ in
   derived stats; a rule proven on one class silently fails on the
   other.

## Banned dispositions

"Deferred", "out of scope", "follow-up issue", "deliberate limitation",
"acceptable for now" are NOT valid outcomes for anything the user named.
A gate turned off, defaulted off, or narrowed is not a fixed gate. If a
named item looks infeasible, ask IN THE TURN YOU DISCOVER IT, with a cost
estimate — never in the completion report. Completion reports list every
user-named item as done/not-done; not-done requires the user's prior
sign-off, quoted.

## Pre-merge sweep (before opening or squash-merging any PR)

1. Nostalgic comments: original Gladiator-era comments (signed initials,
   dated notes, jokes, "this is a hack because…") are load-bearing
   heritage. When code moves, is rewritten, or is ported (including
   C++ → Lua), the comment moves with it verbatim. Check:
   `git diff master...HEAD -- '*.cpp' '*.h' '*.lua' | grep '^-.*//'`
   and review every deleted comment line. BEFORE the merge, not after.
2. Doc litter: agent-facing artifacts (plans, phase reports, audit
   summaries, handoff notes) never live in the tree — scratchpad only.
   Check `git diff --stat master...HEAD -- '*.md'`; anything named
   *_PLAN.md, *_REPORT.md, *_SUMMARY.md, PHASE*, HANDOFF* is deleted.
   A doc worth keeping goes to docs/ as a human-facing document.
3. `git status --porcelain` is empty of surprises.

## Web preview delivery paths (know which build the user is playing)

| URL | Built by | Trigger |
|---|---|---|
| openglad.pages.dev | wasm-e2e.yml (production deploy step) | master pushes only |
| pr-<n>.openglad.pages.dev | wasm-e2e.yml (PR preview step) | every push to an open same-repo PR |
| local tunnel | scripts/refresh_web_preview.sh + cloudflared | manual |

Before explaining why a user "sees the old build": read the git-hash
stamp on the main menu of the build THEY named and compare to branch
HEAD. Never reason about caching from first principles. Always hand back
preview URLs with the commit SHA they serve.
