# Gameplay Fixes From Classic

This document tracks parity issues where the comparison against the
`e761b745d91983f4ba9803b670bae5208dbb8717` classic baseline exposed visible
gameplay bugs. It intentionally excludes scaffolding-only fixes such as parity
harness changes, recording changes, golden regeneration mechanics, and
out-of-bounds/sentinel hardening.

`wip/parity-with-classic` means the modern gameplay branch was changed to match
classic behavior. `parity-companion` means the classic companion recorder was
changed because the e761 behavior was judged to be a bug that should not be
preserved.

| Issue | Side fixed | Visible impact | Why that side was fixed |
|---|---|---|---|
| Small slime no-ranged flag | `parity-companion` | Small slimes could incorrectly fire free blob projectiles at range. They should still be able to melee, but should not launch ranged blobs. | e761 had a loader typo: it compared `order` to `FAMILY_SMALL_SLIME` instead of comparing `family`. The surrounding setup and modern descriptor both clearly intended `BIT_NO_RANGED` for small slimes. |
| Giant skeleton bloodspot | `parity-companion` | Giant skeletons left a bloodspot/death stain when dying. They should behave like the other undead/no-bloodspot enemies. | e761 omitted `FAMILY_GIANT_SKELETON` from the same no-stain death class as skeleton/ghost/tower. The modern descriptor already had `leaves_bloodspot = false`, which matches the intended undead behavior. |
| Expired poison cloud still acting | `parity-companion` | A cloud whose lifetime had expired could still attack and schedule movement during the same tick it died. | Dead effects continuing to act is a classic bug. The correct behavior is to return immediately after marking the cloud dead and running `death()`. |
| Poison cloud random-walk underflow | `parity-companion` | Poison cloud drift was biased because `random(3) - 1` underflowed when `random(3)` returned zero. Negative directions collapsed or became implementation-dependent before the walk command was queued. | The intended random walk is `-1`, `0`, or `1` per axis, excluding `0,0`. Casting to signed before subtracting restores the obvious intended behavior. |
| Slime/weapon auto-attack false positive | `parity-companion` | Ordinary projectiles could trigger `living::collide()` as if they were attackable targets. In the slime case, a soldier's thrown knife (`ORDER_WEAPON`, `FAMILY_KNIFE == 0`) falsely matched generator `FAMILY_TENT == 0`, causing the slime to start an attack response to the projectile itself. | e761 compared raw family IDs without considering object order. Family IDs are reused across orders, so the predicate must be order-aware: living and generators are attackable, while only explicitly attackable weapon objects such as glow, tree, and door should be auto-attackable. |
| Druid faerie summon level | `wip/parity-with-classic` | Druid-summoned faeries inherited the druid's level in the modern branch, changing their stats and combat strength. | e761 did not copy the summoner's level for this summon path. The modern helper introduced extra state, so the branch was changed to create the faerie and set only the classic fields. |
| AI `direct_walk()` fire check | `wip/parity-with-classic` | Ranged AI could walk instead of shooting, or shoot only after different turning, because the branch normalized the target vector before checking whether it could fire. | e761 passes the raw target vector into `fire_check()`, preserving slope information for facing/aiming. Normalized deltas are appropriate for movement steps, not for the fire-vs-walk decision. |
| Facing/turn parity | `wip/parity-with-classic` | Actors could rotate along a different path, affecting facing frames, `lastx`/`lasty`, attack direction, and firing timing. | The branch defensively clamped invalid/transient directions to `FACE_UP`. Classic uses the raw direction value in its modulo turn arithmetic, so the clamp changed behavior. |
| Positional death sounds | `wip/parity-with-classic` | Death sounds could play when classic would consider the death off-screen or otherwise not audible through positional sound visibility. | e761 gates positional death sounds by visibility. The branch emitted too broadly, so the branch sound path was restricted to classic visibility rules. |
| Combat score/XP timing | `wip/parity-with-classic` | Score and XP changes could occur at different ticks or be attributed differently from classic combat. | Modern combat refactoring moved reward sequencing away from classic behavior. The branch was changed so hit rewards, owner/weapon attribution, and melee/ranged scoring occur in the classic locations. |
