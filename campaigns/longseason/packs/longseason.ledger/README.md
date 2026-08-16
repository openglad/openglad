# Kettle's Book (`longseason.ledger`)

The Long Season's campaign script: the ledger every briefing quotes, made
playable. The pack registers the campaign's scripted mission book
(`og.register_campaign_hooks` in `scripts/campaign_book.lua`), the
decision-reactive level hooks (`scripts/level_hooks.lua`), and the pure
data both read (`lib/ledger_data.lua`). Hand-authored: the campaign
generator preserves `packs/` and never rewrites it.

## The book

Open MISSIONS with The Long Season mounted. The root page is the book
itself — wages banked, coins kept, contracts, the season — and under it:

- **THE SEASON'S WORK** — the docket. Level select over the accessible
  set (first level + cursor + completed + exits of completed), in ledger
  voice; backtracks read "decline, go back".
- **THE WARM COIN** — the ritual. After each job one warm coin from that
  pay sits unresolved; the page shows the OLDEST waiting coin, one per
  visit. KEEP THIS COIN earns nothing all year. PASS IT ON pays 150g now.
- **KETTLE'S STORES** — MEAL FOR THE ROAD 150g, THE GOOD CRATE 400g,
  THE STRONG CRATE 600g. A crate is addressed to the level under the
  cursor when you buy it, and appears at that site's start pocket.
  STAND THE CREW A ROUND 200g shows only until the Ashfall Fair is fought.
- **ADVANCES & DEBTS** — TAKE AN ADVANCE grants 700g now and writes 900g
  in the book; SETTLE THE BOOK 900g clears it. One advance at a time.
- **ASK ABOUT THE KETTLE** — ask three times.
- **DRAW YOUR PAY** — on Settlement Day only: 100g per completed level,
  minus any unpaid debt, once. The book closes. The book keeps.

## Decision state

Campaign state lives in the company save, per campaign. The four
**sim-visible** vars (registered, copied into the world at level load,
read by `level_hooks.lua` via `og.campaign_var`):

| var | written by | consequence |
|---|---|---|
| `coin_kept` | KEEP THIS COIN (bit per level, 2..18) | 4+ kept coins stand up The Carried at The Warm Mint (one ally per four, cap 3, help only); 1+ nails a silver coin over the winter-quarters door on Settlement Day |
| `advance_debt` | TAKE AN ADVANCE (900) / SETTLE THE BOOK, DRAW YOUR PAY (0) | debt outstanding when The Long Toll loads sends two Collectors up the west toll road |
| `provisions` | Stores purchases (`kind + 8 * level`) | the addressed crate: 4 or 8 drumsticks (the strong crate adds two silver bars) at the site's start pocket |
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

Campaign state is per-machine, like the campaign cursor. The host's book
drives the shared session (level choices reach peers as the scenario id);
each machine keeps its own decision book, and sim consequences follow the
HOST's book. Guests can read their own book (pages and priced actions work
on every machine against the local wallet), but only the host's coins,
debts and crates reach the fight.

## Regeneration lockstep

`lib/ledger_data.lua` mirrors the shipped package: the exit graph, and
fixed spawn tiles vetted against `campaigns/longseason/scen`. A mapgen
regeneration that moves layouts turns into a red
`test_longseason_ledger.cpp` run (the graph/tile pins), and
`tools/longseason_mapgen` stages this pack and self-checks the book
(registration, vars, page budgets, one var-injected tick of The Long
Toll) on every generation. If a regeneration moves the strongroom, the
toll road, the Kettle pocket or the winter-quarters door, update the
tiles here and re-run both.
