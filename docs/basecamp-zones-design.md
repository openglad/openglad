# Base Camp zones: the Lua-composable camp (issue #206 revamp)

The first campaign-scripting cut bolted a MISSIONS book onto the SCENARIO
submenu — a fourth level-selection door that restructured nothing. This
design makes the campaign script own the between-levels *experience*: the
Base Camp splits into a C++-owned **Game Management zone** and a
Lua-composable **Gameplay zone**, and the shipped books move from a side
page into the screen the player lives on. Red-teamed against the full
recon of the 49-row screen, the lobby coupling, and the terminal twins;
every mechanism below is either shipped machinery or a named extension.

## History strategy: excise via squash

The v1 missions architecture is UNMERGED (PR #227 is open) and this repo
squash-merges every PR (verified: recent merge commits are all
single-parent). Master therefore receives exactly one commit for this
branch, and the excision goal — master never contains the missions door,
its terminal ordinal churn, or a born-dead screen — is met by two
requirements this design enforces instead of a history rewrite: the
FINAL DIFF carries no missions residue (the zone waves retired every
surface, constant, test and capture path), and the PR presents the zone
architecture only. The branch history is retained as an honest audit
trail of the build. `ButtonAction` 106 was removed outright when it
became unreachable; the freed value is documented as free.

## The split

- **Game Management zone (C++ only, never scriptable)**: the seat rail
  (y=164..173), the command strip (y=178..195) with BACK / DIFFICULTY /
  SCENARIO / NETWORK / GO-or-READY at their EXACT current rects, and the
  header's management lines (COMPANY line A, SCEN/status line B, the
  scenario_line door). The 50px gap where HIRE sat was called deliberate
  chrome here; it turned out to be the right place for the DIFFICULTY door
  when difficulty came off the main menu, and the whole strip re-gridded on
  a 6px gutter to ink the full word (docs/camp-controls-design.md). GO and
  NETWORK are wasm-E2E coordinate contracts — the specs moved with the code
  in the same commit.
- **Gameplay zone (Lua-composable, the y=28..160 band plus the header
  GOLD cell)**: composed from widgets on a fixed 14px row grid anchored
  at y=45. No `base_camp` hook ⇒ the C++ default: full-capability
  roster, header GOLD, and HIRE as the roster band's header-right button
  (id `hire_troops` preserved; the one deliberate visual change).
  HIRE's face overlaps line B's strip, so it is also line B's right wall:
  the band is 34 characters while a composition shows HIRE and 41 while
  one hides it (`kBaseCampLineBChars*` in `picker_common.h`, derived from
  the wall x and static_asserted against the button geometry). The
  networked session status composes to the live budget — a narrow band
  takes a whole shorter spelling ("3 PLAYERS/2 PCS", then "3P/2M", then the
  room code goes) rather than a mid-word cut, and a count of one takes the
  singular ("1 PLAYER / 1 MACHINE", "1 PLAYER/1 PC") — always the shorter
  spelling, so it never costs a rung its band. Players lead the census
  because the seat rail shows only this machine's seats.

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
  message-line toast, not a modal, to avoid lobby stranding) AND draw a
  padlock in the locked row's deploy cell on the spent face — a lock the
  player can only learn from a 2.5s toast is a box that silently refuses
  clicks forever. `assign = { key, labels = {A, B}, frozen = nil|"reason"
  }` turns the solo team-chip cell into the OATH COLUMN: a
  six-character word cell at x=44 under a heading named by `key`
  (uppercased), showing `-` while unsworn and the label in words once
  sworn, with the deploy heading shortened to "DEP" beside it. NOT a
  coloured chip carrying an initial — that widget means "team number"
  everywhere else on the screen, and the oath has to outlive its toast.
  Cycle unset→A→B→A (never back to unset), each cycle toasts the full
  word ("Sworn to WAR.") plus the un-deploy when there was one ("Sworn
  to WAR. Stood down from the muster.") — one toast is the only thing
  that speaks on either surface, so it carries the whole consequence of
  the click; `frozen` keeps the column readable but refuses
  cycling with the reason. Oath cells render only when `own &&
  (assign_mode || !networked)`; cycling a DEPLOYED hero first un-deploys
  through the full roster tail (lobby push, ready clears — correct);
  undeployed cycles ride the autosave tail only. Roster DATA is always
  the C++ lobby-merged truth — Lua shapes affordances, never rows.
- `text` — up to 6 lines per widget, inked straight onto the panel face
  in the roster's own identity ink. NO readability strips inside the
  panel (the one-screen rule): the black-strip idiom exists to lift text
  off the title-screen backdrop, and stacking charcoal bars and grey
  plates on an opaque grey panel reads as three materials pasted
  together rather than one screen. Lines ink on an 8px pitch inside the
  widget's 14px units, and a widget weighed SMALLER than its lines need
  CLIPS — an under-weight is a legal composition (only over-weight is
  refused), and the rows below belong to the next widget.
- `actions` — rows in the EXISTING page-entry vocabulary
  (level/page/action, id/label/note/cost) plus `done`, which retires a
  costed action the book has already honored (no price quoted, spent
  face, refuses instead of charging twice). Each actions widget is
  windowed by PageModel with its own two pager ordinals; overflow pages
  in place. Level rows: host-gated, earned-roads-gated (see the voice
  rule below) load-with-rollback set tail. Page
  rows: the zone submenu. Action rows: debit-then-dispatch + autosave
  tail (+ settings sync when the match dirty flag armed). Actions do NOT
  clear ready. **Each kind is legible before the click**: page rows wear
  the repo's door marker (" >"), level rows wear the GO green because
  they start a battle, spent/closed/unaffordable rows wear the dimmed
  face, and a level row whose scenario the campaign does not carry reads
  `[CLOSED]`. That grammar SURVIVES the clip: a row too wide for its face
  loses its own words and keeps its tail (marker, stamp, price), because
  an ellipsis that eats the " >" turns a door into a dead label and one
  that eats `[CURRENT]` hides the fact the row exists to state. A widget
  that does not weigh itself takes three units however many rows it
  carries, so a docket meant to be read at a glance states its `weight` —
  and clamps the ask to the band the roster floor leaves, since
  over-weight falls back to the DEFAULT zone (the camp disappears) while
  a too-small weight only pages. When a docket does page, the pager's
  gutter carries the "p/N" count under its arrows: two bare arrows say a
  row can move, never that rows are hidden.
- `readout` — ONE zone row of up to 3 label/value cells (fetch-composed
  strings; staleness bound = the fetch cadence), labels in the
  column-header ink and values in the row-data ink. When the roster does
  not lead the composition it moves its column headings into the grid and
  abandons the classic header slot at y=33; the readout HOISTS into that
  band as the panel's heading, which fills the band and hands its row
  unit back to the roster. The header GOLD cell stays C++-owned always.

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
meaning; malformed or over-budget compositions fall to the default zone —
or, when the campaign keeps a book, to the book-door composition below,
which is the default plus the one row that reaches the book.
A `weight` past the 8-unit band is refused by the PARSER with a named
error instead — a widget bigger than the whole zone cannot lay out beside
any sibling, and the author deserves to be told which widget it was.

## Zone submenus

The zone submenu (generalized from the v1 missions chassis, which never
ships) is a ROOM INSIDE the camp, not a screen of its own: the same
panel at (8,28)-(311,160), the same header strips (COMPANY + GOLD — you
must be able to see your purse while a shop quotes prices), and the same
message-line toasts. Confirmations and refusals never fork on menu
DEPTH: a purchase that toasts at the root does not become a blocking
modal one page in. C++ owns the strip — the page title on the status
line and BACK at its fixed rect, Escape-hotkeyed, checked before any Lua
row — Lua page rows below, same `CampaignPage` vocabulary, depth ≤ 4,
PageModel windowing (a wordy page shows fewer rows per window rather
than overrunning the panel), remote-start folding, registered in
`MenuScreenId` so the G5 sweep covers it (the registry gap for Company
List et al. remains; not claimed here).

## Networked rules

- Zone content derives from the LOCAL book (four machines, four books —
  documented per campaign). The synced truths are the management zone's:
  scenario id, settings, seats, roster merge.
- Split-party scope: assign chips are solo/local-only (hidden when
  networked); locks derive from (own tag, front_of(synced scen_num))
  where front_of is a pure function of the static road graph — never a
  stored "current front"; absent/conflicted book ⇒ locks fail OPEN
  (no lock, no refusal — the own machine's book is the only authority).
  Where a lock DOES compose it is mechanical: the fetch that declares it
  stands the refused own heroes down, so the padlock, the muster and the
  sortie can never disagree. Other machines' slots are never touched.
  A networked company that never swore plays the unsworn-bypass shape by
  derivation; one that swore in solo keeps its own tags in the lobby (the
  roster snapshot is the local save's), so a guest's fire may narrate a
  different front state than the host's — the documented cost of a
  per-machine book.
- Zone refusals prefer the message line/toast over modal popups (modals
  do not poll the lobby; a stranded joiner misses GO). One click, one
  answer: every dispatch drops the standing toast before it speaks, so
  the previous action's message can never be read as this one's, and a
  successful level set says so rather than leaving the last toast up.
- The campaign's voice slot carries the campaign's words. Engine
  diagnostics never surface there: a level that will not load is "That
  road is not open yet.", not "Invalid level file." The same line now
  also answers the earned-roads gate (docs/camp-controls-design.md):
  a shipped road outside `og::data::accessible_levels` — first_level ∪
  cursor ∪ cleared ∪ exits-of-cleared, with `matchup: versus` and
  `mode: tower` campaigns exempt — is exactly as closed as a missing
  one, on every surface, before any load or autosave tail.
- The answers the ENGINE owns to a level-row click — the refusal on the
  row the cursor already sits on, and the confirmation — come from one
  place (`kCampaignLevelUnchangedMessage`,
  `campaign_level_set_message`) so the panel, the zone submenu and the
  terminals cannot answer one click with two different stories.

## Terminals

One TeamBuild item ("Camp") opens the zone through the mission-book
prompt driver grown to render the readout as one line, text blocks
clipped to their bands, the numbered docket, lock reasons, and an assign
interaction (the oath row opens a numbered own roster; a row number
cycles the tag with the same full-word toasts). Named honestly: for v1
this is a door, not the terminal camp face — the bounded-churn
compromise; the path forward is the zone becoming the terminal TeamBuild
face. The item re-pins the three positional drivers (headless drives,
interactive script, curses route tests) once.

Eight v1 rules the terminals settle, since a prompt cannot hide a control
behind a hover or a spent face:

- **Camp is inserted before Back, not appended.** The curses digit jump
  addresses only the first nine selectable rows, and the camp is the door
  into everything a campaign composes. It takes ordinal 7; back,
  networking, scenario and the CTF trio each move down one, and SCENARIO
  falls out of digit range into the arrow-only band the CTF trio already
  sits in.
- **No composition, no door — but never a stranded book.** The default
  zone is a full-capability roster, which both terminals already carry on
  their own TeamBuild rows, so a camp door onto a second copy of it would
  say nothing: opening it on a campaign with neither a `base_camp` hook
  nor a book prints the guard line. Every client enters a book through a
  camp page row, so a campaign that registered a book before the camps
  existed gets the transitional BOOK-DOOR composition instead of the bare
  default: the same roster plus one page row onto the book's root, named
  by the book's own root title. It retires itself the moment that
  campaign composes a `base_camp`. `run_terminal_campaign_page` is
  therefore the only book entry point on a terminal (the v1 root driver
  and its no-book guard line are gone with the MISSIONS row).
- **Locks read inline; the padlock is a letter.** The roster row's deploy
  cell shows `L` for a locked hero and states the reason on the row. A
  reason delivered only as a refusal toast is invisible on a screen whose
  roster rows are not clickable.
- **The terminals' own roster commands obey the camp.** Deploy (the
  roster row's `d`/`deploy N` and the Deploy item), Train and Hire ask
  `terminal_roster_refusal` before acting, so a deploy lock or a cleared
  `can_deploy`/`can_train`/`can_hire` refuses in the campaign's words
  instead of only removing a control the terminal never drew. The SDL
  panel can hide a retired affordance; a prompt cannot, and a camp that
  shows a padlock while the same client deploys the hero anyway is two
  campaigns.
- **A prompt's context block scrolls.** The camp emits its whole
  composition — a 24-hero roster outruns a stock 24x80 terminal on its
  own — and every emitted line carries a live ordinal, so the curses
  prompt wraps and pages its context (Up/Down, PageUp/PageDown, a
  `-- n-m of N --` marker) rather than cutting the tail off where the
  screen ends.
- **One camp noun, one header strip, at every depth.** The prompt asks
  for a `Camp #` on the camp screen and on every page a page row opens —
  a room inside the camp does not rename the building, and "mission" is
  retired from every player-visible surface with the door it belonged
  to. The same C++-owned strip (`COMPANY  DEP n/n  GOLD g`) leads the
  camp AND every book page: this is the terminal spelling of "you must
  be able to see your purse while a shop quotes prices", and it may
  never depend on a campaign composing gold into its own readout.
- **The dimmed face is a word.** The panel dims a spent, closed or
  unaffordable row; a prompt cannot, so it spells the state in the row's
  own tail vocabulary — `[CLOSED]`, `[DONE]`, `[NEED GOLD]`. The SDL
  faces keep the ink and stay unmarked; the two surfaces state the same
  fact in their own material.
- **The oath column has a heading and a legend.** The channel key is
  padded out to sit directly over the oath cell, so the `-`/word cells
  read as a column instead of trailing the summary facts, and the
  reason of a lock is one more space-separated column (never a dash —
  it collided with the unsworn `-`). Because the oath is a CYCLE rather
  than a menu, the swear prompt names both words, the deploy-cell
  glyphs, and the un-deploy before the player commits; the toast then
  names the un-deploy again when it happens.

## The four camps (adjudicated)

- **Modes — the Gamesmaster's table.** The camp's whole grid is 8 units
  and the roster floor takes three, so text lines and docket rows share
  the other five; the host spends four of them on rows and speaks no
  line — a docket that states its own weight so all four render, rather
  than three and an unlabelled pager. Readout: BOOK 12/40, and nothing
  else (the C++ header cell already inks the purse a few pixels above,
  and it spells an infinite purse "INF" where a scripted cell can only
  push the raw number). Rows: GAME (page → seven-game index with
  per-game stamp tallies — stamp vocabulary everywhere), FIELD (one hop
  into the current game's arena page; arena rows carry manifest facts +
  CLEARED/CURRENT), RANDOM SCENARIO (an ACTION, not a level row — the
  arena is only known after the roll, so the row wears the ceremony's
  own name and the result rides the Acted outcome's `level` through the
  SAME gated set tail as a level row's click; the engine's "Level set to
  <arena>." is the confirmation, so what confirms is always something
  playable), MATCH SETUP (page; the row's own note IS the rules
  digest, and the page's three rows are the knobs themselves — TEAMS,
  TARGET SCORE and TROOPS, each labelled with what it holds and noted
  with the cycle a click steps it along). The roll happens at dispatch,
  never at fetch — a random pick computed at fetch time would be a deck
  by another name, and `base_camp` stays pure — and it never lands on
  the field the table is already set to: a roll that re-offers the
  current pairing steps one row on in the ordered manifest, wrapping,
  because a green button that changes nothing is not a roll. SIGN THE
  BOOK lives on the index page whose cover it takes, asked for by the
  GAME row's note at 40/40; the camp keeps no signature row and no cover
  line. Joiners: own-book readout, the same read-only rules digest,
  "The host calls the game." / "The host calls the rules.", browsable
  pages with (HOST) level rows; the roll, the signature AND the three
  knob rows are cut at fetch.
  Call lines live on field pages only, and a fully stamped page says
  "Every field here is stamped." — never "shut", since #207 keeps every
  cleared field replayable. Every camp row is budgeted against the
  42-char panel face carrying the campaign's longest arena name, which
  is why CTF's clock reads "20m".
- **Westlands — the company fire.** Text: camp stanza + Bearer line.
  Roster: full capabilities; at the Falls, `assign` (WAR/BURDEN) with
  the taught-glyph toasts; after the swearing freezes (any road level
  completed with both musters ≥ 1), `frozen = "The Falls parted the
  company."` and locks by front ("ON THE WAR ROAD" / "WITH THE BEARER" /
  "WAITS AT THE FALLS"). Actions: only the real forward choices with
  plain-category notes to the delve standard ("a plea, a pay chest";
  "gold, and a price"); during the split, one row per live front with
  muster counts that also count waiters ("party 4, 2 wait"); hot
  one-shot offers (delve pair, bread) surface at the fire ONLY;
  QUARTERMASTER keeps the standing trades; THE LEDGER page remains the
  memory. Unsworn bypass preserves v1 semantics with an honest nag; a
  wiped front collapses ("The east fire is ashes. All ride."); reunion
  at the Mountain when both roads are done. Back rows never render.
- **Long Season — the open ledger.** Readout: JOBS / DEBT / COINS — the
  purse is NOT a fourth cell, because the header GOLD cell is C++-owned
  and already on the screen. Text: the season line, one row unit, so the
  docket can hold three rows. Actions, in the order the band shows them:
  the current job with its stake ON the row ("he must not fall"), the
  book's money row, KETTLE'S STORES (destination + contents stated
  before purchase; the kettle gag demoted to the shop's free last row),
  then open optional contracts ("optional, pays extra") — contracts are
  the only rows allowed to page. The money row is TAKE AN ADVANCE ("700
  now, 900 at Toll") or SETTLE THE BOOK, and BOTH halves close once the
  Toll has been fought: settling is then a wash, and an advance would
  write a debt against a dead deadline that the latched settlement could
  never square. The warm-coin ritual is a conditional composition that
  takes the whole camp while a coin waits — the trade in numbers (KEEP
  "1 ally per 4 kept", note swaps once the 3-ally cap is banked; PASS
  "150g now, none later") under the line that names where a kept coin is
  redeemed. Settlement Day is the one face that spends the docket's
  third unit on prose: the day's line, the year's arithmetic, and DRAW
  YOUR PAY with the computed net ("pays 1400g, once").
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

Per camp, two pins the arithmetic above earns: every docket row is in its
band's FIRST window in every reachable state (a camp that pages its own
docket has rows a player cannot see), and every row composes UNCUT on the
42-char panel face over every arena the campaign ships — swept from the
campaign's own data, so a regenerated title that no longer fits fails
here rather than ellipsing a door marker on the panel. The paging path
keeps its own SDL flow: a synthetic docket weighed smaller than its rows
pages in place, counts itself, and comes back.
