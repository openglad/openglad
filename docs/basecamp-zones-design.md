# Base Camp zones: the Lua-composable camp (issue #206 revamp)

The first campaign-scripting cut bolted a MISSIONS book onto the SCENARIO
submenu — a fourth level-selection door that restructured nothing. This
design makes the campaign script own the between-levels *experience*: the
Base Camp splits into a C++-owned **Game Management zone** and a
Lua-composable **Gameplay zone**, and the shipped books move from a side
page into the screen the player lives on. Red-teamed against the full
recon of the 49-row screen, the lobby coupling, and the terminal twins;
every mechanism below is either shipped machinery or a named extension.

## History strategy: excise, don't layer

The v1 missions architecture is UNMERGED (PR #227 is open). Master must
never contain it: the branch history is rewritten so the missions door,
its terminal ordinal churn, its screen, and its tests never exist — the
hooks core, GTL v15, the session, the packs' hook/action layers and their
unit tests carry forward; the SDL/terminal missions surfaces are excised
and replaced by the zone in the same commits that would have introduced
them. `ButtonAction` 106 is born as the zone submenu door. The PR is
force-pushed with the rewritten series (same branch, same number).

## The split

- **Game Management zone (C++ only, never scriptable)**: the seat rail
  (y=164..173), the command strip (y=178..195) with BACK / SCENARIO /
  NETWORK / GO-or-READY at their EXACT current rects (GO and NETWORK are
  wasm-E2E coordinate contracts; the 50px hole where HIRE sat is
  deliberate chrome), and the header's management lines (COMPANY line A,
  SCEN/status line B, the scenario_line door).
- **Gameplay zone (Lua-composable, the y=28..160 band plus the header
  GOLD cell)**: composed from widgets on a fixed 14px row grid anchored
  at y=45. No `base_camp` hook ⇒ the C++ default: full-capability
  roster, header GOLD, and HIRE as the roster band's header-right button
  (id `hire_troops` preserved; the one deliberate visual change).

## The widget contract

`og.register_campaign_hooks` gains `base_camp = function() return {
widgets = { ... } } end` — pure data, same fence and purity rules as
`picker_menu`.

**Fetch cadence (binding, three triggers)**: screen entry; after every
own mutation/action; and whenever remote change lands — the frame_tick
level-reload guard firing (any source) or an applied lobby-settings
change. A joiner's zone must never show a stale pairing or front. Never
per frame.

**Vocabulary v1** (per-kind caps: exactly one `roster`, at most one
`readout`, ≤2 `actions`, ≤2 `text`; ≤6 widgets total):

- `roster` — the existing grid, height-flexed; `rows_per_page` derives
  from its band. Capability flags: `can_deploy`, `can_train`,
  `can_reorder`, `can_team`, `can_hire` (default on; renders the HIRE
  button in the roster band header). `locks = { {slot-independent guy
  reference, reason} }` refuse deploy with the reason (delivered as a
  message-line toast, not a modal, to avoid lobby stranding). `assign =
  { key, labels = {A, B}, frozen = nil|"reason" }` turns the solo
  team-chip cell into an assignment chip: glyph `-`/first letters, cycle
  unset→A→B→A (never back to unset), each cycle toasts the full word
  ("Sworn to the WAR road."); `frozen` keeps chips visible but refuses
  cycling with the reason. Assign chips render only when `own &&
  (assign_mode || !networked)`; cycling a DEPLOYED hero first un-deploys
  through the full roster tail (lobby push, ready clears — correct);
  undeployed cycles ride the autosave tail only. Roster DATA is always
  the C++ lobby-merged truth — Lua shapes affordances, never rows.
- `text` — up to 6 lines per widget on readability strips painted in
  draw_background (draw-order rule).
- `actions` — rows in the EXISTING page-entry vocabulary
  (level/page/action, id/label/note/cost). Each actions widget is
  windowed by PageModel with its own two pager ordinals; overflow pages
  in place. Level rows: host-gated load-with-rollback set tail. Page
  rows: the zone submenu. Action rows: debit-then-dispatch + autosave
  tail (+ settings sync when the match dirty flag armed). Actions do NOT
  clear ready.
- `readout` — ONE zone row of up to 3 label/value cells (fetch-composed
  strings; staleness bound = the fetch cadence). The header GOLD cell
  stays C++-owned always.

**Per-hero identity (GTL v16, shipped in this change)**: the guy record
gains a `campaign_tag` byte in the reserved bytes — the per-hero
assignment channel, zeroed at hire, exposed to Lua as a roster-entry
field and written through an assign provider addressed by the clicked
row's save slot at dispatch time. Campaign_state never keys on per-guy
ids (they regenerate; the tag travels with the record instead).

The tag is machine-private save state and never travels a session:
neither `LobbyCharacterData` nor `GuySnapshot` carries it, so a mission
roster, a local lobby echo and a joiner's mirror world all rebuild their
guys with tag 0. The rule is that whichever record actually stores the
tag wins — `update_guys` carries it through the solo win path on the
copy constructor, `merge_owned_guys_from` re-stamps each survivor from
the disk slot, and the Base Camp lobby round-trip re-stamps from the
pre-reset roster slot. Assignment is menu-authored, so nothing inside a
level ever needs to write it back.

**Bounds arithmetic (closed)**: appended ordinal band 49..71 = 16 action
rows + 4 actions pagers + 3 spare; `MAX_BUTTONS` and
`GameSession::kMaxButtons` rise 50 → 72 together with a static_assert
tying them. Appended rows are statically parked at zero-size rects with
empty labels (gate-lattice safe) and re-banded per frame by the rewire.
The team_build highlight special case is bounded to ordinals 33..48 so
zone rows take the normal focus ring. Existing ordinals 0–48 keep their
meaning; malformed or over-budget compositions fall to the default zone.

## Zone submenus

The zone submenu (generalized from the v1 missions chassis, which never
ships): C++-owned top strip — title + BACK at fixed rects,
Escape-hotkeyed, checked before any Lua row — Lua page rows below, same
`CampaignPage` vocabulary, depth ≤ 4, PageModel windowing, remote-start
folding, registered in `MenuScreenId` so the G5 sweep covers it (the
registry gap for Company List et al. remains; not claimed here).

## Networked rules

- Zone content derives from the LOCAL book (four machines, four books —
  documented per campaign). The synced truths are the management zone's:
  scenario id, settings, seats, roster merge.
- Split-party scope: assign chips are solo/local-only (hidden when
  networked); locks derive from (own tag, front_of(synced scen_num))
  where front_of is a pure function of the static road graph — never a
  stored "current front"; absent/conflicted book ⇒ locks fail OPEN
  (locks are an own-machine courtesy, not an integrity mechanism).
  Networked Westlands co-op plays the unsworn-bypass shape.
- Zone refusals prefer the message line/toast over modal popups (modals
  do not poll the lobby; a stranded joiner misses GO).

## Terminals

One appended TeamBuild item ("Camp") opens the zone through the
mission-book prompt driver grown to render text/readout blocks, the
numbered docket, lock reasons, and an assign interaction (numbered own
roster; digit cycles the chip with the same toasts). Named honestly: for
v1 this is a door, not the terminal camp face — the bounded-churn
compromise; the path forward is the zone becoming the terminal TeamBuild
face. The TeamBuild item append re-pins the three positional drivers
(headless drives, interactive script, curses missions pair) once.

## The four camps (adjudicated)

- **Modes — the Gamesmaster's table.** Readout: BOOK 12/39 + GOLD (hire
  still costs gold). Text: the posted-rules digest. Actions: GAME (page →
  seven-game index with per-game stamp tallies — stamp vocabulary
  everywhere), FIELD (one hop into the current game's arena page; arena
  rows carry manifest facts + CLEARED/CURRENT), TONIGHT'S CARD (level
  row whose note names the exact draw — never blind) + SHUFFLE, MATCH
  SETUP (page; preset rows state their exact writes), SIGN THE BOOK at
  39/39. Joiners: own-book readout, posted rules, "The host calls the
  game.", browsable pages with (HOST) level rows; card/shuffle/sign AND
  the preset action rows are cut at fetch (the digest lines carry the
  value). Call lines live on field pages only.
- **Westlands — the company fire.** Text: camp stanza + Bearer line.
  Roster: full capabilities; at the Falls, `assign` (WAR/BURDEN) with
  the taught-glyph toasts; after the swearing freezes (any road level
  completed with both musters ≥ 1), `frozen = "The Falls parted the
  company."` and locks by front ("ON THE WAR ROAD" / "WITH THE BEARER" /
  "WAITS AT THE FALLS"). Actions: only the real forward choices with
  plain-category notes to the delve standard ("a plea, and a pay chest";
  "gold, and a price"); during the split, one row per live front with
  muster counts that also count waiters ("party 4, 2 wait"); hot
  one-shot offers (delve pair, bread) surface at the fire ONLY;
  QUARTERMASTER keeps the standing trades; THE LEDGER page remains the
  memory. Unsworn bypass preserves v1 semantics with an honest nag; a
  wiped front collapses ("The east fire is ashes. All ride."); reunion
  at the Mountain when both roads are done. Back rows never render.
- **Long Season — the open ledger.** Readout: WAGES / DEBT / COINS.
  Text: season line + the live consequence line ("Owed 900g. Collectors
  at the Toll."). Actions: the docket (current job + open optional
  contracts, stakes on the row: "he must not fall", "optional, pays
  extra"), KETTLE'S STORES page (destination + contents stated before
  purchase; the kettle gag demoted to the shop's free last row), TAKE AN
  ADVANCE ("owe 900 by the Toll") / SETTLE THE BOOK visible only while
  settling still buys something (through the Toll, never after), the
  warm-coin ritual as a conditional composition with the trade in
  numbers (KEEP "1 ally per 4 kept" — note swaps once the 3-ally cap is
  banked; PASS "150g now, none later"), DRAW YOUR PAY on Settlement Day
  with the computed net ("pays 1400g, once") and the dock arithmetic in
  the text.
- **Imaginations — the dream log at camp.** Text line + dream rows.

## Retirement ledger (the excise checklist)

Never lands in the rewritten history: the SCENARIO missions door
(kScenarioMenuMissionsIndex, the 8-row scenario table, the two-axis nav
wire), the SDL MISSIONS MenuScreenSpec/state/wrapper, the terminal
scenario "missions" item and its ordinal shifts, the three SDL missions
UI tests + the book-tour's missions flows (reborn as zone flows), the
missions arms of the media capture script, and the v1 root/road/coin/
debts/card book pages that dissolve into zone compositions (per-campaign
migration tables live with the packs; the hooks/session/provider layers
and the sim-consequence/tile/graph/byte-identity tests carry forward
unchanged).

## Test story

Widget-parse bounds + malformed-fallback units; zone session units
(cadence triggers incl. the poll-driven refetch, assign tails, lock
refusals); regenerated geometry oracle (management rects byte-identical;
zone band; parked rows; highlight bound); nav sweeps over the new
visibility axes; split-party integration (swear → freeze → locks → both
fronts → collapse → reunion → bypass); GTL v16 round-trip + merge
carriage of campaign_tag; terminal Camp drives incl. assign; UXSHOTS
re-captures read back visually; G5 over the zone submenu; the wasm E2E
GO/NETWORK coordinates unmoved.
