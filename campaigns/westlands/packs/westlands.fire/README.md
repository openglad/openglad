# westlands.fire — The Company Fire

Hand-authored campaign pack for War of the Westlands (generator-exempt:
`export_campaign_tree` never touches `packs/` or this file; the level
content around it is regenerated wholesale by `tools/westlands_mapgen`,
which stages this tree into its self-checked package).

The fire IS the Base Camp. `base_camp` composes the gameplay zone every
fetch (docs/basecamp-zones-design.md): the purse heads the panel, the camp
stanza speaks, the company stands, and under it the docket carries the fight
at the company's feet (the level the cursor rests on, named by its own
scenario title), the night's own offer, and the two doors (QUARTERMASTER for
the standing trade, THE LEDGER for what the road has cost). The forward
choices out of that fight are named on the night they open — the night it is
won — so an ordinary night is three rows, which is the whole band the panel
shows at once; a road advertised early would push THE LEDGER behind a pager
arrow for the whole campaign. That is composition, not gating: a road the
company has not earned is closed by the ENGINE's own decoration, never by a
refusal written here. Back rows never render: backtracking lives in the
world's exits, where the player already walks it.

`lib/road.lua` mirrors the shipped FORWARD exits level by level (with each
edge's stake as its note, and the Bearer's SAVE_ALL levels flagged
`escort`); `lib/fronts.lua` holds the split-party derivations;
`lib/stanzas.lua` holds every line the fire speaks. The camp's text band is
three lines and its rows are 24/20 glyphs — `test_westlands_fire.cpp` sweeps
both budgets over every state, and `westlands_mapgen`'s self-check repeats
the graph mirror and the budgets against the produced package.

## Split the party

After the Falls (level 12) the roster's team chip becomes the OATH column
(`assign = { key = "road", labels = { "WAR", "BURDEN" } }`), written to each
hero's persisted `campaign_tag`. The two roads then advance independently —
war = the first uncleared of 13..17, burden = the first uncleared of 19..23
— and the docket carries one march row per live front, each stating its own
muster and the blades that wait ("party 4, 2 wait"). A column nobody swore
reads "all ride" rather than "unsworn": an empty muster lifts the freeze, so
that road is open to everyone.

Everything about the split is DERIVED, never stored:

| State | Derivation | What the camp does |
|---|---|---|
| Swearing window | past the Falls, no road level cleared | the oath column cycles freely, both roads offered, thin/empty columns warned about in prose |
| Frozen | a road level cleared AND both musters >= 1 | `frozen = "The Falls parted the company."`; the other road's column and the unsworn are locked out of tonight's level ("WITH THE BEARER" / "ON THE WAR ROAD" / "WAITS AT THE FALLS", and "WAITS AT THE MOUNTAIN" once that column's road is walked). The hiring board shuts too — see below |
| Unsworn bypass | a road level cleared, nobody sworn | no freeze, no locks, both roads live, an honest nag ("No road is sworn. The fire waits.") — v1 click-forward play is preserved, never punished |
| Collapse | a road level cleared, one column empty (dead or sold) | the locks lift and the fire says so ("The east fire is ashes. All ride."); the company may re-form the column |
| One front done | one road walked | only the other road is offered; its column waits at the mountain |
| Early summit | the cursor RESTS on 24, or 24 is cleared, with a road unwalked | "The Bearer is not yet come." — 17's only forward exit is the mountain, so the war-road-first company arrives there by default; the engine cannot block GO, so the fire refuses before the climb and again after it |
| Reunion | both roads walked, 24 pending | the oath retires, the team chip returns, one row: THE MOUNTAIN OF FIRE |

Deploy locks are derived from the SET level, never from a stored "current
front", so tonight's road decides who is free — and they are MECHANICAL: the
fetch that composes a lock stands the refused own heroes down (engine side,
`CampaignZoneSession::Enforce::Locks`), because a company is deployed before
the freeze exists and a lock consulted only on the deploy toggle would let
the other column march while the fire narrated a split. Choose the road,
then muster the column; the camp says so when nobody is standing ("No sword
is deployed. None march.").

The hiring board shuts while the split is frozen (`can_hire = false`, and
the QUARTERMASTER page says why). A blade bought after the Falls would carry
no oath: the frozen column refuses to swear him and the unsworn lock refuses
to deploy him, so the camp declines the sale rather than take gold for a
hero who can only stand at the waterfall. A wiped column lifts the freeze
and reopens the board with it.

The tags are machine-local save state (never a session field), and so is the
roster snapshot the muster censuses: a networked client reads its OWN sworn
heroes, so freeze, locks and prose all derive from the local book. A company
that never swore plays the unsworn-bypass shape by derivation; a guest who
swore in solo may see a different front state than the host. On a dedicated
server there is no company file at all and every var reads 0.

## Decisions and their keys

| Key | Set by | Sim consequence |
|---|---|---|
| `watch_paid` | THE WATCH'S PAY, 900g, on the QUARTERMASTER page once level 7 is cleared | Level 15: Wall-Warden (lvl 6) and two Watchmen (lvl 5) join the gate |
| `delve_counted` | COUNT THE DELVE GOLD (+800g), at the fire while `completed(9) and not completed(11)` | Level 11: two Gold-Wraiths (team-2 ghosts, lvl 6) rise off the far bank |
| `provisions` | PROVISION PACKS, 300g each, capped at 3 | Levels 13-17 and 19-23: one drumstick per tier, plus a magic potion from tier 2, beside the lead start marker |
| `sneak_bread` | BREAD FOR SNEAK, 60g, at the fire in the 19..21 window | Level 21: Sneak waits at lvl 5 on half strength (property writes; the betrayal itself stands) |
| `delve_sunk` | SINK IT IN THE RIVER — **not a registered var**; narration only | None, by construction |

Level 26: when `watch_paid` and `sneak_bread` are set and `delve_counted`
is not, The Pilgrim (team-0 archmage, lvl 10) waits on the quay. The ledger
hints at that with "Grace follows the open hand." once two of the three
kindnesses hold and the hoard is uncounted — never after it is counted, so
the hint cannot promise what the quay will not deliver.

The two hot one-shot offers (the delve pair, the bread) live at the FIRE and
nowhere else: the moral fork belongs on the night it matters, and one door
means one one-shot re-check. The quartermaster keeps the standing trade and
RETIRES its rows with `done` instead of hiding them — a bought tier and an
honored debt stay legible, quote no price, and refuse a second click.

Every consequence hook's first statement is its `var == 0` early return, so
with no decisions recorded the shipped levels run byte-identical (all
calibration floors, battle smokes, and parity goldens hold). All spawn
tiles are fixed data in `lib/spawns.lua`; the pack draws no randomness
anywhere. Spawned allies keep the default ACT_RANDOM (they are relief that
marches, not posts that hold — the mapgen allied hold-post rule binds only
ACT_GUARD placements).

Each action re-checks its own key against a direct dispatch, and the
provision cap refuses past tier 3 — so the 26→1 replay loop and the
backtrack exits can re-farm score, but never re-open a source or a sink.

## Networked play

Campaign state is per-machine, like the campaign cursor: the host's camp
drives the shared session, while each company file keeps its own decision
book — a guest's ledger speaks only for its own company, never for the
table. The oath tags read the same local book (see "Split the party"), and
the deploy locks derived from them touch only own, editable slots — never
another machine's hero. On a dedicated server there is no company file:
every var reads 0, no camp composes, and the levels run stock.
