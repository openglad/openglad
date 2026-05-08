# Plan — Rebase `wip/networking` onto `origin/master`

## 1. Context

### Goal

`.plan/goal.md` literally reads: `rebase this on origin/master`. The current branch is `wip/networking` and must be rebased onto `origin/master`.

### Branch state at plan time

- `HEAD` = `cd4248e7 rebase: fixup build after master rebase` on `wip/networking`.
- `origin/master` HEAD = `16963de0 Fix ghost scare special and add regression tests for every special (#109)` (PR #109, May 7 2026).
- Common merge-base = `fe2109a652a63c800a4e6cc9a6bec8f77cf1d75d` (`Replace custom .pix graphics format with standard PNG (#28)`).
- `git rev-list --left-right --count HEAD...origin/master` = `327	1`. The branch is 327 commits ahead; `origin/master` has exactly one new commit (`16963de0`) since the merge-base.

### Exact diff `origin/master` introduces relative to merge-base

`git diff fe2109a6 origin/master --stat`:

```
src/gameplay/families/effect_family_ghost_scare.cpp |    2 +-
tests/test_walker_specials.cpp                      | 1095 ++++++++++++++++++++
2 files changed, 1096 insertions(+), 1 deletion(-)
```

Two files. `git merge-tree --merge-base=fe2109a6 HEAD origin/master` reports:

```
Auto-merging src/gameplay/families/effect_family_ghost_scare.cpp
CONFLICT (content): Merge conflict in src/gameplay/families/effect_family_ghost_scare.cpp
Auto-merging tests/test_walker_specials.cpp
```

**Only `src/gameplay/families/effect_family_ghost_scare.cpp` will pause the rebase with a conflict.** `tests/test_walker_specials.cpp` auto-merges; per-commit rebase replay is even less likely to conflict than this single 3-way merge.

#### Master's diff against `tests/test_walker_specials.cpp` has TWO insertion regions

`git diff fe2109a6 origin/master -- tests/test_walker_specials.cpp` produces exactly two hunks:

```
@@ -645,6 +645,115 @@ TEST_F(WalkerSpecials, ghost_scare)        ← inserts 109 lines INSIDE the existing ghost_scare test region
@@ -1401,3 +1510,989 @@ TEST_F(WalkerSpecials, success_returns_true_and_spends_mp)   ← inserts 989 lines after the existing tail
```

- **First region — 109 lines, inserted at merge-base line 651** (immediately after the existing `ghost_scare` test): adds two new tests, `TEST_F(WalkerSpecials, ghost_scare_animates_and_dissipates)` and `TEST_F(WalkerSpecials, ghost_scare_does_not_accumulate)`. These exercise `fx->ani_type`, `fx->cycle`, `fx->dead`, `e->family`, `e->dead`, `w->current_special = ...` etc.
- **Second region — 989 lines, appended after merge-base line 1403**: adds a `tick_world(int n)` helper, a `place_stain_near()` helper, and 40 `TEST_F(WalkerSpecials, ...)` regression cases (42 new TEST_F cases total counting the two from the first region). These exercise the full audit-table set of public-field touches.

Master does **not** add a `session_state.h` include — verified: `git show origin/master:tests/test_walker_specials.cpp | grep session_state` is empty. The branch already added `#include <openglad/interface/session_state.h>` near the top of the file.

Both regions use public-field syntax (`w->family`, `w->busy = X`, `w->current_special = X`, `w->dead`, `w->xpos`, `w->ypos`, `w->stats()->magicpoints = X`, `w->stats()->level`, `marker->owner = w`, `marker->lifetime = N`, `marker->dead = 0`, `w->stats()->special_cost[i] = X`, `w->sizex`, `w->sizey`, `fx->ani_type`, `fx->cycle`, `w->lastx = w->stepsize`, `w->view_all`, `w->charm_left`, `w->shifter_down`, `w->curdir`, `w->lasty`, `w->stats()->frozen_delay`, etc.) that the branch privatized into accessor pairs. The auto-merge produces a file that is on disk and indexed clean after rebase, but it will not compile until **every** master-inserted public-field reference is rewritten to the accessor form. Phase 2 owns that mechanical migration as a post-rebase step (see Phase 2 implementation details).

The branch's modified version of the merge-base region spans 1–1483 (the branch added 80 net lines vs. merge-base's 1403). After rebase the file ends around line 2578: branch lines (already accessor-migrated) up to roughly 1483, then master's 989-line second-region append. Master's first-region 109-line insertion sits in the middle — around branch line 720, immediately after the existing `ghost_scare` test. Therefore: **the migration must cover every public-field reference master inserts, regardless of where in the file it lands.** Scoping the migration by line number (e.g. "lines ≥1404") is wrong and will silently skip the first region. The plan deliberately uses an idempotent whole-file substitution rule: each substitution rewrites public-field syntax to accessor syntax, and applies as a no-op on lines the branch already migrated, so applying it across the entire file is safe.

### Conflict — `src/gameplay/families/effect_family_ghost_scare.cpp`

Three-way diff at the merge-base (verified locally):

- **`origin/master` change (1 line):** `ghost_scare_on_act()` returns `false` instead of `true`, so `effect::act()` falls through to the default animate-and-die path. Previous text:
  ```
  return true; // handled, fall through to animate/die
  ```
  Master text:
  ```
  return false; // delegate to effect::act default animate/die path
  ```
- **First branch commit to touch this file:** `6d6b3a06 Phase 3: privatize owner cross-references`. Its hunk in `ghost_scare_on_act()` rewrites `self->owner` → `self->owner()` and `self->center_on(self->owner)` → `self->center_on(self->owner())` on the two lines immediately above the `return true` line. Master's `return true` → `return false` lives in those same hunk's context window, which is why the cherry-pick conflicts even though no single line is touched by both sides.
- **Later branch commits (`0b2258c1 Phase 8: enforce dirty tracking through setters`, `98e906b3 Phase 3: privatize foe cross-references`, others):** propagate `self->owner->dead` → `self->owner()->dead()`, `self->owner->stats()->level` → `self->owner()->stats()->level()`, `w->xpos` → `w->xpos()`, `w->ypos` → `w->ypos()` in `ghost_scare_on_death()`. These hunks are far below the conflict region in `ghost_scare_on_act()` and replay cleanly once `6d6b3a06` is resolved.

The conflict in `ghost_scare_on_act()` resolves as follows: keep the branch's accessor form (`self->owner()`, `self->center_on(self->owner())`) and apply master's `return false; // delegate to effect::act default animate/die path` line. After resolving `6d6b3a06`, no further branch commits should reconflict in this file; if any does, that means an unanticipated overlap and the implementer must yield with the rebase paused.

### Test-file migration — `tests/test_walker_specials.cpp`

Because rebase auto-merges this file (no operator pause), the migration cannot be performed during a `git rebase` conflict-resolution prompt. Phase 2 instead performs a one-shot scripted migration over the **entire file** immediately after `git rebase` reports completion, and lands it as one extra commit on top of the rebased branch. The rule is explicitly **whole-file**, not line-range-scoped, because master's 109-line first-region insertion sits in the middle of the file (around branch line 720) and a line-range scoped migration would silently skip it. The substitutions are idempotent: every rule is "rewrite public-field touch X to accessor touch X()" and is a no-op on already-migrated branch lines.

Substitution table (every rule applies to the whole file, with the read/write-side discrimination noted):

| Master construct (post-rebase, anywhere in file) | Branch-accessor form |
|---|---|
| `w->family` (read site — RHS, condition, comparison) | `w->family()` |
| `w->busy = X` (write) | `w->set_busy(X)` |
| `w->busy` (read) | `w->busy()` |
| `w->current_special = X` (write) | `w->set_current_special(X)` |
| `w->current_special` (read) | `w->current_special()` |
| `w->dead = X` (write — e.g. `marker->dead = 0`) | `w->set_dead(X)` |
| `w->dead` (read) | `w->dead()` |
| `w->xpos = X` (write) | `w->set_xpos(X)` |
| `w->xpos` (read) | `w->xpos()` |
| `w->ypos = X` (write) | `w->set_ypos(X)` |
| `w->ypos` (read) | `w->ypos()` |
| `w->sizex` (read — used in passability checks) | `w->sizex()` |
| `w->sizey` (read — used in passability checks) | `w->sizey()` |
| `w->user` (read) | `w->user()` |
| `w->team_num` (read) | `w->team_num()` |
| `w->real_team_num` (read) | `w->real_team_num()` *(verify accessor name with grep on `sim_entity.h` / `walker.h` — substitute the accessor that exists)* |
| `w->owner = X` (write — e.g. `marker->owner = w`) | `w->set_owner(X)` |
| `w->owner` (read) | `w->owner()` |
| `w->lifetime = X` (write — e.g. `marker->lifetime = 1`) | `w->set_lifetime(X)` |
| `w->lifetime` (read) | `w->lifetime()` |
| `w->ani_type = X` (write) | `w->set_ani_type(X)` |
| `w->ani_type` (read — e.g. `fx->ani_type == ANI_SCARE`) | `w->ani_type()` |
| `w->cycle = X` (write) | `w->set_cycle(X)` |
| `w->cycle` (read) | `w->cycle()` |
| `w->curdir = X` (write) | `w->set_curdir(X)` |
| `w->curdir` (read) | `w->curdir()` |
| `w->lastx = X` (write — e.g. `w->lastx = w->stepsize`) | `w->set_lastx(X)` |
| `w->lastx` (read) | `w->lastx()` |
| `w->lasty = X` (write) | `w->set_lasty(X)` |
| `w->lasty` (read) | `w->lasty()` |
| `w->stepsize = X` (write) | `w->set_stepsize(X)` |
| `w->stepsize` (read — e.g. RHS of `w->lastx = w->stepsize`) | `w->stepsize()` |
| `w->view_all = X` (write) | `w->set_view_all(X)` |
| `w->view_all` (read) | `w->view_all()` |
| `w->charm_left = X` (write) | `w->set_charm_left(X)` |
| `w->charm_left` (read) | `w->charm_left()` |
| `w->shifter_down = X` (write) | `w->set_shifter_down(X)` |
| `w->shifter_down` (read) | `w->shifter_down()` |
| `w->stats()->magicpoints = X` (write) | `w->stats()->set_magicpoints(X)` |
| `w->stats()->magicpoints` (read) | `w->stats()->magicpoints()` |
| `w->stats()->max_magicpoints = X` (write) | `w->stats()->set_max_magicpoints(X)` |
| `w->stats()->max_magicpoints` (read) | `w->stats()->max_magicpoints()` |
| `w->stats()->hitpoints = X` (write) | `w->stats()->set_hitpoints(X)` |
| `w->stats()->hitpoints` (read) | `w->stats()->hitpoints()` |
| `w->stats()->max_hitpoints = X` (write) | `w->stats()->set_max_hitpoints(X)` |
| `w->stats()->max_hitpoints` (read) | `w->stats()->max_hitpoints()` |
| `w->stats()->level = X` (write) | `w->stats()->set_level(X)` |
| `w->stats()->level` (read) | `w->stats()->level()` |
| `w->stats()->frozen_delay = X` (write) | `w->stats()->set_frozen_delay(X)` |
| `w->stats()->frozen_delay` (read) | `w->stats()->frozen_delay()` |
| `w->stats()->old_family = X` (write) | `w->stats()->set_old_family(X)` |
| `w->stats()->old_family` (read) | `w->stats()->old_family()` |
| `w->stats()->special_cost[i] = X` (subscript-write) | `w->stats()->set_special_cost(i, X)` |
| `w->stats()->special_cost[i]` (subscript-read) | `w->stats()->special_cost(i)` |
| `w->stats()->commands` (read or write) | unchanged — `commands` is still a public `std::list<command>` on `statistics` (verified at `include/openglad/gameplay/statistics.h:149`). The implementer must `grep -n 'commands' include/openglad/gameplay/statistics.h` at migration time to re-confirm before substituting. |
| `w->setxy(...)` | unchanged — still a public method on `walker` (verified at `include/openglad/gameplay/walker.h:94–98`). |
| `w->myguy`, `w->myscreen_`, `w->world`, `w->act(...)`, `w->center_on(...)`, `w->special()`, `w->stats()` (method/public access) | unchanged — verified public/method on the branch. |

**Operating rule for any field NOT in this table** that the implementer encounters while applying the migration or fixing build failures: `grep -n '<name>' include/openglad/gameplay/walker.h include/openglad/gameplay/statistics.h include/openglad/gameplay/sim_entity.h`. If the field appears inside an `OG_WALKER_DIRTY_FIELD` / `OG_STATS_DIRTY_FIELD` macro, or as a private member with a setter/getter pair, migrate it to the accessor form; otherwise leave it as a public field touch. This is the canonical decision procedure when extending the table during Phase 3 or Phase 4 fixups.

Because the migration is whole-file and idempotent, the implementation may use `sed -i` (or equivalent) once over the whole file rather than computing or using a line-number boundary. Branch-side lines are already accessor-form, so the substitutions match nothing on them.

### Repo-root `.juvenal-state.json` blocker

`git ls-files -v .juvenal-state.json` reports `S .juvenal-state.json` — the **repo-root** file is tracked with the `skip-worktree` flag set, and the working-tree blob (`810c6aff…`) currently differs from the HEAD blob (`5522294f…`):

```
$ git hash-object .juvenal-state.json
810c6aff99863eadcd7052f5e5abc269016997d0
$ git rev-parse HEAD:.juvenal-state.json
5522294f7252c829b97dff90fc0b706b41a4cfe3
```

`git status --porcelain` reports nothing because of `skip-worktree`, but `git rebase origin/master` aborts with:

```
error: Your local changes to the following files would be overwritten by checkout:
	.juvenal-state.json
Please commit your changes or stash them before you switch branches.
Aborting
error: could not detach HEAD
```

This is reproducible empirically. **Phase 1 must restore the working-tree content to match the HEAD blob before the rebase can start.** The chosen remediation (Phase 1 implementation details) is:

```bash
git update-index --no-skip-worktree .juvenal-state.json
git checkout HEAD -- .juvenal-state.json
git update-index --skip-worktree .juvenal-state.json
```

This leaves `skip-worktree` set (preserving the user's intent that this file be ignored locally), produces no new commits, and unblocks the rebase. The Phase 1 verifier asserts the blocker is gone via `[ "$(git hash-object .juvenal-state.json)" = "$(git rev-parse HEAD:.juvenal-state.json)" ]` rather than relying on `git status` (which lies while `skip-worktree` is set). Branch commits that touch `.juvenal-state.json` (e.g. `56d92d4c`, `44c728c6`, `d5bf7631`, `0c53fecd`, `14b8f133`) replay normally during rebase — `skip-worktree` keeps the working file untouched while the index advances.

### Strategy

- The cleanest replay path is a single `git rebase origin/master`. Because only one master commit (`16963de0`) sits between the merge-base and `origin/master`, conflicts surface during replay only when a branch commit touches `src/gameplay/families/effect_family_ghost_scare.cpp` — concretely the **first** branch commit to touch it (`6d6b3a06 Phase 3: privatize owner cross-references`). After that resolution, the remaining commits replay without re-conflicting. The test file auto-merges; its master-inserted content (in both regions) is migrated as a post-rebase one-shot whole-file commit.
- An alternative "merge-then-flatten" form (cherry-pick `16963de0` onto HEAD, resolve once against the branch tip) is not adopted: the user's literal request is `rebase`, and we want honest linear history.
- Working tree currently has unstaged modifications under `.plan/` (`goal.md`, `plan.md`, `plan-before-cleanup.md`, `workflow-structure.yaml`, `.juvenal-state.json` inside `.plan/`) plus untracked CMake build artifacts and untracked phase files. Before rebasing, the planning artifacts must be committed. **`git rebase` will refuse to start while tracked files remain dirty**, so Phase 1 produces a single prep commit that stages every modified-or-newly-written `.plan/` path. Untracked build artifacts at the repo root (`CMakeCache.txt`, `Makefile`, `*.a`, `openglad_*`, `compile_commands.json`, `.cache/`, `third_party/ixwebsocket/CMakeFiles/`) stay untracked — `git rebase` ignores untracked files unless they would be overwritten by the replay (none of master's changes touch repo-root untracked paths).

### Existing artifacts to consume (do not regenerate)

- `.plan/goal.md` — authoritative goal text. Read, do not rewrite.
- `.plan/plan.md` (this document) — drives the workflow; not regenerated during execution.
- `.plan/findings.md` — currently contains review notes from a superseded planning iteration. Phase 1 **overwrites** this file with a fresh `## Pre-rebase snapshot` section so the prep commit does not bake stale review notes into the rebased history. Phase 5 then appends a `## Final rebase summary` section.
- `.plan/phases/01-restore-sim-cadence.md` … `05-e2e-speed-verification.md`, `plan-before-cleanup.md`, `verification-notes.md`, `workflow-structure.yaml` — pre-existing planning artifacts. The planner that converts this `plan.md` into a fresh `.plan/phases/*.md` and `.plan/workflow-structure.yaml` runs **before** Phase 1; whatever content sits in those paths at the moment Phase 1 executes is what the prep commit captures. The rebase workflow itself does not modify their content; it only commits them so the rebase can proceed on a clean tree.
- Existing branch commits (327 networking commits, the most recent being `cd4248e7 rebase: fixup build after master rebase`). Do **not** squash, reorder, drop, or amend any of them. The rebase preserves history exactly except for replay onto the new base.
- `cfg/openglad.yaml`, `CMakeLists.txt`, `cmake/presets`, `tests/integration_main.cpp`, `tests/unit/unit_main.cpp` — referenced for build/test invocation only; not edited.
- The local CMake build directory under the repo root (untracked `CMakeCache.txt`, `Makefile`, `*.a`, `openglad_*` binaries) is reused for verification builds. Do not delete it.

### Out of scope

- No code refactoring beyond what is required to resolve the source-file conflict and migrate master-inserted public-field references in the test file.
- No squashing, fixup-merging, or rewriting of the 327 branch commits.
- No changes to CI configuration, presets, or third-party vendored libraries.
- No update to `docs/ARCHITECTURE.md`, `CLAUDE.md`, or other docs even if stale paths (e.g. `src/entities/families/` vs `src/gameplay/families/`) are noticed during review — those are doc bugs unrelated to this rebase.

## 2. Generated Workflow Contract

The generated workflow that executes this plan **must** obey these rules. They are non-negotiable and bind every later planner pass that converts this document into `.plan/phases/*.md` and `.plan/workflow-structure.yaml`:

- **Linear execution only.** No `parallel_groups`. Phases run strictly sequentially in the order numbered in section 3.
- **Self-contained inline-only YAML.** No top-level `include`. No phase-level `prompt_file`, `workflow_file`, `workflow_dir`, or `checks` indirection. Every prompt body is inlined into `workflow-structure.yaml`.
- **No agent-guided `bounce_targets` lists.** Every verifier uses a single fixed `bounce_target` field pointing at exactly one implement phase ID.
- **Every verifier is an explicit top-level `check` phase.** Verification logic must not be embedded inside an implement phase's prompt.
- **Each verifier stays in the implement block it verifies and bounces only to that implement phase.** No cross-block bounces; if Phase 3 fails verification it bounces only to Phase 3's implement, never Phase 1 or 2.
- **Tests / build / lint commands belong in the checker's instructions**, run via Bash. They are not modeled as separate non-agentic phases.
- **Consume existing artifacts in place.** `.plan/goal.md`, the local CMake build tree, branch git history, and the `origin/master` ref are pre-existing. The workflow uses them; it does not re-derive, re-fetch, re-snapshot, or regenerate them. (Re-running `git fetch origin master` for freshness is allowed; recreating snapshots is not. `.plan/findings.md` is the one exception: Phase 1 overwrites it as documented.)
- **Every implement prompt must instruct the agent to commit work to git before yielding.** For phases whose primary action is a `git rebase` operation, this means leaving the rebase in a fully concluded state (no `.git/rebase-merge/` directory present) and any post-rebase migration or verification fixups committed as their own commit on top of the rebased history before returning. For pure-prep or pure-verify phases, the prompt instructs the agent to `git add` and `git commit` any new artifacts (e.g. `.plan/findings.md` updates) before yielding.

## 3. Implementation Phases

### Phase 1 — Stage planning artifacts, clear blockers, capture pre-rebase state

- **Phase Name:** `prepare-rebase`
- **Implement Phase ID:** `01-prepare-rebase`
- **Verification Phases:**
  - **`01a-check-prepare-rebase`** — type `check`, `bounce_target: 01-prepare-rebase`. The checker runs each command below and bounces on the first non-zero exit:
    ```bash
    set -e
    git fetch origin master
    # 1. Tracked working-tree must be clean (untracked allowed).
    dirty=$(git status --porcelain | grep -v '^?? ' || true)
    [ -z "$dirty" ] || { echo "FAIL: tracked changes still present:"; echo "$dirty"; exit 1; }
    # 2. The repo-root .juvenal-state.json blocker must be cleared:
    #    working-tree blob must equal HEAD blob (skip-worktree masks `git status`).
    wt_blob=$(git hash-object .juvenal-state.json)
    head_blob=$(git rev-parse HEAD:.juvenal-state.json)
    [ "$wt_blob" = "$head_blob" ] || { echo "FAIL: .juvenal-state.json wt=$wt_blob head=$head_blob (rebase will abort)"; exit 1; }
    # 2b. skip-worktree should still be set (preserves user intent).
    git ls-files -v .juvenal-state.json | grep -q '^S ' || { echo "FAIL: skip-worktree was unset on .juvenal-state.json"; exit 1; }
    # 3. The most recent commit must be the prep commit and must touch only .plan/ paths.
    nonplan=$(git log -1 --name-only --pretty=format: | sed '/^$/d' | grep -v '^\.plan/' || true)
    [ -z "$nonplan" ] || { echo "FAIL: prep commit touched non-.plan paths:"; echo "$nonplan"; exit 1; }
    # 4. The prep commit's subject must announce the prep step.
    git log -1 --pretty=format:%s | grep -q '^rebase-prep:' || { echo "FAIL: prep commit subject mismatch"; exit 1; }
    # 5. .plan/findings.md must contain only the new snapshot section (no stale prior content).
    grep -q '^## Pre-rebase snapshot' .plan/findings.md || { echo "FAIL: snapshot section missing"; exit 1; }
    # 6. Branch is exactly behind=1, ahead=328 (327 originals + prep). No rebase has happened yet.
    counts=$(git rev-list --left-right --count HEAD...origin/master)
    ahead=$(echo "$counts" | awk '{print $1}')
    behind=$(echo "$counts" | awk '{print $2}')
    [ "$behind" = "1" ] || { echo "FAIL: expected behind=1, got $behind"; exit 1; }
    [ "$ahead" = "328" ] || { echo "FAIL: expected ahead=328 (327 originals + prep), got $ahead"; exit 1; }
    # 7. No rebase is currently in progress.
    [ ! -d .git/rebase-merge ] && [ ! -d .git/rebase-apply ] || { echo "FAIL: rebase already in progress"; exit 1; }
    echo "OK"
    ```
- **Preexisting Inputs:**
  - `.plan/goal.md`
  - `.plan/plan.md` (this document)
  - `.plan/findings.md` (currently contains stale notes; will be overwritten)
  - Working tree state including unstaged `.plan/` modifications, untracked `.plan/phases/*.md`, and the repo-root `.juvenal-state.json` whose working-tree blob currently differs from HEAD.
- **New Outputs:**
  - One commit on `wip/networking` (subject prefixed `rebase-prep:`) that stages every modified or newly-written `.plan/` path: `.plan/.juvenal-state.json`, `.plan/goal.md`, `.plan/plan.md`, `.plan/plan-before-cleanup.md`, `.plan/workflow-structure.yaml`, `.plan/findings.md`, `.plan/verification-notes.md` (if present), and every file under `.plan/phases/`. Untracked CMake build artifacts at the repo root remain untracked — they must not be added. The repo-root `.juvenal-state.json` is **not** committed by this phase; its content is restored to the existing HEAD blob and `skip-worktree` is re-applied so it is invisible to git going forward.
  - Updated `.plan/findings.md` containing only a `## Pre-rebase snapshot` section with the SHAs and conflict file list. Stale prior content (notes from a superseded planning iteration) is **discarded**, not preserved, because committing it onto the rebased history would bake unrelated review notes into the permanent log.
- **File Changes:**
  - Repo-root `.juvenal-state.json`: working-tree blob restored to match the HEAD blob via the `update-index`/`checkout`/`update-index` triplet documented in §1; not committed.
  - `.plan/findings.md`: overwritten with a single `## Pre-rebase snapshot` section recording: `pre_rebase_HEAD = <sha>`, `origin_master = <sha>`, `merge_base = <sha>`, `ahead_behind = <ahead>/<behind>`, `conflict_files = src/gameplay/families/effect_family_ghost_scare.cpp`, `auto_merge_files_requiring_post_rebase_migration = tests/test_walker_specials.cpp (whole-file accessor migration; master inserts at two regions, near merge-base line 651 and at the file tail)`.
  - Stage the listed `.plan/` paths and create the prep commit.
- **Implementation Details:**
  1. Run `git fetch origin master`.
  2. Clear the `.juvenal-state.json` blocker:
     ```bash
     git update-index --no-skip-worktree .juvenal-state.json
     git checkout HEAD -- .juvenal-state.json
     git update-index --skip-worktree .juvenal-state.json
     ```
     After this, `git hash-object .juvenal-state.json` must equal `git rev-parse HEAD:.juvenal-state.json` and `git ls-files -v .juvenal-state.json` must still show `S `.
  3. Capture: `HEAD_SHA=$(git rev-parse HEAD)`, `MASTER_SHA=$(git rev-parse origin/master)`, `BASE_SHA=$(git merge-base HEAD origin/master)`, `AHEAD_BEHIND=$(git rev-list --left-right --count HEAD...origin/master)`.
  4. **Overwrite** `.plan/findings.md` (do not append) with a single `## Pre-rebase snapshot` block containing those values plus the conflict file list and the test-file migration note. Stale prior content is intentionally discarded.
  5. `git add .plan/.juvenal-state.json .plan/goal.md .plan/plan.md .plan/plan-before-cleanup.md .plan/workflow-structure.yaml .plan/findings.md` and `git add .plan/phases/`. If `.plan/verification-notes.md` exists, include it. **Do not** use `git add -A` or `git add .`. **Do not** stage anything outside `.plan/` (no CMake build outputs, no `compile_commands.json`, no `.cache/`, no repo-root `.juvenal-state.json`).
  6. `git commit -m "rebase-prep: stage plan artifacts before rebase onto origin/master"`. The commit lands on `wip/networking` before rebase begins; because it touches only `.plan/` and master's only new commit (`16963de0`) does not touch `.plan/`, the prep commit replays during rebase without conflict.
  7. Confirm `git status --porcelain | grep -v '^?? '` is empty AND the `.juvenal-state.json` blob equality check from step 2 still holds before yielding.
- **Verification:** the verifier commands above. A failed assertion bounces back to `01-prepare-rebase`.

### Phase 2 — Execute the rebase, resolve the source conflict, and migrate master-inserted accessor sites

- **Phase Name:** `execute-rebase`
- **Implement Phase ID:** `02-execute-rebase`
- **Verification Phases:**
  - **`02a-check-rebase-clean`** — type `check`, `bounce_target: 02-execute-rebase`. The checker runs each assertion below and bounces on the first non-zero exit:
    ```bash
    set -e
    git fetch origin master
    # 1. No rebase in progress.
    [ ! -d .git/rebase-merge ] && [ ! -d .git/rebase-apply ] || { echo "FAIL: rebase still in progress"; exit 1; }
    # 2. No unmerged paths.
    unmerged=$(git diff --name-only --diff-filter=U)
    [ -z "$unmerged" ] || { echo "FAIL: unmerged paths:"; echo "$unmerged"; exit 1; }
    # 3. origin/master is the new base.
    base=$(git merge-base HEAD origin/master)
    om=$(git rev-parse origin/master)
    [ "$base" = "$om" ] || { echo "FAIL: branch is not based on origin/master (base=$base, om=$om)"; exit 1; }
    # 4. Branch is exactly 329 commits ahead (327 originals + prep + test-migration commit).
    ahead=$(git rev-list --count origin/master..HEAD)
    behind=$(git rev-list --count HEAD..origin/master)
    [ "$behind" = "0" ] || { echo "FAIL: expected behind=0, got $behind"; exit 1; }
    [ "$ahead" = "329" ] || { echo "FAIL: expected ahead=329 (327 + prep + test-migration), got $ahead. Build/test fixups belong in Phase 3/4, not Phase 2."; exit 1; }
    # 5. No conflict markers anywhere under src/, tests/, include/.
    if grep -RIn -e '^<<<<<<<' -e '^=======' -e '^>>>>>>>' src tests include 2>/dev/null; then
      echo "FAIL: conflict markers remain"; exit 1
    fi
    # 6. The source-file conflict resolved as expected.
    grep -q 'return false; // delegate to effect::act default animate/die path' src/gameplay/families/effect_family_ghost_scare.cpp \
      || { echo "FAIL: ghost_scare_on_act did not adopt master's return false"; exit 1; }
    grep -q 'self->owner()' src/gameplay/families/effect_family_ghost_scare.cpp \
      || { echo "FAIL: ghost_scare branch accessor form missing"; exit 1; }
    # 7. The test file has exactly one session_state.h include.
    inc_count=$(grep -c 'session_state.h' tests/test_walker_specials.cpp)
    [ "$inc_count" = "1" ] || { echo "FAIL: session_state.h include count = $inc_count (expected 1)"; exit 1; }
    # 8. The 42 new TEST_F cases landed (40 from second-region append + 2 from first-region insertion).
    tf_count=$(grep -c 'TEST_F(WalkerSpecials' tests/test_walker_specials.cpp)
    [ "$tf_count" -ge 42 ] || { echo "FAIL: WalkerSpecials TEST_F count = $tf_count (expected >= 42)"; exit 1; }
    # 9. Whole-file migration assertion: NO public-field write/read syntax for any privatized
    #    field anywhere in the file. The migration is whole-file (master inserts in two regions:
    #    around merge-base line 651 inside the existing ghost_scare test region, and at the file
    #    tail). Scoping by line number (e.g. >=1404) misses the first-region insertion, so the
    #    grep checks below are scoped to the entire file.
    F=tests/test_walker_specials.cpp
    # 9a. Privatized-field WRITES (assignment via `=`, not `==`/`!=`/`<=`/`>=`).
    if grep -nE '->(busy|current_special|dead|xpos|ypos|owner|lifetime|ani_type|cycle|curdir|lastx|lasty|stepsize|view_all|charm_left|shifter_down|family|user|team_num|real_team_num|sizex|sizey)[[:space:]]*=([^=]|$)' "$F"; then
      echo "FAIL: walker public-field WRITES remain (one of: busy/current_special/dead/xpos/ypos/owner/lifetime/ani_type/cycle/curdir/lastx/lasty/stepsize/view_all/charm_left/shifter_down/family/user/team_num/real_team_num/sizex/sizey)"; exit 1
    fi
    if grep -nE '->stats\(\)->(magicpoints|max_magicpoints|hitpoints|max_hitpoints|level|frozen_delay|old_family)[[:space:]]*=([^=]|$)' "$F"; then
      echo "FAIL: stats public-field WRITES remain (magicpoints/max_magicpoints/hitpoints/max_hitpoints/level/frozen_delay/old_family)"; exit 1
    fi
    # 9b. special_cost subscript writes/reads must use the accessor form, not array indexing.
    if grep -nE '->special_cost\[' "$F"; then
      echo "FAIL: special_cost array-subscript syntax remains; must use special_cost(i)/set_special_cost(i,X)"; exit 1
    fi
    # 9c. Privatized-field READS without `()`. The pattern excludes the form `name(` (call/accessor)
    #     and excludes identifier-continuation chars to avoid matching e.g. `family_id`.
    if grep -nE '->(busy|current_special|dead|xpos|ypos|owner|lifetime|ani_type|cycle|curdir|lastx|lasty|stepsize|view_all|charm_left|shifter_down|family|user|team_num|real_team_num|sizex|sizey)([^_a-zA-Z0-9(]|$)' "$F" \
       | grep -vE '->(busy|current_special|dead|xpos|ypos|owner|lifetime|ani_type|cycle|curdir|lastx|lasty|stepsize|view_all|charm_left|shifter_down|family|user|team_num|real_team_num|sizex|sizey)\(' >/dev/null; then
      echo "FAIL: walker public-field READS remain (missing parens after privatized field name)"; exit 1
    fi
    if grep -nE '->stats\(\)->(magicpoints|max_magicpoints|hitpoints|max_hitpoints|level|frozen_delay|old_family)([^_a-zA-Z0-9(]|$)' "$F" \
       | grep -vE '->stats\(\)->(magicpoints|max_magicpoints|hitpoints|max_hitpoints|level|frozen_delay|old_family)\(' >/dev/null; then
      echo "FAIL: stats public-field READS remain (missing parens after privatized field name)"; exit 1
    fi
    # 10. The .juvenal-state.json blocker remains cleared (skip-worktree set, blob == HEAD blob).
    wt_blob=$(git hash-object .juvenal-state.json)
    head_blob=$(git rev-parse HEAD:.juvenal-state.json)
    [ "$wt_blob" = "$head_blob" ] || { echo "FAIL: .juvenal-state.json blob drifted"; exit 1; }
    # 11. The post-rebase test-migration commit exists and touches only the test file.
    last_subj=$(git log -1 --pretty=format:%s)
    echo "$last_subj" | grep -q '^rebase: migrate master-inserted walker specials tests to accessor form$' \
      || { echo "FAIL: top commit subject is not the test-migration commit: $last_subj"; exit 1; }
    last_files=$(git log -1 --name-only --pretty=format: | sed '/^$/d')
    [ "$last_files" = "tests/test_walker_specials.cpp" ] \
      || { echo "FAIL: test-migration commit touched extra files: $last_files"; exit 1; }
    echo "OK"
    ```
- **Preexisting Inputs:**
  - The pre-rebase commit from Phase 1 sitting on `wip/networking`.
  - `origin/master` ref (already fetched in Phase 1; re-fetched defensively at the start of Phase 2).
  - The conflict-resolution recipe and whole-file substitution table in section 1.
  - The cleared `.juvenal-state.json` blocker from Phase 1.
- **New Outputs:**
  - `wip/networking` rebased onto `origin/master`. New commit SHAs (rewritten history) but identical commit subjects/authors to the original 328 commits (327 networking + 1 Phase-1 prep commit).
  - One additional commit on top of the rebased branch: `rebase: migrate master-inserted walker specials tests to accessor form`, applying the section-1 substitution table to `tests/test_walker_specials.cpp` whole-file. Total ahead count after Phase 2: 329.
- **File Changes:**
  - `src/gameplay/families/effect_family_ghost_scare.cpp` — final content keeps the branch's accessor form (`self->owner()`, `self->owner()->dead()`, etc.) **and** master's `return false; // delegate to effect::act default animate/die path` line. Resolved during rebase replay of `6d6b3a06`.
  - `tests/test_walker_specials.cpp` — final content keeps the branch's accessor migrations throughout the existing branch portion **and** master's first-region two-test insertion (around branch line ~720, immediately after the existing `ghost_scare` test) **and** master's tail-appended helpers + `TEST_F` cases, with every public-field touch in either master-inserted region migrated to the accessor form per the substitution table in section 1. The single `#include <openglad/interface/session_state.h>` that the branch already had is preserved unchanged.
  - No other files modified. If any third file shows a conflict during rebase, the implement phase yields immediately and the verifier records the failure.
- **Implementation Details:**
  1. Confirm clean working tree (`git status --porcelain | grep -v '^?? '` empty), confirm `.juvenal-state.json` blob still matches HEAD blob, confirm no rebase in progress.
  2. Run `git rebase origin/master`.
  3. On conflict in `src/gameplay/families/effect_family_ghost_scare.cpp` (expected during replay of `6d6b3a06 Phase 3: privatize owner cross-references`):
     - Open the file. The conflict region is in `ghost_scare_on_act()`.
     - Resolve by accepting branch's `self->owner()` accessor form on the `if` and `center_on` lines, and accepting master's `return false; // delegate to effect::act default animate/die path` line.
     - Confirm no `<<<<<<<` / `=======` / `>>>>>>>` markers remain in the file.
     - `git add src/gameplay/families/effect_family_ghost_scare.cpp`.
     - `git rebase --continue`.
  4. If any *unexpected* conflict appears (i.e. anything other than the expected `effect_family_ghost_scare.cpp` pause), run `git rebase --abort`, restore the pre-rebase HEAD, and yield with the unexpected file path recorded in `.plan/findings.md`. Do not invent new resolutions.
  5. After `git rebase` reports completion: confirm no `.git/rebase-merge/` or `.git/rebase-apply/` directory remains.
  6. **Post-rebase whole-file migration of `tests/test_walker_specials.cpp`** (the test file auto-merged during rebase, master inserts in two regions, so this step is unconditional and operates on the entire file):
     - Confirm the substitution targets are still public/private as documented: `grep -n 'commands' include/openglad/gameplay/statistics.h` (expect `commands` to remain a public `std::list<command>` near line 149), `grep -n 'setxy' include/openglad/gameplay/walker.h` (expect `setxy` to remain a public method around lines 94–98), and `grep -n 'OG_WALKER_DIRTY_FIELD\|OG_STATS_DIRTY_FIELD' include/openglad/gameplay/walker.h include/openglad/gameplay/statistics.h | head` (expect the privatized fields enumerated in §1's substitution table to still be DIRTY_FIELDs).
     - Apply the section-1 substitution table to the **entire file** (no line-number scoping). The substitutions are idempotent on already-migrated branch lines, so whole-file application is safe.
     - Recommended pattern (illustrative — implementer may use any equivalent textual transform):
       - For each privatized walker DIRTY field `F` in {`busy`, `current_special`, `ani_type`, `cycle`, `curdir`, `lastx`, `lasty`, `stepsize`, `view_all`, `charm_left`, `shifter_down`, `lifetime`}: rewrite `->F = X` to `->set_F(X)`, then rewrite remaining `->F` (read sites, no following `=` or `(`) to `->F()`.
       - For each privatized sim_entity DIRTY field `F` in {`dead`, `xpos`, `ypos`, `sizex`, `sizey`, `family`, `user`, `team_num`, `real_team_num`}: same write-then-read pair.
       - For `owner`: rewrite `->owner = X` to `->set_owner(X)`, then `->owner` (no `(`) to `->owner()`.
       - For each privatized statistics DIRTY field `F` in {`magicpoints`, `max_magicpoints`, `hitpoints`, `max_hitpoints`, `level`, `frozen_delay`, `old_family`}: rewrite `->stats()->F = X` to `->stats()->set_F(X)`, then remaining `->stats()->F` (read) to `->stats()->F()`.
       - For `special_cost`: rewrite `->stats()->special_cost[i] = X` to `->stats()->set_special_cost(i, X)`, then remaining `->stats()->special_cost[i]` (read) to `->stats()->special_cost(i)`.
       - Leave `->stats()->commands`, `->setxy(...)`, `->myguy`, `->myscreen_`, `->world`, `->act(...)`, `->center_on(...)`, `->special()`, `->stats()` alone (verified public/method).
     - For any field master uses that is **not** in the substitution table, apply the operating rule from §1: grep the relevant header, and if the field is a DIRTY_FIELD or has a setter/getter pair, migrate; otherwise leave alone.
     - Verify no conflict markers and no remaining public-field syntax for any privatized field anywhere in the file (the verifier's grep assertions in `02a-check-rebase-clean` are authoritative — if any of those greps would fire, fix the file before committing).
     - `git add tests/test_walker_specials.cpp`.
     - `git commit -m "rebase: migrate master-inserted walker specials tests to accessor form"`. This commit must touch **only** `tests/test_walker_specials.cpp`.
  7. Do **not** add build/test fixup commits in Phase 2 — those belong in Phases 3/4 so the verifier's exact-`329` count holds. If compile errors are anticipated to remain after the migration, that's Phase 3's responsibility.
  8. Before yielding, confirm `git status --porcelain | grep -v '^?? '` is empty and the `.juvenal-state.json` blob still matches HEAD blob.
- **Verification:** the verifier commands above. A failed conflict resolution or migration bounces back to `02-execute-rebase`. The verifier does **not** attempt to build or test (those are Phases 3 and 4); it only verifies the rebase completed cleanly, produced the expected ref topology, resolved the source conflict, and the whole-file migration is grep-clean.

### Phase 3 — Build verification

- **Phase Name:** `build-verify`
- **Implement Phase ID:** `03-build-verify`
- **Verification Phases:**
  - **`03a-check-build`** — type `check`, `bounce_target: 03-build-verify`. Verify the project compiles cleanly with the project's CI preset:
    ```bash
    set -e
    cmake --preset ci-test
    cmake --build --preset ci-test 2>&1 | tee /tmp/og-build.log
    if grep -E 'error:|fatal error' /tmp/og-build.log >/dev/null; then
      echo "FAIL: compile errors in build log"; exit 1
    fi
    [ -x build/ci-test/openglad ] || { echo "FAIL: build/ci-test/openglad missing"; exit 1; }
    # Working tree must be clean (any fixups must be committed).
    dirty=$(git status --porcelain | grep -v '^?? ' || true)
    [ -z "$dirty" ] || { echo "FAIL: uncommitted changes after Phase 3:"; echo "$dirty"; exit 1; }
    echo "OK"
    ```
- **Preexisting Inputs:** Rebased branch from Phase 2 (with the test-migration commit on top); existing CMake build tree at the repo root (untracked).
- **New Outputs:** A successful build artifact set under `build/ci-test/`. Zero or more follow-up commits titled `rebase: fixup build after master rebase (phase 3)` if additional accessor migration was missed in Phase 2's whole-file pass and surfaces only at compile time (e.g. a field master uses that the substitution table did not enumerate).
- **File Changes:** Most likely `tests/test_walker_specials.cpp` (any field the substitution table missed). Other files possible if compile errors surface there. Apply additional accessor migrations consistent with the operating rule from §1 (grep the header, migrate if the field is a DIRTY_FIELD or has a setter/getter pair); do not introduce unrelated edits.
- **Implementation Details:**
  1. Run `cmake --preset ci-test` then `cmake --build --preset ci-test`.
  2. If compile errors mention `tests/test_walker_specials.cpp` and reference a member as non-existent or private, apply the corresponding accessor substitution. Common edge cases the verbatim substitution may miss: postfix-style usage (`w->busy++`), compound assignment (`w->magicpoints -= cost`), member-access chains via reference instead of pointer, fields not enumerated in the §1 table that master happens to use. For an unfamiliar field, follow the operating rule: `grep -n '<name>' include/openglad/gameplay/walker.h include/openglad/gameplay/statistics.h include/openglad/gameplay/sim_entity.h`.
  3. If errors point elsewhere, treat them as in-scope: they reflect the rebase result and must be fixed.
  4. After fixes: `git add <fixed files>` and `git commit -m "rebase: fixup build after master rebase (phase 3)"`. Each round of fixups is its own commit (do not amend).
  5. Re-run `cmake --build --preset ci-test` until green. **Bound by progress, not commit count:** if two consecutive build attempts produce the same set of errors (no progress), yield with the build still red so the operator can intervene. Otherwise the loop may run as many rounds as are making forward progress.
  6. Before yielding, confirm `git status --porcelain | grep -v '^?? '` is empty.
- **Verification:** `cmake --build --preset ci-test` returns 0, no `error:` / `fatal error` strings appear in build output, and the working tree has no uncommitted tracked changes.

### Phase 4 — Test verification

- **Phase Name:** `test-verify`
- **Implement Phase ID:** `04-test-verify`
- **Verification Phases:**
  - **`04a-check-tests`** — type `check`, `bounce_target: 04-test-verify`. Verify the full ctest preset passes:
    ```bash
    set -e
    ctest --preset ci-test --output-on-failure 2>&1 | tee /tmp/og-ctest.log
    grep -qE '100% tests passed' /tmp/og-ctest.log || { echo "FAIL: not 100% tests passed"; tail -40 /tmp/og-ctest.log; exit 1; }
    dirty=$(git status --porcelain | grep -v '^?? ' || true)
    [ -z "$dirty" ] || { echo "FAIL: uncommitted changes after Phase 4:"; echo "$dirty"; exit 1; }
    echo "OK"
    ```
- **Preexisting Inputs:** Built binaries from Phase 3; the new 42 `TEST_F(WalkerSpecials, ...)` cases brought in by master.
- **New Outputs:** A passing `ctest --preset ci-test` run. Zero or more follow-up commits `rebase: fixup tests after master rebase (phase 4)` if a test fails because the accessor migration in Phase 2 left a behavior gap (e.g. the setter has side-effects the appended tests don't anticipate).
- **File Changes:** Only if test failures point to `tests/test_walker_specials.cpp` lines requiring further accessor adjustment, or to the resolved `effect_family_ghost_scare.cpp` not actually returning `false` in all branches. Adjustments stay in those two files.
- **Implementation Details:**
  1. Run `ctest --preset ci-test --output-on-failure`.
  2. For each `og_unit_*` or `og_test_*` failure, read the gtest output and identify whether the failure is caused by (a) a missed accessor substitution in the rebase resolution, (b) a divergence between the branch's accessor semantics and master's expected field semantics (e.g. `set_busy(0)` vs `busy = 0` differ in side-effects), or (c) a pre-existing branch test failure unrelated to the rebase.
  3. For (a): apply the missing substitution.
  4. For (b): adjust the master-supplied test setup to call the appropriate setter sequence to reproduce master's intended precondition. Document the change in the commit message.
  5. For (c): leave the failure in place but record it in `.plan/findings.md` under `## Known pre-existing test failures` and surface to the operator. Do **not** mask failures by editing tests or skipping them. The verifier will still fail and bounce; the operator decides whether to override.
  6. Commit any fixes (`git add <files>` + `git commit -m "rebase: fixup tests after master rebase (phase 4)"`) before yielding. Confirm `git status --porcelain | grep -v '^?? '` is empty.
  7. **Bound by progress, not commit count:** if two consecutive ctest runs produce the same set of failing test cases (no progress), yield with tests still red so the operator can intervene.
- **Verification:** `ctest --preset ci-test` reports `100% tests passed`, exits 0, and the working tree has no uncommitted tracked changes.

### Phase 5 — Final history & integration verification

- **Phase Name:** `final-verify`
- **Implement Phase ID:** `05-final-verify`
- **Verification Phases:**
  - **`05a-check-final`** — type `check`, `bounce_target: 05-final-verify`. Verify the rebase was successful end-to-end:
    ```bash
    set -e
    git fetch origin master
    base=$(git merge-base HEAD origin/master)
    om=$(git rev-parse origin/master)
    [ "$base" = "$om" ] || { echo "FAIL: not based on origin/master"; exit 1; }
    ahead=$(git rev-list --count origin/master..HEAD)
    behind=$(git rev-list --count HEAD..origin/master)
    [ "$behind" = "0" ] || { echo "FAIL: behind=$behind"; exit 1; }
    [ "$ahead" -ge 329 ] || { echo "FAIL: ahead=$ahead (expected >= 329)"; exit 1; }
    [ ! -d .git/rebase-merge ] && [ ! -d .git/rebase-apply ] || { echo "FAIL: rebase artifacts present"; exit 1; }
    cmake --build --preset ci-test
    ctest --preset ci-test --output-on-failure 2>&1 | tee /tmp/og-ctest-final.log
    grep -qE '100% tests passed' /tmp/og-ctest-final.log || { echo "FAIL: ctest regressed"; exit 1; }
    if grep -RIn -e '^<<<<<<<' -e '^=======' -e '^>>>>>>>' src tests include 2>/dev/null; then
      echo "FAIL: conflict markers remain"; exit 1
    fi
    grep -q '^## Final rebase summary' .plan/findings.md || { echo "FAIL: final summary missing"; exit 1; }
    grep -q '^## Pre-rebase snapshot' .plan/findings.md || { echo "FAIL: pre-rebase snapshot missing"; exit 1; }
    # .juvenal-state.json blocker still cleared.
    wt_blob=$(git hash-object .juvenal-state.json)
    head_blob=$(git rev-parse HEAD:.juvenal-state.json)
    [ "$wt_blob" = "$head_blob" ] || { echo "FAIL: .juvenal-state.json blob drifted from HEAD"; exit 1; }
    dirty=$(git status --porcelain | grep -v '^?? ' || true)
    [ -z "$dirty" ] || { echo "FAIL: uncommitted changes:"; echo "$dirty"; exit 1; }
    echo "OK"
    ```
- **Preexisting Inputs:** Rebased + green branch from Phases 2–4.
- **New Outputs:** A `## Final rebase summary` section appended to `.plan/findings.md` recording: post-rebase HEAD SHA, count of commits ahead of `origin/master`, count of fixup commits added in Phases 3–4, links to any test failures recorded as pre-existing. One commit `rebase: record final findings` adds this section.
- **File Changes:** `.plan/findings.md` (append only). Additionally, if the verifier's build-or-test sanity re-run surfaces a regression (which would normally have been caught in Phase 3/4), Phase 5's implement step performs the **same** in-place build/test fixups as Phases 3/4 — applying the section-1 substitution table to the offending file(s) and committing as `rebase: fixup after master rebase (phase 5)`. This is required because the verifier bounces only to `05-final-verify`; cross-block bounces are forbidden by the workflow contract, so Phase 5 must own its own remediation.
- **Implementation Details:**
  1. Run `cmake --build --preset ci-test` and `ctest --preset ci-test --output-on-failure` as a sanity check.
  2. If either fails, treat it identically to Phase 3 / Phase 4: apply the substitution table, commit with subject `rebase: fixup after master rebase (phase 5)`, and re-run. **Bound by progress, not commit count:** if two consecutive build/ctest runs produce the same error or failure set, yield with the regression recorded in `.plan/findings.md` under `## Known pre-existing test failures` so the verifier's bounce on the next pass cannot loop indefinitely (the recorded section will not change between passes, and the operator will see the loop has exhausted remediation budget).
  3. Append the `## Final rebase summary` section to `.plan/findings.md` with: post-rebase HEAD SHA, ahead count vs `origin/master`, the count of fixup commits added in Phases 3 and 4 (`git log --oneline origin/master..HEAD | grep -c 'rebase: fixup'`), and a list of any pre-existing test failures recorded earlier.
  4. `git add .plan/findings.md && git commit -m "rebase: record final findings"`.
  5. Confirm `git status --porcelain | grep -v '^?? '` is empty and `.juvenal-state.json` blob still matches HEAD blob before yielding.
- **Verification:** all checker commands above pass.

## 4. Critical Files

| Path | Phase | Change |
|---|---|---|
| `.juvenal-state.json` (repo root, skip-worktree) | 1 | Restore working-tree blob to match HEAD blob via `update-index --no-skip-worktree` → `checkout HEAD --` → `update-index --skip-worktree`. **Not committed.** Verifier asserts blob equality, not `git status`. |
| `.plan/findings.md` | 1, 5 | Phase 1 **overwrites** with a single `## Pre-rebase snapshot` section (stale prior content discarded). Phase 5 appends `## Final rebase summary`. |
| `.plan/.juvenal-state.json`, `.plan/goal.md`, `.plan/plan.md`, `.plan/plan-before-cleanup.md`, `.plan/workflow-structure.yaml`, `.plan/verification-notes.md` (if present), `.plan/phases/*.md` | 1 | Staged + committed by the prep commit. Content untouched by this workflow except `findings.md`. |
| `src/gameplay/families/effect_family_ghost_scare.cpp` | 2 | Resolve conflict during rebase: keep branch accessor form **and** apply master's `return false` change in `ghost_scare_on_act()`. |
| `tests/test_walker_specials.cpp` | 2 (post-rebase whole-file migration), 3 (build fixups), 4 (test fixups), 5 (final fixups if any) | Rebase auto-merges. Phase 2's post-rebase migration commit (`rebase: migrate master-inserted walker specials tests to accessor form`) rewrites every public-field reference for any privatized field anywhere in the file (covering both master-inserted regions: ~720 and ~1484+) to the branch's accessor form per section 1's substitution table. |
| `build/ci-test/` (untracked) | 3, 4, 5 | Built artifacts and test outputs. Not committed. |

No other source files are expected to change. If verification surfaces edits to additional files, the relevant phase loops on its own bounce target until clean — no new files are pre-authorized.

## 5. Final Verification

Run from the repo root after Phase 5 completes:

```bash
git fetch origin master
git rev-parse HEAD origin/master
[ "$(git merge-base HEAD origin/master)" = "$(git rev-parse origin/master)" ] || echo "FAIL: not based on origin/master"
git rev-list --count origin/master..HEAD    # >= 329
git rev-list --count HEAD..origin/master    # must be 0
! test -d .git/rebase-merge && ! test -d .git/rebase-apply
cmake --preset ci-test
cmake --build --preset ci-test
ctest --preset ci-test
git log --oneline origin/master..HEAD | head -10
grep -q "^## Final rebase summary" .plan/findings.md
grep -q "^## Pre-rebase snapshot" .plan/findings.md
[ -z "$(git status --porcelain | grep -v '^?? ')" ]
[ "$(git hash-object .juvenal-state.json)" = "$(git rev-parse HEAD:.juvenal-state.json)" ]
git ls-files -v .juvenal-state.json | grep -q '^S '
```

Pass criteria:

- `git merge-base HEAD origin/master == git rev-parse origin/master` → branch is strictly ahead of `origin/master`.
- `git rev-list --count HEAD..origin/master == 0` → no master commit is missing from the branch.
- `git rev-list --count origin/master..HEAD >= 329` → 327 originals + Phase-1 prep + Phase-2 test-migration commit, plus any Phase-3/4/5 fixup commits.
- No `.git/rebase-merge/` or `.git/rebase-apply/` directory present.
- `cmake --build --preset ci-test` exits 0, no `error:` lines.
- `ctest --preset ci-test` reports `100% tests passed`.
- The branch's commit titles match the original 327 commit titles in order, with the Phase-1 prep commit (`rebase-prep:`), the Phase-2 test-migration commit (`rebase: migrate master-inserted walker specials tests to accessor form`), and any Phase-3/4/5 fixup commits (`rebase: fixup ... (phase N)` or `rebase: record final findings`) appearing chronologically after them.
- `.plan/findings.md` contains both the `## Pre-rebase snapshot` and `## Final rebase summary` sections, and no stale notes from prior planning iterations.
- The working tree has no uncommitted tracked changes.
- The repo-root `.juvenal-state.json` working-tree blob equals the HEAD blob and `skip-worktree` is still set, confirming the Phase-1 blocker fix held throughout the rebase.
