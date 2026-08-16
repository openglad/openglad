# Kettle's Book (`longseason.ledger`)

The Long Season's campaign script: the ledger every briefing quotes, made
playable — and open on the table. The pack composes the campaign's Base
Camp (`base_camp` in `scripts/campaign_book.lua`), keeps one room behind
it (`picker_menu`, the stores page), and carries the decision-reactive
level hooks (`scripts/level_hooks.lua`) over the pure data both read
(`lib/ledger_data.lua`). Hand-authored: the campaign generator preserves
`packs/` and never rewrites it.

## The camp

With The Long Season mounted, the Base Camp's gameplay zone IS the open
page of the company ledger. It composes, in order:

- **The readout** — JOBS (done, at 100g each), DEBT (what the book is
  owed), COINS (warm coins kept). It heads the panel; the terminals print
  it as one line. The purse is deliberately absent: the panel's GOLD cell
  and the terminals' COMPANY strip are engine-owned and always on the same
  screen, and two words for one purse only ask whether they are two pots.
- **The stanza** — where the season is. One line, normally: the docket
  buys the second row unit with it. It stretches only when the book has
  something the rows cannot say — "Unpaid debt docks the settlement."
  once the collectors have ridden and no SETTLE row is left to say it,
  "New season. Same book." on the 19-to-1 turn, and the settlement's own
  arithmetic.
- **The docket** — the job in front of you with its stake ON the row
  ("kill work", "he must not fall" for the Assessor, "the Reeve must
  live" for Wolf Winter, "the collectors ride" on Toll week), then the
  money row (TAKE AN ADVANCE "700 now, 900 at Toll" or SETTLE THE BOOK
  "no Toll collectors", 900g), then the door into KETTLE'S STORES
  ("crates for this job"), and last every optional contract still open
  ("optional, pays extra").
- **The roster** — every default capability, three rows in every state.
  The Long Season adds no locks and no oath column; its texture is money
  and consequence, never a hero the camp will not muster.

A widget's row-unit band IS how many rows it shows, so the order above is
the order of what stays on the screen. The job, the money row and the shop
door are inside the band in every ordinary state; optional contracts are
the only rows that ever pay the pager (plus the shop door on Settlement
Day, which spends that unit on the year's arithmetic instead).

**The warm-coin ritual** is the one composition that interrupts, and it
interrupts properly: while a coin waits the docket steps aside (GO still
launches the current job — the camp asks for a decision, it does not hold
the company hostage). Three lines — the coin's own flavor, "It spends
high. Nobody asks why.",
and where a kept coin is redeemed ("Kept coins stand up at the mint.") —
over the two rows that state the whole trade in numbers: KEEP THIS COIN
"1 ally per 4 kept" (it becomes "a coin for the door" once twelve kept
coins have banked the three-ally cap) and PASS IT ON "150g now, none
later". One coin per fetch, oldest first, and one click hands the docket
back; when the book is square the ritual disappears and the COINS cell
carries the tally.

**The money row is open exactly until the collectors ride.** Before The
Long Toll an advance can be taken and squared; once the Toll has been
fought, settling 900g to dodge a 900g dock is a wash *and* an advance
would quote a deadline that has already passed to write a debt with no
exit, so both halves shut together and an outstanding debt rides to the
settlement to be docked.

**Settlement Day** puts the arithmetic under the day's own line —
"Settlement Day. Square the book.", "Eighteen jobs done, 100g the job.",
then the debt dock or the door coin — and leads the docket with DRAW YOUR
PAY, whose note is the computed net ("pays 900g, once"). Drawing the pay
latches the season closed and opens the 19-to-1 loop ("New season. Same
book." / "the year turns").

**KETTLE'S STORES** is the one room: MEAL FOR THE ROAD 150g, THE GOOD
CRATE 400g, THE STRONG CRATE 600g, and — while the Ashfall Fair is still
ahead — STAND THE CREW A ROUND 200g. The page names the delivery rule and
the exact destination before it quotes a price ("Crates land at the
current job's camp." / "This job: The Long Toll."), states what the wagon
already carries, and says where it is going when that is not this job
("Addressed to Two Banners."). ASK ABOUT THE KETTLE is its free last row.

## Decision state

Campaign state lives in the company save, per campaign. The four
**sim-visible** vars (registered, copied into the world at level load,
read by `level_hooks.lua` via `og.campaign_var`):

| var | written by | consequence |
|---|---|---|
| `coin_kept` | KEEP THIS COIN (bit per level, 2..18) | 4+ kept coins stand up The Carried at The Warm Mint (one ally per four, cap 3, help only); 1+ nails a silver coin over the winter-quarters door on Settlement Day |
| `advance_debt` | TAKE AN ADVANCE (900) / SETTLE THE BOOK, DRAW YOUR PAY (0) | debt outstanding when The Long Toll loads sends two Collectors up the west toll road |
| `provisions` | Stores purchases (`kind + 8 * job`, the job the camp is pointed at) | the addressed crate: 4 or 8 drumsticks (the strong crate adds two silver bars) at the site's start pocket |
| `fair_round` | STAND THE CREW A ROUND | Stallkeeper and Potboy hold the strongroom door at the Ashfall Fair, with two meals inside |

Menu-only keys (never sim-visible): `coin_spent`, `kettle_asked`,
`settled`.

With every var at 0 each level is byte-identical to the shipped campaign —
every consequence function's first statement is its `var == 0` early
return, and `tests/unit/test_longseason_ledger.cpp` proves the identity
(census + RNG state + entity fingerprint) on all four consequence levels.

## Crates go where they are addressed

The sim cannot write campaign state, so a crate (and the fair round, and
the rest) is not consumed by being delivered: replaying the addressed
level re-spawns it. That is the fiction — crates go where they are
addressed, not where you are standing — and the contents are food, not
gold. Re-address by buying again (one crate rides the wagon at a time).

## Networked play

Campaign state is per-machine, like the campaign cursor: four machines,
four books. The host's book drives the shared session (level choices reach
peers as the scenario id); each machine composes its camp from its OWN
ledger, and sim consequences follow the HOST's book. A guest's camp reads
and spends against the local wallet, but only the host's coins, debts and
crates reach the fight — and with the ledger now the whole camp face, that
is more visible than it was behind a door.

## Regeneration lockstep

`lib/ledger_data.lua` mirrors the shipped package: the exit graph, the
levels whose package flags a protected NPC (the docket's lose-condition
notes), and fixed spawn tiles vetted against `campaigns/longseason/scen`.
A mapgen regeneration that moves layouts turns into a red
`test_longseason_ledger.cpp` run (the graph/tile pins), and
`tools/longseason_mapgen` stages this pack and self-checks the book
(registration of both hooks, vars, the camp's widget and content budgets
in every state, the stores page's budgets, one var-injected tick of The
Long Toll) on every generation. If a regeneration moves the strongroom, the
toll road, the Kettle pocket or the winter-quarters door, update the
tiles here and re-run both.
