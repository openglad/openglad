# Phase 2 — Execute Rebase

## Phase Name
`execute-rebase`

## Implement Phase ID
`02-execute-rebase`

## Preexisting Inputs
- Phase-1 prep commit at the tip of `wip/networking` (subject `rebase-prep:`).
- Cleared `.juvenal-state.json` blocker from Phase 1 (working-tree blob == HEAD blob, `skip-worktree` set).
- `origin/master` ref (re-fetched defensively at the start of this phase).
- `.plan/plan.md` §1 conflict-resolution recipe for `src/gameplay/families/effect_family_ghost_scare.cpp`.
- `.plan/plan.md` §1 whole-file substitution table for `tests/test_walker_specials.cpp`.
- `include/openglad/gameplay/walker.h`, `include/openglad/gameplay/statistics.h`, `include/openglad/gameplay/sim_entity.h` — header references for the operating rule (`OG_WALKER_DIRTY_FIELD` / `OG_STATS_DIRTY_FIELD` / setter-getter pair → migrate; otherwise leave).

## New Outputs
- `wip/networking` rebased onto `origin/master`. New commit SHAs but identical commit subjects/authors to the original 328 commits (327 networking + 1 Phase-1 prep commit).
- One additional commit on top of the rebased branch with subject `rebase: migrate master-inserted walker specials tests to accessor form` applying the §1 substitution table to `tests/test_walker_specials.cpp` whole-file. **Total ahead count after Phase 2: exactly 329.**

## File Changes
- `src/gameplay/families/effect_family_ghost_scare.cpp` — final content keeps the branch's accessor form (`self->owner()`, `self->owner()->dead()`, `self->owner()->stats()->level()`, etc.) and applies master's `return false; // delegate to effect::act default animate/die path` line in `ghost_scare_on_act()`. Resolved during rebase replay of `6d6b3a06 Phase 3: privatize owner cross-references`.
- `tests/test_walker_specials.cpp` — final content keeps the branch's accessor migrations throughout the existing branch portion, master's first-region 109-line two-test insertion (around branch line ~720, immediately after the existing `ghost_scare` test), and master's tail-appended 989-line helpers + `TEST_F` cases, with every public-field touch in either master-inserted region migrated to the accessor form per §1. The single `#include <openglad/interface/session_state.h>` is preserved unchanged.
- No other files modified. If any third file shows a conflict during rebase, the implement phase yields immediately and records the unexpected file path in `.plan/findings.md`.

## Implementation Details
1. Confirm clean working tree (`git status --porcelain | grep -v '^?? '` empty), confirm `.juvenal-state.json` blob still matches HEAD blob, confirm no rebase in progress.
2. Run `git rebase origin/master`.
3. On conflict in `src/gameplay/families/effect_family_ghost_scare.cpp` (expected during replay of `6d6b3a06`):
   - Open the file. The conflict region is in `ghost_scare_on_act()`.
   - Resolve by accepting branch's `self->owner()` accessor form on the `if` and `center_on` lines, and accepting master's `return false; // delegate to effect::act default animate/die path` line.
   - Confirm no `<<<<<<<` / `=======` / `>>>>>>>` markers remain.
   - `git add src/gameplay/families/effect_family_ghost_scare.cpp` and `git rebase --continue`.
4. If any unexpected conflict appears (anything other than the expected `effect_family_ghost_scare.cpp` pause), run `git rebase --abort`, restore the pre-rebase HEAD, and yield with the unexpected file path recorded in `.plan/findings.md`.
5. After `git rebase` reports completion: confirm no `.git/rebase-merge/` or `.git/rebase-apply/` directory remains.
6. Post-rebase whole-file migration of `tests/test_walker_specials.cpp`:
   - Confirm substitution targets are still public/private as documented: `grep -n 'commands' include/openglad/gameplay/statistics.h` (expect public `std::list<command>` near line 149), `grep -n 'setxy' include/openglad/gameplay/walker.h` (expect public method around lines 94–98), and `grep -n 'OG_WALKER_DIRTY_FIELD\|OG_STATS_DIRTY_FIELD' include/openglad/gameplay/walker.h include/openglad/gameplay/statistics.h | head` (expect the privatized fields enumerated in §1 to still be DIRTY_FIELDs).
   - Apply the §1 substitution table to the entire file (no line-number scoping). Substitutions are idempotent on already-migrated branch lines.
   - Substitution pattern:
     - For each privatized walker DIRTY field `F` in {`busy`, `current_special`, `ani_type`, `cycle`, `curdir`, `lastx`, `lasty`, `stepsize`, `view_all`, `charm_left`, `shifter_down`, `lifetime`}: rewrite `->F = X` to `->set_F(X)`, then remaining `->F` (read sites, no following `=` or `(`) to `->F()`.
     - For each privatized sim_entity DIRTY field `F` in {`dead`, `xpos`, `ypos`, `sizex`, `sizey`, `family`, `user`, `team_num`, `real_team_num`}: same write-then-read pair.
     - For `owner`: rewrite `->owner = X` to `->set_owner(X)`, then `->owner` (no `(`) to `->owner()`.
     - For each privatized statistics DIRTY field `F` in {`magicpoints`, `max_magicpoints`, `hitpoints`, `max_hitpoints`, `level`, `frozen_delay`, `old_family`}: rewrite `->stats()->F = X` to `->stats()->set_F(X)`, then remaining `->stats()->F` (read) to `->stats()->F()`.
     - For `special_cost`: rewrite `->stats()->special_cost[i] = X` to `->stats()->set_special_cost(i, X)`, then remaining `->stats()->special_cost[i]` (read) to `->stats()->special_cost(i)`.
     - Leave `->stats()->commands`, `->setxy(...)`, `->myguy`, `->myscreen_`, `->world`, `->act(...)`, `->center_on(...)`, `->special()`, `->stats()` alone.
   - For any field master uses that is not in the substitution table, apply the §1 operating rule: grep the relevant header, and if the field is a DIRTY_FIELD or has a setter/getter pair, migrate; otherwise leave alone.
   - Verify no conflict markers and no remaining public-field syntax for any privatized field anywhere in the file (the verifier's grep assertions are authoritative).
   - `git add tests/test_walker_specials.cpp` and `git commit -m "rebase: migrate master-inserted walker specials tests to accessor form"`. This commit must touch only `tests/test_walker_specials.cpp`.
7. Do **not** add build/test fixup commits in Phase 2 — those belong in Phases 3/4 so the verifier's exact `ahead=329` count holds.
8. Before yielding, confirm `git status --porcelain | grep -v '^?? '` is empty and the `.juvenal-state.json` blob still matches HEAD blob.

## Verification Phases
- **`02a-check-rebase-clean`** — type `check`, `bounce_target: 02-execute-rebase`. Purpose: assert the rebase completed cleanly, produced the expected ref topology (`ahead=329, behind=0`, base == `origin/master`), the source-file conflict resolved correctly, the test file's `session_state.h` include count is 1, ≥42 `WalkerSpecials` `TEST_F` cases are present, no public-field syntax remains for any privatized field anywhere in the test file, the `.juvenal-state.json` blocker is still cleared, and the top commit is the test-migration commit touching only that file. Does **not** attempt to build or test (those are Phases 3/4). Runs:
  ```bash
  set -e
  git fetch origin master
  [ ! -d .git/rebase-merge ] && [ ! -d .git/rebase-apply ] || { echo "FAIL: rebase still in progress"; exit 1; }
  unmerged=$(git diff --name-only --diff-filter=U)
  [ -z "$unmerged" ] || { echo "FAIL: unmerged paths:"; echo "$unmerged"; exit 1; }
  base=$(git merge-base HEAD origin/master)
  om=$(git rev-parse origin/master)
  [ "$base" = "$om" ] || { echo "FAIL: branch is not based on origin/master (base=$base, om=$om)"; exit 1; }
  ahead=$(git rev-list --count origin/master..HEAD)
  behind=$(git rev-list --count HEAD..origin/master)
  [ "$behind" = "0" ] || { echo "FAIL: expected behind=0, got $behind"; exit 1; }
  [ "$ahead" = "329" ] || { echo "FAIL: expected ahead=329 (327 + prep + test-migration), got $ahead. Build/test fixups belong in Phase 3/4, not Phase 2."; exit 1; }
  if grep -RIn -e '^<<<<<<<' -e '^=======' -e '^>>>>>>>' src tests include 2>/dev/null; then
    echo "FAIL: conflict markers remain"; exit 1
  fi
  grep -q 'return false; // delegate to effect::act default animate/die path' src/gameplay/families/effect_family_ghost_scare.cpp \
    || { echo "FAIL: ghost_scare_on_act did not adopt master's return false"; exit 1; }
  grep -q 'self->owner()' src/gameplay/families/effect_family_ghost_scare.cpp \
    || { echo "FAIL: ghost_scare branch accessor form missing"; exit 1; }
  inc_count=$(grep -c 'session_state.h' tests/test_walker_specials.cpp)
  [ "$inc_count" = "1" ] || { echo "FAIL: session_state.h include count = $inc_count (expected 1)"; exit 1; }
  tf_count=$(grep -c 'TEST_F(WalkerSpecials' tests/test_walker_specials.cpp)
  [ "$tf_count" -ge 42 ] || { echo "FAIL: WalkerSpecials TEST_F count = $tf_count (expected >= 42)"; exit 1; }
  F=tests/test_walker_specials.cpp
  if grep -nE '->(busy|current_special|dead|xpos|ypos|owner|lifetime|ani_type|cycle|curdir|lastx|lasty|stepsize|view_all|charm_left|shifter_down|family|user|team_num|real_team_num|sizex|sizey)[[:space:]]*=([^=]|$)' "$F"; then
    echo "FAIL: walker public-field WRITES remain"; exit 1
  fi
  if grep -nE '->stats\(\)->(magicpoints|max_magicpoints|hitpoints|max_hitpoints|level|frozen_delay|old_family)[[:space:]]*=([^=]|$)' "$F"; then
    echo "FAIL: stats public-field WRITES remain"; exit 1
  fi
  if grep -nE '->special_cost\[' "$F"; then
    echo "FAIL: special_cost array-subscript syntax remains; must use special_cost(i)/set_special_cost(i,X)"; exit 1
  fi
  if grep -nE '->(busy|current_special|dead|xpos|ypos|owner|lifetime|ani_type|cycle|curdir|lastx|lasty|stepsize|view_all|charm_left|shifter_down|family|user|team_num|real_team_num|sizex|sizey)([^_a-zA-Z0-9(]|$)' "$F" \
     | grep -vE '->(busy|current_special|dead|xpos|ypos|owner|lifetime|ani_type|cycle|curdir|lastx|lasty|stepsize|view_all|charm_left|shifter_down|family|user|team_num|real_team_num|sizex|sizey)\(' >/dev/null; then
    echo "FAIL: walker public-field READS remain (missing parens)"; exit 1
  fi
  if grep -nE '->stats\(\)->(magicpoints|max_magicpoints|hitpoints|max_hitpoints|level|frozen_delay|old_family)([^_a-zA-Z0-9(]|$)' "$F" \
     | grep -vE '->stats\(\)->(magicpoints|max_magicpoints|hitpoints|max_hitpoints|level|frozen_delay|old_family)\(' >/dev/null; then
    echo "FAIL: stats public-field READS remain (missing parens)"; exit 1
  fi
  wt_blob=$(git hash-object .juvenal-state.json)
  head_blob=$(git rev-parse HEAD:.juvenal-state.json)
  [ "$wt_blob" = "$head_blob" ] || { echo "FAIL: .juvenal-state.json blob drifted"; exit 1; }
  last_subj=$(git log -1 --pretty=format:%s)
  echo "$last_subj" | grep -q '^rebase: migrate master-inserted walker specials tests to accessor form$' \
    || { echo "FAIL: top commit subject is not the test-migration commit: $last_subj"; exit 1; }
  last_files=$(git log -1 --name-only --pretty=format: | sed '/^$/d')
  [ "$last_files" = "tests/test_walker_specials.cpp" ] \
    || { echo "FAIL: test-migration commit touched extra files: $last_files"; exit 1; }
  echo "OK"
  ```

## Success Criteria
- No `.git/rebase-merge/` or `.git/rebase-apply/` directory.
- No unmerged paths.
- `git merge-base HEAD origin/master == git rev-parse origin/master`.
- `ahead=329`, `behind=0`.
- No conflict markers under `src/`, `tests/`, `include/`.
- `effect_family_ghost_scare.cpp` contains both `return false; // delegate to effect::act default animate/die path` and `self->owner()`.
- `tests/test_walker_specials.cpp` has exactly one `session_state.h` include and ≥42 `TEST_F(WalkerSpecials,` cases.
- No public-field WRITE/READ syntax remains for any privatized walker / sim_entity / statistics field anywhere in `tests/test_walker_specials.cpp`. No `->special_cost[` subscripting.
- `.juvenal-state.json` working-tree blob still equals HEAD blob.
- Top commit subject is exactly `rebase: migrate master-inserted walker specials tests to accessor form` and touches only `tests/test_walker_specials.cpp`.

## Git Commit Requirement
The implementer MUST conclude the rebase fully (no `.git/rebase-merge/` directory remaining) and commit the post-rebase test-file migration as its own commit on top of the rebased history before yielding. Do not add build/test fixup commits in this phase. After yielding, the working tree must have no uncommitted tracked changes.
