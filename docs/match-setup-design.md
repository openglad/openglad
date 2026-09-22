# The SETUP wizard, the untheme and bodies above FAIR (#304, #305, #306) — 2026-09-19, PR #307

A dated design snapshot of what this PR shipped, written against the branch
tip and not revised afterwards. **Its section numbers are the numbers the
code comments cite** (`docs/match-setup-design.md §2.0`, `§3.4`, `§2.7`,
`D19`, …), so they follow the spec this PR implements rather than running
1..n; the gaps are sections of that spec which carry no shipped fact worth
repeating here. Line-level anchors belong to the branch tip, not to a later
tree: when a constant moves, the code is the truth and this file is the
record of what was designed.

Companion documents: `docs/lineup-design.md` (Amendment 8 carries the #305
bodies rule), `docs/basecamp-zones-design.md` (the camp chassis the wizard
is a room inside), `docs/staged-lobby-design.md` (the staged world the MATCH
step censuses), `docs/mp-game-modes.md` (the player-facing description).

---

## 0. What ships, in one screen

| Issue | Answer |
|---|---|
| **#304** setup too complicated | ONE door, **SETUP**, on the Base Camp command strip of every `matchup: versus` campaign (the DIFFICULTY door's twin on one rect, the GO/READY shape). It opens a five-step wizard on the camp's own chassis — **GAME → ARENA → TEAMS → RULES → MATCH** — with a tab strip for direct access, PREV/NEXT for the sequence, BACK/Escape to leave from any step, every value preselected from the save, and a last step that states the whole match and offers GO (dimmed, with its reason on its face, whenever the strip GO would refuse). The camp docket's `GAME:` / `ARENA:` rows become shortcuts INTO the wizard, so the versus campaign has one chassis for its pages. Cold 2v2 soccer: **22–26 clicks over 9 screens → 13 over 5** (7 clicks over 2 screens once the company is on the campaign). **Update (2026-09-20, PR #307):** round 2 collapsed the doors to ONE: the wizard opens from the Base Camp docket's single `SETUP - <TITLE>  >` row, the strip twin is deleted and the command strip reads DIFFICULTY on every campaign again (§10). |
| **#306** rip out the theming | **UNTHEME**, in the game's own plain words (GAME, ARENA, TEAMS, SIDES, RULES, MATCH, SETUP, CLEARED, FIGHTERS, BOTS; BAND stays — the original Gladiator's noun for a company). No narrator, no ledger, no signature, no deck. Campaign display title **Multiplayer Arenas**. 40 briefings lose the narrator through the generator, guarded by a generator-side theme lint (word list + no lower-case letters). `BaseCampSlotKind::Card` is renamed `Seat`. No Roman anachronism is introduced. **Update (2026-09-20, PR #307):** CLEARED left the vocabulary in round 2 — Multiplayer Arenas carries no progress word anywhere (§10). |
| **#305** AI needs more people | In **soccer and basketball only**, each FILL step above FAIR buys **one more bot at roughly one human's power** instead of 25 % more power: STRONG = H+1 bodies, BRUTAL = H+2, bounded by the five-family squad and the court's room. One helper, `lineup.squad_shape`, consumed by the decision (`match.fills`) and the apply (`lineup.spawn_bots`); one shape table, `mode_shape.lua`, read by both impls and the campaign picker. **The campaign declares what a fresh ball arena deals: STRONG** (`match_knobs.deal`), so a solo game on THE PITCH fields two bots without a knob being touched; every other arena and campaign keeps the FAIR deal byte-identically. The player sees the consequence as a **count** (`2 BOTS`) on the TEAMS step, on LINEUP and in the MATCH step's census. |

Shape decisions that bind everything below (the full list is §7):

- **Preview == launch through the same code.** Every knob the wizard turns
  is a `SaveData` write through the existing pure helper in `picker_common`,
  followed by the existing per-client tail (lobby sync + autosave). The
  MATCH step censuses the STAGED world the launch adopts, and its GO is
  gated by the SAME predicate the strip GO refuses on
  (`local_seats_deployed_for_go`). GO on the MATCH step dispatches the very
  `ButtonAction::GoMenu` the strip GO dispatches.
- **One rule, one home.** The TEAMS/FILL macros and the TIME LIMIT cycle
  moved out of `campaign_picker.lua` into `picker_common` (faces and
  said-lines byte-identical); the Lua score-cycle twin is deleted and the
  SCENARIO `SCORE` row is parked, so `ctf_capture_limit` has ONE surface.
  Six rules that already existed twice are hoisted to one home (§3.4).
- **The Lua picker contract grows by two additive items**: a versus
  campaign's `picker_menu("")` root is the wizard's GAME step, and the
  optional `match_knobs` hook (one table, seven keys).
- **No wire, save, snapshot or replay version moves** (protocol 18, GTL 19).
- **Grids are declared as constants** (§2.0) with ONE right edge
  (`kSetupRightEdge = 310`); alignment is pinned as relations.
  **Update (2026-09-20, PR #307):** two statements in this list moved: the
  said-lines were DELETED in round 2 rather than carried over byte-identically, and the save format DID
  move — GTL 19 → **20**, the one-shot heal of a company the round-1 FILL
  bug had collapsed (§10). Protocol 18, the snapshot and the replay are still
  untouched.

---

## 2. Screen specs

### 2.0 The wizard chassis grid

The wizard is a **room inside the camp** (`docs/basecamp-zones-design.md`
"Zone submenus"): the Base Camp panel and header lines A and B are its own,
and it REUSES the zone constants. Every right-side rect ends on ONE declared
edge.

**Where the constants live (lead ruling 3 of this PR's execution plan).**
The spec first said "declared once in `picker_sdl_defs.h`". As built they are
split, because the SDL-free session needs the vertical rhythm and
`picker_sdl_defs.h` is an SDL header:

- the nine vertical/rhythm names — `kSetupLinesMax`, `kSetupContentY0`,
  `kSetupLinePitch`, `kSetupLineToRowGap`, `kSetupRowPitch`,
  `kSetupPanelBottomY`, `kSetupRowsMax`, `setup_row_y0`, `setup_rows_fit` —
  live in `include/openglad/interface/ui/match_setup_session.h`;
- every x/w/tab/cell/footer/team-column/ordinal name lives in
  `src/interface/ui/picker_sdl_defs.h`, which includes the session header
  and `static_assert`s the shared names against the zone constants (one
  namespace, so a redefinition would not compile).

```
// panel + header (== zone submenu)
kSetupPanel            = (8,28)-(311,160)
kSetupLeftX            = kZoneSubmenuRowX                        // 12  — the one left edge
kSetupRightEdge        = kBaseCampPanelInnerRightX               // 310 — the one right edge
kSetupTabY             = kZoneSubmenuLineY0                      // 33
kSetupTabH             = 10
kSetupTabW             = 54                                      // label budget (54-8)/6 = 7
kSetupTabGap           = 7                                       // 5*54 + 4*7 = 298 = 310 - 12
kSetupTabCount         = 5
tab_x(k)               = kSetupLeftX + k * (kSetupTabW + kSetupTabGap)   // 12, 73, 134, 195, 256
static_assert(tab_x(kSetupTabCount - 1) + kSetupTabW == kSetupRightEdge)
// content: the tab->content gap is the same 4 px the line->row gap is
kSetupLinePitch        = CampaignZoneSession::kTextLinePitch     // 8
kSetupLineToRowGap     = 4
kSetupContentY0        = kSetupTabY + kSetupTabH + kSetupLineToRowGap    // 47
kSetupLinesMax         = 10                                      // C++ steps; hosted Lua pages keep 6
setup_row_y0(lines)    = lines == 0 ? 47 : 47 + 8*lines + 4
kSetupRowX             = kSetupLeftX                             // 12
kSetupCellX            = kBaseCampZonePagerPrevX                 // 280 — the right cell column
kSetupCellW            = kSetupRightEdge - kSetupCellX           // 30
kSetupRowW             = kSetupCellX - kSetupLineToRowGap - kSetupRowX   // 264, ends x=276
kSetupRowH             = 10
// ^ round 4 (2026-09-22, PR #307): kSetupCellX/kSetupCellW are DELETED
//   with the pager pair below, and a row runs the panel's whole width —
//   kSetupRowW = kSetupRightEdge - kSetupRowX = 298, ending on x=310 with
//   the tab strip. kSetupRowLabelChars goes 42 -> 48.
kSetupRowPitch         = kZoneSubmenuRowPitch                    // 12
kSetupPanelBottomY     = kZoneSubmenuPanelBottomY                // 158
setup_rows_fit(lines)  = (158 - setup_row_y0(lines)) / 12
kSetupRowsMax          = 9                                       // setup_row_0..8 (RULES is exactly 9)
                                                                 // round 4: the LAST of the nine is
                                                                 // the pager row when a step pages
kSetupLineChars        = 48                                      // the face holds 49; the 48 budget
                                                                 // clears the bevel by a column
// the right cell column: the docket's pager pair on the ARENA step's first row …
kSetupPagerPrevX       = kBaseCampZonePagerPrevX                 // 280, w 14
kSetupPagerNextX       = kBaseCampZonePagerNextX                 // 296, w 14 — ends 310
// ^ round 4 (2026-09-22, PR #307): DELETED, with kBaseCampZonePagerPrevX /
//   NextX / Width themselves. A step whose rows outrun their band spends
//   the window's LAST ROW on the pager row `MORE ARENAS - 2/2  >` (§12),
//   so nothing lives in the 30 px column any more and it is gone.
// … and ONE 30x10 reverse cell on every cycler row, label "<"
kSetupRevX             = kSetupCellX                             // 280
kSetupRevW             = kSetupCellW                             // 30 — ends 310
// ^ round 3 (2026-09-21, PR #307): kSetupRevX/kSetupRevW are DELETED — the
//   wheels turn forward only, so the cell column holds the pagers alone.
//   kSetupCellX/kSetupCellW and kSetupRowW = 264 are unchanged.
// footer (the zone submenu's BACK and NEXT rects; PREV is NEXT's mirror on the same 6 px gap)
kSetupBack             = (10,169,44,20)   id setup_back, Escape
kSetupNext             = (270,169,40,20)  id setup_next, label NEXT
kSetupPrev             = (224,169,40,20)  id setup_prev, label PREV — 270 - 6 - 40
// TEAMS line columns (the LINEUP band grammar: swatch, label, seats, census)
kSetupSwatchX          = kLineupChipX                            // 12 — a 6x6 swatch
kSetupSwatchSize       = 6
kSetupTeamLabelX       = kLineupTeamTextX                        // 26 — "TEAM n", ends 62
kSetupTeamSeatX        = 70                                      // 18 glyphs, ends 178
kSetupTeamSeatChars    = 18
kSetupTeamCensusX      = 188                                     // 20 glyphs, ends 308
kSetupTeamCensusChars  = 20
```

`setup_rows_fit(lines)` is **9, 8, 7, 6, 6, 5, 4, 4, 3, 2, 2** for `lines`
0..10. (The spec's own inline comment printed a different series; the
formula and its `static_assert`s — `setup_rows_fit(3) == 6`,
`setup_rows_fit(6) == 4`, `setup_rows_fit(7) == 4` — are the shipped truth,
and that is what the header carries. The comment was the error, not the
code.)

`kBaseCampZoneActionRowX/Width` and the two pager constants moved out of
`menu_screen_specs.cpp`'s file statics into `picker_sdl_defs.h` (values
unchanged) because a second SCREEN now derives from them. The strip family
stays file-static: its one new consumer, the SETUP twin row, is a row of the
same table in the same file.

Button table `kMatchSetupRows` (`kMatchSetupButtonCount = 28`), all
`ButtonAction::MenuSpecRow` with `arg == ordinal`:

**Update (2026-09-21, PR #307):** the nine `setup_rev_r` rows (ordinals 9..17) are REMOVED — the cyclers
turn forward only (§11) — and every ordinal after them shifts down by nine:
`setup_back` 9, `setup_prev` 10, `setup_next` 11, `setup_page_prev` 12,
`setup_page_next` 13, `setup_tab_k` 14..18, `kMatchSetupButtonCount = 19`.
`kMatchSetupRevBase` is deleted. The rects of every surviving row, the
264-wide row face and the pager pair's own column are unchanged.

| ord | id | rect | face | gate (per frame, rewire) |
|---|---|---|---|---|
| 0..8 | `setup_row_r` | (12, row_y0 + 12r, 264, 10) | `compose_scripted_row_face(row, 42, …)` (§3.4) | hidden past the step's visible rows (round 4: **298** wide, budget **48**, and the last visible slot is the pager row on a paged step) |
| 9..17 | `setup_rev_r` | (280, row_y0 + 12r, 30, 10) | `<` | visible iff row r is a cycler and the viewer may turn it (host) |
| 18 | `setup_back` | (10,169,44,20) | BACK, Escape | always |
| 19 | `setup_prev` | (224,169,40,20) | PREV | hidden on the first present step |
| 20 | `setup_next` | (270,169,40,20) | NEXT | hidden on MATCH |
| 21 | `setup_page_prev` | (280, row_y0, 14, 10) | `<` | multi-window pages only (ARENA) — **Update (2026-09-22, PR #307):** DELETED; the window's last ROW pages (§12) |
| 22 | `setup_page_next` | (296, row_y0, 14, 10) | `>` | same — **Update (2026-09-22, PR #307):** DELETED with its twin (§12) |
| 23..27 | `setup_tab_k` | (tab_x(slot), 33, 54, 10) | step word; the current step wears `[WORD]` and `RowState::Disabled` | one tab per PRESENT step, re-banded left-to-right from x=12 |

Ordinal constants: `kMatchSetupRowBase = 0`, `kMatchSetupRevBase = 9`,
`kMatchSetupBackIndex = 18`, `kMatchSetupPrevIndex = 19`,
`kMatchSetupNextIndex = 20`, `kMatchSetupPagePrevIndex = 21`,
`kMatchSetupPageNextIndex = 22`, `kMatchSetupTabBase = 23`,
`kMatchSetupButtonCount = 28`.

**Update (2026-09-22, PR #307):** the two `setup_page_*` rows go the way of
the nine `setup_rev_r` rows — the window pages on its own last ROW now —
and the table is **17**: `setup_row_0..8` (12, row_y0 + 12r, **298**, 10),
`setup_back` 9, `setup_prev` 10, `setup_next` 11, `setup_tab_k` 12..16.
Every row face is 48 glyphs and closes on the one right edge, 310, with
the tab strip (§12).

Row faces follow the chassis grammar through the ONE hoisted composer
(§3.4 `compose_scripted_row_face`): page rows and doors wear ` >`, level
rows and GO wear the GO green, the current arena wears `[CURRENT]`, cleared
arenas `[CLEARED]`, cycler rows wear their value, inert rows the GREY face
with the bevel fix.
**Update (2026-09-22, PR #307):** maintainer ruling — inside the wizard,
**green is GO's alone**. The ARENA step is a LIST of arenas: painting all
ten of them the launch green says "this launches" about every row and so
about none, and `[CURRENT]` is already the mark that says which arena is
armed. The wizard's level rows are PLAIN (the ordinary grey face); the
MATCH step's GO row and the Base Camp strip's GO keep the green. The seam
is one argument on the shared composer,
`compose_scripted_row_face(row, budget, level_rows_actionable,
level_rows_green = true)`, which the wizard alone passes `false` — every
classic campaign's book page, zone submenu and camp docket keeps the green
level row it has always had, and both halves are pinned
(`CampaignZoneUi.wizard_arena_rows_are_plain_while_the_camps_stay_green`
and `…joiner_level_rows_pay_for_the_host_marker_out_of_the_label`). **The current tab** wears square brackets AND the
`RowState::Disabled` face: it is inert (a click is a no-op refetch, traced
`setup tab_current`), and the dimmed face with its darker right/bottom
bevels reads as the pressed-in tab of a tab strip. Green would say "this
launches" and yellow "gated"; both would lie. Pinned by the centre pixel.
**Update (2026-09-20, PR #307):** the `[CLEARED]` tail is suppressed on a
campaign that carries no progress vocabulary — the session blanks it through
`og::ui::progress_marks_shown(save)`, so no wizard row on Multiplayer Arenas
wears it. `[CURRENT]` is unchanged (§10).

**Nav**: pattern-b full-graph rewire every frame. Tabs chain left↔right; a
tab's ↓ lands on the first visible row (else `setup_back`); row 0's ↑ lands
on the current tab; rows chain vertically; a row's → lands on its reverse
cell when one is visible (row 0's → on `setup_page_prev` when pagers show);
the reverse cells chain vertically among themselves and ← back to their row;
the last row's ↓ lands on `setup_back`; `setup_back` ↔ `setup_prev` ↔
`setup_next` (absent members skipped); `setup_next`'s ↑ is the last row.
LEFT/RIGHT stay NAVIGATION everywhere (the `<` cell is how a keyboard or pad
steps a wheel back: → then FIRE). Pinned by a BFS over {host, joiner} ×
{5 steps} × {book, no-book} × {paged, unpaged} × {2-side, 4-side arena}.
**Update (2026-09-21, PR #307):** with the reverse cells gone, the cell column holds nothing but the ARENA
pagers: a row's → is `setup_page_prev` on row 0 when they show and −1
otherwise, and there is no second vertical chain. The BFS is unchanged in
shape and still runs over the same lattice (§11).
**Update (2026-09-22, PR #307):** with the pagers gone too, a row's → is
−1 always and the panel's whole graph is the ROW CHAIN: the pager row is
an ordinary row in it, entered from the row above and leaving into
`setup_back`. The BFS is unchanged in shape and still runs over the same
lattice, paged and unpaged (§12).

On entry to a step the rewire moves the keyboard highlight onto a LIVE row.
A step entered by clicking its own tab would otherwise leave the highlight ON
that tab, and the current tab is inert by design (D36), so Enter did nothing
until the player arrowed away. One rule, one table
(`match_setup_entry_highlight`):

| Step | Where the keyboard lands on entry |
|--|--|
| GAME | the row whose id is `match_knobs.arena_page` (the current game) |
| ARENA | the `[CURRENT]` arena row, when the window holds it |
| TEAMS | the first live row |
| RULES | the first live row |
| MATCH | GO while it is live, else VIEW LEVEL (the row that still does something when GO is gated) |

Every arm falls back to the first live row, and a step with NO live row (a
non-networked joiner's RULES, where every fact is a line) hands the keyboard
to the footer's own way forward — never to the inert tab. The
`rewire(buttons, count, int& highlighted_button)` signature is what makes
that a one-shot write on the entered-step edge, pinned through
`menu_screen_testing_highlighted_button()`, with
`ensure_highlighted_button_visible` after.

Line B is UNTOUCHED: it carries the Base Camp census and `SCEN n: <title>`
readout on every step, or the standing toast. The tab strip is the step
indicator; the terminals' `--- SETUP: TEAMS ---` banner is the step word's
only other home.

### 2.1 Base Camp — the strip twin and the unthemed docket

The strip's (58,178,68,18) slot reads **SETUP** on a versus campaign and
**DIFFICULTY** everywhere else — one rect, two ordinals, the GO/READY twin
shape. Ordinal 73 is `setup`; the ceiling goes 73 → 74 (§3.7).
**Update (2026-09-20, PR #307):** REVERSED: ordinal 73 `setup` is deleted and the
ceiling is back to 73. The slot reads **DIFFICULTY** on every campaign; the
wizard's door is the docket row below (§10).

The camp docket is three host rows and no text line:
**Update (2026-09-20, PR #307):** the docket is ONE host row, and the roster leads the panel (§10).

```
 CLEARED 12/40                                     header readout
 GAME: SOCCER - 2/4 cleared                     >  shortcut into the wizard's GAME step
 ARENA: THE PITCH - 2 sides, 3 goals            >  shortcut into the wizard's ARENA step
 RANDOM ARENA - any game, any arena             >  the roll (host only)
```
**Update (2026-09-20, PR #307):** the docket is now one row and no readout:
```
 SETUP - SOCCER: THE PITCH                       >  y=143, the wizard's one door
```
The readout, the shortcut pair and the roll row are all retired; the freed
units go back to the roster, which leads the panel so the y=33 heading band is
the classic one (§10).

A joiner sees the two shortcut rows, browsable, and no line: the roll is cut
at fetch and the roster takes the unit the old joiner line spent.
**Update (2026-09-20, PR #307):** the joiner sees the SAME single SETUP row, browsable (§10).

### 2.2 SETUP: GAME (the campaign's book root, hosted)

```
 SCEN 820: SOCCER: THE PITCH   1 PLAYER / 1 MACHINE     y=14  line B (unchanged)
 +--------------------------------------------------+   y=28
 | [GAME]  ARENA   TEAMS   RULES   MATCH            |   y=33  tabs (54x10 at 12/73/134/195/256)
 | Cleared: 0 of 40.                                |   y=47  line 1 (<= 38 from Lua; 48 on the panel)   # Update (2026-09-20, PR #307): retired, see §10
 | TEAM DEATHMATCH - 0/6 cleared                  > |   y=59  setup_row_0   page "tdm"   # Update (2026-09-20, PR #307): retired, see §10
 | CAPTURE THE FLAG - 0/10 cleared                > |   y=71  setup_row_1   # Update (2026-09-20, PR #307): retired, see §10
 | ONSLAUGHT - 0/4 cleared                        > |   y=83   # Update (2026-09-20, PR #307): retired, see §10
 | MUTANT - 0/4 cleared                           > |   y=95   # Update (2026-09-20, PR #307): retired, see §10
 | SOCCER - 0/4 cleared                           > |   y=107  <- keyboard highlight (the current game)   # Update (2026-09-20, PR #307): retired, see §10
 | BASKETBALL - 0/6 cleared                       > |   y=119   # Update (2026-09-20, PR #307): retired, see §10
 | FREE FOR ALL - 0/6 cleared                     > |   y=131  setup_row_6   # Update (2026-09-20, PR #307): retired, see §10
 +--------------------------------------------------+   y=158
  [BACK]                                    [NEXT]      y=169
```
**Update (2026-09-20, PR #307):** the rows carry no tally — the note is `N arenas` — there is no `Cleared:` line, and a host-only
`RANDOM - any game, any arena` row is appended LAST at y=131 (§10).

Worst row `CAPTURE THE FLAG - 0/10 cleared  >` = 35 ≤ 42. The rows wear the
tallies and nothing else: the header names the arena, the highlight names
the game. Pages are open to every machine. NEXT on GAME (and on ARENA) means
"keep what is set" and lands on TEAMS.
**Update (2026-09-20, PR #307):** the worst row is `CAPTURE THE FLAG - 10 arenas  >`
(31); eight rows still fit without a pager (§10).

### 2.3 SETUP: ARENA (a game's page, hosted)

```
 | GAME   [ARENA]  TEAMS   RULES   MATCH            |   y=33
 | Kick the ball into their goal.                   |   y=47  line 1 (the game's rule line, <= 38)
 | Next uncleared: THE PITCH.                       |   y=55  line 2 ("Every arena here is cleared." when done)   # Update (2026-09-20, PR #307): retired, see §10
 | THE PITCH - 2 sides, 3 goals         [CURRENT]   |   y=67  setup_row_0  level 820, GO green
 | THE MUDBOWL - 2 sides, 3 goals                   |   y=79
 | FOURSQUARE - 4 sides, 3 goals                    |   y=91
 | BONEYARD CUP - 2 sides, 3 goals                  |   y=103
 +--------------------------------------------------+
  [BACK]                           [PREV]    [NEXT]
```
**Update (2026-09-20, PR #307):** line 2 is gone — the game's rule line is the
only line, so the rows start at y=59 — no arena wears `[CLEARED]`, and a
host-only `RANDOM ARENA - any arena of this game` row is appended LAST (§10).

**Page resolution.** The ARENA step IS the book at depth ≥ 2.
`goto_step(Arena)` from any other step descends from the root into the root
row whose id is `match_knobs.arena_page` (the page that lists the cursor's
arena — modes answers `current_pair().mode.id`, the only owner of
level→game); from a page already at depth ≥ 2 it is a no-op refetch (the
player browsed there on purpose). The GAME tab / PREV pops to the root. When
`arena_page` names no root row the ARENA step is the manifest list (the
bookless fallback, §3.1) with a `setup arena_page_unresolved` trace. The
window opens on the page holding the `[CURRENT]` row on every entry to the
step, so the host never sees four green rows none of which is theirs.

A 10-arena page shows rows 0..6 with `<` `>` at (280,67)/(296,67) and `1/2`
under them. (The ARENA pager pair is unchanged in round 3; it is the only
thing left in the cell column — §11.) A row that overflows 42 glyphs clips the LABEL, never the tail.
**Update (2026-09-22, PR #307):** there is no pager pair and no `1/2`
strip. The window's LAST slot is the pager ROW, and it says both things a
bare arrow never did — what it does and where you are:

```
 | GAME   [ARENA]  TEAMS   RULES   MATCH            |   y=33
 | Take their flag. Bring it home.                  |   y=47  the game's rule line
 | THE OLD KEEP - 2 sides, 5 flags                  |   y=59  setup_row_0
 | ...                                              |
 | (seven arenas)                                   |
 | MORE ARENAS - 1/2  >                             |   y=131 setup_row_7 — the pager row
 +--------------------------------------------------+   y=158
```

CTF's eleven rows (ten arenas + RANDOM ARENA) over `setup_rows_fit(1)` = 8
slots therefore window 7 + 4, the pager row closing each. A click steps to
the next window and WRAPS home from the last — a row has one direction,
like every cycler on this screen (§11). A row that overflows **48** glyphs
clips the LABEL, never the tail.
Level rows are host-gated at the click (joiner: `(HOST)` face, refusal
toast), never hidden. **Refusals never advance**: `DeniedHost`,
`DeniedGate`, `Unchanged`, `LoadFailed` keep the step and toast; only
`Set`/`SetReplay` advances to TEAMS, through the one hoisted
`scripted_level_set_answer` (§3.4).
**Update (2026-09-20, PR #307):** with one flavour line the window holds EIGHT rows,
so CTF's ten arenas plus RANDOM ARENA page 8 + 3 and the roll row sits on
window 2/2 (§10).

### 2.4 SETUP: TEAMS

```
 | GAME    ARENA  [TEAMS]  RULES   MATCH            |   y=33
 | ■ TEAM 1   P1 WASD  P2 ARROWS   2 FIGHTERS       |   y=47  swatch x=12, label x=26, seats x=70 (18), census x=188 (20)
 | ■ TEAM 2                        2 BOTS           |   y=55  a two-side arena prints two lines
 | STRONG adds a fighter, BRUTAL two.               |   y=63  the campaign's match_knobs lines (<= 2, <= 38)
 | FILL: STRONG - none to brutal                  < |   y=75  setup_row_0  cycler + setup_rev_0 at 280..310
                                                         ^ round 3 (2026-09-21): no `<` cell — the wheel is forward-only (§11)
 | LINEUP - fill per team, map units              > |   y=87  setup_row_1  door -> LINEUP (nested), refetch on return
 +--------------------------------------------------+
  [BACK]                           [PREV]    [NEXT]
```
**Update (2026-09-20, PR #307):** the macro row's note is `weak to brutal`: NONE left
the wizard's FILL wheel, so the wizard can no longer empty an arena. LINEUP
keeps per-team NONE and its own `none to brutal` note (§10).

On a four-side arena the SIDES row appears above FILL and the pointer line
follows the team lines whenever any of them carries a diagnostic:

```
 | ■ TEAM 1   P1 WASD  P3 IJKL     2 FIGHTERS       |   y=47
 | ■ TEAM 2   P2 ARROWS  P4 TFGH   NEEDS 1 FIGHTER  |   y=55  the diagnostic outranks the census
 | ■ TEAM 3                        2 BOTS           |   y=63
 | ■ TEAM 4                        EMPTY            |   y=71
 | Deploy and seats: the Base Camp roster and rail. |   y=79  47 <= 48
 | STRONG adds a fighter, BRUTAL two.               |   y=87
 | SIDES: 3 - 2, 3, 4                             < |   y=99  setup_row_0
 | FILL: STRONG - none to brutal                  < |   y=111 setup_row_1
 | LINEUP - fill per team, map units              > |   y=123 setup_row_2
```
**Update (2026-09-20, PR #307):** same — `FILL: STRONG - weak to brutal` on the
four-side mock too (§10).

**Team lines** (`compose_setup_team_line`, returning CELLS, not a string):
one line per **authored** team (the loaded arena's `authored_team_mask` —
the deal's own reader), ascending:

- **swatch** (6×6 at x=12) — the team colour every other screen uses;
- **label** `TEAM n` at x=26 — the rail's and roster's vocabulary;
- **seat cell** (x=70, 18 glyphs) — `format_lineup_seat_run`, the seat-run
  rule hoisted out of LINEUP's content pass into ONE helper both screens
  call (LINEUP at 26, the wizard at 18), with three tiers: every
  `P# owner` label joined by two spaces when they all fit; else every bare
  `P#` token; else the whole labels that fit plus ` +k`. Blank when the team
  has no seat;
- **census cell** (x=188, 20 glyphs) — `format_match_preview(band, report,
  team)`, precedence: (1) a band diagnostic (`NEEDS k FIGHTER(S)` /
  `NO SEAT: AI`) because it mirrors GO's refusal; (2) with a staged report
  `n FIGHTER(S)` (+ ` +m BOT(S)`), `n MAP UNIT(S)`, `n BOT(S)`,
  `n GENERATOR(S)`, `EMPTY`; (3) no report → `format_lineup_census(band)`
  so the cell never goes blank. Singular/plural everywhere. Worst
  `12 MAP UNITS +5 BOTS` = 20.

The terminals join the cells with two spaces and spell the colour where they
have no swatch — `TEAM 1 BLUE  P1 WASD  P2 ARROWS  2 FIGHTERS`, worst 55 ≤ 72.

**Rows** (host only; a joiner sees the LINEUP door alone):

- `SIDES: n - 2, 3, 4` — `match_sides_face` / `turn_match_sides`: the wheel
  runs 2..N where N is the arena's authored side count, opponents are the
  authored teams other than `my_team` ascending, the note is derived from N,
  and **the row is hidden when N ≤ 2** (one legal value is not a wheel). The
  word is SIDES because the row counts bot sides and the step cannot reseat
  anyone. Said lines verbatim (`Two sides. One squad at FAIR.`);
- `FILL: x - none to brutal` — `match_fill_face` / `turn_match_fill` (on
  opponents + own band, lowest opponent when none on, MIXED face, wrap
  clears);
- `LINEUP - fill per team, map units  >` — door to LINEUP, refetch on return.
  **Update (2026-09-20, PR #307):** SIDES and FILL say NOTHING: the said-lines
  are deleted and the face is the whole answer. FILL's wheel is WEAK → FAIR →
  STRONG → BRUTAL over the ON opponents plus the own band, or over EVERY
  authored opponent when none is on (fix B); there is no wrap to NONE (§10).

`match_knobs` shapes the step: `teams=false` drops the SIDES row AND the
team lines (the lines are then the staged report's own lines plus the
campaign's); `fill="band"` makes FILL team 1's wheel (the LINEUP knob
grammar, same word); `fill=false` hides FILL. Onslaught KEEPS the TEAMS step
— its lines are the staged census of the two armies and its one row is the
LINEUP door, the home of MAP UNITS, its live knob.

### 2.5 SETUP: RULES

```
 | GAME    ARENA   TEAMS  [RULES]  MATCH            |   y=33
 | SCORE: MAP - map, 1, 3, 5, 10                  < |   y=47   setup_row_0
 | TIME LIMIT: MAP - map, 5 to 20 min             < |   y=59   setup_row_1
 | RESPAWNS: HEROES - off to team 1               < |   y=71   setup_row_2
 | SPAWN DELAY: NORMAL - normal, fast, slow       < |   y=83   setup_row_3
 | PERMADEATH: OFF - on, off                      < |   y=95   setup_row_4
 | GENERATORS: NORMAL - calm to frenzy            < |   y=107  setup_row_5
 | DIFFICULTY: BATTLE - up to slaughter           < |   y=119  setup_row_6   # Update (2026-09-20, PR #307): retired, see §10
 | INFINITE GOLD: OFF - gold never runs out       < |   y=131  setup_row_7
 | CROSS CONTROL: OWN - own, all                  < |   y=143  setup_row_8 (NetworkedOnly)
 +--------------------------------------------------+   y=158
```
**Update (2026-09-20, PR #307):** RULES is TWO rows, SCORE and TIME LIMIT, above one
pointer line:
```
 | SCORE: MAP - map, 1, 3, 5, 10                  < |   y=47   setup_row_0
 | TIME LIMIT: MAP - map, 5 to 20 min             < |   y=59   setup_row_1
 | Respawns and the rest: the Base Camp DIFFICULTY. |   y=71   the pointer line (48)
```
Round 3 (2026-09-21, PR #307): neither row carries a `<` cell any more —
both wheels turn forward only, and the row face runs to 276 as before (§11).
RESPAWNS, SPAWN DELAY, PERMADEATH, GENERATORS, DIFFICULTY, INFINITE GOLD and
CROSS CONTROL went back to the DIFFICULTY screen, which is on the Base Camp
strip of every campaign again (§10).

Case: the DIFFICULTY formatters emit Title Case and every camp row is upper
case, so the SESSION upper-cases the shared formatter's output when it
composes the row — words are one implementation, case is presentation at the
one composer, and all three renderers receive the same string. Two formatter
words changed at their ONE home: `format_time_limit_label` (new) spells
minutes `5 MIN` … `20 MIN` (`5M` reads as five million on a 6 px font), and
`format_cross_control_label` spells `CROSS CONTROL: OWN` / `ALL` (`CTRL`
reads as the key; the DIFFICULTY screen's 140 px face holds 23 glyphs, so
its row takes the full word too). Notes are budgeted PER ROW against that
row's longest label (`42 - 3 - max label`), and the sweep pins EVERY label
value × its note.
**Update (2026-09-20, PR #307):** DIFFICULTY is no longer a wizard row, so this
paragraph now describes the DIFFICULTY screen's own rows; `CROSS CONTROL: OWN`
and the `n MIN` clock faces are unchanged (§10).

`match_knobs` hides SCORE (`score=false`) and TIME LIMIT (`time=false`)
where the game ignores them (onslaught: `score_limit=0` IS elimination).
Every visible row is a cycler, so every visible row has its `<` cell.
Joiner: the shared caption plus `format_match_rules_lines`, composed
WITHOUT the `CROSS CONTROL` cell when that fact is already the read-only row
below — a fact on the step as a ROW is never also a LINE. The
`CROSS CONTROL` row stays a visible read-only row without a `<` cell; the
MATCH step, which has no rows at all, keeps all five lines.
**Update (2026-09-21, PR #307):** no row on any step has a `<` cell: the cyclers turn forward only (§11).
**Update (2026-09-20, PR #307):** the wizard's RULES step holds SCORE and TIME LIMIT
only. When `match_knobs` hides both, the step reads `This game takes its rules
from the map.` above the pointer line and has no rows. The joiner face is the
caption plus the ONE packed line; the CROSS CONTROL row is gone from this step
(its read-only home is the strip's DIFFICULTY door). The MATCH step still
recaps every rule line (§10).

### 2.6 SETUP: MATCH (reads the staged world)

```
 | GAME    ARENA   TEAMS   RULES  [MATCH]           |   y=33
 | SOCCER: THE PITCH                                |   y=47  the scen title upper-cased
 | ■   BLUE TEAM  ACTIVE - COMPANY (2)              |   y=55  format_scenario_report_team_line per active team
 | ■   RED TEAM  ACTIVE - MATCHED BOTS (2) STRONG   |   y=63       the swatch inked in the line's own indent
 | SCORE: MAP  TIME LIMIT: 5 MIN                    |   y=71  format_match_rules_lines — the RULES faces, two per line
 | RESPAWNS: HEROES  SPAWN DELAY: NORMAL            |   y=79
 | PERMADEATH: OFF  GENERATORS: NORMAL              |   y=87
 | DIFFICULTY: BATTLE  INFINITE GOLD: OFF           |   y=95
 | VIEW LEVEL - the arena and every team          > |   y=107 setup_row_0  door -> view_scenario (nested)
 | GO                                               |   y=119 setup_row_1  GO green, host only
 +--------------------------------------------------+
  [BACK]                           [PREV]               NEXT hidden on the last step
```

A networked four-team arena spends 1 + 4 + 5 = 10 lines exactly; when the
report leads with `STAGING FAILED` the rules lines are dropped last-first.
Hidden knobs are not recapped. The composer never pages.

**GO** (`setup_row_1`, host only) is live exactly when the strip GO would
launch, and says why when it would not — the face IS the refusal:

| condition (per frame, first match wins) | face | state |
|---|---|---|
| `!og::ui::local_seats_deployed_for_go(save, players, networked)` — the predicate hoisted from the strip GO's handler; the strip GO calls the same function and pops `kDeployForEveryPlayerTitle` | `GO - DEPLOY FOR EVERY PLAYER` (28) | Disabled |
| `staged_health == None` (the debounce window or the first stage) | `GO - STAGING` (12) | Disabled |
| `staged_health == Failed` / `Unavailable` | `GO - STAGING FAILED` (19) | Disabled |
| otherwise | `GO` | Visible, GO green |

`choose(GO)` on a Disabled row answers `Refused` and stays. There is no
"dirty" clause: the client interface exposes `staged_preview_health()` only,
the report's generation watch re-renders on the restage, and GO forces
`ensure_current()` through the shared start path regardless. Joiner: the GO
slot holds a Disabled row `READY is on the Base Camp strip.` — it replaces
the row, so it takes the row's position, never a line.

### 2.7 Terminal projection (text; curses identical through the shared driver)

```
--- SETUP: TEAMS ---
TEAM 1 BLUE  P1 WASD  P2 ARROWS  2 FIGHTERS
TEAM 2 RED  2 BOTS
STRONG adds a fighter, BRUTAL two.
   1. FILL: STRONG - none to brutal
   2. LINEUP - fill per team, map units  >
   3. Next: RULES
   4. Prev: ARENA
   5. Back
Setup # [1-5] (0 = back, N- steps a wheel back):
```
**Update (2026-09-21, PR #307):** the prompt is `Setup # [1-5] (0 = back): ` — the `N-` grammar is
retired with the reverse cell, and a trailing `-` is now just an answer the
driver cannot parse, so it takes the existing `Invalid setup row.` notice
(§11).
**Update (2026-09-20, PR #307):** the terminal wizard prints the same two RULES rows,
the ARENA page's `RANDOM ARENA` and the GAME step's `RANDOM`, and no said
line. The mock's FILL row reads `FILL: STRONG - weak to brutal` now: NONE left
the wizard's wheel (only LINEUP's per-band wheel keeps `none to brutal`).
Confirmed ordinals: GAME `8 RANDOM, 9 Next: TEAMS, 10 Back`; a soccer
ARENA page `5 RANDOM ARENA, 6 Next, 7 Prev, 8 Back`; RULES `1 SCORE, 2 TIME
LIMIT, 3 Next: MATCH, 4 Prev, 5 Back` (§10).

Rows through `campaign_picker_row_text(row, 72)`; `Next: <STEP>`,
`Prev: <STEP>` and `Back` are appended items — the two steppers ARE the tab
strip's projection, and `N-` is the `<` cell's.
**Update (2026-09-22, PR #307):** the terminals project the SESSION's own
window, pager row and all: a paged step prints its window's rows and then
`MORE ARENAS - 1/2  >` as a numbered item, which steps the window and
wraps. `Next:` / `Prev:` are unchanged and still follow it. One window
model for every surface, so the number a player types names the row they
are reading and the same face means the same thing at a prompt as on the
panel. (The camp DOCKET's terminal listing is deliberately NOT windowed —
it still prints the whole widget, because there the window is a pixel
constraint of the panel and a prompt has no band; the wizard's window is
the session's, which the terminals share.)
**Update (2026-09-21, PR #307):** `N-` is gone with the cell, and `TerminalMatchSetupItem::reversible`
with it: the prompt takes a plain row number (§11). The joiner face prints the
lines and the navigation items only. Team Build item 13 `setup` ("Setup") is
appended, gated `Custom` versus-only with guard
`This campaign has no arena setup.`; item 11 `difficulty` is gated `Custom`
classic-only with guard `The fight's rules are on SETUP: RULES.`
**Update (2026-09-20, PR #307):** REVERSED: Team Build item 13 `Setup` is RETIRED (12
items) and item 11 `Difficulty` is a plain item again; both guard strings are
deleted. The terminals' one wizard door is the camp's SETUP row (Team Build
`7 Camp` → `1`), exactly as on SDL. The one `Custom`-gated terminal item is
SCENARIO's `Replay Level`, which refuses on versus with `Arenas are set, never
replayed.` (§10).

### 2.8 Every new or moved label, with its budget

| Surface | Label | Glyphs | Budget |
|---|---|---|---|
| Base Camp strip | `SETUP` | 5 | 10 (68 px beveled) **Update (2026-09-20, PR #307):** the strip slot reads DIFFICULTY on every campaign; the wizard's SDL door is the docket row `SETUP - SOCCER: THE PITCH  >` (39 worst, budget 42) (§10). |
| tabs | `GAME` `ARENA` `TEAMS` `RULES` `MATCH`; current `[TEAMS]` | ≤ 7 | 7 (54 px beveled) |
| footer | `BACK` / `PREV` / `NEXT` | 4 | 6 / 5 / 5 |
| pagers, reverse cell | `<` `>` and `1/2`; `<` | 1 / 3; 1 | 14 px face; 30 px face **Update (2026-09-21, PR #307):** the reverse cell is deleted; the row reads `pagers` alone, `<` `>` and `1/2` on the 14 px face (§11). **Update (2026-09-22, PR #307):** the pager pair is deleted too — the pager is a ROW, `MORE ARENAS - 2/2  >` (20) on the wizard and `MORE - 2/3  >` (13) on the docket, both on the 48-glyph row face (§12). |
| GAME rows | `CAPTURE THE FLAG - 0/10 cleared  >` | 35 | 42 **Update (2026-09-20, PR #307):** GAME rows carry no tally: worst is `CAPTURE THE FLAG - 10 arenas  >` (31), and the RANDOM row is `RANDOM - any game, any arena` (28) (§10). |
| ARENA rows | `DUNGEON OF STARS - 4 sides, 20m  [CURRENT]` | 43 → label clipped, tail kept | 42 **Update (2026-09-22, PR #307):** the face is 48 and this worst row fits uncut (§12). |
| TEAMS line cells | `TEAM 4` / `P1 WASD  P2 ARROWS` / `12 MAP UNITS +5 BOTS` | 6 / 18 / 20 | columns 26 / 70 / 188 |
| TEAMS pointer line | `Deploy and seats: the Base Camp roster and rail.` | 47 | 48 |
| TEAMS rows | `SIDES: 2 - 2, 3, 4` / `FILL: BRUTAL - none to brutal` / `LINEUP - fill per team, map units  >` | 18 / 28 / 36 | 42 **Update (2026-09-20, PR #307):** the macro row's note is `weak to brutal` (NONE left the wizard wheel); `none to brutal` is the LINEUP band wheel's note (§10). |
| RULES rows | worst `SPAWN DELAY: NORMAL - normal, fast, slow` | 40 | 42 **Update (2026-09-20, PR #307):** RULES is two rows, SCORE and TIME LIMIT; worst is `TIME LIMIT: MAP - map, 5 to 20 min` (§10). |
| MATCH lines | title 28; census rows ≤ 48; rules pairs ≤ 44 | ≤ 48 | 48 |
| MATCH rows | `VIEW LEVEL - the arena and every team  >` / `GO` / `GO - DEPLOY FOR EVERY PLAYER` | 40 / 2 / 28 | 42 |
| joiner | `READY is on the Base Camp strip.` (row) 31; `The host sets these for everyone.` (line) 33 | | 42 / 48 |
| camp docket (Lua) | `GAME: CAPTURE THE FLAG - 10/10 cleared  >` 41; `ARENA: DUNGEON OF STARS - 4 sides, 20m  >` 41; `RANDOM ARENA - any game, any arena` 34 | | 42 **Update (2026-09-20, PR #307):** the docket is ONE row, `SETUP - <TITLE>  >` (worst 39); the roll rows moved into the wizard as `RANDOM - any game, any arena` and `RANDOM ARENA - any arena of this game` (37) (§10). **Update (2026-09-22, PR #307):** the docket face is 48 too — the worst `SETUP - ` row has six glyphs of room it did not have (§12). |
| camp readout | `CLEARED` `12/40` | 7 / 5 | header band **Update (2026-09-20, PR #307):** there is no camp readout — the roster leads the panel (§10). |
| campaign lines (Lua) | `STRONG adds a fighter, BRUTAL two.` 34; `FILL sets how strong the bots are.` 34 | | 38 |
| terminal | rows ≤ 72; items `Next: RULES` / `Prev: ARENA` / `Back`; team line worst 55 | | 72 |
| DIFFICULTY screen row | `CROSS CONTROL: OWN` | 18 | 23 (140 px) |
| campaign title | `Multiplayer Arenas` | 18 | 29 |
| LINEUP census column | `10 FIGHTERS +5 BOTS` / `1 FIGHTER +1 BOT` / `2 BOTS` / `EMPTY` | ≤ 20 | 21 |

---

## 3. Architecture

### 3.1 The shared model: `og::ui::MatchSetupSession` (SDL-free)

`include/openglad/interface/ui/match_setup_session.h`,
`src/interface/ui/match_setup_session.cpp`, listed in THREE source lists in
one commit: the `og_interface` component list, `openglad_text`'s
`HEADLESS_SOURCES` and the curses `OG_CURSES_MENU_SOURCES` (the two headless
targets dual-list the shared menu sources by hand). The
`CampaignPickerSession` / `TerminalLineupModel` precedent: one SDL-free step
machine, three renderers.

- `Step { Game, Arena, Teams, Rules, Match }`; `steps()` answers the PRESENT
  steps in order (4 or 5).
- `Row` = a `CampaignPickerSession::Row` plus `Extra { None, Cycler, Door,
  Go }`, `Knob { None, Sides, Fill, BandFill, Score, Time, Respawns,
  SpawnDelay, Permadeath, Generators, Difficulty, InfiniteGold,
  CrossControl }` and a `RowState`.
- `TeamLine { team; label, seats, census; diag }` — the three cells behind
  the swatch.
- `Inputs` is borrowed per call and never cached: the save, host/networked
  flags, `my_team`, the session difficulty, the arena's `authored_mask`, the
  lobby players and local indices, map-unit counts, the staged
  `ScenarioRosterReport*` (nullptr = not staged) and the staged health.
- `Outcome { Stayed, Advanced, SetLevel, Turned, SetDifficulty, OpenLineup,
  OpenViewLevel, Go, Refused, Closed }` with the level, knob, difficulty
  value and message the caller needs.
- `open(Inputs, entry_page)`, `page()`, `step()`, `choose(row, dir, Inputs)`
  (`dir = +1` click, `-1` the `<` cell / right-click / `N-`), `next`, `prev`,
  `goto_step`, `page_step`, `level_applied`, `refetch`, `take_message`.
- **Update (2026-09-20, PR #307):** three names in the two bullets above are
  DELETED — `OutcomeKind::SetDifficulty` and `Outcome::difficulty` (the wizard
  no longer sets difficulty; the row went back to the DIFFICULTY screen) and
  `MatchSetupSession::take_message` with the `message_` field behind it.
  `Outcome::message` is the one message channel (§3.3, §10).
- **Update (2026-09-21, PR #307):** the signature is `choose(row, Inputs)` — the `dir`
  argument is gone with the reverse, and `turn(knob, Inputs)` with it. The
  same collapse ran down through `picker_common`: `wheel_next(wheel,
  current)`, `cycle_ctf_capture_limit(save)`, `cycle_time_limit(save)`,
  `turn_match_sides(save, my_team, mask)`, `turn_match_fill(save, my_team,
  mask)`. `cycle_lineup_fill(current, dir)` keeps its `dir`: it predates
  this PR and LINEUP owns it (§11).

Level rows answer `SetLevel` WITHOUT touching the save (the
`CampaignPickerSession` contract); the renderer runs its client's gated tail
and calls `level_applied` only on `Set`/`SetReplay`. Knob rows WRITE the save
through the shared helper and answer `Turned{knob}`; the renderer runs its
client's post-write tail. `Difficulty` answers `SetDifficulty{value}` because
the value is session state, not save state.
**Update (2026-09-20, PR #307):** the last sentence is REVERSED: the wizard has
no `Difficulty` row any more (R2-3 sent the seven rules back to the Base Camp
DIFFICULTY door), so `SetDifficulty` and the outcome's difficulty value are
deleted rather than renamed (§3.3, §10).

**Bookless versus campaign** (`picker_menu("")` fetches nothing) or an
`arena_page` that names no root row: the ARENA step lists every level of the
mount in id order through `og::data::list_levels_v()` — the level browser's
own source, no new scan and no edit to `level_picker.cpp` (2013 Dearborn code
with heritage comments). The one shipped versus campaign has a book; the
fallback is pinned on a synthetic fixture.

### 3.2 The Lua picker contract — two additive items

1. **Root hosting.** A versus campaign's `picker_menu("")` is the page the
   wizard's GAME step hosts. Modes returns the games index from `""` and
   keeps `"games"` as a valid alias. A nil root on a versus campaign means
   "no book" (the §3.1 fallback). Depth cap 4 and the 24-entry cap are
   unchanged.
2. **`match_knobs` hook** — one table, seven keys:

```lua
match_knobs = function()          -- pure; campaign-dispatch fence; per navigation and per deal, never per frame
  return {
    teams = true,                 -- the SIDES row + the per-team lines (default true)
    fill  = "macro",              -- "macro" (default) | "band" (team 1's wheel alone) | false
    score = true,                 -- SCORE row + rules-line cell (default true)
    time  = true,                 -- TIME LIMIT row + rules-line cell (default true)
    lines = { "STRONG adds a fighter, BRUTAL two." },   -- <= 2 lines, <= 38 glyphs, on the TEAMS step
    arena_page = "soccer",        -- the ROOT ROW whose page lists the cursor's arena
    deal  = "strong",             -- the FILL word a fresh arena deals (default "fair"; §3.8.7)
  }
end
```

Parsed into `og::script::hooks::CampaignMatchKnobs` (defaults = everything
on, no lines, no `arena_page`, `deal_fill = kFillFair`) with the
`campaign_picker_page` fence/bracketing discipline; a malformed answer is a
named load error. Absent hook = defaults, so every other campaign and every
bookless pack is unaffected. Modes answers from ONE `MODE_KNOBS` table keyed
by `current_pair().mode.id`: tdm/ctf/soccer/basketball get all four knobs;
ffa/mutant get `teams=false, fill="band"` with the line
`FILL sets how strong the bots are.`; onslaught gets `teams=false,
fill=false, score=false`. `deal` and the ball-arena line both derive from
`mode_shape.lua`'s `bodies` flag — one home for "which games buy bodies".
Why a hook and not a C++ table keyed on the staged `mode_name`: the C++
table would spell mode semantics twice, and `mode_name` only exists after a
stage lands, which would flicker rows during the debounce.

### 3.3 The three renderers, the shared terminal driver and the model item

| Client | What it adds |
|---|---|
| SDL | **Update (2026-09-21, PR #307):** `kMatchSetupRows` is 19, not 28 (§11). `menu_screen_specs.cpp`: `kMatchSetupRows` (28), `match_setup_menu_screen_spec()`, `match_setup_rewire`, `_draw_background` / `_draw_content`, `match_setup_on_spec_row`, `match_setup_frame_tick` (level-reload guard + `match_settings_fingerprint` compare + a `stage_generation()` watch rebuilding the cached report), `run_match_setup_screen(entry_page)`; `MenuScreenId::MatchSetup` registered **Runtime** so the engine-wide sweeps cover it; the screen state installed through the file-static seam pattern. |
| Shared terminal driver | `run_terminal_match_setup(SaveData&, const TerminalMatchSetupIo&)` in `match_setup_session.cpp` — ONE prompt loop for both terminal clients (the `run_terminal_campaign_camp` precedent). Per prompt: the deal (`deal_arena_lineup_for_cursor` + autosave, the `present_menu` cadence, so the TEAMS prompt after an arena pick reads the dealt word), `io.census(stage)` for the report, `build_terminal_match_setup_model`, `io.prompt`, then dispatch — `SetLevel` through `terminal_level_set_gate` (§3.4), `Turned` → `io.autosave()`, `SetDifficulty` → `io.set_difficulty(value)`, `Refused` → `io.notice`. **Update (2026-09-20, PR #307):** `SetDifficulty` is gone from the driver; `Turned` autosaves unconditionally and says nothing, and an `Acted` answer carrying a level routes through `terminal_route_acted_level` (§10). |
| Text | `text_picker.cpp`: `setup_screen()` wires the io and calls the driver; `handle_team_build_item` gains `PickerMenuCommand::MatchSetup`. |
| Curses | `curses_picker_client.cpp`: `setup_flow(...)` with a MUTABLE options reference (its `set_difficulty` writes `options_.difficulty`); numbered-prompt driver, not `Menu::choose` (up to 12 items). **Update (2026-09-20, PR #307):** the curses `setup_flow` takes a const options reference — the wizard no longer writes difficulty (§10). |
| Projection | `terminal_menu_model.h`: `TerminalMatchSetupModel { lines, items }` + `build_terminal_match_setup_model`. |
| Model | `menu_model.cpp`: `kTeamBuildItems` 12 → 13 with `{"setup", "Setup", PickerMenuCommand::MatchSetup}` appended; `terminal_item_gate` gains the two `Custom` gates of §2.7. **Update (2026-09-20, PR #307):** `kTeamBuildItems` is 12: item 13 `Setup` is retired and item 11 `Difficulty` is ungated. The one `Custom`-gated terminal item is now SCENARIO's `Replay Level`, which refuses on versus with `Arenas are set, never replayed.` (§10). |

**The SetDifficulty tail** (lead ruling 2 of this PR's execution plan): the
session computes the VALUE, so `<`, right-click and the terminal `N-` can
step −1. **Update (2026-09-21, PR #307):** none of those three doors
exists any more (§11); the value-taking tails are unaffected. Each client extracts ONE value-taking tail
(`apply_difficulty_value` / `apply_options_difficulty`) that the existing
cycling case calls with `cycle_difficulty(current)` and the wizard calls
with the value. One tail, two callers — no twin.
**Update (2026-09-20, PR #307):** the wizard no longer sets difficulty at all — the
row went back to the DIFFICULTY screen — so `OutcomeKind::SetDifficulty`,
`Outcome::difficulty` and `TerminalMatchSetupIo::set_difficulty` are deleted.
The value-taking tails stay: each client keeps ONE of them for its own cycling
case (§10).

### 3.4 One implementation per rule — the no-twins audit

| Rule | Home after this PR | What moved / died |
|---|---|---|
| SIDES macro and face, clamped to the arena's authored side count | `og::ui::turn_match_sides` / `match_sides_face` in `picker_common` | from `campaign_picker.lua` (which looped 0..3 with no clamp); the Lua is deleted |
| FILL macro (on opponents + own band, lowest opponent when none on, MIXED face, wrap clears) | `turn_match_fill` / `match_fill_face` | deleted from Lua **Update (2026-09-20, PR #307):** fix B: with no band on, a FILL turn lights EVERY authored opponent, not the lowest; the wheel is `weak to brutal` and there is no wrap to NONE (§10). |
| the said-lines (`Two sides. One squad at FAIR.`, …) | with the macros, strings verbatim | deleted from Lua **Update (2026-09-20, PR #307):** the said-lines are DELETED, not moved — the knobs speak through their faces alone (§10). |
| SCORE cycle `{0,1,3,5,10}` + toasts | `cycle_ctf_capture_limit(SaveData&, int dir)` in `picker_common` | the Lua twin is deleted; the SCENARIO row is PARKED, `change_ctf_caps` deleted, `ButtonAction::CycleCtfCaptureLimit = 61` retired-do-not-reuse |
| TIME LIMIT cycle + face `MAP` / `n MIN` + toasts | new `cycle_time_limit` / `format_time_limit_label` | from Lua; its `nM` face retired with it |
| off-wheel value rejoins at the head | one C++ `next_value` inside the cyclers | from Lua |
| the rules, spelled once | the RULES faces = the shared formatters upper-cased; `format_match_rules_lines` packs the SAME faces two per line and is BOTH the joiner-RULES lines and the MATCH step's | no abbreviated recap spellings exist |
| cross control's word | `format_cross_control_label` says `CROSS CONTROL: OWN/ALL` | `CTRL:` is gone from every surface |
| the authored side mask | `og::sim::authored_team_mask` through the deal's two readers → `Inputs.authored_mask` | nothing new |
| reverse step on a cycler row | ONE session entry, `choose(row, -1)`, reached three ways: the `<` cell, the terminal `N-`, and the mouse right-click (`do_call_right` stashes the row with `menu_spec_row_reverse = true` and returns the callback's real value; the runner resets the stash after dispatch, and a stash that survives a frame fails under TESTING) | one entry, three doors **Update (2026-09-21, PR #307):** there is no reverse step: all three doors and the whole `menu_spec_row_reverse` stash are DELETED, and a right-click on a spec row dispatches nothing (§11) |
| GO | `ButtonAction::GoMenu` dispatched by Base Camp for the wizard's `Go` | nothing copied; the popups stay the host's own |
| the deploy refusal | `og::ui::local_seats_deployed_for_go` hoisted from the strip GO handler's inline block; the strip GO pops the title, the MATCH step dims GO | one predicate for both GO surfaces |
| level-set answer | `scripted_level_set_answer(ScriptedLevelSet)` — the enum→string/trace switch; the Base Camp tail, the zone submenu tail and the wizard tail keep only their own refetch/toast/return around it | two byte-similar switches collapse to one |
| terminal level-set gate | `terminal_level_set_gate(...) → { DeniedHost, Closed, Unchanged, Applied }` in `campaign_picker_session.cpp` | three inline copies collapse to one; the wizard cannot skip the gate |
| scripted row face | `compose_scripted_row_face(row, budget, level_rows_actionable)` file-static in `menu_screen_specs.cpp`, used by the zone submenu rewire, the Base Camp zone rewire and the wizard rewire | two inline copies collapse to one |
| lobby-settings fingerprint | `og::ui::match_settings_fingerprint(const SaveData&)` in `picker_common`; each consumer keeps its own seeded/last logic over it | the field list exists once |
| seat run | `format_lineup_seat_run(labels, seat_count, budget)` in `picker_common`; LINEUP's content pass and the wizard's TEAMS line both call it | one composition; LINEUP's mid-word cut is gone |
| the report's team line | `format_scenario_report_team_line(report, team)` hoisted out of `format_scenario_report_lines`' loop; the loop and the MATCH step both call it | one composition |
| joiner caption | `kHostSetsForEveryoneCaption`, used by `difficulty_panel_caption()` and the wizard | hoisted out of the team-build screen |
| what a fresh arena deals | `deal_arena_lineup_fill(save, mask, code)`; the two readers fetch the code through `arena_deal_fill_code(save)` = `campaign_match_knobs().deal_fill` when a deal is pending | the FAIR literal has one home and one override |
| which knobs a game uses, which page lists its arena | the campaign's `match_knobs` hook | the engine spells no mode fact |

### 3.7 The Base Camp ceiling: 73 → 74

`docs/lineup-design.md` §2 ruled "Base Camp itself is at its 73-button
ceiling with a fully packed strip, so it gets no new door." This PR
supersedes that sentence the way camp-controls §5 added DIFFICULTY:
**append, never carve** — ordinal 73 `setup`, ceiling 74 across the tied
triple. No established ordinal moves and the parked spares stay a reserve.
The strip's geometry is untouched because SETUP is a TWIN of DIFFICULTY on
one rect, statically hidden like READY, so the no-overlap pin holds on the
materialized table with no test change. The wasm coordinate contracts (GO,
NETWORK) do not move.
**Update (2026-09-20, PR #307):** REVERSED: the twin is deleted and the ceiling is
**73** again. `kCreateMenuSetupIndex` is gone, `MAX_BUTTONS` /
`GameSession::kMaxButtons` / `kCreateMenuButtonCount` are all 73, and the
wizard's door is the Base Camp docket's own SETUP row — one door on every
client, no new ordinal (§10).

### 3.8 The #305 rule: the FILL step buys bodies in the ball games

Keep the five-word wheel (the 12-glyph face is spent). Keep the headcount
`H = MATCHED.SIZE` as the baseline. In **soccer and basketball**, on the
EMPTY-team arm only (a squad that IS the team's whole side), each wheel step
above FAIR adds **one body at roughly one human's power**. A step the shape
cannot absorb as a body falls back to the 25 % power step, so the wheel is
never a no-op and a full roster (H ≥ 5) plays byte-identically. WEAK, FAIR,
the allies and troops arms, TDM, CTF, onslaught, FFA/mutant and every
classic campaign are byte-identical.

```
steps    = code - FILL_FAIR ;  F = #families ;  room = squad_room(shape.cap, fielded)
base     = H > 0 ? min(H, F, room) : min(F, room)             -- today's count
bodies   = (shape.bodies and fielded == 0 and steps > 0 and H > 0)
             ? min(H + steps, F, room) : base
absorbed = bodies - base
pct      = FILL_PERCENT[code] - (FILL_PERCENT[code] - FILL_PERCENT[code - 1]) * absorbed
                                    -- one wheel step per absorbed body, read off the
                                    -- table's own spacing, never a second constant
target   = div(P * pct * bodies, 100 * base)                  -- P = the empty-team reference
```

`target` reduces to `div(P × pct, 100)` whenever `bodies == base` (the floor
is invariant under a common positive factor), so the formula is written ONCE
with no fast-path twin, and per body the solve lands at the FAIR per-fighter
figure.

| H (soccer 820-823 / basketball 824-829, empty bench) | WEAK | FAIR | STRONG (the dealt default on these arenas) | BRUTAL |
|---|---|---|---|---|
| 1 | 1 @ 75 % | 1 @ 100 % | **2**, ≈ 1 human each | **3**, ≈ 1 human each |
| 2 | 2 @ 75 % | 2 @ 100 % | **3** | **4** |
| 3 | 3 @ 75 % | 3 @ 100 % | **4** | **5** |
| 4 | 4 @ 75 % | 4 @ 100 % | **5** | 5 @ 125 % (one step absorbed, one falls to power) |
| ≥ 5 | 5 @ 75 % | 5 @ 100 % | 5 @ 125 % | 5 @ 150 % — today's numbers |
| 0 (no human power) | the legacy difficulty squad of 5, unchanged | | | |

Allies (a squad beside a company) and troops (a squad beside standing map
units) keep `bodies = base`: #305 is about the opponent the reporter faces.

**The seams.** `packs/core/lib/lineup.lua` exports
`squad_shape(knob, headcount, room, table_size, fielded_is_empty,
bodies_allowed) → count, pct, base`; the two size helpers collapse into a
pure `squad_prefix(families, count)`; `fill_target` takes `count, base` and
computes `og.div(P * pct * count, 100 * base)` in both arms;
`spawn_bots`' fifth parameter becomes `shape = nil | { cap, bodies }`.
`campaigns/modes/packs/modes.core/lib/mode_shape.lua` is the ONE table
(`soccer = { bodies = true }`, `basketball = { cap = 5, bodies = true }`),
read by soccer's and basketball's `T`, and by `campaign_picker.lua`'s
`match_knobs` for both the ball-arena line and the `deal` word. Soccer gets
no cap: a cap would change its allies room below FAIR, which the issue never
asked for, and `F = 5` bounds the body count anyway. Every other mode passes
a nil shape and is untouched.

**3.8.4 What the player sees — counts, never a rule twin.** The wizard's
TEAMS lines and the MATCH step census the staged world (`2 BOTS`,
`MATCHED BOTS (2) STRONG`). LINEUP's census column switches from
`format_lineup_census` to `format_match_preview` — the same formatter the
wizard uses, fed by the report the page already stages — so an empty bot
team at STRONG reads `2 BOTS` where it read `NO FIGHTERS`: the page where
the wheel turns finally shows what the wheel buys. The map-units hint keeps
its own column. The campaign's line on the TEAMS step says
`STRONG adds a fighter, BRUTAL two.` VIEW LEVEL is unchanged.
`openglad_demo` gains `OPENGLAD_DEMO_FILL=<0..4>` so the BRUTAL evidence is
capturable.

**3.8.6 Anchors and the obmap ledger.** The per-team ceiling is unchanged
(5): the rule reaches today's maximum at smaller H and never exceeds it, so
basketball's one-bot-per-anchor ruling stands, soccer's 12 anchors hold 5
with room, and no `.fss`, marker or regeneration is needed. The generator
ledger gains `+ row.team_count * kFillSquadCeiling` with
`kFillSquadCeiling = 5`, **on the ball arenas only** (lead ruling 8 of this
PR's execution plan): #305 adds bots on soccer and basketball alone, and a
brawl arena's FILL squads stay inside the pre-existing `+16 heroes`
assumption, which this rule leaves alone. Cap 190 stays; every ball row
lands under 100 (worst 823 = 98 on soccer, 826 = 99 on basketball; the whole
map is `expected_ledger` in `test_modes_levels.cpp`). The pin mirrors the
expression; output bytes are unchanged.

**3.8.7 The deal: what a fresh ball arena fields.** Amendment 7 deals
`FILL: FAIR` to a versus arena's authored teams once per cursor. Under the
rule above, FAIR on THE PITCH is still the 1v1 the reporter called too easy,
so a rule that only bit above FAIR would be invisible at the shipped
default. The fix that keeps ONE meaning for every wheel word and puts
mode-ness in Lua: **the campaign declares the dealt word**.
`match_knobs.deal` is `"strong"` for soccer and basketball (derived from
`mode_shape`'s `bodies`) and absent everywhere else;
`deal_arena_lineup_fill(save, mask, code)` writes the code the hook names.
The memo is unchanged, so a player who turns a ball arena down to FAIR keeps
FAIR through every re-entry, and every brawl arena, every classic campaign
and every existing test on 300/500 is byte-identical.

The two rejected alternatives, recorded for the maintainer:

- **(A) Above-FAIR only, FAIR deal unchanged.** Literal to the issue's hedge
  ("maybe for modes above FAIR") and zero re-pins on the ball arenas — but
  invisible at the default; the reporter's game does not change.
- **(B) Redefine FAIR as H+1 on ball arenas.** Changes the default without a
  new word, but breaks the one-table formula (the extra body is no longer an
  absorbed wheel step, so it needs a second constant) and gives one wheel
  word two meanings across games; re-pins every FAIR test on 820/824 and the
  headcount documentation.

### 3.9 Untheme (#306): the vocabulary rule

The engine's own words. A cleared level is CLEARED, a level is an ARENA, a
mode is a GAME, the settings are the RULES, the flow is SETUP, the last step
is the MATCH, the bot-side count is SIDES. No narrator, no ledger, no
signature, no address to an audience. Nothing Roman is introduced: the
original is a fantasy mercenary-band game, and ludus/lanista would be an
invention — replacing one cute layer with another is the failure mode #306
names.
**Update (2026-09-20, PR #307):** CLEARED is NOT one of this campaign's words any
more. Multiplayer Arenas states no progress at all: no readout, no tally line,
no `[CLEARED]` tail, no `n/m cleared` note. The engine's own CLEARED strings
stay for the campaigns that earn their roads (§10).

What STAYS, deliberately:
- `arena` — this campaign's own word for a level, and the repo's one
  Gladiator-flavoured non-Roman noun.
- `BAND` — the original game's noun for a company
  (`campaigns/gladiator/campaign.yaml` "a band of mercenaries",
  `mode_fighters.lua`'s `BAND FULL`). The briefings keep it.
- **The engine's "book" strings** — `THE BOOK` (the BOOK-DOOR label),
  `This book takes no orders.`, `The book goes no deeper.` and every `book`
  in `campaign_picker_session.*` are the ENGINE's noun for a scripted page
  tree, used by longseason (`SETTLE THE BOOK`, pinned), imaginations and
  westlands. #306 names the Multiplayer Arena campaign; a repo-wide
  de-booking would be a separate decision, and this PR deliberately did not
  take it.

What went, and to where — **[SUPERSEDED — issue #306.]** every old string in
this list is retired and its replacement is what ships:
- the narrator (`-- THE GAMESMASTER` × 40 briefing sign-offs, the header
  comment, the camp's fourth-row page name) → deleted; each briefing ends on
  its last rule line, and the strip door is `SETUP`;
- the ledger/deck vocabulary in briefings (`THE BOOK`, `LEDGER`,
  `CONTENDERS`, `PURSE`, `TALLY`, `PAGE OF`, `TONIGHT`) → the game's own
  words (`SCORE`, `COUNT`, `WINS THE MATCH`, `FIGHTER KILLS`);
- the signature (`SIGN THE BOOK`, `your name, for good`, the `sign` /
  `sign_book` actions) → deleted; a full tally reads `Cleared: 40 of 40.`
  and `book_signed` becomes an orphaned save key (§9);
- the stamp words (`n/m stamped`, `Stamped: n of m.`,
  `Every field here is stamped.`, the `BOOK` readout) → `n/m cleared`,
  `Cleared: n of 40.`, `Every arena here is cleared.`, the `CLEARED`
  readout;
- `FIELD ` / `FIELD: <arena>` → `ARENA ` / `ARENA: <arena>`;
- the index page's two retired titles (the seven-games one, and the
  per-campaign `THE BOOK OF <NAME>`) → the one title `GAMES`;
- `RANDOM SCENARIO` / `any game, any field` → `RANDOM ARENA` /
  `any game, any arena`;
- the camp's `MATCH SETUP` row and `TARGET SCORE` knob → the SETUP wizard's
  TEAMS and RULES steps, where the score knob has one home, `SCORE:`;
- the campaign display title `Multiplayer Game Modes` → `Multiplayer
  Arenas` (the id `modes` never changes: it is the mount, state and
  completed key);
- `BaseCampSlotKind::Card` → `Seat`, and the "P# card" prose → "P# seat"
  (no live player-visible CARD string existed; the deck was already retired
  by the roll).
  **Update (2026-09-20, PR #307):** the stamp words' replacements are retired
  in turn: `n/m cleared` is now `N arenas`, and `Cleared: n of 40.`,
  `Every arena here is cleared.`, `Next uncleared:` and the `CLEARED` readout
  are all deleted with no replacement. `RANDOM ARENA - any game, any arena`
  left the camp for the wizard's two roll rows, and `MATCH SETUP` is the
  docket's one `SETUP` row (§10).

The generator-side **theme lint** is the one guard for briefings: a builder
`fail()` when any briefing line contains `GAMESMASTER`, `THE BOOK`,
`LEDGER`, `CONTENDERS`, `PURSE`, `TALLY`, `PAGE OF` or `TONIGHT`, begins
`-- `, **or contains any lower-case letter** (the briefings are the 4×6
upper-case font, so the case-sensitive word list is complete by
construction). `THE GAMESMASTER` is in neither prose gate (§4.6): it never
was an on-screen label, and two guards for one rule would be a rule twin.

---

## 4. Blast radius

### 4.6 Gate rows — one gate per retired name

The two prose gates differ in mechanism, and that decides which name goes
where:

- `scripts/check_retired_phrases.sh` scans `docs/`, `.claude/skills/`,
  `campaigns/**/*.md`, `scripts/**/*.md`, `tools/**/*.{cpp,h}`,
  README/AGENTS/CLAUDE and accepts a hit whose BLOCK carries a dated note.
  **Every name that survives verbatim in a dated snapshot goes here and ONLY
  here**, because the annotation is the only mechanism that can excuse a
  snapshot.
- `scripts/check_retired_hud_labels.sh` scans EVERYTHING (src, include,
  tests, tools, scripts, docs, cmake, packs, campaigns, web, relay, skills,
  README/AGENTS/CLAUDE — including `scripts/retired_phrases.txt` itself) and
  accepts NO annotation, only a count allowlist whose bar excludes "prose
  that merely names the row". **Only a name whose every residual mention is
  rewritten or deleted in this PR goes here** — and no phrases-gate sample
  may spell one of these regexes, or the label gate reds on the sample.

This PR adds thirteen rows to `scripts/retired_phrases.txt` and three to the
label gate's parallel arrays (the seven-games index title, the
per-campaign book-of title, and the `FIELD:` arena prefix — the three names
whose every residual was a test fixture, a test pin or the Lua this PR
rewrote; here and in §3.9 they are named only behind a `<placeholder>` or a
lower-case tail, which the upper-case tails of those three regexes never
match — keep it that way, or this file reds the gate it documents).
`scripts/retired_hud_label_sites.txt` gains NO row: a hit is rewritten at
its site, never allowlisted. Neither gate scans `src/` or `tests/` for the
phrase rows, so the comments there that name a retired page were rewritten
by hand in the same PR.

Until this PR has a number, every dated note it adds spells one with the
`#N` placeholder, and the PR author seds them in one pass. Two occurrences
of that placeholder are TEMPLATES of the annotation rule rather than notes
to fill, and the sed must skip both: the rule's statement in
`.claude/skills/openglad-pr-workflow/SKILL.md` (~:93) and the hint
`scripts/check_retired_phrases.sh` (~:254) prints beside every hit.

---

## 5. The one residual

**R1.** A five-step wizard adds a click to one warm flow. Base Camp GO is
unchanged (1 click for the same match) and the tab strip puts every knob at
most 2 clicks from Base Camp, but **the warm clock/respawns change is 4
clicks where it was 3**. That is the price of the single door, and the PR
body states it rather than hiding it.
**Update (2026-09-20, PR #307):** recounted. Cold 2v2 soccer is still 13 clicks over
5 screens (the docket row replaces the strip door one for one). Respawns are
now **3** clicks, not 4, because they went back to the DIFFICULTY door. Two
warm flows got longer instead: "another arena of the same game" is 4 clicks
(was 3) and a random arena to launch is 4 (was 2). That is the price of one
button, and the PR body states it (§10, R2-R3).

---

## 7. Decisions log

| # | Decision | Rationale |
|---|---|---|
| D1 | **Theme = UNTHEME**, in the game's own words; no Roman retheme | the original is a fantasy mercenary-band game; replacing one cute layer with another is the failure mode #306 names |
| D2 | **BAND stays** in briefings; the narrator's vocabulary goes | BAND is the game's own noun for a company and already a live HUD word; the untheme removes the narrator, not the game's words |
| D3 | **Card** = the seat-slot enum rename and the "P# seat" prose | no live CARD string existed; renaming the last two readings closes the word at zero player-visible cost |
| D4 | **Campaign display title `Multiplayer Arenas`**; id `modes` never changes | the maintainer's own name for the campaign; 18 ≤ 29; the id is the mount/state/completed key |
| D5 | **Flow = a SETUP wizard** with a tab strip + PREV/NEXT + BACK/Escape-closes; GAME → ARENA → TEAMS → RULES → MATCH | the tabs keep any knob ≤ 2 clicks and answer the 3-press-exit complaint; the last step is the "one screen with the whole match" |
| D6 | **`ctf_capture_limit` gets ONE surface**, the RULES `SCORE:` row; the SCENARIO row is parked; the Lua twin dies | "the score on two screens" is part of the #304 complaint, and the wizard now serves every versus pack |
| D7 | **`time_limit`'s home is the RULES row** (`cycle_time_limit` in `picker_common`), faces `n MIN` | one home as before, moved to C++ so bookless versus packs can set the clock; `5M` reads as a quantity, not minutes |
| D8 | **Base Camp ceiling 73 → 74**: ordinal 73 `setup`, DIFFICULTY's twin on one rect, statically hidden like READY | append-never-carve; strip geometry and wasm contracts untouched **Update (2026-09-20, PR #307):** REVERSED: the strip twin is deleted, the ceiling is 73 again, and the wizard's door is the docket's SETUP row (§10). |
| D9 | **DIFFICULTY's knobs on versus campaigns live on the RULES step** (two surfaces, one rule); the DIFFICULTY door stays on classic campaigns, on all three clients | the fight's rules belong in the match setup; one door per campaign kind per client **Update (2026-09-20, PR #307):** REVERSED: the DIFFICULTY door is back on the strip of every campaign; the RULES step keeps SCORE and TIME LIMIT only (§10). |
| D10 | **Joiner face = cut the row, print the line** + browsable tabs; caption shared with DIFFICULTY; the joiner camp docket is the two rows and no line | the established grammar; Disabled cells would be a second grammar for one rule **Update (2026-09-20, PR #307):** the joiner camp docket is the SAME one SETUP row the host sees, browsable; the joiner's RULES is the caption plus one packed line (§10). |
| D11 | **No batching**: each knob write syncs at once, the stage debounce coalesces, the MATCH step recomposes on `stage_generation()`, GO forces `ensure_current()` | the zone action tail's behaviour today; preview == launch by construction |
| D12 | **Which knobs a game uses, which root row lists its arena, and what a fresh arena deals are the campaign's `match_knobs` hook**, not a C++ table keyed on `mode_name` | the engine must not spell mode semantics; `mode_name` exists only after a stage lands and would flicker rows |
| D13 | **#305 = bodies above FAIR** (STRONG = H+1, BRUTAL = H+2 at FAIR per-body power) on the empty-team arm, bounded by the squad and the room; **the ball arenas DEAL STRONG** | literal to the issue's hedge for the rule, while the shipped default changes for the arenas the issue names; alternatives costed in §3.8.7 |
| D14 | **Allies and troops arms keep today's count** | #305 is about the opponent; "allies field the gap" is a documented ruling nobody complained about |
| D15 | **Soccer stays uncapped** (`{bodies = true}`); basketball keeps `cap = 5` | a soccer cap would change its allies room below FAIR; `F = 5` bounds the body count anyway |
| D16 | **The headcount rulings are amended for soccer and basketball above FAIR** (lineup-design Amendment 8; matched-teams Update notes) | the headcount stays the BASELINE; the striker-only arm is reached only at WEAK/FAIR and by whittled squads |
| D17 | **No `FILL_STEP_PERCENT` constant**; the per-body step is read off `FILL_PERCENT`'s own spacing | one table stays the only copy of what a wheel step is worth |
| D18 | **The player learns the body rule from counts** (`2 BOTS` on TEAMS, LINEUP and MATCH) plus one campaign-authored line | counts are the staged census, never a rule twin; the words come from the campaign that owns the fact |
| D19 | **A cycler row steps back through ONE session entry, `choose(row, -1)`, reached by its 30×10 `<` cell, the terminal `N-` item and the mouse right-click; LEFT/RIGHT stay navigation** | forward-only wheels cost a full lap on overshoot, and right-click alone left touch and pads without a reverse; a key that steps a wheel on one screen and navigates on every other is a rule the whole picker would have to learn **Update (2026-09-21, PR #307):** REVERSED by the maintainer (§11). The wizard's cyclers cycle FORWARD ONLY, like every other cycler in the picker (the DIFFICULTY rows, the LINEUP wheels); the wheels are short — SIDES 3 stops, FILL 4, SCORE 5, TIME LIMIT 5 — so a mis-click costs at most four clicks. LEFT/RIGHT still stay navigation. |
| D20 | **GO on the MATCH step IS the strip GO's click**: Base Camp dispatches `GoMenu`, the TeamBuild intercept selects StartGame and answers `MENU_EXIT`, and the state machine runs the popups and the launch | one body; the game must run from Base Camp's frame, not nested in the wizard's |
| D21 | **The wizard hosts the campaign's book at its root** rather than a title-prefix catalog; the ARENA step IS the book at depth ≥ 2 | the book carries campaign-authored facts (rule lines, notes, cleared tallies) the engine cannot derive **Update (2026-09-20, PR #307):** still true, and the book's root now ends with the wizard's own `RANDOM` row — appended LAST so no arena ordinal moves (§10). |
| D22 | **A generator-side theme lint** (word list, `TONIGHT`, no lower-case letters) is the one guard for briefings | one implementation per rule; the sign-off never was an on-screen label |
| D23 | **The engine's "book" strings stay** | the scripted-page tree's engine noun, used by three other campaigns |
| D24 | **No wire/save/snapshot/replay bump**; `book_signed` orphaned | no new knob; `campaign_state` keys are free-form |
| D25 | **The rows are the docket's 42-glyph face beside a declared 30 px cell column (280..310) holding the pagers and the `<` cells; tabs, cells and pagers share ONE right edge (310)** | the footer NEXT is the step advance, so the pagers stay beside the rows they page; three unrelated right edges were a defect **Update (2026-09-21, PR #307):** the column holds the pagers alone now (§11); the rows keep their 264-wide face and everything still closes on 310. |
| D26 | **`kSetupLinesMax = 10` for C++ steps**; hosted Lua pages keep the contract's 6; content starts at y=47 | the MATCH step needs title + 4 teams + 5 rules lines; the Lua contract is untouched; vertical rhythm is part of the design |
| D27 | **The ARENA tab lands on the page that lists the cursor's arena**, opened on the window holding `[CURRENT]`; from depth ≥ 2 it is a no-op | a tab that searched seven pages or landed on the last page browsed would be a dead tab or a surprise |
| D28 | **On a versus campaign the docket's page rows are shortcuts into the wizard**; the zone submenu serves classic campaigns only | two chassis for one page tree, with a NEXT that meant two things, was the clutter #304 names **Update (2026-09-20, PR #307):** there is ONE docket row and it is a page row into the wizard's GAME step; the shortcut pair is gone (§10). |
| D29 | **Footer = BACK · PREV · NEXT**, own ids on the zone submenu's rects (PREV at 224) | BACK/Escape keeps meaning "close"; the shared rects keep the footer geometry the chassis already has, and the new ids keep the zone submenu's own oracle honest |
| D30 | **SIDES is clamped to the arena's authored side count**; the row hides on a two-side arena; team lines print authored teams only | a wheel that turns to 3 on a two-goal pitch writes a knob the map ignores while the line under it says EMPTY |
| D31 | **Team identity on the wizard is the colour swatch**; TEAMS labels `TEAM n`, MATCH keeps VIEW LEVEL's `<COLOR> TEAM` line; terminals spell `TEAM n COLOR` | one identity every screen already uses; spelling the colour in the TEAMS label would overflow a 2-seat team's line |
| D32 | **Line B is untouched inside the wizard**; the tab strip is the step indicator | the census is what a host watches while configuring a 2v2 with a remote joiner |
| D33 | **The MATCH step's GO is gated by the strip GO's own predicate and the stage health, and its FACE says why**; no "dirty" clause | a GO that ejects the player onto Base Camp to read a modal breaks the step's one promise |
| D34 | **The rules are spelled once**: the RULES faces are the shared formatters upper-cased, and one packer serves both the joiner's RULES lines and the MATCH step | abbreviated recap spellings were a second face for one knob |
| D35 | **The TEAMS rows are SIDES / FILL / LINEUP; the pointer line appears with any diagnostic** | the step cannot reseat anyone, so `NEEDS 2 FIGHTERS` sent the player hunting on the wrong step |
| D36 | **The current tab wears `[WORD]` and the Disabled (pressed-in) face** | it is inert, and the dimmed face with darker bevels is the chassis's pressed-in look |
| D37 | **`teams=false` steps show the staged report's own lines + FILL + LINEUP; onslaught keeps a TEAMS step** | four `TEAM n` lines about teams a band mode does not have were a lie; MAP UNITS is onslaught's live knob |
| D38 | **One retired name, one gate** (§4.6); `\|` is never written in a regex row | the label gate reads the phrases file and accepts no note, so a name in both reds the build on its own sample |
| D39 | **One shared terminal driver** with a per-prompt deal and the hoisted level-set gate; each client wires callbacks | two prompt loops would be the LINEUP twin repeated |
| D40 | **Six existing or imminent twins are hoisted in this PR** and the deploy predicate gets one home | the wizard would otherwise be the third copy of each; a no-twins PR cannot add copies |
| D41 | **The seat run has three tiers** in one helper LINEUP also calls | on an 18-glyph cell a 4-seat team must still name all four players; LINEUP's mid-word cut was a defect the hoist removes |
| D42 | **The description's last lines point at SETUP** | a gladiator player sees DIFFICULTY, not SETUP, until they switch campaigns |

---

## 8. Documented rulings this PR reverses, and where each is annotated

Each dated snapshot below keeps its text verbatim and carries a note in its
own block; the two living/code items (`docs/mp-game-modes.md` and
`picker_sdl_defs.h`) are rewritten in place instead.
**[SUPERSEDED — issues #304, #305, #306.]**
- `docs/lineup-design.md` §2 "gets no new door" → the SETUP door is an
  appended twin ordinal (73), ceiling 74 (§3.7).
- `docs/camp-controls-design.md` §3 (the random-draw row's deck vocabulary),
  §4 (the camp's direct-knobs page), §5 (the strip's DIFFICULTY door on
  versus campaigns, the SCENARIO score copy, the terminal item list).
- `docs/basecamp-zones-design.md`'s Modes camp bullet — the docket is three
  rows, the readout is `CLEARED n/40`, there is no signature row and no
  joiner line.
- `docs/campaign-scripting-design.md` "#212" — a versus campaign's root page
  is hosted by the wizard, the optional `match_knobs` hook is the new
  contract item, and the Book's setup page is retired.
- `docs/matched-teams-design.md` D34/D38/D39 for the ball games above FAIR,
  and `docs/lineup-design.md` Amendment 7's FAIR literal for the ball
  arenas → `docs/lineup-design.md` Amendment 8.
- `docs/basketball-design.md` D8's justification — the anchors still seat
  one bot each, but the wheel above FAIR fills the court toward five.
- `docs/mp-game-modes.md`'s squad-size paragraph (a living doc: rewritten in
  place, no note).
- `picker_sdl_defs.h`'s "this screen's back shares no other screen's
  geometry" — the wizard shares the rects under its own ids.
  **Update (2026-09-20, PR #307):** round 2 reverses four of these in turn:
  `docs/lineup-design.md` §2's ceiling is **73** again (the twin is deleted),
  `docs/camp-controls-design.md` §5's DIFFICULTY door is back on the strip of
  EVERY campaign, `docs/basecamp-zones-design.md`'s camp bullet is ONE row
  with no readout, and its Terminals bullet loses item 13. G3/H1's "wrap
  clears the own band" and "the lowest opponent when none are on" in
  `docs/lineup-design.md` are reversed by fix B (§10).

### The wave-3 test renames

Three click-helper cases and the LINEUP round trip were renamed with the
screen (`match_setup_*` → `setup_*` / `setup_wizard_*`):
`setup_click_helper_retries_a_dropped_press`,
`setup_click_helper_waits_out_a_landed_cycler`,
`setup_click_helper_recheck_saves_a_witnessless_cycler` and
`LineupUi.setup_wizard_macros_round_trip_with_lineup`. The capture scene
`CampaignZoneUi.zzz_uxr_capture_modes_match_setup_page` was **retired**: the
page it photographed no longer exists, and the wizard's own per-step capture
points replace it. The LINEUP round trip used to hang to the 420 s ctest
timeout when its 15 s wait expired without an escape tail; it now returns
through `tests/test_escape_tail.h`, and
`setup_click_helper_reports_a_ladder_that_never_lands` is the give-up
oracle.

---

## 9. The `book_signed` migration

The signature is gone with the untheme, and the save key it wrote is not
migrated: `book_signed` becomes an **orphaned campaign-state key**, read by
nobody and written by nobody, exactly as `card_seq` was left when the deck
retired. `campaign_state` keys are free-form (GTL v15), so an old save
carrying the key loads unchanged, a new save never writes it, and no version
moves. There is no cleanup pass — a migration that rewrote saves to delete a
key nothing reads would be more risk than the byte it saves.

---

## 10. Round 2 (2026-09-20, PR #307) — the maintainer's five items

The maintainer reviewed the branch at `e3582852` and sent five items. This
section is the record of what round 2 changed; every note above points here.
Sections 0-9 keep their text verbatim, as a dated snapshot should.

### 10.1 The five items and the rulings they became

| Item | Ruling | What ships |
|---|---|---|
| 1. "drop the whole toast. What's its point?" | R2-1 | The four wizard said-lines (SIDES, FILL, SCORE, TIME LIMIT) and their five formatters are DELETED, together with `kMatchCountWords`, `match_squads_phrase`, `match_fill_said`, `format_time_limit_said` and `format_ctf_score_said`. `turn_match_sides` and `turn_match_fill` return `void`. `Outcome::message` is the one message channel (`message_`/`take_message()` deleted), and it carries refusals, the engine's `Level set to <title>.` tail and the campaign's own Lua voice — which now reaches the terminals too. |
| 2. "the FILL: selector only changes teams 1 and 2, even for 4-player maps" | R2-2 | Fix B: when NO band is on, a FILL turn lights **every authored opponent** plus the own band, not just the lowest. A deliberate SIDES value is still respected while bands are on. NONE also leaves the wizard's FILL wheel (WEAK → FAIR → STRONG → BRUTAL, note `weak to brutal`): in the wizard NONE only ever emptied a versus arena. LINEUP keeps per-team NONE. Pinned end to end on SDL and on the text client, on a four-side arena, and from the collapsed state. |
| 3. "why does Rules include all the difficulty settings?" | R2-3 | RULES = **SCORE and TIME LIMIT** — the two `match_knobs` — plus one pointer line, `Respawns and the rest: the Base Camp DIFFICULTY.` RESPAWNS, SPAWN DELAY, PERMADEATH, GENERATORS, DIFFICULTY, INFINITE GOLD and CROSS CONTROL return to the DIFFICULTY screen, which is on the strip of every campaign again: `BACK · DIFFICULTY · SCENARIO · NETWORK · GO`. Five steps stay; the MATCH step's recap still states every rule line. |
| 4. "get rid of the CLEARED: bullshit" | R2-4 | No progress vocabulary anywhere in Multiplayer Arenas, through ONE predicate, `og::ui::progress_marks_shown(save)` = `!is_versus_campaign(save)`. It is read by the wizard's `[CLEARED]` seam, the camp, SCENARIO → SET LEVEL, PROGRESS (header, status column and row affordance), the SET CAMPAIGN card and the terminals' `Replay Level`. `[CURRENT]` stays. Classic campaigns are byte-identical. |
| 5. "replace all three multiplayer arena buttons with just one button… add a RANDOM button for game type and map" | R2-5 | The docket collapses to ONE row, `SETUP - <GAME>: <ARENA>  >`, which states the current match and opens the wizard on GAME; the joiner sees the same row, browsable. The strip twin is deleted. The wizard gains two roll rows, appended LAST so no arena ordinal moves: `RANDOM - any game, any arena` on the GAME step and `RANDOM ARENA - any arena of this game` on each game's ARENA page, both from ONE Lua `roll(rows)` helper and both host-gated in Lua. An `Acted` answer that carries a level now routes through each surface's existing gated level tail. |

R2-6 records that item 2 is fixed on its own terms: RANDOM is not a
workaround for a collapsed arena.

### 10.2 Rows this round reverses

- **D8** — ordinal 73 `setup` and the raised ceiling: both deleted. The
  Base Camp ceiling is 73.
- **D9** — DIFFICULTY's knobs on the RULES step: reversed. The DIFFICULTY
  door is on the strip of every campaign, and RULES holds the two match
  knobs.
- **D10** — the joiner camp docket "two rows and no line": one row.
- **D28** — "the docket's page rows are shortcuts": there is one docket row.
- **G3 / H1** in `docs/lineup-design.md` — "with none on it turns on the
  lowest opponent" and "the wrap clears the own band": both reversed. Fix B
  lights every authored opponent, and NONE is off the wizard wheel so there
  is no wrap to clear anything.
- **The camp docket's three rows** in `docs/basecamp-zones-design.md`: one row.
- **Terminal Team Build item 13 `Setup`**: retired. `kTeamBuildItems` is 12,
  item 11 `Difficulty` is ungated, and both guard strings are deleted.

### 10.3 The FILL rule after fix B

Wheel `{WEAK, FAIR, STRONG, BRUTAL}`. Off-wheel faces (NONE = every band
off, MIXED) rejoin at the head, WEAK, in either direction. Targets are the
ON authored opponents, or every authored opponent when none is on, plus the
own band. On a four-side arena with `my_team 0`:

| State (fill[0..3]) | SIDES face | FILL face | turn +1 → | turn −1 → | SIDES after |
|---|---|---|---|---|---|
| dealt `{F,F,F,F}` | 4 | FAIR | `{S,S,S,S}` | `{W,W,W,W}` | 4 |
| SIDES 3 `{F,F,F,N}` | 3 | FAIR | `{S,S,S,N}` | `{W,W,W,N}` | 3 — a deliberate SIDES value is respected |
| SIDES 2 `{F,F,N,N}` | 2 | FAIR | `{S,S,N,N}` | `{W,W,N,N}` | 2 |
| all off `{N,N,N,N}` | 1 | NONE (off-wheel) | `{W,W,W,W}` | `{W,W,W,W}` | **4** — the reported trap, closed |
| own only `{F,N,N,N}` | 1 | FAIR | `{S,S,S,S}` | `{W,W,W,W}` | 4 |
| MIXED `{N,W,S,N}` | 3 | MIXED | `{W,W,W,N}` | `{W,W,W,N}` | 3 — no wrap to NONE any more |
| BRUTAL `{B,B,B,B}` | 4 | BRUTAL | `{W,W,W,W}` | `{S,S,S,S}` | 4 |

Two-side arenas (820, 824, 500) have one opponent, so fix B is a no-op
there; only the loss of NONE is visible. SIDES owns "fewer sides"; the
wizard's FILL can no longer empty an arena.

### 10.4 The save bump: GTL 19 → 20, and what a player sees

The round-1 build could persist the collapsed state — `SIDES: 2` with the
deal memo stamped — and the memo stops a re-deal, so fix B alone would leave
an affected company at two sides forever. The memo pair entered the `.gtl`
at v19; the writer moves to **v20** and the memo is read only from v20
files, so a v19 file loads with the memo cleared and the next arena visit
re-deals once, lifting NONE bands only. No other format-free heal exists:
the byte signature of the collapsed state is also the signature of every
deliberate SIDES 2/3 after a FILL turn, so a signature heal would re-fill a
chosen SIDES on every entry.

What the player sees, stated in the PR body verbatim:

> A company saved by the previous build re-deals every arena once on its
> first visit: a four-side arena that had collapsed to SIDES: 2 comes back
> to SIDES: 4 with the two restored bands at the arena's deal word (FILL
> reads MIXED until the next click). An arena you had deliberately narrowed
> with SIDES in that older company is re-filled once the same way — turn
> SIDES again.

Protocol 18, the snapshot and the replay formats do not move.

### 10.5 The SCENARIO surfaces (R2-4's full scope)

On `progress_marks_shown(save) == false`:

| Surface | Before | After |
|---|---|---|
| SCENARIO → SET LEVEL status column | `CLEARED` / `CURRENT` / `LOCKED` | `CURRENT` only (an arena is never LOCKED on versus) |
| SCENARIO → PROGRESS header | `Level Progress: 3 cleared of N discovered` | `Arenas: N` |
| PROGRESS status column | `CLEARED` / `CURRENT` / `-------` | `CURRENT` / `-------`, through the one `level_row_status_label` |
| PROGRESS row affordance | REPLAY / VISIT on cleared rows | GO on every row |
| SCENARIO → SET CAMPAIGN card line | `3 out of 40 completed` | `40 arenas` |
| Terminals → SCENARIO → `Replay Level` | prompts for a level id it would call uncleared | refuses first: `Arenas are set, never replayed.` |

The PROGRESS header's number is the ACCESSIBLE set, not 40 — that is the
screen's own pre-existing listing rule, shared with the terminals, and
round 2 did not widen into it.

### 10.6 Click counts, recounted to LAUNCH

Cold SDL 2v2 soccer: **13 clicks over 5 screens, unchanged** — the docket
row replaces the strip door one for one. Warm: the clock is 4; **respawns
are 3** (DIFFICULTY → row → BACK), down from 4; "another arena of the same
game" is **4** (SETUP row → ARENA tab → row → Esc), up from 3; "another
game" is 4; **a random arena to launch is 4** (SETUP → RANDOM → MATCH tab →
GO, or SETUP → RANDOM → Esc → GO), up from 2.

### 10.7 Residuals

- **R2-R1** — a versus campaign with NO `base_camp` hook has no wizard door
  on ANY client (the bookless fallback stays pinned at the session level; a
  C++ default-zone SETUP row was rejected as a second composition of the
  row — revisit if a second versus pack ships).
- **R2-R2** — no-level `Acted` rows on wizard pages still skip the #212
  sync/autosave tail; their MESSAGE now reaches the terminals.
- **R2-R3** — warm "other arena, same game" is 4 clicks (was 3) and a random
  arena is 4 clicks to launch (was 2), the price of one button.
- **R2-R4** — the CTF page's RANDOM ARENA sits on window 2/2.
- **R2-R5** — a company saved by the previous build re-deals each arena once
  on its first visit after the upgrade (a deliberately narrowed arena in
  such a company is re-filled once; turn SIDES again).
- **R2-R6** — the SCENARIO door is still labelled PROGRESS / `Progress` on
  the modes campaign; the screen behind it is an arena list with GO and no
  progress word.
- One test-depth residual carried from the wave: the curses client's own
  binding of `io.seat_short_name` on the TEAMS step lost its last witness
  when the camp took the prompt back, so that one assertion is dropped. The
  RULE is still pinned at session level and end to end on the text client;
  restoring the curses pin needs a frame-history accessor on
  `HeadlessTerminal`.

### 10.8 What did NOT change

Lead ruling 3's grid split stands exactly as §2.0 describes it: the nine
vertical/rhythm names in `match_setup_session.h`, every x/w/tab/cell/footer
name in `picker_sdl_defs.h`. Five steps, the tab strip, the `<` reverse
cell, the entry-highlight table, GO's gating and the MATCH recap are all
unchanged.
**Update (2026-09-21, PR #307):** the `<` reverse cell is the one item on
that list round 3 removed (§11). Everything else in the sentence still
holds.

---

## 11. Round 3 (2026-09-21, PR #307) — the cyclers turn forward only

Maintainer ruling, after round 2: **the SETUP wizard's cycler rows cycle
FORWARD ONLY, like every other cycler in the picker** (the DIFFICULTY
rows, the LINEUP wheels). D19 is reversed.

The wheels are short — SIDES 3 stops, FILL 4, SCORE 5, TIME LIMIT 5 — so a
mis-click costs at most four clicks, and the alternative was a reverse the
rest of the picker does not teach: a `<` cell on one screen's rows, a
right-click that means something here and nothing anywhere else, and a
prompt grammar (`N-`) no other prompt in the game accepts.

What went, in one list:

- **The `<` cell.** Ordinals 9..17 (`setup_rev_0..8`), the
  `OG_SETUP_REV` table macro, `match_setup_rev_state`, `kSetupRevX` /
  `kSetupRevW`, the per-frame visibility, the cell column's own vertical
  nav chain and the row's `→` into it. The button table is **19** rows,
  not 28; every ordinal after the rows shifts down by nine. The ARENA
  pager pair, `kSetupCellX` / `kSetupCellW` and the 264-wide row face are
  untouched, so the screen's geometry is exactly what round 2 shipped
  minus one column of cells.
- **The right-click.** `vbutton::do_call_right`'s `MenuSpecRow` arm, the
  `menu_spec_row_reverse` stash with its accessors, the runner's
  nested-screen clear, its post-dispatch clear and its TESTING
  "survived a frame" invariant, and the wizard spec's
  `right_click_enabled`. What a right-click does now is whatever the
  engine already did for every other `MenuSpecRow` screen: without
  `right_click_enabled` the legacy `if (leftmouse(buttons))` rule applies
  and any nonzero click activates `leftclick`, so the right button steps
  the wheel FORWARD, exactly like the left one. (On a screen that DOES
  set `right_click_enabled`, `do_call_right` now answers 4 for a spec row
  and dispatches nothing.) Either way there is no reverse.
- **The terminal `N-`.** The prompt reads `Setup # [1-N] (0 = back): `
  again, the driver parses a plain number, and
  `TerminalMatchSetupItem::reversible` is deleted. A trailing `-` is an
  unparsable answer and takes the driver's existing `Invalid setup row.`
  notice.
- **The `dir` argument**, wherever it could only be `+1` afterwards:
  `MatchSetupSession::choose` / `::turn`, and in `picker_common`
  `wheel_next`, `cycle_ctf_capture_limit`, `cycle_time_limit`,
  `turn_match_sides`, `turn_match_fill`. `cycle_lineup_fill(current, dir)`
  keeps its `dir` — it predates this PR and belongs to LINEUP.

Red-then-green, because two behaviours changed rather than disappeared:
`MenuEngine.spec_row_right_click_dispatches_nothing` (with a left click on
the same row as the control arm),
`MatchSetupUi.a_rules_cycler_laps_forward_and_ignores_a_right_click` (the
right-click steps the same wheel FORWARD, 1 -> 3, and the lap comes home),
`PlatformHeadless.text_picker_setup_wizard_walks_every_step` (the `2-` leg
is now the invalid-row notice) and
`CursesPickerClient.setup_flow_knob_turn_autosaves_and_laps_forward` (leg 3
walks the five-stop wheel home in four presses). All four failed on the
round-2 tip before the removal landed.

---

## 12. Round 4 (2026-09-22, PR #307) — full-width rows, a pager ROW, and green for GO alone

Two maintainer rulings, after round 3.

### 12.1 Green is GO's alone inside the wizard

The ARENA step's level rows are **plain** — the ordinary grey face — not
the GO green. A list of ten arenas painted green says "this launches"
about every one of them and therefore about none; `[CURRENT]` is already
the mark that says which arena is armed, and the only rows on this screen
that launch anything are the MATCH step's GO row and the Base Camp strip's
GO, which keep the green.

The grammar itself is not rewritten. `compose_scripted_row_face` is shared
by three chassis (the zone submenu's book pages, the Base Camp docket, the
wizard), and every classic campaign's level rows are green today on
purpose. So the change is a SEAM: the composer takes
`level_rows_green = true`, and the wizard is the one caller that passes
`false`. Both halves are pinned, in the same file and beside each other:
`CampaignZoneUi.wizard_arena_rows_are_plain_while_the_camps_stay_green`
and the docket/submenu half of
`CampaignZoneUi.joiner_level_rows_pay_for_the_host_marker_out_of_the_label`.

Nowhere else shows a versus campaign a green level row: the versus docket
composes ONE page row (`SETUP - <GAME>: <ARENA>  >`, no green), and the
SCENARIO and PROGRESS screens have their own GO buttons, which the ruling
leaves alone.

### 12.2 Rows take the whole panel width, and the pager is a ROW

Every row on every step of the wizard, and every row of the Base Camp
docket, now runs `kSetupLeftX` (12) to `kSetupRightEdge` (310): 298 px of
bevelled face, `(298 - 8) / 6 = 48` glyphs. The 30 px column of cells
that used to close the right rail is gone, and with it every constant
that described it — `kSetupCellX`, `kSetupCellW`, `kSetupPagerPrevX`,
`kSetupPagerNextX`, `kSetupPagerW`, `kBaseCampZonePagerPrevX`,
`kBaseCampZonePagerNextX`, `kBaseCampZonePagerWidth` — plus the two
`setup_page_*` ordinals and both "p/N" gutter strips. The Base Camp's four
docket pager ordinals (65..68) are RETIRED rather than deleted: they park
every frame as `zone_pager_spare_0..3`, the way the seat rail's own
retired ordinals do, because renumbering every ordinal above them would
buy nothing.

What replaces them is one rule with one implementation, in
`campaign_picker_session.h` where both SDL-free sessions can reach it:

```cpp
PageModel make_row_window(int count, int fit);   // fit, or fit - 1 when paging
CampaignPickerSession::Row make_more_row(std::string_view label,
                                         const PageModel& page);
void step_row_window(PageModel& page);           // next window, WRAPPING
int  current_row_index(const std::vector<Row>& rows);   // the [CURRENT] row
void open_row_window(PageModel& page, int current_index);  // the window it is on
```

**Update (2026-09-22, PR #307):** the last two are the round-4 follow-up.
A band that pages OPENS on the window holding the `[CURRENT]` row, window
0 when no row is marked. The wizard's ARENA step already did this with its
own inline arithmetic (the F1/F24 fix: a host must not land on a page of
arenas none of which is theirs); the Base Camp docket did not, and the
pager row's slot is what made that visible — Settlement Day's `[CURRENT]`
level row landed one click behind the window. There is no rule twin: the
inline copy in `MatchSetupSession::compose` is gone and both sessions call
`open_row_window`. Browsing still belongs to the player: the pager row
moves the window, a refetch keeps it, and only a NEW `[CURRENT]` row —
a level set under the open camp — re-opens the band
(`CampaignZoneSession::ActionsLayout::current_id` is the memory).

A band of `fit` row slots holding more rows than fit spends its LAST slot
on the pager row, and only then: `make_row_window(8, 9)` is one window of
eight, `make_row_window(11, 8)` is two windows of seven plus the row. The
row itself is an ordinary `Kind::Page` row composed through
`campaign_picker_row_text`, so it wears the same `  >` door marker as
every other "more behind this" row, clips the same way, rides the same
vertical nav chain, and needs no face of its own. Its note is the window
the player is standing on, so it reads `MORE ARENAS - 1/2  >` on the
wizard's ARENA step and `MORE - 2/3  >` on the docket. A click steps to
the next window and wraps home from the last — a row has one direction,
exactly like the cyclers of §11.

**The shipped data.** GAME = 7 games + RANDOM = 8 rows against
`setup_rows_fit(0)` = 9: no pager. Each game's ARENA page carries one
flavour line, `setup_rows_fit(1)` = 8: CTF's 10 arenas + RANDOM ARENA = 11
rows window 7 + 4 with the pager row closing each; every other game's band
(4 to 6 arenas + the roll) fits. TEAMS is at most three rows, RULES two,
MATCH two. So the wizard shows exactly one pager row, on one page, and the
Base Camp's versus docket — one row — never pages at all.

**The terminals** project the session's window, pager row included: a
paged step prints its window's rows, then `MORE ARENAS - 1/2  >` as a
numbered item, then `Next:` / `Prev:` / `Back` unchanged. One window model
for every surface. The camp DOCKET's terminal listing is deliberately not
windowed (docs/basecamp-zones-design.md, "Terminals"): there the window is
a constraint of the panel's pixels, and a prompt has no band.

### 12.3 What it cost the shipped camps

`kBaseCampZoneActionRowWidth` 264 → 298 gives every camp docket six more
glyphs; nothing that fitted stops fitting, and the versus docket's worst
`SETUP - <GAME>: <ARENA>  >` row is no longer near its ceiling.

The pager row costs a paging band one authored row per window, and one
shipped camp pays it: **The Long Season**. Its spring docket (4 rows over
a 3-unit band) now shows the job and the advance, with the shop door and
the open contract behind the pager instead of just the contract; its
Settlement Day docket (3 rows over a 2-unit band) shows `DRAW YOUR PAY`
and the pager row, with the `[CURRENT]` Settlement Day level row one click
behind. That row is a signpost rather than a decision — header line B
already names the scenario and the command strip's GO already launches it
— which is why it is the row the camp can afford to lose, and the budget
sweep in `tests/unit/test_longseason_ledger.cpp` now says so in those
terms. The camp cannot simply buy the unit back: its band is
`readout(0) + stanza(1..2) + docket(2..3) + roster header(1) + roster(3)`
= 8, and the roster's three rows are that campaign's own adjudicated
floor. Westlands' fork night (4 rows over 3 units) pays the same way, and
Imaginations and the versus camp do not page at all.

**Update (2026-09-22, PR #307):** the ruling above — "a `[CURRENT]` level
row may page, because it is a signpost" — is REVERSED. A camp exists to
point at the job in front of you, so the window that opens is the one
holding that row (§12.2, `open_row_window`), everywhere and on every
chassis. What that changes in the shipped data:

- **The spring docket** is unchanged in what it shows: the `[CURRENT]`
  job is already its first row, so the camp still opens on the job and
  the advance with the shop door and the contract behind the pager.
- **Settlement Day** opens on window 2 of 3 instead: the `Settlement Day`
  level row alone, over `MORE - 2/3  >`, with `DRAW YOUR PAY` the one
  click behind it and the shop door the click before. Its one-slot window
  can hold the signpost or the payout, and the rule picks the signpost.
  The camp would show both if that state's docket had three units, which
  it cannot buy back without dropping the roster below its three-row
  floor — a Long Season content decision, not this rule's to take.
- **Westlands' fork night**, Imaginations and the versus camp are
  unaffected: their `[CURRENT]` row is always the docket's first.
