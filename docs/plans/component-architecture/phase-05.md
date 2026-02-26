# Phase 5: Decouple SaveData from Gameplay

**Part of:** [Component Architecture Plan](README.md)
**Depends on:** [Phase 4](phase-04.md)
**Followed by:** [Phase 6](phase-06.md)
**Key types:** [GameWorld](key-types.md#gameworld-new--gameplay-component), [GameplayContext](key-types.md#gameplaycontext-new--gameplay-component)

---

## Goal

Entity code stops referencing `SaveData`. Gameplay is save-agnostic.
`current_game` (introduced in Phase 4) provides the access path for
replacements.

## Current `sim_save` Usages in Entity Code

| Usage | File | Replacement |
|---|---|---|
| `sim_save->m_score[team_num] += N` | `walker_combat.cpp:287,315` | `current_game->world->m_score[team_num] += N` |
| `sim_save->m_score[team_num] += N` | `treasure_family_valuables.cpp:33,45,57` | Same |
| `sim_save->allied_mode` | `walker.cpp:1531,1587` | `current_game->world->allied_mode` |
| `sim_save->is_level_completed(n)` | `treasure_family_navigation.cpp:76,77` | `current_game->world->completed_levels.count(n)` |
| `sim_save->scen_num` | `treasure_family_navigation.cpp:77,106` | `current_game->world->current_scenario` |
| `sim_save->load("save0")` | `treasure_family_navigation.cpp:103` | Emit `WithdrawToLevel` event |
| `sim_save->save("save0")` | `treasure_family_navigation.cpp:110` | Emit `WithdrawToLevel` event |

## Layering Violations in `treasure_family_navigation.cpp` (also fix)

| Violation | Replacement |
|---|---|
| `yes_or_no_prompt()` — UI call from entity code | Emit `RequestExitConfirmation` event; outer layer shows prompt and handles transition |
| `clear_keyboard()` — input call from entity code | Remove (becomes unnecessary once prompt is event-driven) |

## `sim_config` Removal

Also in this phase: Remove `sim_config` pointer from `SimEntity`. The
`cfg_store` effect settings (`attack_lunge`, `hit_recoil`, `damage_numbers`,
`hit_flash`, `hit_anim`, `heal_numbers`) don't affect `worldx_`/`worldy_` or
damage calculations. **Migration:** Remove the `sim_config->is_on()` gates
from entity code — sim code unconditionally sets `attack_lunge`, `hit_recoil`,
pushes `DamageNumber` entries, etc. The interface layer (`walker_draw.cpp`
etc.) checks `cfg.is_on()` before reading these fields for display. This
means the fields accumulate even when visually disabled, which is harmless.

### Additional `sim_config` Call Sites

| Usage | File | Migration |
|---|---|---|
| `sim_config->is_on("effects", "heal_numbers")` | `family_orc.cpp:94` | Remove gate — set heal number unconditionally |
| `sim_config->is_on("effects", "heal_numbers")` | `family_cleric.cpp:123` | Remove gate — set heal number unconditionally |

## Navigation Treasure Redesign

The hardest part of this phase. The
current EXIT treasure behavior is a blocking interaction: entity code calls a UI
prompt and waits for the answer.

**New model — deferred action queue:** The treasure entity is a dumb sensor. It
detects "player stepped on exit" and emits an event. The outer layer owns the
entire exit orchestration flow. Entity code never re-enters for this decision.

1. Entity detects player on exit tile
2. Entity emits `RequestExitConfirmation { dest_level, prompt_text }` event
3. Tick finishes normally — treasure's job is done
4. Outer layer (platform) drains events, sees `RequestExitConfirmation`
5. Outer layer shows the prompt via interface layer
6. Player confirms → outer layer handles the level transition directly
   (it already owns the game loop, SaveData, level loading)
7. Player denies → outer layer does nothing, next tick proceeds normally

**Why this approach:** No state machine on the entity, no "pending response"
field on GameWorld, no multi-tick continuation protocol. The exit/withdrawal
logic (`sim_save->load/save`) was already an outer-layer concern shoved into
entity code — this moves it where it belongs.

### Behavior Change Note

In the current code, the withdrawal path immediately
kills all living entities mid-tick. In the new model, the tick finishes normally
and the outer layer handles the transition afterward. This means other entities
get one extra tick of actions before the level is discarded.

### Withdrawal Edge Case

During that extra tick, enemies can damage the player,
the player can die, and score can change — all in a tick whose results are about
to be discarded. The save happens in the outer layer AFTER the tick, so those
spurious changes would be persisted. **Mitigation:** When `tick()` sees a
`withdraw_requested` flag on GameWorld (set by the `WithdrawToLevel` event
emission), it skips entity updates for the remainder of the current tick. This
preserves the current behavior of "nothing happens after withdrawal" without
requiring mid-tick save I/O. The outer layer checks `withdraw_requested` after
tick, performs the save, and loads the target level.

### Multiple Exit Events in One Tick

If two players step on different exit
tiles in the same tick, or one player touches two exits, multiple
`RequestExitConfirmation` events could be emitted. The outer layer processes
only the first `RequestExitConfirmation` per tick and discards duplicates.
This matches the current behavior where the first `yes_or_no_prompt()` call
blocks and preempts any subsequent exit interactions.

## Steps

1. Add score accumulators, `allied_mode`, `current_scenario`, `completed_levels`
   to GameWorld (may already be done in Phase 3)
2. Replace `sim_save->m_score` and `sim_save->allied_mode` references in entity
   code with `current_game->world->` access (mechanical find-and-replace)
3. Remove `sim_config->is_on()` gates from entity code (sim sets fields
   unconditionally); add `cfg.is_on()` checks in interface layer before
   reading those fields for display (`walker_draw.cpp` etc.)
4. Remove `sim_config` pointer from `SimEntity`
5. Redesign exit treasure as event-driven (the hard part)
6. Remove `sim_save` pointer from `SimEntity`
7. Update outer layer (screen::act / platform code) to populate GameWorld from
   SaveData before level start and read results back after level end

## Risk

Medium-High — the exit treasure redesign requires careful thought.
Score/allied_mode migration is trivial. `sim_config` removal is mechanical.

## Accepted Deviation: hit_anim

`hit_anim` uses `GameWorld::create_hit_effects` (bool) instead of unconditional
entity creation. Unlike the other 5 display-only effects, hit_anim spawns actual
`Order::FX` entities with memory/CPU cost. The flag is set by the runtime layer
(`screen.cpp`), not by entity code — no sim_config coupling exists in entity code.
This is accepted as a pragmatic adaptation.

## Testing

Full ctest. Add specific tests for exit and withdrawal behavior.
