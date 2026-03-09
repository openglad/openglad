# Phase 18: Client-Side Visual Interpolation

> **See also:** [Context (Sim Tick Rate)](docs/plans/networking/common/context.md) | [Phase 14 (GameClient)](phase-14-server-client.md) | [Verification Strategy](docs/plans/networking/common/verification-strategy.md)

At default speed (~12 sim ticks/sec), entities update position every ~83ms. On a 60fps display, this produces visible teleporting between positions. Linear interpolation between the last two received snapshot positions makes movement smooth at render time.

**Changes:**
- Client stores the two most recent entity positions: `prev_pos` (from tick N-1) and `curr_pos` (from tick N)
- Each render frame, compute `alpha = time_since_last_tick / tick_interval` (0.0 to 1.0)
- Render entities at `lerp(prev_pos, curr_pos, alpha)` for `worldx_`/`worldy_` and `xpos`/`ypos`
- Interpolation applies ONLY to position. Other fields (frame, ani_type, action, curdir) snap to the latest value — interpolating discrete animation state causes glitches.
- When an entity spawns (first tick it appears), `prev_pos = curr_pos` (no interpolation, snap to position)
- When an entity dies, stop rendering immediately (don't interpolate toward a dead position)
- Implementation lives in the render path, not the simulation. The GameClient stores interpolation state per-entity alongside the WorldSnapshot.
- **Camera/viewport interpolation:** The viewscreen camera follows the player entity. The camera must track the *interpolated* position, not the snapped position — otherwise smooth entity movement is undermined by jerky camera. Apply the same lerp alpha to the camera's follow target.

**Verify:** Visual smoothness at 60fps with 12 tick/sec sim rate. Entities don't overshoot or rubber-band. Spawning entities appear at correct position (no lerp from origin). Dying entities disappear cleanly.
