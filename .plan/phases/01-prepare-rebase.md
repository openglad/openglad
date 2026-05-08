# Phase 1 — Prepare Rebase

## Phase Name
`prepare-rebase`

## Implement Phase ID
`01-prepare-rebase`

## Preexisting Inputs
- `.plan/goal.md` — authoritative goal text. Read, do not rewrite.
- `.plan/plan.md` — drives the workflow.
- `.plan/findings.md` — currently contains stale notes from a superseded planning iteration; will be overwritten in this phase.
- `.plan/plan-before-cleanup.md`, `.plan/workflow-structure.yaml`, `.plan/verification-notes.md` (if present), and every file under `.plan/phases/` — pre-existing planning artifacts whose current content is captured by the prep commit as-is.
- Repo-root `.juvenal-state.json` — tracked with `skip-worktree`; working-tree blob currently differs from HEAD blob (`810c6aff…` vs `5522294f…`) and is the active blocker for `git rebase`.
- `origin/master` ref (re-fetched at the start of this phase).
- Existing `wip/networking` branch HEAD `cd4248e7 rebase: fixup build after master rebase` with 327 commits ahead of merge-base `fe2109a6`.

## New Outputs
- One commit on `wip/networking` with subject prefix `rebase-prep:` staging every modified or newly-written `.plan/` path: `.plan/.juvenal-state.json`, `.plan/goal.md`, `.plan/plan.md`, `.plan/plan-before-cleanup.md`, `.plan/workflow-structure.yaml`, `.plan/findings.md`, `.plan/verification-notes.md` (if present), and every file under `.plan/phases/`.
- Updated `.plan/findings.md` containing only a `## Pre-rebase snapshot` section (stale prior content discarded).
- Repo-root `.juvenal-state.json` working-tree blob restored to match the HEAD blob (not committed; `skip-worktree` re-applied).

## File Changes
- Repo-root `.juvenal-state.json`: working-tree blob restored to match the HEAD blob via `update-index --no-skip-worktree` → `git checkout HEAD --` → `update-index --skip-worktree`. **Not committed.** Verifier asserts blob equality (`git status` lies under `skip-worktree`).
- `.plan/findings.md`: overwritten (not appended) with a single `## Pre-rebase snapshot` section recording `pre_rebase_HEAD = <sha>`, `origin_master = <sha>`, `merge_base = <sha>`, `ahead_behind = <ahead>/<behind>`, `conflict_files = src/gameplay/families/effect_family_ghost_scare.cpp`, and `auto_merge_files_requiring_post_rebase_migration = tests/test_walker_specials.cpp (whole-file accessor migration; master inserts at two regions, near merge-base line 651 and at the file tail)`.
- All listed `.plan/` paths staged and committed under one prep commit.

## Implementation Details
1. `git fetch origin master`.
2. Clear the `.juvenal-state.json` blocker:
   ```bash
   git update-index --no-skip-worktree .juvenal-state.json
   git checkout HEAD -- .juvenal-state.json
   git update-index --skip-worktree .juvenal-state.json
   ```
   After this, `git hash-object .juvenal-state.json` must equal `git rev-parse HEAD:.juvenal-state.json` and `git ls-files -v .juvenal-state.json` must still show `S `.
3. Capture: `HEAD_SHA=$(git rev-parse HEAD)`, `MASTER_SHA=$(git rev-parse origin/master)`, `BASE_SHA=$(git merge-base HEAD origin/master)`, `AHEAD_BEHIND=$(git rev-list --left-right --count HEAD...origin/master)`.
4. Overwrite `.plan/findings.md` (do not append) with a single `## Pre-rebase snapshot` block containing those values plus the conflict file list and the test-file migration note.
5. `git add .plan/.juvenal-state.json .plan/goal.md .plan/plan.md .plan/plan-before-cleanup.md .plan/workflow-structure.yaml .plan/findings.md` and `git add .plan/phases/`. If `.plan/verification-notes.md` exists, include it. Do not use `git add -A` or `git add .`. Do not stage anything outside `.plan/` (no CMake build outputs, no `compile_commands.json`, no `.cache/`, no repo-root `.juvenal-state.json`).
6. `git commit -m "rebase-prep: stage plan artifacts before rebase onto origin/master"`.
7. Confirm `git status --porcelain | grep -v '^?? '` is empty AND the `.juvenal-state.json` blob equality check from step 2 still holds before yielding.

## Verification Phases
- **`01a-check-prepare-rebase`** — type `check`, `bounce_target: 01-prepare-rebase`. Purpose: assert the working tree is clean, the `.juvenal-state.json` blocker is cleared, the prep commit landed and touched only `.plan/` paths, `findings.md` carries the new snapshot section, the branch is exactly `behind=1, ahead=328`, and no rebase is in progress. Runs each command below and bounces on the first non-zero exit:
  ```bash
  set -e
  git fetch origin master
  dirty=$(git status --porcelain | grep -v '^?? ' || true)
  [ -z "$dirty" ] || { echo "FAIL: tracked changes still present:"; echo "$dirty"; exit 1; }
  wt_blob=$(git hash-object .juvenal-state.json)
  head_blob=$(git rev-parse HEAD:.juvenal-state.json)
  [ "$wt_blob" = "$head_blob" ] || { echo "FAIL: .juvenal-state.json wt=$wt_blob head=$head_blob (rebase will abort)"; exit 1; }
  git ls-files -v .juvenal-state.json | grep -q '^S ' || { echo "FAIL: skip-worktree was unset on .juvenal-state.json"; exit 1; }
  nonplan=$(git log -1 --name-only --pretty=format: | sed '/^$/d' | grep -v '^\.plan/' || true)
  [ -z "$nonplan" ] || { echo "FAIL: prep commit touched non-.plan paths:"; echo "$nonplan"; exit 1; }
  git log -1 --pretty=format:%s | grep -q '^rebase-prep:' || { echo "FAIL: prep commit subject mismatch"; exit 1; }
  grep -q '^## Pre-rebase snapshot' .plan/findings.md || { echo "FAIL: snapshot section missing"; exit 1; }
  counts=$(git rev-list --left-right --count HEAD...origin/master)
  ahead=$(echo "$counts" | awk '{print $1}')
  behind=$(echo "$counts" | awk '{print $2}')
  [ "$behind" = "1" ] || { echo "FAIL: expected behind=1, got $behind"; exit 1; }
  [ "$ahead" = "328" ] || { echo "FAIL: expected ahead=328 (327 originals + prep), got $ahead"; exit 1; }
  [ ! -d .git/rebase-merge ] && [ ! -d .git/rebase-apply ] || { echo "FAIL: rebase already in progress"; exit 1; }
  echo "OK"
  ```

## Success Criteria
- `git status --porcelain | grep -v '^?? '` is empty.
- `git hash-object .juvenal-state.json == git rev-parse HEAD:.juvenal-state.json`, and `skip-worktree` is still set.
- The most recent commit's subject begins with `rebase-prep:` and touches only `.plan/` paths.
- `.plan/findings.md` contains the `## Pre-rebase snapshot` section and no stale prior content.
- Branch is exactly `behind=1, ahead=328` versus `origin/master`.
- No `.git/rebase-merge/` or `.git/rebase-apply/` directory present.

## Git Commit Requirement
The implementer MUST commit the prep work to git (one commit, subject prefixed `rebase-prep:`) before yielding. The commit must stage only the listed `.plan/` paths. After committing, confirm `git status --porcelain | grep -v '^?? '` is empty and the `.juvenal-state.json` blob still matches HEAD before returning.
