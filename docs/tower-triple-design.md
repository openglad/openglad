# Master Specification — Game-Mode Seam, Fall Damage, HUD Floor Awareness, Tower Climb

Branch `feature/z-axis-multifloor` @ fd097693. All line anchors below were re-verified at this revision. **Line numbers drift as work packages land — every implementer must re-verify an anchor with `sed -n '<line>p'` / `grep -n` immediately before editing, and re-grep the canary pin map (`grep -n '"src/' tests/parity/scenario_table.h`) before every commit that touches a pinned file.**

---

## 0. Decision record (conflict resolutions — binding)

| # | Decision | Ruling & rationale |
|---|----------|--------------------|
| D1 | **Seam winner** | Design 4 "Two-Tier Game-Mode Seam": Tier A (sim rulesets, CTF-shaped) stays a documented recipe with zero code changes; Tier B (meta-progression) is a new `og::mode::IProgression` interface in og_resources, identity from campaign.yaml `mode:` key. Grafts: direct-to-user_path floor writes (Design 2), fold double-apply test (Design 2), pin-literal discipline + CTF-gate truth-table recipe (Design 1). |
| D2 | **Floor-id scheme: monotonic** | Floor N = scen id **700+N** (Gate = 700, floors 701+). The 701/702 ping-pong is REJECTED. Consequences: floors-climbed is DERIVED (`world.id − 700` in-sim, `scen_num − 700` in-session) and never stored as a run counter; SaveData v13 = `tower_best_floor` + `tower_run_seed` only (no `run_floor` field); the HUD relabel contract (D5) becomes satisfiable because the floor number is GameWorld-visible; the MP door's "counter flows via existing replication" claim is true. File growth in user_path is bounded by deepest-floor-ever and pruned at run start. |
| D3 | **Respawn clamp in tower: YES** | `world.respawn_mode` is clamped to 0 for tower levels. Reason (engineering lens): with respawns active, `respawn_suppress_team_wipe_endgame` makes team-wipe endgame unreachable → an endless score mode that cannot end. Implemented as a *seam hook* (`IProgression::clamp_respawn_mode`, identity for Classic) applied in both `sync_world_from_save_data` twins — reusable, parity-invisible (Classic identity; tower levels never in the parity table). Surfaced to the player via the Gate briefing line "No second lives within." `generator_rate`, difficulty %, and `keep_fallen_heroes` remain player-controlled in tower. Owner may revert by deleting TowerProgression's one-line override. **[OWNER SIGN-OFF #2]** |
| D4 | **Fall damage: always-on, no knob** | Physics like pit death. Single-floor levels can never see it (`apply_z_motion` early-return); all multifloor content is branch-authored. Escape hatch documented, not built: a `short fall_damage_percent = 0;` world scalar in the game_world.h:392-396 template slot, paid only if ever demanded (full sim-knob plumb per knob-plumbing recipe). **[OWNER SIGN-OFF #5]** |
| D5 | **Shared HUD element: one function** | `std::string floor_hud_label(const GameWorld&, int walker_floor)` in og_gameplay. Tower relabels by branching inside this function on `(world.type & TYPE_TOWER) && world.id > 700` — both GameWorld-visible, so the "edit only this function" contract holds. Formats: normal multifloor `"FLR: 2/3"`; tower single-story floor `"FLR: 23"`; tower multi-story `"F23: 2/3"`. Curses shows the same string (the tower spec's `"Fl 23"` cosmetic variant is dropped in favor of the shared helper). |
| D6 | **Scoring channels disjoint** | `m_score` keeps stock kill/gold semantics (parity-compared, wire-carried — untouched). Floors climbed = derived. Persistent: `tower_best_floor` (i16) + `tower_run_seed` (u32 as 2×i16), GTL v13. Zero snapshot/protocol changes. |
| D7 | **File pipeline: direct user_path writes** | The builtin `tower.glad` contains ONLY scen 700 (Gate) + campaign.yaml + icon and **must never contain ids ≥ 701** (mounted campaigns are PREPENDED and would shadow/freeze the run — enforced by a unit test on the zip member list). Generated floors are written to `<user_path>/scen/scenN.fss` + `<user_path>/pix/*.png` via the PhysFS write dir; loads fall through the mounted .glad miss to the user_path mount (og_file fallback chain, verified src/resources/io/og_file.cpp:169-201). NO runtime repack/remount — the editor temp/ race and "files still open" soft-fail are avoided entirely. |
| D8 | **Generation timing: prefetch at GO** | Floor `scen_num+1` is generated at GO time (`prepare_launch`), not at win-finalize. This makes the in-level exit prompt ("Exit to Floor N+1?", which reads the DESTINATION level's title) work, moves all generation to the safest window (pre-launch: no level files open, no sim context installed — the `GameplayContextGuard` non-reentrancy assert is satisfiable for audits), and makes `advance_cursor` nearly trivial. |
| D9 | **Tower never marks levels completed** | `marks_level_completed() == false` for all tower-campaign levels incl. the Gate. Falls out free: the already-completed entity purge (game.cpp:225-288) never fires, full time bonus every floor, `completed_levels` doesn't grow, level-select stays honest, exit-withdraw (`can_withdraw` needs completed destination) unreachable. |
| D10 | **Run-end semantics** | Team wipe / mission timeout / quit-mission mid-run = RUN OVER: `on_run_ended` resets the cursor to 700 and performs ONE field-merge save0 write (the enumerated exception to "losses persist nothing" — without it, relaunch resumes the death floor = infinite retries). Results RETRY is suppressed in tower (`suppress_retry()`); popup copy explains. Roster/gold/XP persist across runs (checkpointed climb, not strict roguelike; the strict variant is a documented one-flag flip). **[OWNER SIGN-OFF #3, #4]** |
| D11 | **PREF_FOES gating accepted** | The FLR row lives inside the F-key-hideable FOES box. In tower the floor is also pervasive elsewhere (level title "Floor N" at load, exit prompt, results overview line, between-floor picker cursor), so hiding the info corner does not orphan the score. Documented quirk; revisit only if playtests demand an always-on slot. |
| D12 | **Parity/canary wording** | Correct acceptance wording everywhere: "all PRE-EXISTING og_test_parity rows/tests pass byte-identically; the scenario table grows by exactly one Invariant row (fall damage), mutation-less exempt Invariant rows go 4 → 5, and walker.cpp pin 2173 is re-pinned once." Claims of "187/187 unchanged" are void once WP-1 lands. |
| Errata | **Pin-map corrections** | `save_data.cpp` canary pin is line **118** (scenario_table.h:1179) — designs citing 107 were wrong. `game_world.cpp` has THREE pins: **1620, 1622, 1710** (scenario_table.h:1193/1165/3291) — the HUD spec's "1620/1622 only" was wrong; its EOF append is still safe (appends shift nothing). walker.cpp pins: **1192×8, 1354, 2173** (2173 @ scenario_table.h:4805). walker_specials.cpp, screen.cpp, results_screen.cpp, score_panel.cpp, radar.cpp, fps_overlay.cpp, local_transport_shadow.cpp, headless_server_runtime.cpp, curses_*.cpp, picker_*.cpp, campaign_yaml.cpp, campaign_metadata.cpp carry NO pins. |

**Owner sign-off items** (enumerate in the PR description; each has a pinning test): (1) win-fold behavior deltas §2.4-B; (2) respawn clamp D3; (3) run-end semantics D10; (4) roster persists across runs D10; (5) fall damage always-on D4; (6) tower is desktop-first — Emscripten/IDBFS verified manually, not gated in CI (§7 R11).

---

## 1. Global invariants (apply to every work package)

1. **Deterministic SDL-free sim.** No new code may draw `world.rng_`, `ctx().rng`, or libc `rand()` on any path reachable by existing content. Generation uses a scratch `GameWorld(seed)` and counter-hash streams only.
2. **Single-floor byte-identity.** All fall-damage code lives inside `apply_z_motion`'s existing `floor_count()<=1` early-return (walker.cpp:1942-1944). All HUD code keys on a label that is empty for single-floor non-tower worlds. Radar bake cases key on tile bytes 140/141 absent from all legacy content.
3. **Wire discipline.** v1 = ZERO wire changes: protocol stays 6 (net_transport.h:64), snapshot 8 (world_snapshot.h:34), replay 9 (replay.h:17); the 5 literal wire-byte tests (test_net_transport.cpp:240, 829-830, 858-859, ~2654; test_input_state_net.cpp:133, 149) and the wrong-version literals (:910/:924/:940) are untouched. The MP door (§5.10) is enumerated and deferred.
4. **Mutation canary.** Pins are line+TEXT anchored; the in-CI gate checks line-range only, so drift is SILENT — every repin must be sed-verified and dry-run through `scripts/parity/_apply_mutation.py` (exit 6 = wrong line, 7 = ambiguous). Only WP-1 repins anything (walker.cpp 2173 → 2173+Δ at scenario_table.h:4805).
5. **GCC -Wconversion.** Every new arithmetic site uses the existing cast idioms (`static_cast<Sint32>(...size())`, `static_cast<short>`, `static_cast<float>`).
6. **Coverage gate ~90.00% line on src/** (razor-thin). Each WP ships its unit tests in the same change; judge by LOCAL baseline→delta (CI accumulates .gcda over `--repeat until-pass:3`, local single runs undercount); wipe stale `build/ci-coverage/**/*.gcda` before measuring. One-line `if (!x) return v;` keeps guard lines covered.
7. **Save format append-only.** GTL v12 → v13 exactly once (WP-5), all edits below the save_data.cpp:118 pin, both format-comment blocks (:143-194, :736-787) updated, test_save_data_versions.cpp writer branch + roundtrip + defaults.
8. **Menus: triple-client rules.** This spec adds NO menu_model rows, NO ButtonActions, NO subscreens (campaign-shaped entry). The only menu churn is the kShelf insert + its test re-pin and a shared lobby filter. If any picker edit grows beyond §5.9, consult the `openglad-menus` skill first.
9. **Shared dev machine: no wall-clock timing in tests.** All new tests are tick/trace/byte-compare driven.
10. **Test hygiene traps:** mount a campaign in every headless test that touches level IO (cfg-clobber hazard: failing unmounted runs rewrite `cfg/openglad.yaml` — `git checkout cfg/` to heal); remount `gladiator` after any test that writes save0 with the tower campaign current (`SaveData::load` mounts the saved campaign — the `--gtest_shuffle` trap); run a 30-seed shuffle sweep of og_test_view after HUD changes; settle the camera (one redraw) before any world_to_screen assertion.

---

## 2. PART I — The seam: Two-Tier Game-Mode

### 2.1 Taxonomy (the contract)

- **Tier A — sim-ruleset modes** (CTF today; a future horde mode): change what happens *within a tick* — win conditions, team-wipe suppression, per-tick scoring. They live in og_gameplay, are deterministic, snapshot-visible, parity-relevant, and irreducibly expensive. **Tier A gets NO new abstraction in this spec** — it gets a written recipe (§2.8). The tick fork (game_world.cpp:1785-1828), `respawn_suppress_team_wipe_endgame` (ctf.cpp:1161-1183), and walker.cpp are untouched by the seam.
- **Tier B — meta-progression modes** (Tower Climb; future boss rush / time attack / draft): change what happens *between levels* — sequencing, run lifecycle, persistence policy, results. They live in og_resources behind `og::mode::IProgression`. **Design theorem: a Tier-B mode must be implementable with no og_gameplay edits and no wire changes.** Tower Climb is the existence proof. The tiers compose: a future mode may pair a Tier-B object with a Tier-A engine.
- **Classic-respawn is NOT a mode.** It is a shared engine (CtfState respawn substate, `ctf.active` false) that composes with modes via the `respawn_mode` knob; the seam's clamp hook (D3) is how a mode constrains it.

### 2.2 Mode identity

**Source of truth: campaign package metadata.** `campaign.yaml` gains an optional `mode:` key (absent/`classic` → Classic; `tower` → Tower; unknown string → Classic + LogError once). Verified safe: the parser (`apply_campaign_pair`, src/resources/campaign_yaml.cpp:95-132) is an if/else key chain that silently ignores unknown keys — old binaries load a tower package as an ordinary campaign (graceful degradation). Rationale: the save already keys per-campaign cursors on campaign id, the lobby already syncs `campaign_id` (LobbySettings), and the campaign shelf is the shared menu surface for all three clients — deriving mode from the mounted campaign means nothing new to sync and nothing new on the wire, ever (the `lobby_settings_allow_shared_teams` keys-on-`kCtfCampaignId` precedent, ctf_constants.h:22).

Edits:
- `include/openglad/resources/campaign_yaml.h` — `CampaignYaml` += `std::string mode; bool saw_mode = false;`
- `src/resources/campaign_yaml.cpp` — parser: `else if (key == "mode") { out.mode = value; out.saw_mode = true; }` appended to the chain (~:128); emitter (`write_campaign_yaml_with_result`, ~:300-334): `emit_pair("mode", data.mode)` **only when `!data.mode.empty()`** — this keeps every existing campaign repack byte-stable (LOAD-BEARING; pin with a writer unit test).
- `src/resources/campaign_metadata.cpp` — memoized `std::string og::data::mounted_campaign_mode()` following the `campaign_display_title` pattern (:93; fast path when the query is the mounted campaign) with invalidation alongside `forget_campaign_display_title` (:125) on mount changes.

**Forward sim identity (authored, one v1 reader):** `SCEN_TYPE_TOWER = 16` (bit 0x10, free — verified constants.h:184-186 tops out at 4, game_world.h:134-137 at 0x8):
- `include/openglad/core/constants.h` (after :186): `inline constexpr char SCEN_TYPE_TOWER = 16;`
- `include/openglad/gameplay/game_world.h` (after :137): `static constexpr char TYPE_TOWER = 0x10;`
- The .fss type byte already round-trips arbitrary bits (read level_file_io.cpp:182-183 `world.type = new_scen_type`, write :678-679). The generator stamps it into every tower level. **The ONLY v1 reader is `floor_hud_label` (§4.1)** — a display-only pure function, never on the tick path, drawing no RNG. Sim rules never read it. It exists so a future Tier-A consumer can gate without a campaign lookup. TYPE_TOWER must NEVER be runtime-mutated (unlike CTF's activation-time bit drop); a level authored with both 0x8 and 0x10 resolves CTF-first (document in game_mode.h; unit-test the precedence in `kind` resolution if ever both exist — v1 content never authors both).
- `include/openglad/core/tower_constants.h` (NEW, mirrors ctf_constants.h): `inline constexpr std::string_view kTowerCampaignId = "tower"; inline constexpr int kTowerGateLevel = 700; inline constexpr int kTowerFirstFloorLevel = 701;`

### 2.3 `og::mode::IProgression` — the Tier-B interface

Placement forced by the dependency matrix: needs `SaveData` (resources) + `GameWorld` (gameplay, visible from resources); consumed by interface, platform-SDL, server, curses (all link og_resources). GameServer (gameplay) correctly cannot see it — its `std::function` hooks (game_server.h:196-199) remain the frontend-installed sequencing seam; only hook *bodies* change.

**NEW `include/openglad/resources/game_mode.h`** (+ `src/resources/game_mode.cpp`):

```cpp
namespace og::mode {

enum class ProgressionKind : short { Classic = 0, Tower = 1 };

// Filled by the calling frontend at endgame time.
struct LevelOutcome {
    short ending = 0;       // 0 = win, 1 = loss/team-wipe/timeout/withdraw,
                            // SCEN_TYPE_SAVE_ALL (4) = protect-fail
    short next_level = -1;  // sim next_level (-1 = none)
    bool networked = false;
    bool withdrawn = false; // ending==1 with a withdraw destination / quit-mission
};

struct ModePopup { std::string title; std::string body; };

class IProgression {
public:
    virtual ~IProgression() = default;
    virtual ProgressionKind kind() const = 0;

    // GO pressed with this campaign mounted, before the pre-launch save0 write.
    // Ensure content exists (generate/heal/prefetch); may veto the launch.
    virtual bool prepare_launch(SaveData& save, bool networked_session)
    { (void)save; (void)networked_session; return true; }

    // Idempotent content heal: make the level at save.scen_num loadable
    // (campaign-select / picker-preview hook; regenerates missing files).
    virtual void ensure_level_available(SaveData& save) { (void)save; }

    // Inside the shared win fold (§2.4): provision/verify content for
    // next_level and return the id the cursor should advance to
    // (== next_level normally; == save.scen_num to HOLD, e.g. on a
    // provisioning failure -> the player replays the current level).
    // MUST be idempotent: both screen::endgame and the shadow finalize call
    // the fold in local play.
    virtual short advance_cursor(SaveData& save, const GameWorld& world,
                                 short next_level)
    { (void)save; (void)world; return next_level; }

    // Whether a win marks completed_levels (tower: false — see D9).
    virtual bool marks_level_completed() const { return true; }

    // Whether frontends may autosave after this win (tower: true — D10
    // checkpointed climb; the strict-roguelike variant flips this AND gates
    // the enumerated autosave sites — documented, not built).
    virtual bool persist_after_win() const { return true; }

    // Per-mode world-knob clamps, applied in BOTH sync_world_from_save_data
    // twins right after the respawn_mode copy. Classic: identity.
    virtual short clamp_respawn_mode(short requested) const { return requested; }

    // Run terminated (team wipe / timeout / quit-mission / withdraw).
    // Classic: no-op (losses persist nothing — unchanged).
    virtual void on_run_ended(SaveData& save, const GameWorld& world,
                              const LevelOutcome& outcome)
    { (void)save; (void)world; (void)outcome; }

    // Results surfaces. Empty optional / empty vector = mode adds nothing.
    virtual bool suppress_retry() const { return false; }
    virtual std::optional<ModePopup> ending_popup(
        const SaveData&, const GameWorld&, const LevelOutcome&) const
    { return std::nullopt; }
    virtual std::vector<std::string> results_summary_lines(
        const SaveData&, const GameWorld&) const { return {}; }
};

ProgressionKind kind_for_mode_string(std::string_view mode); // pure; unit-tested
IProgression& current_progression(); // dispatch on og::data::mounted_campaign_mode()
IProgression& classic_progression(); // the static Classic instance (for tests)

} // namespace og::mode
```

Dispatch is a **switch over ProgressionKind with no default** (-Wswitch makes a missed case a compile error) returning static **stateless** instances — all mode state lives in SaveData (cursor = `scen_num`, seed/best = v13 fields) or is derived from `world.id`. No registry: modes are few, and resolution-from-mounted-campaign means zero staleness. WP-3 ships the switch with `case Tower:` falling through to Classic + a `// WP-5 replaces this line` marker; WP-5 (which depends on WP-3) edits exactly that marked line to return `tower_progression()`.

`game_mode.h` also carries the **Tier-A recipe** and the **future-mode checklist** as documentation blocks (§2.8, §2.9).

### 2.4 The shared win fold — four sites converge

**Verified current state of the four duplicated win-finalize sites** (all four re-implement: fold `m_score` → `m_totalscore`/`m_totalcash(×2)`, time bonus, zero `m_score`, `add_level_completed`, cursor advance, `update_guys`):

| Site | Anchor | Divergences found (verified in-tree) |
|---|---|---|
| `screen::endgame` win block | screen.cpp:1429-1491 | Zeroes `m_score` BEFORE calling `get_time_bonus` (which reads `save_data.m_score[0]`, results_screen.cpp:446-461) → its own bonus adds are 0 in practice; has the CTF rematch `add_level_completed` gate (:1459-1463); relies on `SaveData::save` forcing `current_levels` (:923-931); `!networked` autosave gate (:1472-1481). |
| `finalize_level_and_advance_cursor` | local_transport_shadow.cpp:339-391 | Canonical order (fold → bonus iff `!already_completed` → zero → mark → cursor → update_guys) but **MISSING the CTF rematch gate** (adds `add_level_completed` unconditionally); persists netsession + `persist_owned_characters_to_save0` (:173-212) or save0. |
| `complete_headless_level_and_load_next` | headless_server_runtime.cpp:444-492 | Shadow order with `calculate_headless_time_bonus` (:469); no rematch gate in the body; sets `current_levels` explicitly; `update_primary_team_totals`; in-memory checkpoint copy. |
| `advance_save_after_win` | curses_game_runtime.cpp:408-421 | NO time bonus (zeroes in the fold loop); rematch handled by the caller via `is_ctf_rematch_end` (:400-406); resolves `next_level<0` as `scen_num+1`; explicit `current_levels`; `update_primary_team_totals`. |

**NEW `include/openglad/resources/progression.h` + `src/resources/progression.cpp`:**

```cpp
namespace og::progression {

// CTF loss/rematch shape: a decided match whose next level IS this level.
// Centralizes the predicate previously duplicated at screen.cpp:1459-1461 and
// curses_game_runtime.cpp:400-406 (and missing from the shadow).
bool ctf_rematch_shape(const GameWorld& world, const SaveData& save,
                       short next_level);
// == ending==0 && (world.type & TYPE_CTF) && world.ctf.active
//    && world.ctf.winner_team >= 0 && next_level == save.scen_num

struct WinFoldContext {
    std::array<std::uint32_t, 4> time_bonus{}; // CALLER-computed (sources differ
                                               // legitimately per frontend)
    bool rematch_shape = false;                // caller fills via ctf_rematch_shape
    og::mode::LevelOutcome outcome{};
};

// The canonical fold (transplanted from the shadow body, verbatim order):
//  1. already = save.is_level_completed(save.scen_num)
//  2. for t: m_totalscore[t] += m_score[t]; m_totalcash[t] += m_score[t]*2
//  3. for t: if (!already) m_totalcash[t] += ctx.time_bonus[t]; m_score[t] = 0
//  4. if (current_progression().marks_level_completed() && !ctx.rematch_shape)
//         save.add_level_completed(save.current_campaign, save.scen_num)
//  5. if (ctx.outcome.next_level >= 0) {
//         short next = current_progression().advance_cursor(save, world,
//                                                 ctx.outcome.next_level);
//         save.scen_num = next;
//         save.current_levels[save.current_campaign] = next; }
//  6. save.update_guys(world.oblist)
// Callers do sync_save_data_from_world FIRST, compute time_bonus SECOND,
// call the fold THIRD, and keep their persist tails
// (save0 / netsession+owned-merge / in-memory checkpoint /
//  update_primary_team_totals) — persistence policy stays site-owned.
void apply_win_fold(SaveData& save, const GameWorld& world,
                    const WinFoldContext& ctx);
}
```

**Idempotence contract (MANDATORY unit test "apply twice == once"):** in local play BOTH the shadow finalize and screen::endgame run against the same SaveData. Second pass: `m_score` already zero → step 2/3 add zero; `already` is now true → no bonus; `add_level_completed` is set-idempotent; `advance_cursor` must be idempotent (Classic: pure; Tower: `best = max` + files-exist check — §5.5); cursor reassign idempotent; `update_guys` rebuild-idempotent. The implementer must additionally TRACE the local call graph (which site finalizes first in local / networked / headless / curses flows) and record it in a comment above `apply_win_fold`.

**B. Deliberate behavior deltas — [OWNER SIGN-OFF #1], each with a pinning test:**
1. **Shadow gains the CTF rematch gate** it was missing (networked CTF rematch no longer marks the level completed — aligns with the header-documented intent and the other two gated sites).
2. **Bonus semantics unified on the shadow's order** (bonus computed from live `m_score` before zeroing, iff `!already_completed`). The screen site becomes a provably-no-op second pass in flows where the shadow finalizes first; in any flow where the screen site is the FIRST finalizer, the fold's order can award a bonus the old zero-first code did not — the e2e locks (test_save_load_team, test_game_loop win flows, headless host_and_join_win_level1) must pass unchanged, and any delta they surface goes to the owner before merge. Escape hatch if rejected: per-site override flags on WinFoldContext.
3. **`current_levels` set explicitly in the fold** (headless/curses already did; screen/shadow relied on `SaveData::save` forcing it — harmless superset).
4. **Curses keeps zero time bonus** (passes a zero array — current behavior preserved; do NOT unify bonus math in this PR).

**Site rewrites (WP-3):**
- `screen.cpp:1429-1491` ending==0 body → `sync` stays implicit (endgame tail already syncs), compute `bonuscash` array via `get_time_bonus` BEFORE the fold, `ctx.rematch_shape = ctf_rematch_shape(...)` (evaluated before the cursor moves), `apply_win_fold`, keep the `!networked` + NEW `current_progression().persist_after_win()` gate around `update_guys`+`save("save0")` (note: fold already ran `update_guys`; keep the save-only tail), keep `world_.end = 1`.
- `local_transport_shadow.cpp:339-391` → sync + bonus array + rematch predicate + fold + existing netsession/save0 persist tail gated on `persist_after_win()`.
- `headless_server_runtime.cpp:444-492` → same with `calculate_headless_time_bonus`; keep `update_primary_team_totals` + checkpoint copy + reload tail.
- `curses_game_runtime.cpp:408-421` → fold with zero bonus array and caller-resolved `next_level<0 → scen_num+1`; `is_ctf_rematch_end` callers now route through `ctf_rematch_shape` (keep the thin local wrapper if call sites need the (world, ending, next) shape).

### 2.5 Loss / run-end routing

`on_run_ended` is called EXACTLY ONCE per non-win glad_main exit, at these sites (WP-3; Classic = no-op so all existing behavior is unchanged):
- `screen.cpp` — at the TOP of `screen::endgame`, BEFORE the `results_screen` call (:1404), guarded `ending != 0` (covers team wipe `ending==1/nextlevel==-1`, withdraw `ending==1/nextlevel!=-1`, timeout, `SCEN_TYPE_SAVE_ALL`). Running before results means the tower popup/summary read post-reset save state — they derive the death floor from `world.id`, best/seed from save (§5.7), so ordering is safe.
- `local_transport_shadow.cpp:399-417` — `finalize_withdraw_and_advance_cursor` prepends the hook (networked path; tower-unreachable in v1, kept for uniformity).
- `headless_server_runtime.cpp` loss/withdraw path; `curses_game_runtime.cpp` loss path.
The implementer must enumerate glad_main exit shapes (win / team-wipe / withdraw / quit-mission / Esc-abort / timeout) and confirm each non-win routes through exactly one of the above; an e2e per shape (§6, WP-6 tests) locks it.

### 2.6 Knob clamp

In BOTH `sync_world_from_save_data` twins — `screen.cpp:1183` and `headless_server_runtime.cpp:84` (bodies verified identical) — replace the respawn copy line:
```cpp
world.respawn_mode =
    og::mode::current_progression().clamp_respawn_mode(save.respawn_mode);
```
Classic identity ⇒ byte-identical everywhere today. Difficulty submenu's Respawns row stays visible-but-inert under tower (accepted; Gate briefing surfaces it).

### 2.7 Results surface dispatch (consumed in WP-6)

- `results_screen.cpp` `show_ending_popup` (:121-154): before the CTF check in ending==0 AND at the top of the ending==1 / SAVE_ALL branches, ask `current_progression().ending_popup(save, world, outcome)`; if set, `popup_dialog(title, body)` and return. `show_ctf_ending_popup` (:142) stays as-is (CTF migration optional, out of scope).
- Overview injection at the CTF banner point (:716-751 region, mode==0, before the gold/time block): draw `results_summary_lines(save, world)` centered at 8px pitch behind a non-empty guard.
- Retry suppression: in the results UI where the RETRY choice is offered (results_screen.cpp:463+ button loop — implementer greps for the retry button), gate on `!current_progression().suppress_retry()`. The TESTING short-circuit path (s_force_full_results_ui, :465-474) returns no-retry already.
- Curses verdict: `mission_verdict_line` (curses_game_runtime.cpp:390-399) appends `results_summary_lines` joined — done in WP-3 (curses file is WP-3-owned).

### 2.8 Tier-A recipe (documentation block in game_mode.h + docs/game-modes.md — NO code changes)

A sim-ruleset mode = (verbatim, with anchors): core `SCEN_TYPE_*` bit + `GameWorld::TYPE_*` constant; POD value-struct state embedded on GameWorld beside `CtfState ctf` (game_world.h:397; leaf header discipline of ctf_state.h:1-14); reset at level load; listed in BOTH LevelRuntimeData copy lists + old-world reset (level_runtime_data.cpp:275-277, 623-625, 644); `<mode>_run_tick` free function joining the fork at game_world.cpp:1785-1828 with the lazy-init `init_attempted` latch (ctf.cpp:1018-1024) and **the win latch that RE-ASSERTS `game_ended/ending/next_level` every tick** (tick entry resets them, game_world.cpp:1617-1621 — this trap has bitten twice); a clause in `respawn_suppress_team_wipe_endgame` (ctf.cpp:1161-1183; all three consumers — view.cpp:1393, game_server.cpp:1414/2148 — already gate on it); if replicated: snapshot block appended AFTER the CTF block (world_snapshot.cpp:807/841/2393/2839/2969 + OG_REPLAY_COMPARE) + the protocol/snapshot/replay triple bump + 5 wire-byte repins. Grafted disciplines from Design 1: (a) if the fork condition is ever refactored, the CTF gate `!(init_attempted && !active)` must be reproduced EXACTLY including owning the failing-init tick — pin with a 4-state truth-table unit test; (b) route new declarations into game_world.cpp via game_world.h includes, never by adding `#include` lines to game_world.cpp (pins 1620/1622 sit high; any line added above them shifts them silently). Create a `mode_run_completion_tick` dispatcher only when a THIRD sim engine appears.

### 2.9 The future-mode checklist (docs/game-modes.md, WP-3 deliverable; linked from docs/ARCHITECTURE.md)

Mode #3's bill, row by row: (1) identity = campaign.yaml `mode:` string + `kind_for_mode_string` case + dispatch case (exhaustive switch = compile error if missed); (2) one `IProgression` subclass (stateless if possible; persistent stats = v-gated SaveData fields); (3) content = campaign package (kShelf slot + shelf-test re-pin) and/or a generator over the WP-4 builder lib; (4) sequencing/persistence = `advance_cursor`/`marks_level_completed`/`persist_after_win`/`on_run_ended` overrides — ZERO edits to the four finalize sites; (5) results = `ending_popup`/`results_summary_lines`/`suppress_retry` — ZERO edits to results_screen dispatch; (6) knob policy = `clamp_respawn_mode` (generalize to a knob-clamp struct when a second knob needs clamping); (7) sim rules (only if genuinely new) = the Tier-A recipe, paid in full. Cross-cutting invariants (§1) appended. Deferred upgrades named: results-provider hoist at mode #4; in-session (non-return-to-menu) advance requires funding the currently-unreachable in-session reload path — stated so it lands on that feature's bill.

---

## 3. PART II — Fall damage (feature A)

### 3.1 Rule (final numbers)

**damage = min(0.15 × (N − 1), 0.50) × max_hitpoints**, N = total stories fallen in one uninterrupted cascade; clamped to ≥ 1.0 hp when nonzero; applied ONCE at settle (final landing on a non-air cell). Float math end-to-end (precedent: difficulty scaler walker.cpp:2082).
- 1 story: **free** (campaign fall-routes are designed traversal — Westlands E5 fall-line rule; keeps the teethed 1-story parity soldier byte-identical; costless for pursuing AI).
- 2/3/4 stories: 15% / 30% / 45% of max HP. 5+: capped at **50%** — a full-HP unit survives ANY fall; only wounded units die to one (fall cause is unattributable — no kill credit possible — so an instant-kill would be a degenerate MP tactic; knockback-off-ledge stays a *finisher*).
- **Always-on, no knob** (D4). Percent-of-max needs no per-family table and is symmetric across difficulty/level/family.

### 3.2 Accumulator lifecycle

New field `int fall_stories_ = 0;` in the **server-transient, non-replicated block** at walker.h:451-472 beside `z_cooldown_`/`z_stair_latched_`, with the same acceptance comment (mirrors/late joiners mid-cascade resolve a shorter fall; hp self-corrects on next snapshot — the accepted z_stair_latched_ contract; **no wire bump**).
- **Increment** at both landing sites in the TYPE_AIR branch (change_floor(below) @ walker.cpp:2028; nudge-landing true-branch @ 2031-2038).
- **Settle probe** after each landing: `w->smoother_for_floor(floor()).query_genre_x_y` at the RECOMPUTED post-move centre cell (the A5 nudge moves up to 4 cells). `!= TYPE_AIR` ⇒ `resolve_fall_landing()`; `== TYPE_AIR` ⇒ keep accumulating (cascade continues after z_cooldown_).
- **Reset without damage** — one line after the per-tick genre read (~walker.cpp:1968): `if (genre != TYPE_AIR) fall_stories_ = 0;` (covers stairs, ordinary ground, hover-walk-off stale case — forgiving by design, documented).
- **Reset** at both teleport `change_floor` sites (walker_specials.cpp:214, :262 — no pins in file) and in `walker::death()` after the `death_called` guard (~:1673; classic respawn REVIVES the same object — ctf.cpp:209-211).

### 3.3 Damage application — `walker::resolve_fall_landing()` (new private method, body just above apply_z_motion, ~:1939)

Flight-expiry idiom (living.cpp:172-191), NOT `do_combat_damage` (its FAMILY_HIT FX consumes one `rng_.next(3)` per landing — deterministic but perturbs every multifloor RNG stream for a cosmetic; the chosen idiom draws **zero RNG**):
1. `const int extra = fall_stories_ - 1; fall_stories_ = 0;` (reset first, unconditionally). 2. `if (extra <= 0) return;`
3. **Invulnerability RESPECTED**: `if (stats_->query_bit_flags(BIT_INVINCIBLE) || invulnerable_left() != 0) { TRACE(...); return; }` (walker_combat.cpp:251-253 shape) — deliberate divergence from the environmental precedents (pit death/flight expiry check nothing): the potion's promise is "no damage", and fall damage is damage. Pit death stays unprotected (a void, not damage). **Flag to reviewers.**
4. **Armor NOT applied** (percent-of-max already scales with toughness; stacking `get_damage_reduction` double-scales; rule stays legible).
5. `float dmg = std::min(0.15f * static_cast<float>(extra), 0.50f) * stats_->max_hitpoints(); if (dmg < 1.0f) dmg = 1.0f;`
6. Deterministic bookkeeping mirroring do_combat_damage's plain writes: `set_last_hitpoints`, deduct hp, push `DamageNumber` (RED, centre coords as floats, `tick_count_`), `set_regen_delay(50)`, myguy `scen_damage_taken`/`scen_min_hp` (walker_combat.cpp:220-225 shape; casts for -Wconversion).
7. Sound: `og::sim::emit_sound(current_game->sim_events, SOUND_CLANG)` unconditional (walker.cpp:1755 SOUND_EXPLODE precedent; NOT the DIE1/DIE2 RNG-drawing pattern; reusing EventKind::PlaySound avoids the entire new-EventKind coverage chain). SOUND_CLANG verified = 1 (sound_ids.h:23).
8. `if (stats_->hitpoints() <= 0) { set_dead(1); death(); }` — inline kill; unwinds through living::act's `if (dead()) return 0;` (living.cpp:87-88) like pit death. No score, no kill credit.

TRACE hooks (`"zaxis"` category): settle (stories/dmg/hp — fired on free settles too, giving tests a no-HP-math oracle), invulnerable-skip, lethal, teleport-reset.

**Exempt by construction (no code):** flyers (`BIT_FLYING || flight_left()`, guards :1954-1955/:2019) never enter the air branch; weapons excluded at :2019 (fall via act_fire separately); only livings call apply_z_motion (sole caller living.cpp:86). Enemies take it symmetrically. **Pit death (walker.cpp:2047-2051) stays byte-for-byte** — unconditional kill, no invulnerability check (death() now clears the accumulator, covering this branch).

### 3.4 Canary repin procedure (walker.cpp 2173 — the ONLY repin in this program)

All insertions (death() +1, helper ~30, apply_z_motion ~12, TRACEs) sit strictly between pins 1354 and 2173 ⇒ pins 1192×8 and 1354 unmoved. Procedure: (1) compute Δ = exact inserted-line count (expect ≈ 49); (2) edit the literal at scenario_table.h:4805 → 2173+Δ; (3) verify `sed -n '<new>p' src/gameplay/walker.cpp` prints exactly `return headus->team_num() == headtarget->team_num();` (the CI gate checks line-range only — drift is silent — the sed check is MANDATORY); (4) dry-run `scripts/parity/_apply_mutation.py` for all three walker.cpp pin texts (exit 6/7 = wrong/ambiguous; the "level_done = 0;" 5-identical-lines incident is the cautionary tale); (5) clean worktree before any canary run (it restores via `git checkout --`). No edits above walker.cpp:1391 are permitted in WP-1.

### 3.5 Parity plan

- **154 single-floor SemanticParity rows: provably untouched** — 4 independent gates (sim early-return walker.cpp:1942-1944; harness floor_count default 1 + apply_floor_setup no-op; dump floor-key omitted when 0; master-sourced goldens). The 2 existing Z rows are Invariant+branch-internal (dual-capture, no goldens) — deterministic HP changes pass.
- **Teeth 1:** extend `TEST(Parity, z_multifloor_walker_floor_transitions)` (test_parity_scenarios.cpp:401-442) with `pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, cents, cents)` on the existing 1-story fall dump pinning FULL HP (READ the actual dump value first — expected 120.00 → 12000 cents; args in cents per fact_predicate.h) — makes "first story free" teethed.
- **Teeth 2 — NEW row `z_fall_two_story_scen9301`:** clone the 5107-5111 row shape; `floor_count=3`; `kZFall2Spawns` (soldier, floor 2, pixel 112,112) + `kZFall2Paints` (PIX_AIR at cell (7,7) on floors 2 AND 1, grass pad floor 0); Invariant + branch_internal ⇒ no golden, no facts, no mutation, no master mirror. Same-gtest teeth: `WalkerOnFloor(soldier,0,0)` alive + `WalkerHpRangeAtFinalTick` band **[10199, 10201]** (120 − 15% = 102.00; regen_delay 50 > remaining tick budget so the value is exact — verify against the real dump before pinning). Add `OG_PARITY_TEST(z_fall_two_story_scen9301)` (test_parity_scenarios.cpp:~205). **Commit the regenerated `tests/parity/scenario_facts_generated.json`** (build regenerates into the source tree; a dirty tree blocks the canary).
- **Canary baseline:** mutation-less exempt Invariant rows go 4 → 5; `run_mutation_canary.sh --all` still exits 1 by design; **genuine toothless must stay 0**.

### 3.6 Calibration plan (shipped campaigns)

Extend both mapgen fall audits — westlands `cell_is_fall_landing` (tools/westlands_mapgen/main.cpp:465-489, already chases stacked air columns) and the longseason twin (~main.cpp:545) — to compute **fall depth in stories per designed fall line**, report per-level max, and self-check-fail any line deeper than 4 stories (the cap knee). Audits are report-only (generation unchanged ⇒ committed .glads stay byte-identical; regen+commit happens only if a flagged line forces a content ruling). Review attention set: Westlands 6/7/16/24 (3-floor), 14/22 (4-floor); Long Season 18 (3-floor) — each flagged line gets a design ruling: optional-shortcut (keep; damage is the price) vs mandatory-path (reroute). Mirror the ≤4-story bound into tests/unit/test_westlands_levels.cpp per its lockstep-classifier convention. Human playtest pass on Westlands 14/22 follows (WP-7).

### 3.7 Fall-damage tests (tests/unit/test_zaxiscpp — file already in og_unit_sim, no CMake edit; direct `apply_z_motion()` idiom per :110-129)

1. one_story_fall_free (hp unchanged, floor 0, no damage number); 2. two_story_fall_15pct (3-floor stack; EXPECT_NEAR hp = max×0.85; 1 RED damage number; regen_delay 50); 3. cap_at_50pct (6-floor shaft); 4. cascade_no_partial_damage (probe mid-cascade); 5. hover_walkoff_silent_reset (+ `fall_stories_for_test()` accessor under `#ifdef TESTING`); 6. stair/teleport reset (next fall charges only its own stories); 7. invulnerable_skip; 8. lethal_when_wounded (hp preset 10%; dead; heart drop with myguy); 9. knockback_off_ledge_kill (cause-agnostic — shove and walk-off indistinguishable at sim layer); 10. flyer_exempt_hp; 11. pit_death_unchanged (no damage number; invulnerability does not save). **Audit pass required:** existing test_zaxis falls (~:587-900) and AI-chase fixtures (:1164/:1262, pre-set hitpoints) for any ≥2-story drop now perturbing HP asserts — all must pass (no "pre-existing failure" excuses).

### 3.8 Tower coupling

The Mage Spires band (§5.6, band 4) shaft density and its no-flight-potion rule are calibrated against THIS formula (1-story free, 50% cap). Any formula change triggers a band-4 recheck. The knob escape hatch cannot ride a tower protocol bump (tower v1 takes none) — it would pay its own full plumb.

---

## 4. PART III — HUD floor awareness (feature B)

### 4.1 Shared element — `floor_hud_label` (THE mode-relabel seam)

Declaration in `include/openglad/gameplay/game_world.h` after the class (~:436), with the contract comment; definition appended at **EOF of src/gameplay/game_world.cpp** (file is 1983 lines; all pins ≤ 1710 — appends shift nothing):

```cpp
// Floor-awareness HUD label. Empty => call sites draw NOTHING (single-floor
// frames stay byte-identical). Game modes re-label by editing ONLY this
// function; every surface (SDL FOES-box row, curses status line) updates
// automatically. Call sites key on non-empty, never on floor_count.
std::string floor_hud_label(const GameWorld& world, int walker_floor);
```
Body: `floors = world.floor_count()`; `tower = (world.type & GameWorld::TYPE_TOWER) && world.id > og::kTowerGateLevel`; clamp `f = std::clamp(walker_floor, 0, floors-1)` (mirror int8 safety — walker_floor is attacker-controllable on a network mirror). Returns: non-tower & floors≤1 → `""`; non-tower → `std::format("FLR: {}/{}", f+1, floors)`; tower & floors≤1 → `std::format("FLR: {}", world.id - og::kTowerGateLevel)`; tower & floors>1 → `std::format("F{}: {}/{}", world.id - og::kTowerGateLevel, f+1, floors)` (8 chars for 2-digit floors — fits the 53px box via right-align). The Gate (id==700) falls through to the non-tower branch. Source is `control->floor()` (sim truth, snapshot-replicated — world_snapshot.h:142-149; same source the radar bakes from), NOT `viewscreen::current_floor_`: during Floor Glide the label snaps with the radar re-bake — intentional consistency. No RNG, no mutation, never on the tick path.

### 4.2 SDL row — last row of the PREF_FOES box, no pref, no cfg

Stair-overlay precedent ("core usability, deliberately not an effects toggle", view.cpp:1516-1523): no keyprefs slot (avoids the slot-8/9 garbage-sanitize hazard), no cfg key, no ButtonAction. Inside the PREF_FOES block (score_panel.cpp:567-631):
- Compute `floor_label = floor_hud_label(s->world_, static_cast<int>(control->floor()))`; `show_floor = !floor_label.empty()`.
- Box height: `Sint32 box_bottom = show_wave ? 24 : 16; if (show_floor) box_bottom += 8;` — replaces the literal in `draw_button(rm-57, tm+1, rm-2, tm + box_bottom, 1, 1)` (:588-589).
- Row drawn AFTER the wave block (:630) at `tm + (show_wave ? 26 : 18)` (touch variant `+44+8`, mirroring the adjacent `#ifndef USE_TOUCH_INPUT` blocks byte-for-byte structurally), right-aligned with the wave idiom `max<Sint32>(lm, rm - 2 - 6*static_cast<Sint32>(floor_label.size()))`, color `text_color`; add `TRACE("hud", "floor %s", floor_label.c_str())`. NEXT WAVE keeps its position; single-floor frames are pixel-identical (label empty).

### 4.3 fps_overlay dynamic placement (fixes the :74 hardcode collision)

Signature change (single caller verified: score_panel.cpp:638; header fps_overlay.h:7): `void draw_fps_overlay(screen& s, int counter_bottom_y)`, drawing at `counter_bottom_y + 2`. new_score_panel initializes `Sint32 fps_below_y = OVERSCAN_PADDING + 16;` before the player loop (FOES-off path byte-identical), records `fps_below_y = tm + box_bottom` for viewport 0 when the FOES block draws, passes it at :638. Bonus: fixes the pre-existing show_wave overlap. Existing tests fps_overlay_draws_when_enabled / fps_overlay_clears_foes_counter see identical y on their single-floor fixtures (tm = 0 + pad for viewport 0; box_bottom = 16 no-wave/no-floor).

### 4.4 Radar Z-stair bake — two static pinned colors, no pulse

Insert before the `default:` in radar::update()'s tile switch (radar.cpp:~729, `temp = 0` default verified):
```cpp
case PIX_ZSTAIR_UP:   temp = YELLOW;     break; // 88 — climb objective pops (reads on snow)
case PIX_ZSTAIR_DOWN: temp = LIGHT_BLUE; break; // 120 — exit-idiom cyan
```
Constants — ZERO new rng draws. Decor still wins per the :732-735 override. Editor minimap inherits via editor_floor_override_ → radar_terrain_floor. Single-floor byte-identity by tile absence (bytes 140/141 = branch-new, verified pixdefs.h:207-208, absent from master pixdefs and all legacy content). Pulse marker: explicitly deferred; documented insertion point = after the terrain putbuffer_alpha (radar.cpp:254-261), scan `grid_for_floor(bmp_floor_)` over the view window, `pointb` with the draw_stair_overlays triangle-wave alpha on `effects_frame_tick` (effects.cpp:645-648) — NEVER ctx().rng.

### 4.5 Curses status line

curses_renderer.cpp line1 builder, immediately after the `Lv` append (~:317), inside the `followed && followed->stats()` block: `const std::string flr = floor_hud_label(world, static_cast<int>(followed->floor())); if (!flr.empty()) line1 += "  " + flr;` — before the CTF group (clip priority: floor is navigation-critical). Tower relabeling flows automatically. Text client: nothing (protocol JSON already carries `floor` — text_protocol.cpp:49/:306).

### 4.6 Non-goals

World-view chevrons already ship (draw_stair_overlays — untouched). No pref slot / PREF_MAX bump / options_menu key; no cfg key / UI-FX button / ButtonAction; no wire changes; no text-client HUD; no radar reposition; no fix for the pre-existing view.cpp:937 mirror radar gate.

### 4.7 HUD tests

- **Pure label suite** (in `tests/test_glad_hud.cpp`, og_test_view — WP-2 owns this file; keeps tests/unit/test_zaxis.cpp exclusively WP-1's): empty at floor_count 1; "FLR: 2/3" at floor 1 of 3; "FLR: 1/3" at floor 0 (1-index pin); clamp −5→"FLR: 1/3", 99→"FLR: 3/3"; tower branch: TYPE_TOWER + id 723 + 1 story → "FLR: 23"; + 3 stories, floor 1 → "F23: 2/3"; Gate id 700 → "".
- test_glad_hud: `score_panel_shows_floor_indicator_on_multifloor` (trace_contains("hud","FLR: 2/3") + box-region pixel lambda over x∈[rm-57,rm-2], y∈[tm+17,tm+24]); `score_panel_floor_row_absent_single_floor` (region empty — byte-identity witness); `fps_overlay_clears_extended_foes_counter` (clone of :392's test with floor_count 2 → overlay strictly below tm+24).
- test_radar_more.cpp: `zstair_tiles_bake_pinned_radar_colors` (westlands-pin pattern :158-198 + fill_floor_grid :35-42; UP→bmp YELLOW, DOWN→LIGHT_BLUE after forced re-bake; restore editor_floor_override_ per :232-235).
- tests/curses/test_curses_renderer.cpp: `hud_line1_shows_floor_on_multifloor` (HeadlessTerminal; row1 contains "FLR: 2/3"; single-floor row1 has no "FLR").
- 30-seed `--gtest_shuffle` sweep of og_test_view after landing.

---

## 5. PART IV — Tower Climb ("The Endless Tower", feature C)

### 5.1 Concept & loop

Endless, seeded, procedurally generated climb shipped as a campaign-shaped mode: select **The Endless Tower** on the campaign shelf → team-build (your real save0 crew climbs; suggested_power 1) → GO → **The Gate** (scen 700, authored antechamber, no foes, exit → 701) → Floor 1 → win → return to team-build (the existing every-win invariant IS the between-floor shop: spend gold, hire, train) → GO → Floor 2 → … Each floor is one multifloor level (1-4 z-stories with Z-stairs — the branch's showcase; fall damage applies) whose FAMILY_EXIT destination is id+1. Per-floor win = classic rules (no sim changes): most floors kill-all-then-exit (exit refuses while foes live); ~1 in 4 non-boss floors are "open stairs" (TYPE_CAN_EXIT_WHENEVER — sprint past living foes, trading gold/XP for progress; briefing announces "The stairs stand open."). Death of the whole team (respawns clamped, D3) = RUN OVER. Full HP/MP restore at floor spawn (engine default — difficulty comes from the ramp, not attrition). Per-floor autosave stays ON (checkpointed climb; quit-and-resume works free; zero autosave-gate sites to audit).

### 5.2 Campaign package

`builtin/tower.glad` — committed, built by NEW `tools/tower_mapgen` (concept-campaign precedent: links the WP-4 builder lib + the 7 glue TUs; CMake target per :1309-1354 pattern; `scripts/generate_tower_campaign.sh`), staged (CMakeLists.txt:2473 pattern), restored by `restore_default_campaigns` at io_init (platform_io_common.cpp:391-414 — safe: Gate-only package; user-dir floors untouched). Contents EXACTLY: `campaign.yaml` (`mode: tower`, `first_level: 700`, `suggested_power: 1`, title "The Endless Tower"), `icon.png`, `scen/scen700.fss` + Gate grid PNG. **A unit test asserts the zip member list contains no `scen7XX` with id ≥ 701** (D7 shadowing invariant).

**The Gate (scen 700):** ~20×15 authored antechamber; `world.type = SCEN_TYPE_CAN_EXIT | SCEN_TYPE_TOWER`; one FAMILY_EXIT destination 701; `time_bonus_limit = 0`, `par_value = 0` (no free gold — get_time_bonus returns 0 when limit ≤ 0, verified results_screen.cpp:451-453); briefing (33-char lines): "The Tower has no end." / "Each floor bears its number." / "Death ends the climb." / "No second lives within."

### 5.3 Floor files & placement (D7/D8)

Floor N = `scen{700+N}.fss` (filename via the existing `std::format("scen{}.fss", id)` convention) + grid PNG `scen{:04d}` (8-char grid-field cap ⇒ id ≤ 9999 ⇒ floor cap 9299, unreachable), written DIRECTLY to `<user_path>/scen/` + `<user_path>/pix/` via the PhysFS write dir (set at io_init, platform_io.cpp:329). Load path: mounted tower .glad (prepended) misses ids ≥ 701 → falls through to the user_path mount / stdio user_path (og_file.cpp:169-201). NEW thin writer in og_resources: `og::data::save_level_to_user_dir(GameWorld&, int id)` — same payload/PNG writers as `save_level` (level_file_io.cpp:851-926) minus the `temp/` staging prefix; preserves the v9/v10/v11 version cascade (:653-661; multifloor floors emit v10/v11 automatically). Companions: `bool tower_floor_files_exist(int id)` — verifies the COMPLETE required set (non-empty .fss + floor-0 PNG + every `_fN` plane the .fss's floor_count implies; a torn set — truncated .fss, missing/zero-byte plane, e.g. interrupted IDBFS persistence — reads as absent so the heal regenerates the whole floor; `_dN` decor planes are NOT required: the writer skips empty ones so absence is ambiguous, and decor loss is cosmetic) — and `bool delete_tower_floor_files(int id)` (removes .fss + all grid/decor PNGs for the id). The loader hard-fails (gameplay path) on a declared-but-unloadable `_fN` plane instead of shipping an impassable void story; the editor bridge keeps its fix-it-up leniency. On Emscripten call the explicit IDBFS sync helper after writes (platform_io.cpp:265-311); tower is desktop-first — wasm verified manually (**[OWNER SIGN-OFF #6]**).

### 5.4 Seed policy & determinism

`tower_run_seed` (u32) drawn ONCE per fresh run in `prepare_launch` from the SESSION RNG (GameContext IRandom / std::random_device — NEVER `world.rng_`, NEVER libc rand; the *choice* needn't be deterministic, only regeneration from it), stored in SaveData v13 (persisted by the very next pre-launch save0 write — no extra write), shown in the run-end popup for sharing. `floor_seed(run_seed, N) = static_cast<uint32>(splitmix64(run_seed ^ (uint64(N) * 0x9E3779B97F4A7C15ULL)))`. All generation randomness = position hashes seeded with floor_seed + a local splitmix64 counter stream; the autotiler/smoother runs on a **scratch `GameWorld(floor_seed)`** (ctor takes seed — game_world.h:140). Audit-fail rerolls salt floor_seed +1..+3 deterministically; 4th attempt uses template T0 (audit-clean by construction) — **generation can never fail a run** (and `advance_cursor` still holds the cursor if file WRITING fails). Determinism contract pinned by test: (run_seed, N) → byte-identical .fss + PNG bytes, generate-twice compare. This contract IS the MP door (§5.10).

### 5.5 TowerProgression (the IProgression instance — stateless; NEW `src/resources/tower_progression.cpp`)

- `kind()` → Tower. `marks_level_completed()` → **false** (D9). `persist_after_win()` → **true** (D10). `clamp_respawn_mode(v)` → **0** (D3). `suppress_retry()` → **true**.
- `prepare_launch(save, networked)`: if `networked` → **false** (veto; picker shows the shared reason string "TOWER CLIMB is local-only") — backstop behind the shelf filter (§5.9). If `save.scen_num <= 700` (at the Gate = fresh run): draw new `tower_run_seed`; delete stale floors (`for (id = 701; delete_tower_floor_files(id); ++id)` — floors are contiguous, stop at first miss); generate floor 1 (prefetch — the Gate's exit prompt reads 701's title "Floor 1") — a failed floor-1 write **vetoes the GO** (returns false; the local picker shows "Could not prepare the tower's floors" — never a silent Gate-replay masquerading as Floor 1). Else (resume): heal current floor (`ensure` files for scen_num, regenerating from `(seed, scen_num−700)` if missing) — an unhealable current floor **vetoes the GO** (nothing to load) — + prefetch scen_num+1 (the in-level exit prompt needs its title; prefetch failure only logs — advance_cursor retries and holds). Return true.
- `ensure_level_available(save)`: the resume-heal alone (campaign-select/picker-preview hook — R9; missing-file fallback game.cpp:105-127 wraps to the mounted campaign's lowest id = the Gate, degraded-never-crashed).
- `advance_cursor(save, world, next)`: `save.tower_best_floor = max(best, static_cast<short>(next - 700))` (floors-climbed = highest floor REACHED; recorded when the cursor advances to it — die on 24 ⇒ "climbed 24", already ≥24 on disk from floor 23's win autosave); if `!tower_floor_files_exist(next)` regenerate from `(seed, next−700)`; on write failure return `save.scen_num` (HOLD — replay the floor; defuses the levels.front() wrap fallback that would silently reset a run); return `next`. Idempotent (max no-op; files exist ⇒ pass-through) — required by the double-finalize contract §2.4.
- `on_run_ended(save, world, outcome)`: guard `save.current_campaign == kTowerCampaignId && save.scen_num > 700` (mid-run; Gate withdraw = no-op). Field-merge write via the withdraw-rewrite pattern (screen.cpp:334-358): load a fresh save0 copy (its campaign IS tower mid-run ⇒ the `SaveData::load`-mounts trap is a no-op here), set `scen_num = 700`, `current_levels[kTowerCampaignId] = 700`, `tower_best_floor = max(disk, session)`, keep seed, write save0. Roster on disk stays the last floor-win checkpoint (losses don't update_guys). ONE write — the enumerated exception (D10).
- `ending_popup(save, world, outcome)`: derive N = `world.id − 700` (order-independent of the cursor reset). Win (id > 700): {"FLOOR CLEARED", "Floor {N} conquered.\nFloor {N+1} awaits."}; Gate win: {"THE TOWER OPENS", "Floor 1 awaits."}; team-wipe/timeout loss (id > 700): {"THE TOWER CLAIMS YOU", "You fell on Floor {N}.\nBest climb: {best}.\nSeed {seed:08X}"}; withdraw/quit mid-run: {"THE CLIMB ABANDONED", "You left the Tower on Floor {N}.\nBest climb: {best}."}; Gate loss → nullopt (generic popups). Win shapes consult the **post-advance cursor** (the win fold runs advance_cursor before the results dispatch, §2.4): when advance_cursor HELD (destination provisioning failed), the popup never claims "Floor {N+1} awaits" — it says the stair could not be raised and Floor {N} (or the Gate) stands again.
- `results_summary_lines(save, world)`: win overview → {"Floor {N} conquered — best {best}"}. Backed by pure `format_tower_summary(int world_id, short best)` / `format_tower_loss(...)` helpers — headlessly unit-tested (format_ctf_caps_segments coverage pattern).

### 5.6 Generation recipe (NEW `src/resources/mapgen/tower_floor_gen.{h,cpp}` over the WP-4 builder lib)

**Ramp (Normal 100%; session difficulty % multiplies via world.difficulty at load — game.cpp:132-133):** foe level `L(f) = 1 + (f−1)/3` (int div, soft cap 50 at f≈148); count `N(f) = min(7 + f, 30)` (cap f≥23; growth past 23 is level+elite composition); elites `E(f) = f/5` slots at L+2; boss floors (f%5==0): N×0.7 + named boss at L+3 (name ≤11 chars), vault posture. Player curve target `crew(f) ≈ 1 + f/4` vs `L(f) = 1 + f/3` ⇒ widening deficit ⇒ every run ends; target death windows at Normal: floors 20-35 (first runs), 40-60 (optimized) — calibrated, not assumed (§5.11).

**Bands (theme cycles every 30 floors; lap k = (f−1)/30; the ramp NEVER resets; lap k>0: elite share +10%/lap cap 50%, +1 named elite per lap on boss floors):**

| Band (floors) | Theme / base | Size, stories | Foe mix (% of N) | Boss (f%5==0) | Generators (4 families: tent=skel, tower=mage, bones=ghost, treehouse=elf) | Templates (weights) | Potion |
|---|---|---|---|---|---|---|---|
| 1-5 The Bailey | grass, paths, wall clusters, trees | 34×34, 1 (f5: 2) | soldier 50, elf 25, archer 25 | "Gatewarden" (BIG_ORC) | treehouse ×1 at f≥3 | T1 60/T0 20/T2 20 | speed |
| 6-10 The Barracks | pavement/cobble wall-grid rooms, carpet officer room | 36×30, 1-2 | soldier 40, archer 25, thief 15, cleric 10, big orc 10 | "Drillmaster" (BARBARIAN) | none | T2 60/T5 20/T0 20 | magic |
| 11-15 The Undercroft | dark dirt warren, water channels, boulders, bones | 40×36, 2 | skeleton 40, ghost 20, slime 15, cleric 15, thief 10 | "Cryptlord" (GIANT_SKELETON) | bones ×1 + tent ×1 | T3 50/T2 30/T6 20 | magic |
| 16-20 The Mage Spires | carpet/pavement, columns, PIX_AIR ring shafts on upper stories | 30×30, 3 | mage 35, fire elem 20, faerie 20, archer 15, archmage 10 (f≥18) | "Spirelord" (ARCHMAGE) | mage tower ×1-2 + 2 FAMILY_TOWER1 turrets flanking stairs | T4 70/T2 30 | speed; **NO flight** (fall-hazard integrity — feature-A coupling §3.8) |
| 21-25 The Furnace | ash, lava moats (solid to grounded walkers) + pavement bridges | 38×34, 1-2 | orc 35, big orc 25, fire elem 25, golem 10 (f≥23), druid 5 | "Forgeheart" (GOLEM) | tent 0-1; turrets 0-2 | T5 50/T3 25/T0 25 | invis |
| 26-30 The Summit | snow (≥40 snow tiles ⇒ Snow weather, deliberate), cliff edging | 40×40, 2 | barbarian 30, druid 20, elf 15, giant skel 15, archmage 10, ghost 10 | "Stormcrown" (ARCHMAGE + golem pair) | treehouse ×1 | T1 40/T2 30/T4 30 | flight allowed |

Every ~3rd floor rolls a variant (hash-picked): "rival raiders" (a team-2 squad hostile to both sides — Westlands third-party precedent) or "ambush posture" (guards-only ACT_GUARD ambush posts per the guard-wake policy — room-by-room fights; tower has no allies so hold-post never applies).

**Templates:** T0 arena (walled rect hall — the always-clean fallback), T1 courtyard (paint_rect clusters + paths), T2 halls (wall-grid 4-9 rooms, 2-tile doors), T3 warren (drunk-walk tunnel carver + end chambers), T4 spire (stacked shrinking stories, paint_ring, AIR ring gaps on stories ≥1), T5 moat (paint_ring lava/water + 2 bridges), T6 vault (T2 + treasure room behind a guard post).

**Treasure economy per floor:** gold bars `2+f/10` (cap 5) at L (value 200×L), silver `2+f/8` (cap 6) at L (50×L), drumsticks `2+stories` along the route, exactly 1 band-schedule utility potion hash-placed mid-route. Vault floors (f%5==0): +3 gold at L+1 + the run's only invulnerable potions, in a guarded T6 vault room. `time_bonus_limit = 2600 + 300×stories + 12×N(f)` ticks; `par_value = 4 + 2×band_index + 3×lap`.

**Recipe order (deterministic):** (1) floor_seed; pick template by band weights; (2) init_world(stories, tw, th); paint band base; carve template; (3) `stair_pair` ×2 per z-boundary on standable cells, opposite quadrants; smooth_world (scratch-world rng); (4) `place_start` ×10 in the entry zone (story 0, LEAD MARKER FIRST, 2×2 clearance); (5) sort rooms by path distance from entry; fill squads room-by-room from the composition row — room guards plain ACT_GUARD, 25% of N corridor roamers; (6) generators in a rear room; elites/boss final room / top story near the exit; turrets at chokepoints where the band allows; (7) treasures per economy (scatter helpers keep clearance, never cover Z-stairs); (8) scatter boulders/litter/decor; (9) `place_exit` top story, far quadrant, destination id+1; `world.type = SCEN_TYPE_TOWER | (open-stairs ? SCEN_TYPE_CAN_EXIT : 0)`; (10) title "Floor {N}", 1-3 band-voice briefing lines (33-char budget; + "The stairs stand open." on open floors), par/limit; `world.id = 700+N`; (11) **audits** (copied westlands set + additions): footing/standability, stair alignment per z-boundary, A*-reachability of every living + generator + exit from the lead marker, fall-line (no walkable-adjacent PIX_AIR over unstandable ground) + fall-depth ≤ 4 stories, MAXOBS worst-case ≤ 120 **modeling generator_rate=400 (Frenzy) and band-3 slime splitting at 2× slot count**, title/briefing budgets; fail ⇒ salt reroll ×3 ⇒ T0; (12) `save_level_to_user_dir`.

**Audit execution context:** audits needing pathfinding/obmap install a `GameplayContextGuard` (gameplay_context.h:51-92) over the SCRATCH world. Safe because all generation runs at GO-time (§D8) when no gameplay context is installed (the guard asserts non-reentrancy). The live sim RNG / ctx().rng / libc rand are never touched — the generate-twice byte-compare test catches stream leaks.

### 5.7 Scoring & results (D6)

`m_score` = stock in-floor gold/kill scoring (funds the shop; folds to totals at each floor win — default path). Floors climbed = derived (`world.id − 700` in results/popups; `scen_num − 700` in menus). `tower_best_floor` updated in `advance_cursor`, persisted by the normal floor-win autosave — loss-time write only resets the cursor. Results surfaces per §2.7 + §5.5. MVP runs stock. HUD floor display = feature B's label (§4.1) — zero extra work.

### 5.8 Persistence — SaveData GTL v13 (WP-5)

- `include/openglad/resources/save_data.h` (~:74, after keep_fallen_heroes): `short tower_best_floor = 0; std::uint32_t tower_run_seed = 0;`
- `src/resources/save_data.cpp`: version literal :710 `12 → 13`; write: append after the v12 trio (:999-1005) three int16s — `tower_best_floor`, `seed_lo = seed & 0xFFFF`, `seed_hi = seed >> 16`; read: `if (temp_version >= 13)` gate with else-defaults after the :536-553 v12 block; update BOTH format comments (:143-194, :736-787). All edits below the :118 pin — zero repins.
- tests/test_save_data_versions.cpp (:79-211): v13 synthetic-writer branch + roundtrip pinning the version byte + old-save (v12) load-defaults test.

### 5.9 Menu entry, gating, MP scope

- **Shelf:** insert `"tower"` into kShelf (picker_common.cpp:734-744) after longseason, before ctf → {gladiator, tryxian, westlands, longseason, **tower**, ctf, arenas, concept}. Re-pin `tests/unit/test_picker_common.cpp` order_campaigns_for_select test (~:1524). That is the ENTIRE menu surface: no main-menu item, no PickerMenuCommand, no ButtonAction, no menu_model rows — all three clients inherit entry via the shared campaign select.
- **Networked gating, two independent layers + server backstop:** (1) NEW pure helper `og::ui::filter_campaigns_for_networked_lobby(std::list<std::string>&)` in picker_common — v1 removes `kTowerCampaignId` (the kCtfCampaignId keying precedent; a yaml-driven `campaign_mode(id)` accessor is the documented upgrade when a second mode campaign exists). Called when in a networked session at the shelf call sites: `src/interface/ui/campaign_picker.cpp:309` and `:483` (SDL — gate on `networked_session_`, session_state.h:91), `text_picker.cpp:148`, `curses_picker_client.cpp:852`. (2) `prepare_launch` veto (§5.5) at GO with the shared reason string. (3) `LobbyServer::sanitize_settings`, **networked lobbies only**: `if (!local_session && sanitized.campaign_id == og::kTowerCampaignId) { campaign_id = fallback.campaign_id; scenario_id = fallback.scenario_id; }` (crafted-client defense; explicit rejection via the existing settings-refresh flow). The locality comes from a `LobbyServer` constructor flag (`local_session`, default false = reject, NOT a wire field): the solo picker's `LocalPickerLobbyClient` round-trips its own settings through this same server, so an unconditional rejection silently reverted a just-picked tower to the fallback pair without a remount — mount = tower, save = fallback, a split-brain ghost session (display bootstraps the Gate, the authoritative sim loads the fallback level, every keyframe is grid-size-rejected). Only the local in-process echo opts in; all networked construction sites keep the default backstop.
- **prepare_launch call site:** go_menu (picker_team_build.cpp:2569-2679) BEFORE the pre-launch save0 writes (:2587 emscripten / :2615 native) — the fresh seed rides that write; veto aborts GO with a status message. `ensure_level_available` at campaign-select completion (post-mount, pre-preview).
- **Local clients:** SDL and curses both get tower (curses' finalize/loss paths are seam-threaded in WP-3; generation is SDL-free). The headless server only hosts networked sessions — tower filtered.
- **MP door (enumerated, DEFERRED — no v1 work):** identity already syncs (campaign_id in LobbySettings). The later change is exactly: LobbySettings += `tower_run_seed` (u32 as 2×i16) appended at end of write/read_lobby_settings (net_transport.cpp:288-319) + LobbySaveDataEquivalent + sanitize + apply_lobby_game_start_config plumb; joiners run the prepare_launch regeneration from the seed before load (deterministic by §5.4's pinned contract); fold remains identical. Wire bill: kNetworkProtocolVersion 6→7 (+ changelog comment, net_transport.h:49-64) + kSnapshotFormatVersion 8→9 + kReplayFormatVersion 9→10 (convention: the three bump together); re-pin the 5 literal wire-byte tests (test_net_transport.cpp:240, 829-830, 858-859, ~2654; test_input_state_net.cpp:133, 149); wrong-version literals (:910/:924/:940 use version+1) must stay distinct. NO snapshot payload change (no sim-side tower state, ever, by construction).

### 5.10 Interactions & quirks (documented)

Respawns clamped (D3); generator_rate (Calm/Frenzy), difficulty %, keep_fallen_heroes stay player-controlled (dead heroes at a floor WIN follow the permadeath knob — per-floor roster stakes exist between wipes). Mission timeout (36000 ticks) = run over (floors target 1800-4500 ticks — far under). Withdraw-at-exit unreachable (D9); quit-mission = run over. Continue Game resumes a run across restarts (checkpoint + heal). Retry suppressed. CtfState untouched (tower adds no Tier-A state).

### 5.11 Calibration (tower)

- **CI pin:** NEW `tests/unit/test_tower_calibration.cpp` (clone tests/unit/test_westlands_calibration.cpp mechanics): fixed run_seed, floors {1,5,10,15,20} generated via tower_floor_gen, loaded headlessly, tick-600 survivor-floor pins (min across 3 seeds where the harness supports it).
- **Pre-ship sweep (WP-7 verification, not CI):** openglad_text-protocol bracket sweeps at floors {1,5,10,15,20,25,30} × seeds {42,1337,2025} × rosters {8-mixed, 4-soldier} with crews at curve(f)=1+f/4. Gates: f1-10 clear at curve 3/3 seeds; f11-20 2/3; f21-30 survival-to-tick-1500; wipe rate monotone in f. Expect 1-2 recalibration waves (Westlands F4 precedent). Economy watch: gold 200×L/floor at depth vs the training-cost curve — flag if tower out-pays campaigns.

---

## 6. PART V — Implementation plan (DAG of work packages)

**File-ownership rule:** a file appears in exactly one WP below (except CMakeLists.txt — see note). Parallel agents build Wave 1 concurrently; Waves 2-3 are sequential on their deps. Every WP leaves the tree green (`cmake --build --preset ci-test && ctest --preset ci-test`).

**CMakeLists.txt note:** touched by WP-3/4/5/6 with append-only edits to DISTINCT lists (OG_RESOURCES_SOURCES, OG_GAMEPLAY_SOURCES, unit-group registrations, tool target, staging, ALL_INTEGRATION_TEST_SOURCES). Each WP enumerates its delta; the integrator rebases/merges in wave order — conflicts are mechanical.

### Wave 1 (parallel — mutually file-disjoint)

**WP-1: Fall damage** (§3). Owns: `include/openglad/gameplay/walker.h`, `src/gameplay/walker.cpp`, `src/gameplay/walker_specials.cpp`, `tests/unit/test_zaxis.cpp`, `tests/parity/scenario_table.h` (+kZFall2 arrays, +row, 4805 repin), `tests/parity/test_parity_scenarios.cpp`, `tests/parity/scenario_facts_generated.json` (regen+commit), `tools/westlands_mapgen/main.cpp` + `tools/longseason_mapgen/main.cpp` (fall-depth audit ADDITIONS ONLY — report-mode; no generation change; no .glad regen unless a ruling forces one), `tests/unit/test_westlands_levels.cpp`, `docs/z-axis-design.md` (§ falls: the rule, invulnerability, no armor, pit death unchanged, exemptions). No CMake edits. Tests: §3.7 battery + §3.5 parity teeth + repin verification (§3.4). Constraint: no edits above walker.cpp:1391; no edits to living.cpp/walker_combat.cpp/game_world.cpp.

**WP-2: HUD floor awareness + identity constants** (§4 + §2.2 constants). Owns: `include/openglad/gameplay/game_world.h` (TYPE_TOWER + floor_hud_label decl), `src/gameplay/game_world.cpp` (EOF definition ONLY — verify includes for `<format>`/`<algorithm>` exist at file top; if an include must be ADDED, it shifts pins 1620/1622/1710 → forbidden; use fully-qualified alternatives or add includes to game_world.h instead), `include/openglad/core/constants.h` (SCEN_TYPE_TOWER), NEW `include/openglad/core/tower_constants.h`, `include/openglad/interface/fps_overlay.h`, `src/interface/fps_overlay.cpp`, `src/interface/score_panel.cpp`, `src/interface/render/radar.cpp`, `src/platform/curses/curses_renderer.cpp`, `tests/test_glad_hud.cpp`, `tests/test_radar_more.cpp`, `tests/curses/test_curses_renderer.cpp`. No CMake edits. Tests: §4.7.

**WP-3: The seam** (§2). Owns: NEW `include/openglad/resources/game_mode.h` + `src/resources/game_mode.cpp`, NEW `include/openglad/resources/progression.h` + `src/resources/progression.cpp`, `include/openglad/resources/campaign_yaml.h`, `src/resources/campaign_yaml.cpp`, `src/resources/campaign_metadata.cpp`, `src/interface/screen.cpp` (endgame fold + on_run_ended + sync clamp), `src/platform/sdl/local_transport_shadow.cpp` (finalize fold + withdraw hook), `src/server/headless_server_runtime.cpp` (fold + clamp twin), `src/platform/curses/curses_game_runtime.cpp` (fold + loss hook + verdict-line summary), NEW `docs/game-modes.md` + `docs/ARCHITECTURE.md` link, NEW `tests/unit/test_game_mode.cpp` + `tests/unit/test_progression.cpp`. CMake: OG_RESOURCES_SOURCES + 2 unit registrations. Tests: kind_for_mode_string table; dispatch exhaustiveness (every ProgressionKind returns an instance); classic defaults; clamp identity; campaign.yaml mode round-trip + **emit-only-when-non-empty byte-stability**; mounted_campaign_mode memoization/invalidation; fold shapes — fold-of-zeros no-op, **apply-twice == apply-once**, bonus iff !already_completed, rematch no-mark (4-state CTF truth table over {active,winner,next==scen} vs the old inline predicate), tower-independent `marks=false` path via a stub progression, next_level −1 guard, current_levels set. Behavior locks: run test_save_load_team, test_game_loop win flows, headless host_and_join e2e before/after; any delta → owner (§2.4-B).

**WP-4: Builder library** (mapgen extraction — COPY, tools unchanged). Owns: NEW `include/openglad/gameplay/mapgen/builders.h`, NEW `src/gameplay/mapgen/builders.cpp` + `src/gameplay/mapgen/audits.cpp` (copied from tools/westlands_mapgen main.cpp:165-309 + builders.h:37-147 + audits :341-524/926-1399 — the subset tower needs: init_world/make_grid, paint/paint_rect/paint_ring, stair_pair, smooth_world, place/place_living/place_generator/place_start/place_exit, scatter_*, standability/footing/stair-alignment/A*-reachability/fall-line+depth audits; every helper takes an explicit `std::uint32_t seed` mixed into its position hashes), NEW `tests/unit/test_mapgen_builders.cpp`. CMake: OG_GAMEPLAY_SOURCES + unit registration. Tools keep their copies in v1 (protects committed .glad bytes); the lib is authoritative going forward; tool retargeting = WP-8 follow-up with byte-identity verification, **named owner required** (drift risk if unowned). Tests: grid-byte pins, placement clearance, audit pass/fail fixtures, seed determinism (same seed → identical grids/entity lists), GameplayContextGuard audit smoke.

### Wave 2

**WP-5: Tower content + progression + save v13** (§5; depends WP-2 constants, WP-3 interface, WP-4 builders). Owns: NEW `include/openglad/resources/mapgen/tower_floor_gen.h` + `src/resources/mapgen/tower_floor_gen.cpp` (bands/templates/economy/audits/reroll/fallback + floor_seed), `src/resources/level_file_io.cpp` (save_level_to_user_dir + exist/delete helpers — verify no other WP touches this file: none do), NEW `src/resources/tower_progression.cpp`, `src/resources/game_mode.cpp` **dispatch-switch marked line only** (sequential edit, sanctioned), `include/openglad/resources/save_data.h` + `src/resources/save_data.cpp` (v13), `tests/test_save_data_versions.cpp`, NEW `tools/tower_mapgen/main.cpp` + `scripts/generate_tower_campaign.sh` + committed `builtin/tower.glad`, NEW `tests/unit/test_tower_floor_gen.cpp`, NEW `tests/unit/test_tower_progression.cpp`, NEW `tests/unit/test_tower_calibration.cpp`. CMake: OG_RESOURCES_SOURCES entries, tool target, .glad staging, 3 unit registrations. Tests: generate-twice byte-compare (.fss + PNGs); audit sweep floors 1-60 × 3 seeds (the coverage engine for the recipe); T0 fallback path; MAXOBS Frenzy budget; reload self-check (LevelRuntimeData + headless hooks in the test binary — westlands self-check pattern); .glad member-list (no ids ≥ 701); v13 writer/roundtrip/defaults; TowerProgression lifecycle — fresh-run seed+prune+prefetch, resume heal, advance/best/hold-on-write-failure, advance idempotence, on_run_ended reset+field-merge (+ Gate no-op guard), clamp=0, popup/summary formatters (all popup shapes); mount hygiene per §1.10 in every test that writes save0.

### Wave 3

**WP-6: Tower UI surfaces + entry + gating + e2e** (depends WP-3, WP-5). Owns: `src/interface/ui/picker_common.cpp` + `include/openglad/interface/ui/picker_common.h` (kShelf insert + filter helper), `src/interface/ui/campaign_picker.cpp` (2 filter call sites + ensure_level_available heal call), `src/platform/text/text_picker.cpp`, `src/platform/curses/curses_picker_client.cpp` (filter calls), `src/interface/ui/picker_team_build.cpp` (prepare_launch at GO + veto message), `src/gameplay/lobby_server.cpp` (sanitize rejection), `src/interface/ui/results_screen.cpp` (popup dispatch + summary injection + retry suppression per §2.7), `tests/unit/test_picker_common.cpp` (shelf re-pin + filter unit), NEW `tests/test_tower_run.cpp` (integration; CMake: ALL_INTEGRATION_TEST_SOURCES + og_add_test_group assignment). Tests: shelf order re-pin; filter drops tower only when networked; sanitize rejects tower campaign (lobby unit patterns); retry suppressed (TESTING results path); e2e — mount tower → prepare_launch (fresh: seed drawn, 701 files exist) → drive `finalize_level_and_advance_cursor` for a Gate win (cursor 701, best 1) and a floor win (files for 702 prefetched at next GO; cursor/best advance) → loss shape: on_run_ended (cursor 700, best retained, ONE save0 write) → resume shape (heal regenerates deleted floor byte-identically); every non-win glad_main exit shape routes on_run_ended exactly once; remount gladiator in teardown.

**WP-7: Integration & verification** (no file ownership — gates only): merge order WP-1→2→3→4 (any order within wave 1) → 5 → 6; full ctest; §6-global gates (below); manual canary run; fall-depth audit report review + design rulings + Westlands 14/22 playtest; tower calibration sweep (§5.11); wasm manual verification (tower generation + IDBFS persistence + HUD row); coverage baseline→delta.

**WP-8 (follow-ups, out of v1 scope, tracked as issues):** tool retargeting onto the builder lib (byte-identity gate); the MP wire door (§5.9); radar pulse markers if playtests demand; user_path floor-file windowed prune beyond run-start cleanup; strict-roguelike flag if the owner wants it.

---

## 7. PART VI — Test plan (global gates; per-WP tests listed in §6)

1. **Full suite:** `cmake --preset ci-test && cmake --build --preset ci-test && ctest --preset ci-test` green at every WP merge.
2. **Parity:** all PRE-EXISTING og_test_parity tests byte-identical (goldens never re-captured; `scripts/parity/run_parity_diff.sh` spot-checks on any doubt); after WP-1 the table has +1 Invariant row, `OG_PARITY_TEST` added, regenerated `scenario_facts_generated.json` committed; WP-2..6 make NO parity-visible change by construction (no sim semantics, no RNG, tower levels never in the table; the WP-3 fold is session-layer, invisible to parity_runner which drives GameWorld directly).
3. **Mutation canary (manual, not CI):** clean worktree; dry-run `_apply_mutation.py` on all walker.cpp pins after WP-1's repin (sed-verify first); spot canary run on walker.cpp; expected: 5 mutation-less exempt Invariant rows, `--all` exits 1 by design, **genuine toothless = 0** (`grep -c '0 flips (mutation' log`). Before EVERY commit in any WP: `grep -n '"src/' tests/parity/scenario_table.h` and confirm no pinned file's pinned line shifted (pins: walker.cpp 1192×8/1354/2173+Δ; living.cpp 114; walker_combat.cpp 99/209/302; game_world.cpp 1620/1622/1710; save_data.cpp 118).
4. **Coverage:** local baseline→delta per WP (wipe stale .gcda; CI accumulates over repeats so absolute local numbers undercount); the dominant new-line masses (builders/audits ~550, tower gen ~650, seam ~300) are covered by their determinism/audit-sweep/fold-shape unit batteries by design.
5. **Menu/layout pins:** only tests/unit/test_picker_common.cpp shelf test re-pins; test_menu_layout untouched (no menu shape change) — verify by running it.
6. **Shuffle/order:** 30-seed `--gtest_shuffle` sweep of og_test_view (HUD) and of the new tower unit binaries (mount-order trap).
7. **Wire pins:** test_net_transport + test_input_state_net untouched and green (protocol 6 / snapshot 8 / replay 9 asserted unchanged).
8. **Save:** test_save_data_versions v13 branches; old-binary tolerance is by-construction (trailing bytes ignored — reader never reads past known fields).
9. **e2e locks for the fold deltas:** test_save_load_team, test_game_loop win flows, headless host_and_join_win_level1 — must pass unchanged or the delta goes to the owner.
10. **Calibration gates:** test_tower_calibration (CI pin) + test_westlands_levels fall-depth mirror (CI) + the WP-7 sweeps (pre-ship, non-CI).

---

## 8. PART VII — Risk register

| # | Risk | Sev | Mitigation / tripwire |
|---|------|-----|----------------------|
| R1 | Fold convergence changes save-affecting behavior (bonus semantics, shadow rematch gate) | HIGH | §2.4-B enumerated deltas + owner sign-off; per-site commits; e2e locks (§7.9); double-apply unit test; escape hatch = per-site WinFoldContext flags. |
| R2 | Double-finalize (screen + shadow on same SaveData) breaks idempotence | HIGH | Fold order transplanted verbatim from shadow; advance_cursor idempotence contract + tests; implementer traces and documents the local call graph. |
| R3 | Canary pin drift silent in CI (walker.cpp 2173 repin miscounted; any stray line above a pin in ANY pinned file) | HIGH | §3.4 sed + dry-run procedure mandatory; §7.3 pre-commit pin grep in every WP; WP-1 forbidden to edit above walker.cpp:1391; WP-2 forbidden to add includes to game_world.cpp. |
| R4 | user_path shadowing assumption (floors load under the prepended .glad) fails somewhere (esp. Emscripten/IDBFS) | HIGH | Verified fallback chain (og_file.cpp:169-201) + .glad member-list test (no 701+) + WP-6 e2e loads a generated floor; wasm manual check (WP-7); fallback if broken on a platform: editor-style repack (machinery exists; costs the remount soft-fail handling). |
| R5 | Coverage gate trip (~1.5k new src/ lines) | MED | Determinism/audit-sweep/fold batteries designed as coverage engines; delta discipline §7.4; resist extracting more of the tools than tower needs. |
| R6 | Generation RNG hygiene (accidental live-world rng_/ctx().rng/rand() use breaks regen determinism or, worst case, parity) | HIGH | Scratch-world pattern + GO-time-only generation + generate-twice byte-compare test; GameplayContextGuard non-reentrancy assert catches context leaks. |
| R7 | Run-end consistency across frontends (a loss path missing on_run_ended silently diverges SDL vs curses) | MED | WP-3 threads ALL FOUR sites (incl. curses — the Design-1 failure judges flagged); WP-6 e2e enumerates every non-win exit shape. |
| R8 | Economy/difficulty calibration off (tower out-pays campaigns or walls too early) | MED | §5.11 sweeps + CI pins; expect 1-2 recalibration waves; band tables are data — retune without structural change. |
| R9 | Existing test_zaxis HP asserts perturbed by ≥2-story drops | MED | WP-1 audit pass budgeted; all failures are WP-1's to fix (no pre-existing-failure exemption). |
| R10 | Westlands 14/22 mandatory fall route now costs HP (shipped-campaign balance change) | MED | Fall-depth report + per-line design ruling + playtest (WP-7); reroute only if mandatory-path. |
| R11 | Emscripten: per-floor IDBFS writes/sync untested on the win path | MED | Desktop-first (owner sign-off #6); explicit sync after writes; wasm jitter lane + manual session before enabling prominently on web. |
| R12 | Non-replicated fall accumulator on mirrors (late joiner under-counts a mid-cascade fall) | LOW | Accepted z_stair_latched_ contract; hp self-corrects on next snapshot; documented in the field comment. Remote clients also miss the DamageNumber — matches flight-expiry today. |
| R13 | campaign.yaml emitter churn dirtying every repack | LOW | Emit-only-when-non-empty is load-bearing; pinned by a writer byte-stability test (WP-3). |
| R14 | Float determinism of the fall formula across compilers | LOW | Dual-capture + same-binary replay only; formula mirrors the in-tree float precedent; parity band [10199,10201] absorbs ±0.01 — widen the band, never the formula. |
| R15 | Mode #4+ decay (switch shotgun; results-block accumulation; team-build-return invariant blocking seamless modes; first replicated-state mode pays the triple bump) | LOW | Priced, not solved: exhaustive switches (compile-error on missed case); provider hoist deferred to mode #4; in-session advance explicitly on that feature's bill (docs/game-modes.md). |
| R16 | UX reads as punitive (retry suppressed, respawn clamp overriding a user setting) | LOW | Popup copy explains run-over; Gate briefing surfaces the clamp; roster survives runs (D10). Owner sign-offs #2/#3 cover the calls. |
| R17 | FAMILY_TOWER1 turret is legacy, art/balance-untested | LOW | Bands 4-5 only; playtest before ship; trivially removable from the band tables. |
| R18 | WP-4 leaves a fifth builder copy (drift if tool retargeting stalls) | LOW | WP-8 issue with a NAMED owner + byte-identity gate; lib documented authoritative in docs/game-modes.md. |

---

## 9. Appendix — verified anchor quick-reference (at fd097693; re-verify before editing)

- Tick fork: game_world.cpp:1785-1828; latch reset :1617-1621; timeout :1631-1646. game_world.cpp = 1983 lines.
- Suppress gate: ctf.cpp:1161-1183; consumers view.cpp:1393, game_server.cpp:1414/2148. CTF init/type-drop: ctf.cpp:770-997/:863-866.
- Finalize sites: screen.cpp:1429-1491 (loss :1414-1428; rematch predicate :1459-1463; autosave gate :1472-1481); local_transport_shadow.cpp:339-391 (withdraw :399-417; owned-persist :173-212); headless_server_runtime.cpp:444-492 (bonus :469); curses_game_runtime.cpp:400-421 (rematch :400-406).
- get_time_bonus: results_screen.cpp:446-461 (reads m_score[0]; limit≤0 → 0). Popup chain :121-154 (CTF :142, :72-116); overview banner :716-751; TESTING short-circuit :465-474; formatter precedent :412-427.
- Sync twins: screen.cpp:1174-1197 (respawn_mode :1183); headless_server_runtime.cpp:76-98 (:84).
- Knob channel (MP door recipe): LobbySettings write/read net_transport.cpp:288-319; sanitize lobby_server.cpp:34-75; apply_lobby_game_start_config glad_gameplay.cpp:82-160; InitialSetup net_transport.h:158-176; snapshot scalar 6-site world_snapshot.cpp:807/841/2393/2839/2969 + replay.cpp:~476; versions net_transport.h:64 / world_snapshot.h:34 / replay.h:17; wire-byte pins test_net_transport.cpp:240/829-830/858-859/~2654 + test_input_state_net.cpp:133/149; wrong-version :910/:924/:940.
- apply_z_motion: walker.cpp:1940-2053 (gate :1942-1944; flyer :1954-1955; genre read :1968; air branch :2019-2045; landings :2028/:2031-2038; hover :2040-2045; pit :2047-2051); change_floor :1854-1866; death() :1663+; teleports walker_specials.cpp:214/:262; sole caller living.cpp:86-88; transient block walker.h:451-472; flight-expiry idiom living.cpp:172-191; invuln check walker_combat.cpp:251-253; SOUND_CLANG=1 sound_ids.h:23.
- Parity: Z rows scenario_table.h:5101-5111 (arrays :4927-4948; FloorPaint mirrors :194-212); teeth test test_parity_scenarios.cpp:401-442; OG_PARITY_TEST ~:205; Invariant path :55-66; facts regen CMakeLists.txt:2350-2361.
- Pins: scenario_table.h:4805 (walker 2173), :4289 (1354), :1965 et al ×8 (1192), :4242 (living 114), :1165/:1193/:3291 (game_world 1622/1620/1710), :1179 (save_data 118).
- Save: save_data.h:62-76 (v12 trio :72-74); save_data.cpp:536 (v12 read gate), :710 (version literal), :999-1005 (v12 write), format comments :143-194/:736-787; current_levels forcing :923-931; version tests test_save_data_versions.cpp:79-211.
- Campaign machinery: parser campaign_yaml.cpp:95-132; emitter :300-334; metadata memoization campaign_metadata.cpp:93/:125; mount prepend platform_io_common.cpp:130 (mount :107-142, zip :316-324, restore :391-414); og_file fallback og_file.cpp:169-201; write dir platform_io.cpp:329; IDBFS sync :265-311; level id resolution level_runtime_data.cpp:806-863; copy lists :275-277/:623-625/:644; level writers level_file_io.cpp:851-926 (cascade :653-661; type byte :182-183/:678-679); load fallback game.cpp:105-127 (difficulty :132-133); already-completed purge game.cpp:225-288.
- Menus/HUD: kShelf picker_common.cpp:727-755 (test ~:1524); shelf consumers campaign_picker.cpp:309/:483, text_picker.cpp:148, curses_picker_client.cpp:852; go_menu picker_team_build.cpp:2569-2679 (saves :2587/:2615); networked_session_ session_state.h:91; FOES block score_panel.cpp:567-631 (box :588-589; fps call :638; HUD gate :385-402); fps_overlay.cpp:64-88; radar switch radar.cpp:547-731 (default :729-730; floor :73-89; re-bake :242-247); PIX_ZSTAIR pixdefs.h:207-208; curses line1 curses_renderer.cpp:302-374 (Lv :317); GameWorld id game_world.h:147, floor_count :189, TYPE block :134-137, migrating block :382-408, CtfState embed :397; GameplayContext gameplay_context.h:51-92; ctf/campaign constants ctf_constants.h:18/:22/:26.
- Mapgen: builders tools/westlands_mapgen/main.cpp:165-309 + builders.h:37-147; audits :341-524/:926-1399; fall-line :465-489; self-check :1448-1497; westlands mount guard :1423-1433; tool targets CMakeLists.txt:1309-1383; staging :2473; scen-id conventions per mapgen fact (gladiator 1-45, tryxian 103-115, arenas 300-305, CTF 500-509, concept 600-604 — tower claims 700+).