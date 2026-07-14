# Runaway Specials Fix — Final Implementation Spec

Branch: `feature/z-axis-multifloor` @ 1435f302, repo `/home/yans/code/openglad`.
Owner complaint: mage freeze-time indefinite, ghost scare infinite (+ character-switch laundering bug), thief bombs comedic, faerie freeze stunlocks, "probably many others." Direction: limits / sublinear progression; low-level behavior byte-identical. This is a FIX, not an option — no "legacy scaling" toggle (rejected: SaveData v12 field + WorldSnapshot field = kNetworkProtocolVersion bump = 5 literal wire-byte test repins, for behavior the owner called silly).

Everything below is implementation-ready. All file:line anchors were verified against 1435f302 (census `runaway-map.json`). Time base: 1 tick = 81.6 ms (12.25 t/s); 368 ticks ~ 30 s. Screen ~320 px.

---

## 0. Non-negotiable invariants (the determinism commandments)

1. **RNG stream discipline.** Never add, remove, or reorder an `rng_.next()` call on any exercised path at ANY level. Never change a draw's bound at level ≤ the effect's knee. Only two legal shapes: (a) post-draw transform (draw with the original bound, then map the result), (b) draw-free formula change provably identity at/below the knee. `rng_state` is in every parity dump (`tests/parity/state_dump.cpp:352`); a draw-count change diverges every downstream field.
2. **Identity knees.** `do_special` paths: byte-identical for caster level ≤ 13 (max golden do_special caster = L13; L12 freeze golden requires exactly 152 > 150-tick budget). Weapon-hit paths (`compute_freeze_duration`, cleric glow, anything reachable from weapon fire/hit): identity through level ≤ 20 (weapon-emission goldens run L20 wielders; `weapon_glow_emission` serializes weapon `lifetime` per tick via `state_dump.cpp:338`).
3. **Goldens are unregenerable.** They are captured from the master companion (`../openglad-master`, branch `parity-companion`) which will never contain this change. Any golden flip = design error. Above-knee behavior is pinned by new unit tests instead of new parity scenarios (do NOT add parity rows for above-knee casts).
4. **Pin discipline.** Canary mutations are line+text anchored (`_apply_mutation.py` exits 6/7 on miss) and NOT in CI. Any edit ABOVE a file's pins must be strictly line-count-neutral. Inserts are legal only BELOW a file's max pin. Exactly 2 pinned lines are rewritten (repin drills in WP-3). `family_mage.cpp:200` and `game_world.cpp` are NOT touched.
5. **No float `log`/`sqrt`/`pow` in sim formulas** (not correctly-rounded cross-platform: nix GCC15, CI GCC, wasm). All curves are integer mul/div.
6. **No wire change.** No new replicated field, no kNetworkProtocolVersion bump. The command queue is not replicated (snapshot apply clears it, `world_snapshot.cpp:1710`) and not parity-dumped; frozen_delay's negative phase is masked before serialization (§3.3).
7. **Monotonic cap idiom.** Every accumulator cap is `new = max(cur, min(cur + gain, CAP))` — never reduces an existing value (protects potion-granted invisibility 450, legacy saves, snapshots).
8. **Coverage gate ~90.00% on src/.** Every new branch gets a unit row; prefer one-line clamp/guard forms; judge by local baseline→change DELTA, not absolute (single-run gcovr undercounts CI's repeat-until-pass accumulation). Wipe stale `.gcda` before re-measuring.
9. **Single PR.** WP-1..WP-5 land as ordered commits in ONE PR (WP-2's switch fix removes the player's only escape from stacked scares; WP-3's fright merge is its mandatory counterpart — no partial merge).

---

## 1. The curve primitive

`og::combat::soften(int raw, int knee, int ceiling)` in `include/openglad/core/combat_math.h` + `src/core/combat_math.cpp` (both ZERO canary pins):

```cpp
// Integer soft-knee asymptote. raw <= knee: identity (bit-exact legacy).
// raw > knee: knee + g*G/(g+G) rounded half-up; g = raw-knee, G = ceiling-knee.
// Slope ~1 just above the knee; strictly increasing; bounded by ceiling
// (the asymptote is reached only at absurd inputs, g >= 2G^2-G).
inline constexpr int soften(int raw, int knee, int ceiling) {
    if (raw <= knee) return raw;
    const long long g = raw - knee, G = ceiling - knee;
    return knee + static_cast<int>((g * G + (g + G) / 2) / (g + G));
}
```

Knee construction rule: knee = the exact legacy formula value at the max golden-exercised level for that effect, so every below-knee input maps to itself bit-for-bit. Every knee/ceiling/cap is a named `inline constexpr` in `og::combat` (single legibility home). Designer note in the header: raising a ceiling can never break parity (only above-knee values move); half the remaining headroom is consumed roughly `G/slope` levels past the knee.

Named constants (values justified in §2): `kEnemyFreezeBankCap=300`, `kFrozenStunStackCap=150`, `kInvisibilityCloakCap=350`, `kFreezeThawImmunityTicks=12`, `kSprinkleRefreshOwnerLevel=21`, `kSprinkleRefreshFloor=10`, `kScareDurationKnee=325/Ceiling=375`, `kScareRadiusCap=250`, `kBombDamageKnee=210/Ceiling=300`, `kYellRadiusCap=420`, `kSprinkleRollKnee=79/Ceiling=110`, `kCharmKnee=264/Ceiling=350`, `kThiefCharmKnee=375/Ceiling=490`, `kGlowBonusCap=2200`, `kFaerieLifeKnee=570/Ceiling=800`, `kElementalLifeKnee=980/Ceiling=1350`, `kImageLifeKnee=360/Ceiling=520`, `kSkeletonLifeKnee=645/Ceiling=900`, `kGhostRaiseLifeKnee=670/Ceiling=925`, `kMpPoolDamageCap=600`, `kStarburstAddCap=40`, `kMaceLifeCap=468`, `kShotDrainCap=50`.

Named per-effect wrappers (call sites read as policy-by-name): `scare_duration(L)`, `scare_radius(L)`, `bomb_damage(L)`, `yell_radius(L)`, `cloak_total(cur,gain)`, `stun_total(cur_raw,add)`, `druid_faerie_lifetime(L)`, `elemental_lifetime(L)`, `image_lifetime(L)`, `skeleton_lifetime(L)`, `ghost_raise_lifetime(L)`, `glow_bonus(L)`.

---

## 2. Per-effect policy table

All durations in ticks. Columns: L1 / L5 / L10 / L13 / L14 / L20 / L30 / L50, `before → after` (single value = unchanged). Computed with the round-half-up `soften` above — these exact numbers are unit-test pins (§6.1).

**1. MAGE FREEZE TIME, player branch** (`family_mage.cpp:200` — NOT edited; 500 MP, no busy; held key recasts 1/tick; mana potions refill mid-freeze).
Before: `enemy_freeze += 20 + 11*L`, uncapped world accumulator → indefinite.
After: per-cast formula UNCHANGED at all levels; world bank clamped post-cast in pin-free `walker_specials.cpp`: `enemy_freeze = min(enemy_freeze, 300)` (24.5 s max pending time-stop).
Per-cast from empty: 31 / 75 / 130 / 163 / 174 / 240 / 350→300 / 570→300. Pending total: ∞ → ≤300.
Knee proof: goldens cast ONCE at L7 (=97), L12 (=152, must stay >150 — unchanged), L13 (=163, `input_special_switch_wrap` needs the freeze to cover the whole 150-tick window with no palette-reset event — unchanged); all ≤300 so the clamp is the identity map on every golden; formula draws no RNG. Identity actually holds through L25 (295). Enemy branch (`5+2L` capped 50 into `bonus_rounds`, :206-217) is the in-file precedent — untouched. Cheat `+=50` (`cheat_handler.cpp:100`) untouched. Feel contract kept: L20 cast is still the full 240-tick spectacle; only the bank is bounded; chain-casting at the cap wastes 500 MP/cast (self-punishing; no busy added — busy would change AI cast cadence in goldens/calibration).

**2. GHOST SCARE duration** (`effect_family_ghost_scare.cpp:41-46`, pin-free file; 30 MP, no busy; AI recasts on ~2/3 of hits when foe <130 px).
Before: `generic = 25*L` (L20 = 40.8 s), minus `rng(con)` for myguy victims only; `force_command` PREPENDS → repeat scares stack end-to-end → infinite.
After: `generic = soften(25*L, 325, 375)`, THEN the legacy myguy-only `generic -= rng(con)` conditional draw verbatim (order/count identical), then injected via `force_fright` (merge, §3.2) instead of `force_command`.
25 / 125 / 250 / 325 / 342 / 364 / 370 / 372 — ceiling 375 (30.6 s), and by the merge rule the TOTAL queued fright can never exceed one cast's value.
Knee proof: scare goldens are L1 (arena, :1533-1535 region; =25) and L5 (`effect_ghost_scare_emission`, =125, must shove the soldier past x=240 — unchanged); both ≤ knee; single-cast goldens on fresh victims make `force_fright` degenerate to the legacy prepend byte-for-byte. Campaign ghosts top at L8 (=200, identity).

**3. GHOST SCARE radius** (line :27). Before: `50 + 10*L` px (off-screen at L27). After: `min(50 + 10*L, 250)`.
60 / 100 / 150 / 180 / 190 / 250 / 350→250 / 550→250. Draw-free; binds only at L21+ → identity through L20, stronger than required.

**4. ORC YELL freeze add** (`family_orc.cpp:62`, above pin 130 — line-count-neutral edit).
Before: `frozen_delay += max(0, 10 + rng(10L) − rng(10*con))`, unbounded stacking.
After: BOTH rng draws kept verbatim (bounds untouched at all levels); write becomes `set_frozen_delay(stun_total(frozen_delay_raw(), tempy))` where `stun_total(cur,add)`: if `cur < 0` return `cur` (thaw immunity, §3.3 — TRACE-probed, with fallback); else `max(cur, min(cur + max(add,0), 150))`.
Per-cast mean (con 0): ~15 / ~35 / ~60 / ~75 / ~80 / ~110 / ~160 / ~260 — per-cast unchanged; victim TOTAL from yells: ∞ → ≤150 (12.2 s).
Knee proof: orc goldens are L1/L4 single casts (max add ≪150) on fresh victims → identity. Campaign orcs ≤L8: a double-max-roll stack (178) can bind with small probability → calibration re-measure (scen17/scen23/scen24), §6.4.

**5. ORC YELL radius** (:46). Before: `160 + 20*L` (beyond screen at L8). After: `min(160 + 20*L, 420)`.
180 / 260 / 360 / 420 / 440→420 / 560→420 / 760→420 / 1160→420. Identity ≤L13 (420 = f(13)); draw-free.

**6. FAERIE SPRINKLE FREEZE** (`combat_math.cpp:52-65` compute_freeze_duration, pin-free; apply site `weapon_family_animate.cpp:111-119`, BELOW max pin 100 — inserts safe).
Before: `frozen_delay = rng(40 + 2L − con/21)` SET on every 2-MP hit — per-hit sane, but re-SET before thaw = permanent stunlock; note sprinkle is a WEAPON effect, NOT gated by `specials_disabled`.
After, three independent pieces:
(a) Roll bound + draw unchanged at ALL levels; post-draw `return soften(roll, 79, 110)`. Max (con 0): 41 / 49 / 59 / 65 / 67 / 79 / 99→91 / 139→99. Identity through L20 is EXACT in the distribution tail: the L20 con-0 bound is 80 → rolls 0..79 ≤ knee 79 map to themselves (the `weapon_sprinkle_emission` golden — L20 wielder, per-tick weapon tracks + faerie-death timing byte-compared — cannot observe it at any roll).
(b) Refresh gate: the roll is ALWAYS drawn (stream identical); the SET is skipped when `owner_level >= 21 && target->frozen_delay() > 10` (the charm `charm_left<=10` gate mirrored). Level-gated ≥21 because the L20 golden's soldier thaw schedule must stay byte-identical. Residual accepted: L≤20 AI-vs-AI faerie stunlock persists (campaign faeries ≤L7; tower spire faeries L6-7/L16-17) — see Deferred #6 and Risk 3.
(c) Thaw immunity (player-controlled victims only, §3.3): SET also skipped while `frozen_delay_raw() < 0`. Grants the controlled hero ≥12 actable ticks after every freeze cycle THE PLAYER DRAIN TERMINATES; golden-safe by construction (the sprinkle golden's victim is an AI soldier draining in `living.cpp`, which never writes the negative phase). The guarantee is cadence- and drain-parity-CONDITIONAL, not per-cycle at all cadences — two pinned residuals (WP-4 battery `sprinkle_player_victim_sub_gate_cadence_residual` / `_even_span_residual`) where no window arises: (i) sustained hit cadence at or below the 10-tick (b) gate window re-SETs the freeze while `frozen_delay` is in (0,10], BEFORE the 1→0 transition that arms the immunity, so the relock persists by the gate's own design; (ii) a player-controlled victim drains twice per tick (sim_input_handler + living::act, both master behavior) and only the player drain writes −12, so even-parity spans thaw at the `living::act` site with no window. In real play faerie AI cadence sits above the gate window (measured end-to-end: real L16 ACT_RANDOM faerie vs a controlled soldier over 2500 ticks → 80% actable ticks; every immunity window that fired spanned ≥12 actable ticks), so the player-experienced stunlock is broken in practice — but not by a per-cycle guarantee. Owner sign-off item alongside Risk 3.

**7. THIEF BOMB damage** (`family_thief.cpp:77`, between pins 69/95 — line-neutral one-line edit; 35 MP, no busy).
Before: `15*(L+1)` (one-shots livings at L10+). After: `static_cast<float>(bomb_damage(L))` = `soften(15*(L+1), 210, 300)` (ints ≤300 exactly representable in float → below-knee bit-identity).
30 / 90 / 165 / 210 / 225→223 / 315→258 / 465→277 / 765→287.
Knee proof: all bomb goldens are L5 thieves (=90); draw-free. Knockback (`min(2+L/15, 8)`, `effect_family_bomb.cpp:71-75`) and blast radius (`clamp(4L,16,96)`, `effect.cpp:160-169`) already capped — UNCHANGED. The "comedic flying" is emergent (damage-triggered 16-tick yell-flee + queue stacking + ally chain-shoves); the damage curve starves the flee chains, and per-bomb knockback keeps its legacy prepend on purpose (multi-bomb slapstick is half the game's charm; explosion-funnel dedupe is Deferred #2).

**8. THIEF CLOAK** (`family_thief.cpp:95` — PINNED line, one-line rewrite ⇒ REPIN, drill in WP-3).
Before: `invisibility_left += 20 + rng(20)*L`, uncapped (short overflow at high L).
After: `rng(20)` draw at the call site unchanged; `set_invisibility_left(cloak_total(invisibility_left(), 20 + roll*L))` with `cloak_total(cur,gain) = max(cur, min(cur+gain, 350))`.
Per-cast mean: 29.5 / 67.5 / 115 / 143.5 / 153 / 210 / 305 / 495 — unchanged where it fits; TOTAL ≤350 (28.6 s). Also fixes the overflow.
Knee proof: golden thief is L4, single cast, max 96 ≪350 (and the cloak must END inside the 150-tick budget — unchanged); max single below-knee cast (L13 max = 267) never clamps. Invisibility POTIONS (`+= 150*item_level`, pinned consumable lines) share the field and are NOT capped — the monotonic form guarantees a potion-granted 450 is never reduced.

**9. CHARM.** The model citizen — existing anti-refresh gates (`real_team_num()==255`; archmage also `charm_left<=10`) UNTOUCHED.
Archmage (`compute_charm_duration`, `combat_math.cpp:76-80`, pin-free): draw `25 + rng(20*diff)` unchanged; `return soften(res, 264, 350)`. Knee 264 = exact max result at diff 12 (caster L13 vs L1) → blanket L≤13 rule honored even in the tail; golden is diff 9 (max 204) → identity. Max by diff: d9 204 / d12 264 / d19 404→317 / d29 604→333 / d49 1004→341; ceiling 350.
Thief charm (`family_thief.cpp:150`, draw-free, parity-cold, between pins 95/171 — line-neutral): `soften(75 + 25*d, 375, 490)`. d12 identity 375; d14 425→410 / d19 550→444 / d29 800→466 / d49 1300→477. (Knee 375 forced by diff-12 identity; thief charm being stronger per diff than archmage is legacy design.)

**10. CLERIC GLOW** (`family_cleric.cpp:31`, above pins 333/352 — line-neutral).
Before: `lifetime += 110*L` on top of init 350 (L50 = 477 s objects, 8 MP each, MAXOBS pressure).
After: `lifetime += min(110*L, 2200)`. Totals: 460 / 900 / 1450 / 1780 / 1890 / 2550 / 3650→2550 / 5850→2550 (cap = 208 s).
Knee L20 IDENTITY-FORCED, flat cap not soften: `weapon_glow_emission` runs an L20 cleric and weapon dumps serialize `lifetime` per tick (`scenario_table.h:2322` region, `state_dump.cpp:338`) — 110*20 = 2200 = cap exactly; above-L20 glow growth is pure MAXOBS pressure with no gameplay reward. (This is the trap two candidate designs failed; do not "improve" it to a knee-13 curve.)

**11. SUMMON LIFETIMES** (counts NOT capped in v1 — Deferred #1; MAXOBS=150 backstop + MP economy retained).
- Druid faerie `50+40L` (`family_druid.cpp:78` — PINNED ⇒ REPIN): `druid_faerie_lifetime(L) = soften(·, 570, 800)`: 90 / 250 / 450 / 570 / 610→604 / 850→696 / 1250→742 / 2050→769. Goldens L1 (=90) and L4 (=210, must OUTLIVE the 150-tick budget — trivially holds).
- Archmage elemental `200+60L` (:327, between pins 241/509 — line-neutral): `soften(·, 980, 1350)`: 260 / 500 / 800 / 980 / 1040→1032 / 1400→1177 / 2000→1252 / 3200→1297. Parity-cold branch (shifted); knee 13 anyway.
- Archmage image `100+20L` (:419, line-neutral): `soften(·, 360, 520)`: 120 / 200 / 300 / 360 / 380→378 / 500→435 / — / 1100→492. Golden: L7 illusion (=240) identity.
- Cleric skeleton `125+40L` (:194, line-neutral): `soften(·, 645, 900)`: 165 / 325 / 525 / 645 / 685→680 / 925→778 / — / 2125→863. Goldens L4/L7 identity.
- Cleric ghost `150+40L` (:249, line-neutral): `soften(·, 670, 925)`: 190 / 350 / 550 / 670 / 710→705 / 950→803 / — / 2150→888.

**12. MP-POOL DAMAGE** (closes the gold-bought-INT and 200%-difficulty axes a level knee cannot reach; all draw-free in-place `min()` wraps).
- Heartburst pool (`family_mage.cpp:239` region, between pins 231/289; `family_archmage.cpp:230`, above pin 241): pool = `min((MP − cost)/2, 600)` (binds only above ~1280-1300 MP).
- Chain lightning initial bolt (`family_archmage.cpp:267-270`): `min((MP−80)/2, 600)`.
- Starburst per-fireball add (`family_mage.cpp:165-189` region — ABOVE pin 200, line-count neutrality is critical here): `min((MP−60)/15, 40)`; the derived `lineofsight += add/3` inherits the bound (binds >660 MP).
- Mystic mace lifetime (`family_cleric.cpp:150-155`): `min(100 + (MP−2)/2, 468)` (binds >738 MP).
- Archmage per-shot drain bonus (`family_archmage.cpp:39-42`): `min(MP/20, 50)` (binds >1000 MP).
Identity: golden caster MP values sit far below every bind point (mage/archmage spawns mp 600 → pool 250, starburst add 36, drain 30; cleric mace mp 80 → 139; thief mp 300). MANDATORY pre-merge audit (WP-3 exit): grep every `SpawnSpec` mp field in `scenario_table.h` and verify margin; if any outlier exists, RAISE the cap above it (never move a golden). HEAL: no change (cost self-limiting).

**13. SANE / deliberate no-change** (documented so nobody "fixes" them): enemy-branch mage freeze (capped 50); `bonus_rounds` (capped 50, `living.cpp:70`); bomb knockback (8) + explosion radius (96); yell-flee 16 (`yo_delay` 80 regated); whirlwind/disarm/charge/rush; soldier boomerang (`30+12L`, single object, >30 s only at L29+); skeleton tunnel (range-only); boulder (step clamp 15); elf rocks; slime split (MAXOBS-gated); poison cloud (`40+3L`); druid trees (permanent by design — editor feature, MAXOBS backstop) and protection-circle HP stacking (base-stat magnitude); potions (item-level driven, pinned consumable lines, out of complaint scope); druid reveal / archmage radar (radar-only); archmage teleport spam ("the teleport IS the boss texture" — Moon Warden/Spirelord identity); taunt (contest + busy).

---

## 3. Mechanism rules (per-cast curves cannot fix "indefinite"; these can — they apply at ALL levels and are golden-invisible by construction)

### 3.1 Accumulator caps (Class 1)
Precedent: `bonus_rounds ≤ 50` (`living.cpp:70`) and the enemy-branch freeze cap 50 six lines below the bug.
- `enemy_freeze ≤ 300`: clamped in `walker::special()` (pin-free `walker_specials.cpp`, immediately after the do_special success/charge block ~:154-167): `if (world->enemy_freeze > kEnemyFreezeBankCap) world->enemy_freeze = kEnemyFreezeBankCap;`. NOT at `family_mage.cpp:200` (pinned — stays byte-identical, canary keeps teeth: the mutation still flips the L12 golden below the cap) and NOT at the `game_world.cpp:1648` decrement (pin-locked region; cap-on-read would interact with the parity-visible "TIME LEFT" notification). Every do_special routes through `walker::special`; the clamp is a no-op for non-freeze specials.
- `frozen_delay` from orc yell ≤ 150 via `stun_total` at the write site (§2.4). The faerie SET path is independently bounded by its soften ceiling (≤110 < 150). Rejected alternative: clamping inside `set_frozen_delay` — it is an `OG_STATS_DIRTY_FIELD` macro accessor (manual rewrite risks dirty-bit semantics) and would silently sanitize snapshot-applied values; write-site caps are determinism-safer.
- `invisibility_left` from cloak ≤ 350 via `cloak_total` at the cloak site only (§2.8); potions untouched.
All caps exceed every single-cast golden value (300 ≥ 163 freeze L13; 350 ≥ 267 cloak L13-max; 150 ≥ orc L1/L4 adds) ⇒ byte-identical on all goldens.

### 3.2 Fright queue merge (Class 2)
The command struct (`include/openglad/gameplay/statistics.h`) gains `bool forced = false;` — set to `true` inside `force_command` (`stats.cpp:185-207`), left false by `add_command`. Wire/dump-free: queues are never serialized (snapshot apply does `commands.clear()` at `world_snapshot.cpp:1710`; parity dumps carry no queues; VERIFY in WP-2 that no save/replay path persists `commands`).
New `statistics::force_fright(int iterations, int dx, int dy)` in pin-free `stats.cpp`:
- If the queue FRONT is a `forced` `COMMAND_WALK`: `count = max(existing_count, iterations)`, direction = the new (dx,dy) (a fresh scare re-points and refreshes-to-max, never sums).
- Else: fall through to legacy `force_command(COMMAND_WALK, iterations, dx, dy)` (exact prepend semantics, including degenerate iteration values after the myguy `rng(con)` subtraction).
ONLY ghost scare calls it. All other `force_command`/forced-walk users keep exact legacy behavior: bomb knockback (≤8), rush 4, yell-flee 16, archer retreat 8, soldier shove 8, CTF `issue_front_command`, forced FOLLOW 100 — all constant-bounded. Result: N overlapping scares = one bounded fright (≤375 ticks), never N×25L. Golden-identity: every golden casts scare once on victims with no forced front entry → fall-through → byte-identical. Rejected: `COMMAND_FLEE` new command type (constants.h + do_command + score_panel + HUD-test ripple, and replace-semantics would let an 8-tick knock truncate a 364-tick scare); global `force_command` clamp (collides with CTF/FOLLOW). Documented cross-effect collateral: a scare landing while a bomb-knockback entry is at front merges into it (count → scare value, direction → away-from-ghost) — acceptable, note in stats.cpp comment block.
HUD: `score_panel.cpp:132-143` (`hud_scared_flee_ticks` reads the front WALK) is UNTOUCHED and keeps working — the flee countdown remains visible, which is the perceived-fairness linchpin for long scares.

### 3.3 Thaw immunity + refresh gate (Class 3)
A stunlock fix needs memory of the previous stun; instead of a new replicated field (protocol bump), reuse the SIGN of the existing replicated i16 `frozen_delay`:
- **Masked getter**: `statistics::frozen_delay()` returns `raw < 0 ? 0 : raw`; add `frozen_delay_raw()`. The `OG_STATS_DIRTY_FIELD` macro pair for this field is unpicked into manual accessors PRESERVING dirty-bit semantics byte-for-byte on set. Every existing reader (AI drain guards, input swallow at `sim_input_handler.cpp:309`, `ctf_ai.cpp:58`'s `<= 0` readiness test, HUD, snapshot capture at `world_snapshot.cpp:2097`) sees master-identical values with zero edits — negatives never reach the wire (capture reads the getter; a −12→−11 tick may emit a redundant dirty send of value 0 for ≤12 ticks — harmless; verify capture goes through the getter, else clamp at capture).
- **Player-only write**: the player-side drain (`sim_input_handler.cpp:309-315`) on the 1→0 transition writes `−kFreezeThawImmunityTicks` (−12) instead of 0 — single-line in-place rewrite (line-count-neutral; pins 340/345/353 below). AI drains (`living.cpp:281-285`, `walker.cpp:845-847`) thaw to 0 as today: with the masked getter their `if (frozen_delay())` guards skip negatives with ZERO edits (WP-2 audits both shapes; if either decrements without a getter guard, fix in-place line-neutrally — walker.cpp:845 sits above pin 1192).
- **Climb**: one inserted call early in `living::act` (below `living.cpp`'s only pin, 114 — insert safe): `if (frozen_delay_raw() < 0) tick_freeze_immunity();` (raw +1/tick). Runs for player-controlled walkers too (command drain already lives in `living::act` before the ACT_CONTROL switch); a walker switched away mid-immunity keeps climbing.
- **Apply-site refusal** (draws ALWAYS performed first): sprinkle SET skipped while `frozen_delay_raw() < 0` (§2.6c); orc add discarded while `< 0` (inside `stun_total`).
- **Scope of the immunity** (do not oversell): it arms ONLY on the player drain's 1→0 transition, so it protects freeze cycles that reach thaw via that drain. See §2.6c's two pinned residuals (sub-gate-window cadence relocks before arming; even-parity spans thaw at the `living::act` drain with no window). Closing either is a parity-visible sim change (extra/earlier writes on golden-exercised paths) — rejected by §0; the shipped shape is pinned so it is a documented decision, not a surprise.
Golden safety by construction: only the player drain writes negatives; the sprinkle golden's victim is an AI soldier; calibration crews are AI-driven. ONE empirical obligation: any golden that thaws a team-0 walker from `frozen_delay` (flagged suspect: `special_orc_1`) — TRACE-probe the −12 write and the refusal; if a second application lands inside the window in any golden, FALLBACK = drop the orc-site immunity check (keep sprinkle-only; the 150 stack cap still bounds orcs) — pre-approved, no redesign.
Transform-copy at `walker.cpp:1585` copies raw — benign (immunity transfers with the body). Header comment on the field: "negative = thaw-immunity phase; ALWAYS read via frozen_delay()/frozen_delay_raw()". Audit text/curses clients for raw prints (text client prints frozen state ~:55 — must use the masked getter).

---

## 4. Scare-reset (laundering) fix + charm decision

**Bug**: `sim_input_handler.cpp:306-307` (`if (control != oldcontrol) control->stats()->clear_command();`) and the claim-path clear at :142 (:138-143) wipe the whole command queue on every character switch. `clear_command` (`stats.cpp:134-146`) additionally resets the weapon, restores `real_team_num` (silently un-charms), and clears leader. Two SwitchChar presses launder any scare/knockback and cleanse charm.

**Fix**: new `statistics::clear_command_for_control_switch()` in pin-free `stats.cpp`:
1. PRESERVE the leading run of `forced == true` `COMMAND_WALK` entries (fright/knockback/flee — `score_panel.cpp:125-131` documents "a front walk command IS the fleeing state", so the HUD countdown keeps working and now survives switches). Erase everything behind that run.
2. KEEP the weapon reset and leader clear (legacy switch hygiene; dropping leader-clear was rejected as an unexamined semantics change).
3. DO NOT restore `real_team_num` — **charm survives control switches**.
Swap the calls at :142 and :306-307 as one-line, line-count-neutral method-name replacements (pins 193/209/340/345/353 intact). ALL other `clear_command` sites keep full legacy semantics on purpose: level load (`game.cpp:316` — fright/charm must not cross levels), `replay_runtime.cpp:117`, `game_server.cpp:1144/1312/1358` (join/reconnect), and `hit_response`'s in-combat clear (golden-baked knockback-wipe asymmetry).

**Charm decision — YES it survives, reasons** (owner sign-off flagged):
(a) Un-charm-by-switching is the identical laundering exploit, via the identical line.
(b) The switch-cycle filter (`sim_input_handler.cpp:191`) already requires `real_team_num()==255` to switch INTO a walker — the engine's own model says charmed units are not yours; the claim-path un-charm is an accident of `clear_command`'s bundling, not a designed counter. The filter itself is NOT weakened.
(c) `COMMAND_UNCHARM` is fully commented-out dead code; expiry belongs solely to `charm_left` decay (`living.cpp:194-204`), and the new 350-tick charm ceiling bounds the worst case (a charmed hero is lost ≤28.6 s; no softlock — the filter skips it during cycling). Archmage mind control stays load-bearing boss texture (Moon Warden L9, Founder L10, Spirelord).

**Coherence**: this fix REMOVES the player's only current escape from stacked scares — the §3.2 fright merge MUST land in the same PR (worst case post-fix: one bounded scare ≤375 ticks, not a laundered unbounded stack). Feel note for the owner: "shake off flee by switching" dies everywhere (knockback/flee included) — one teachable rule: forced effects survive control switches.

**Parity proof**: the selective clear is extensionally identical to `clear_command` whenever the queue has no leading forced entries AND `real_team_num==255`. Census-verified: no parity row switches control mid-scare or mid-charm; `input_switch_char_scen99` (K_SWITCH at tick 5 onto a fresh archer: empty queue, real_team 255) and the claim path at scenario start (empty queues) satisfy both conditions. Obligations: run `run_parity_diff.sh` on `input_switch_char` and `input_special_switch_wrap` + the full suite; TRACE-probe "selective clear preserved N>0 forced entries" — must fire 0 times across all goldens (catches any unnoticed shove-then-claim row).
Networked: fright stays queue-resident (no new snapshot field, no protocol bump); mirror HUDs remain blind to flee countdowns exactly as today (pre-existing gap, recorded as a framework flip-trigger).

---

## 5. Work packages (ordered commits, ONE PR; disjoint file ownership; pin discipline per file)

**WP-1 — Core curve + constants + unit pins.**
Files: `include/openglad/core/combat_math.h`, `src/core/combat_math.cpp`, `tests/unit/test_combat_softcap.cpp`, `CMakeLists.txt` (og_add_unit_group registration only). Zero pins.
Edits: `soften`, all named constants, all wrappers (§1); `compute_freeze_duration` post-draw `soften(roll,79,110)` keeping the `rng.next(40+2L−con/21)` bound/callsite untouched; `compute_charm_duration` post-draw `soften(res,264,350)` keeping `25+rng(20*diff)`.
Proof obligations: og_test_parity full suite green (188/188); `run_parity_diff.sh` + `diff_dumps.py` on `weapon_sprinkle_emission`, `summon_druid_pet_scen950`, `special_druid_2`, the archmage mind-control row, faerie family rows (compute_* callers) — expected byte-identical by knee construction. No canary impact (no pinned file touched). Unit tables land here (§6.1).

**WP-2 — Mechanism plumbing (queue, switch, immunity, freeze bank).**
Files: `include/openglad/gameplay/statistics.h`, `src/gameplay/stats.cpp`, `src/gameplay/sim_input_handler.cpp`, `src/gameplay/living.cpp`, `src/gameplay/walker.cpp` (audit; edit only if the :845 drain lacks a getter guard), `src/gameplay/walker_specials.cpp`, `tests/unit/test_stats_fright.cpp`.
Edits: `forced` flag + `force_fright` + `clear_command_for_control_switch` + `tick_freeze_immunity` (stats/statistics — pin-free/header); manual masked accessors for frozen_delay preserving OG_STATS_DIRTY_FIELD dirty-bit semantics; `sim_input_handler.cpp` :142/:306-307 method swaps + :311 thaw −12 one-liner (ALL strictly line-count-neutral; pins 193/209/340/345/353); `living::act` climb insert (below pin 114 — safe); `walker_specials.cpp` enemy_freeze bank clamp.
Audits (exit criteria): commands never serialized by any save/replay path; snapshot capture of frozen_delay goes through the masked getter; the three drain-site shapes; `ctf_ai.cpp:58`; text/curses frozen prints; TRACE probes added under TESTING at every new conditional (merge taken, selective-clear preserved forced entries, −12 written, immunity refusal, bank clamp bound).
Proof obligations: og_test_parity 188/188; `run_parity_diff.sh`/`diff_dumps.py` on `input_switch_char`, `input_special_switch_wrap`, `enemy_freeze_mage`, `special_mage_3/5`, `special_orc_1`(+`special_orc_2`); TRACE-probe grep over the full parity run = zero firings inside goldens (except fall-through paths); canary `run_mutation_canary.sh --scenario` for every row anchored in `sim_input_handler.cpp`, `living.cpp`, `walker.cpp` (if touched) — genuine-toothless stays 0; no repins expected here (verify pin lines by grep after the line-neutral edits).

**WP-3 — Cast sites + the 2 repins + companion mirror.**
Files: `src/gameplay/families/{family_mage,family_thief,family_orc,family_cleric,family_archmage,family_druid}.cpp`, `src/gameplay/families/effect_family_ghost_scare.cpp`, `src/gameplay/families/weapon_family_animate.cpp`, `tests/parity/scenario_table.h`, `../openglad-master/tools/parity_scenario_table.h` (byte-mirror).
Edits (pin constraint per file): ghost scare :27/:41/:45 (pin-free); sprinkle gates at `weapon_family_animate.cpp:111-119` (inserts BELOW max pin 100 — safe); `family_orc.cpp` :46/:62 (line-neutral above pin 130); `family_thief.cpp` :77 and :150 (line-neutral between pins 69/95 and 95/171) and :95 (repin #1); `family_mage.cpp` :165-189 starburst + :239 heartburst (line-neutral; :200 pin UNTOUCHED — pins 200/231/289/308); `family_archmage.cpp` :39-42/:225-230/:267-270/:327/:419 (line-neutral; pins 241/509/528); `family_cleric.cpp` :31/:150-155/:194/:249 (line-neutral; pins 333/352); `family_druid.cpp` :78 (repin #2; line-neutral — pins 78/123/172/191).
REPIN DRILL (both pins):
1. Rewrite the source line one-for-one (same line number).
2. In `tests/parity/scenario_table.h`, update the Mutation's from-text to the NEW exact line (including indentation): `kMut_invisibility_thief_scen99` at :4137-4141; `kMut_summon_lifetime_faerie_scen99` at :4218-4224. Design the to-text so the canary KEEPS TEETH: cloak to-text zeroes the gain (thief HP≠1500 exact-band flips); faerie to-text sets lifetime 1 (dies inside budget, summon facts flip). Line numbers stay 95 / 78.
3. Sweep stale formula comments in the table (most stay true — below-knee formulas unchanged; fix only text describing the rewritten cloak/druid lines).
4. Byte-mirror the WHOLE table to `../openglad-master/tools/parity_scenario_table.h` IN THE SAME COMMIT (`_apply_mutation.py` refuses to mutate the companion; the mirror must precede any future golden operation).
5. Dry-run `_apply_mutation.py` per repinned row (exit 0), restore, then `scripts/parity/run_mutation_canary.sh --scenario` per row.
Proof obligations: og_test_parity 188/188; `run_parity_diff.sh` + `diff_dumps.py` (authoritative semantic check) per touched-path row: `special_mage_3/5`, `enemy_freeze_mage`, `input_special_switch_wrap`, `special_ghost_1` + `effect_ghost_scare_emission` (+arena), `special_thief_scen789`, `effect_bomb_emission`, `effect_bomb_timer`, `invisibility_thief`, `effect_poison_cloud_emit`, `weapon_sprinkle_emission`, `weapon_glow_emission`, `special_orc_1/2`, `special_cleric_1..4`, `special_druid_2`, `summon_druid_pet_scen950`, `summon_lifetime_faerie` + `_decrement`, `special_archmage_scen123`, `special_archmage_2`, `effect_heartburst_multitarget` — all expected green by knee construction. Canary per Mutation row in every touched family file + weapon_family_animate; genuine-toothless 0 (`--all` exits 1 by design — 2 exempt Invariant rows; do not chase it). Golden-MP audit (§2.12). TRACE-probe grep across the full parity run: zero gate/clamp firings inside goldens.

**WP-4 — Silliness battery + regression tests.**
Files: `tests/unit/test_silliness_battery.cpp`, CMake registration. Details §6.2-6.3. Run new tests under `--gtest_shuffle`, 30-seed sweep (known trap: settle any camera-dependent assertions; these are headless so mostly N/A).

**WP-5 — Calibration re-measure + deliberate floor repins + playtests.**
Files: `tests/unit/test_{westlands,longseason,tower}_calibration.cpp` (floor values only), docs note.
Run `{WESTLANDS,LONGSEASON,TOWER}_CALIBRATION_MEASURE=1` across seeds {42,1337,2025}, BEFORE (baseline at WP-0/HEAD) and AFTER. Drift is EXPECTED OUTPUT where all-level mechanism fixes bind in-battle, not regression: fright merge (westlands scen11 Great River floor 4 — L5 ghosts + allied TREEHOUSE faerie gen; scen24 floor 2 — L8 ghosts; merged scares keep pessimistic crews ENGAGED, floors can move EITHER way), orc stack cap (scen17 floor 2, scen23 floor 5, scen24), cloak cap (longseason scen8 floor 3, Long Tom L7 — binds only if the AI stacks casts). Player-only immunity and the ≥21 gate are calibration-invisible by construction (crews AI-driven; all calibration casters ≤L13). Re-pin moved floors deliberately with a comment citing this spec; then `scripts/westlands_playtest.sh`, `scripts/longseason_playtest.sh`, and the tower WP-7 wipe-rate sweep (0-floor pins cover the boss levels — CI is blind there; the sweeps are the real check). Shared-machine load can flake wall-clock tests — re-run before concluding.

---

## 6. Test plan

### 6.1 Per-effect unit pins (`test_combat_softcap.cpp`, WP-1)
- `soften` properties: identity for raw ≤ knee (exhaustive over each curve's below-knee range); `soften(knee+1) == knee+1` for every (knee,ceiling) pair in use; non-decreasing in raw; `≤ ceiling` always and `< ceiling` for every tabulated level ≤ 60.
- Exact tables from §2 asserted verbatim per wrapper at L1/L5/L10/L13/L14/L20/L30/L50 (before-values in comments): scare 25/125/250/325/342/364/370/372; radius min-cap table; bomb 30/90/165/210/223/258/277/287; orc radius; sprinkle max-roll mapping (79→79, 99→91, 139→99) + exhaustive identity for rolls 0..79; charm knees (204/264 identity, 404→317, 604→333, 1004→341; thief 375 identity, 550→444, 1300→477); glow bonus; all 5 lifetime tables; `cloak_total`/`stun_total` monotonic-cap semantics incl. never-reduce (cur=450 stays 450) and immunity-discard (cur=−5 returned unchanged).
- Reward guardrail (executable owner contract): strict `value(L30) > value(L14)` for scare duration, bomb damage, sprinkle max, both charms, all 5 lifetimes, and freeze single-cast-from-empty (300 > 174). Documented flat-cap exemptions asserted as equalities: scare radius from L20, yell radius from L13, glow from L20, freeze from L26.

### 6.2 Silliness battery (`test_silliness_battery.cpp`, WP-4 — headless GameWorld scripted casters; asserts AFTER values, BEFORE values in comments; the before/after table is pasted into the PR description as the evidence artifact)
1. L30 mage, 10k MP, freeze cast every affordable tick for 2000 ticks → `enemy_freeze ≤ 300` at every tick (before: >3000 and climbing); single L20 cast from empty == 240 (spectacle preserved); single L30 cast == 300 > L14's 174.
2. L20 ghost recasts scare 5× on one L1 soldier → victim's total forced-walk ticks ≤ 375 (before: ~1800+ stacked), victim issues a self-command within 375 ticks of the last cast, and the front entry's direction tracks the latest cast.
3. L30 faerie hitting a con-0 PLAYER-CONTROLLED soldier every 13 ticks (above the 10-tick §2.6b gate window; odd roll 65) for 2000 ticks → soldier acts ≥12 consecutive ticks per thaw cycle, total actable ≥15% of ticks (before: 0); every freeze span ≤ 91. NOTE: this plan's original 5-tick script CANNOT pass against the shipped shape and was re-parameterized — sub-gate cadence relocks before the immunity arms (§2.6c residual i). Two honest-residual pins ship alongside: cadence-5 → `immunity_cycles == 0`, actable 5/500 (`sprinkle_player_victim_sub_gate_cadence_residual`); even roll 64 at cadence 13 → `immunity_cycles == 0`, actable gap ≤ cadence (`sprinkle_player_victim_even_span_residual`, §2.6c residual ii). AI-victim variant at owner L30: re-freeze only lands through the 10-tick gate window (geometric escape; assert nonzero actable ticks).
4. L20 thief 10 cloaks → `invisibility_left ≤ 350` (before ~2100); potion-450 then cloak → still 450.
5. L20 orc 10 yells vs con-0 victim → `frozen_delay ≤ 150` (before ~1100).
6. 3 simultaneous L10 bombs on one walker → per-bomb knockback entries still prepend (legacy slapstick preserved, each ≤8); damage table L20/L30/L50 == 258/277/287.
7. Heartburst at 4000 MP → pool == 600 (before 1960).

### 6.3 Switch-launder regression (WP-4)
Scare a player-controlled unit; SwitchChar away and back → the forced walk survives (count/direction), weapon was reset, leader cleared. Charm a claimed walker; run the claim path → still charmed (`real_team_num` untouched), and it expires normally via `charm_left`. Level-load still full-clears.

### 6.4 Parity, canary, calibration, coverage
- Gate after EVERY WP: `og_test_parity` 188/188, untouched (no golden regenerated, no predicate retuned, no new parity scenario). Per-WP `run_parity_diff.sh`/`diff_dumps.py` row lists in §5. Parity runs at difficulty 100 (no scaling — verified).
- Canary: `run_mutation_canary.sh --scenario <id>` for every Mutation row whose file was touched (mage 200/231/289/308, thief 69/95/171/195/214, orc 130, cleric 333/352, archmage 241/509/528, druid 78/123/172/191, weapon_family_animate 72/84/100, living 114, sim_input_handler 193/209/340/345/353, walker if touched); clean worktree; genuine-toothless stays 0; post-edit grep confirms every pinned line's text+line still matches.
- TRACE pre-audit (Kneecap graft): every new conditional TESTING-traced; full parity run must show zero firings inside goldens (merge fall-through and clamp-no-op paths excepted).
- Calibration per WP-5. Coverage: local baseline→delta ≥ 0; wipe stale `.gcda`.
- cfg clobber hazard: after any failed headless run, `git status cfg/` and restore before re-running (`git checkout cfg/`).

---

## 7. Risk register

1. **Calibration churn — certain, budgeted.** Fright merge/orc cap/cloak cap bind in pinned battles at campaign levels; merged scares keep crews engaged (levels can get HOTTER; floors may DROP). Treat movement as expected output: re-measure, repin deliberately, playtest-sweep. This is the largest verification cost, not a code risk.
2. **special_orc_1 vs thaw immunity** — the one empirical gamble, deliberately tiny: only fires if a golden re-freezes a team-0 walker within 12 ticks of thaw. TRACE probe decides; pre-approved fallback = sprinkle-only immunity (orc keeps stack-cap only).
3. **Accepted residual: L≤20 AI-vs-AI faerie stunlock** (≥21 gate protects the L20 golden; the player side is fixed at all faerie levels in PRACTICE — real faerie AI cadence sits above the gate window — but the immunity is cadence- and drain-parity-conditional, NOT a per-cycle guarantee: §2.6c residuals i/ii, pinned in the WP-4 battery). Campaign faeries ≤L7 (~2 s rolls); tower spire swarms at L6-7/L16-17 can still lock AI allies. Flag to owner — sign-off covers BOTH the AI-vs-AI residual and the two player-side immunity residuals (the original complaint was exactly the faerie stunlock); full fix = AI-victim immunity (Deferred #6, calibration re-measure + possible sprinkle-scenario respec via the sanctioned table-edit→mirror→master-recapture flow) and/or an in-window player refusal — each a parity-visible sim change, out of scope by §0.
4. **Canary discipline.** 2 text repins + strict line-count neutrality above pins in 9 pinned files; the canary is out-of-CI (silent breakage). Mandatory: per-row canary runs + pinned-text grep as exit criteria; companion mirror in the same commit as any table edit.
5. **`forced` flag assumes commands are never serialized** — WP-2 audit is a merge blocker if any save/replay path persists the queue (none known; snapshots clear, replays compare state fields).
6. **Negative frozen_delay is a semantic overload** — masked getter + header comment + reader audit (interface/text/curses, `ctf_ai.cpp:58` correct for free, `walker.cpp:1585` copy benign). Future raw-field readers are the tail risk; the comment is the mitigation.
7. **Old recorded replays diverge** above the caps (`replay.cpp:445` compares `enemy_freeze`; `frozen_delay` also compared). Expected; replays are dev tools, not goldens — document in the PR.
8. **Knee-13/20 identity floors stay long by fiat** (L13 scare 325t = 26.5 s; thief charm 375t; L20 glow 208 s). Unlowerable without scenario-spec changes + master recapture + predicate retunes — explicitly out of scope; the knee is a parity constraint, not a taste choice.
9. **MP-pool caps rest on the golden spawn-MP audit** (WP-3 exit criterion); resolution for any outlier is raising the cap, never moving a golden.
10. **`family_mage.cpp:165-189` starburst edit sits ABOVE pin 200** — the single most brittle line-neutral edit in the plan (a reflow there silently breaks the freeze canary). Review with `git diff --stat` line-count check + post-edit pin grep.
11. **OG_STATS_DIRTY_FIELD unpick** for the masked accessor must preserve dirty-bit behavior exactly (netplay delta correctness); covered by existing snapshot tests + WP-2 audit.
12. **Shared-machine flake** (root Chromium/qemu load spikes) on long canary/calibration wall-clock runs — re-run before concluding failure; all tests must pass, no "pre-existing flake" excuses.

## 8. Deferred / owner decisions / rejected (recorded so context is not lost)

Deferred (each with a revisit trigger): (1) per-caster summon-count cap (~6 live owned lifetimed summons via a pin-free `walker_specials.cpp` oblist scan) — owner never named it; druid-faerie/cleric-ghost owner-linkage unverified; calibration exposure scen4/scen7; trigger: playtest army-building complaint. (2) Explosion-funnel knockback dedupe (`force_fright` at `effect_family_bomb.cpp:71-75`, covers boulders/fire-arrows/chain/generator-x4) — trigger: bombs still "comedic" after the damage curve; carries multi-explosion golden risk + descope fallback. (3) Knee-8 ghost scare (L9-13 + tower lap-1 ghosts are the worst remaining feel moment: 250-364t) — OWNER DECISION: parity-legal (no golden ghost >L5) but relaxes the blanket L≤13 byte-identity rule to "no shipped/pinned content changes"; default = knee-13 (this spec). (4) Deterministic victim-resist texture above the knee (R = victim level*10 + con/4 on scare/charm) — pure feel upside, zero parity exposure; deferred for blast radius. (5) Per-cast freeze taper above L25 (costs the mage:200 repin). (6) AI-victim thaw immunity (see Risk 3). (7) Potion caps (pinned consumable lines, different economy). (8) StatusEffects framework — rejected NOW (protocol bump + 4 drain migrations under 188 byte-locked goldens for no player-visible gain); flip triggers: a protocol bump scheduled anyway / networked flee-status HUD demanded / 3+ new statuses planned / a golden-rebaseline event.

Rejected outright: legacy-scaling toggle (wire bump; owner said "silly" ⇒ fix); level-independent sprinkle gate (near-certain `weapon_sprinkle_emission` flip — unregenerable); `COMMAND_FLEE` type (blast radius + scare-truncation + HUD regression); setter-level frozen_delay clamp (dirty-field macro + snapshot sanitize semantics); global force_command clamp (CTF/FOLLOW collateral); busy cooldowns on no-busy specials (AI cadence in goldens/calibration); float transcendental curves; hard caps everywhere (kills progression — violates the reward guardrail); drain-site cloak cap at 736 (60 s is still silly; write-site 350 chosen); capping enemy_freeze at the game_world decrement (pin-locked + "TIME LEFT" visibility).

Estimated effort: ~16-18 files, ~250-350 LOC production + ~450 LOC tests; 3-4 focused days (day 1 WP-1/2, day 2 WP-3 + parity/canary, day 3 WP-4/5 calibration + playtests; contingency half-day for the special_orc_1 fallback). Zero protocol bumps, zero golden recaptures, 2 repins, expected deliberate floor repins in ≤6 pinned levels.

---

## 9. WP-5 calibration/playtest record (2026-07-13, measured against the WP-0 baseline at HEAD 1435f302)

Re-measured `{WESTLANDS,LONGSEASON,TOWER}_CALIBRATION_MEASURE=1` across seeds {42, 1337, 2025}:
- **Tower: zero movement** (f1 7/7/7, f5 1/4/8, f10 0/0/0, f15 0/0/1, f20 0/0/0 — byte-equal to the baseline table).
- **Long Season: only scen11 moved, UP** (7/3/7 → 7/5/7; Cold Seams carries ghosts — fright-merge class). scen8 (the predicted cloak level) did not move. No pin change (min 5 = existing pin).
- **Westlands: 10 ghost/orc levels churned, both directions** — 3: min 7→6, 19: min 5→4 (re-pinned, the only two dips below pinned minima), 11/17/22/24 within floors (17 min 5→4 vs pin 2; 22 min 1→0 vs pin 0; 24 min 2=2), 13/16/23 moved up (floors deliberately NOT raised). Every moved level contains GHOST and/or ORC (mapgen census) — the §5 expected class (fright merge + stun cap bind at all levels); every no-ghost/no-orc level re-measured identical, per the below-knee identity construction. The predicted cloak binder (thief levels) produced no movement.

Playtest sweeps (post-fix contract checks; no pre-fix campaign jsonl exists): `scripts/westlands_playtest.sh` and `scripts/longseason_playtest.sh` full sweeps (3 seeds × {4-soldier, 8-mixed}), plus the §5.11 tower harness at floors {5, 10, 15} × 3 seeds × both rosters, diffed against the pre-fix tower jsonl (captured 10:31 the same day, before the first WP edit landed 18:54). Findings: (a) tower wipe windows at f5/f10 are IDENTICAL to pre-fix at census granularity for all 12 baseline keys, and f15 (Undercroft, 20% ghosts) wipes the pessimistic stand-in at 300-600 both rosters — the crew dies to raw damage at curve level, so the scare/stun runaway was never its binding constraint; no clearability regression, and no measurable improvement either at these floors. (b) Every ghost/orc campaign level where the merge/cap binds stays clearable or engaged for the 8-mixed stand-in (W11 2/3, W13 3/3, W17 3/3, LS11 2/3 with 5-7 survivors); the war/0-floor levels remain stand-in losses by design. The player-facing improvement (thaw immunity, fright merge under manual play) is pinned by WP-4's battery, not by these AI sweeps.