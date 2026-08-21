---
name: openglad-networking
description: Networked-multiplayer engineering rules — protocol/snapshot/replay/save version bumps and their test-pin fallout, adding a synchronized sim setting, headless-server vs render divergence, snapshot seeding, and transport/mirror gotchas. Use whenever a task touches net_transport, world_snapshot, GameServer/GameClient, LobbyServer, save/replay formats, adds a lobby-negotiated setting, or debugs desyncs, disconnects, "works locally but mirrors are wrong", or "script/hook doesn't run in real play".
---

# OpenGlad networking engineering

The authoritative sim runs headless in `GameServer`; every display (SDL,
text, curses, web) is a mirror fed by snapshots. `docs/ARCHITECTURE.md`
covers the architecture; this file is the traps.

## Version bumps come as ONE coordinated commit

A wire or snapshot change bumps `kNetworkProtocolVersion`
(include/openglad/gameplay/net_transport.h), `kSnapshotFormatVersion`, and
the derived `kReplayFormatVersion` together, plus every literal pin:

- FIVE wire-byte test pins break on a protocol bump — in
  `tests/unit/test_net_transport.cpp` (protocol + min_protocol bytes) and
  `tests/unit/test_input_state_net.cpp`. The "wrong_version" rejection
  test must use a byte != the new current version. The replay
  rejection-test literal repins too.
- Payload-offset pins (test_ctf_snapshot and friends) shift when fields
  are inserted. Serialize new world scalars AFTER existing blocks where
  possible — that choice alone preserved the CTF offset pins once.
  Beware silently-stale offsets: two lobby offsets were wrong for months
  and only surfaced at the next bump; re-derive, don't just +N.
- `find_first_snapshot_difference` must learn any new field, or unknown
  fields masquerade as snapshot_hash divergence.
- Save-format (`.gtl`) bumps are separate but often ride along; legacy
  saves must keep loading (read-side defaults, never write-side).

## Adding a synchronized sim setting (the template)

Follow the respawn/permadeath/generator-rate template exactly: SaveData
field → LobbySettings → InitialSetup/world mirror applied in BOTH sync
twins → snapshot + replay compare. Requirements that were each a review
finding:

- Defaults byte-identical: the new setting at its default must produce a
  byte-identical sim (parity-safe by construction).
- RNG-adjacent knobs scale the COMPARISON, not the draw bound
  (`draw_a*rate > draw_b*100` in uint64) so identical draws happen at
  default and the RNG stream never moves.
- Networked consumers read the SESSION-negotiated flag copied into the
  peer save, never the on-disk save0 flag (stale by definition).
- Sim-side clamp the value (crafted saves/snapshots reach it unchecked).
- End-of-level actions (revive-all, flushes) must fire the SAME TICK the
  end latches — the server stops ticking afterward; wire them into every
  exit shape (win, timeout, exit portal, withdraw, abort — there are
  five, not four).

## Headless sim vs render (the drawcycle class of bug)

If sim logic reads a field, check WHERE it is written: a field only the
render path writes freezes on the headless server (drawcycle froze and
hung boomerangs, stopped shield orbits, gave the archmage full map view).
`scripts/check_render_no_sim_writes.sh` (build dep of og_interface) fails
if `src/interface/render/` writes any network-synced entity field — keep
it green rather than exempting it. The parity companion must mirror any
render-bump a sim behavior depends on (see openglad-parity).

## Snapshot seeding and the hook latch

Three construction paths build a world — direct load+tick, snapshot seed,
transport-shadow install — and they do NOT share hook guarantees. The
canonical bug: level `on_load` never ran in real SDL play because the
tick-0 snapshot seed pre-closed the `last_level_id_` latch, while every
unit test used the direct path and stayed green. `apply_snapshot` claims
the latch only for mid-level snapshots (`level_tick_count > 0`); a tick-0
seed calls `reset_level_progress()`. Rules:

- Any "script/hook works in tests but not in the game" report: check the
  snapshot-seed path FIRST.
- Every new level-hook kind gets a snapshot-seeded dispatch test next to
  the direct-tick one (capture keyframe → reset → apply → tick → assert
  the side effect).
- Dormant (spawn-delayed) walkers are excluded from snapshot capture;
  apply_snapshot must let dormant-absent survive reconciliation and let
  present-in-snapshot clear dormancy.

## Transport and session gotchas

- **In-process peer 1 is the DISPLAY client**, not a background seat
  (peer ids start at 1). "peer N is not connected" on a local send names
  that client's own connection.
- **Pause/menu suspension starves input freshness**: while a blocking
  menu holds the loop, forced keyframes → hash-check replies suppress
  client heartbeats, and every peer's `last_received_input_ms` freezes.
  Clearing the suspension must restamp freshness
  (`restamp_input_freshness`), and local runtime clients are
  `mark_peer_local` — never timeout-disconnected. Any new blocking UI
  over a live session must pump the transport and respect this.
- **obmap context swap**: with a server world and a co-located mirror in
  one process, set `current_game` to the server ctx around
  `server->step()` and the client ctx around `poll_messages()`, and load
  each world under its own ctx — or mirror walkers register in the
  server obmap and the avatar can't move.
- **EndGame is latched, not stored in the mirror world**: in
  return-to-lobby mode the server never sets its own `world.end`, so
  delta snapshots re-serialize `end=0` and clobber a mirror-stored flag.
  Headless clients latch a session-level PendingEnd.
- **Mirror family clamp**: snapshot apply must clamp wire family to
  NUM_FAMILY_SLOTS, not NUM_FAMILIES — pack families above the core
  range otherwise stamp to 0 on mirrors only. `openglad_text` has no
  GameClient mirror and CANNOT see mirror bugs; verify mirrors with a
  client that has one.
- `GameServer::bind_player` does NOT broadcast ControlChange — follow
  with `set_player_control` or the new viewport shows nothing.
- Mock wall clocks: after `set_wall_clock_ms_source`, send one
  input/tick so freshness stamps land on the mock timeline, or
  stale-guards silently skip the repro.

## Wire-data hygiene

- CI sets `VALIDATE_SERIALIZATION=ON` (test/drift/asan/tsan lanes); it
  round-trips every typed message and requires equality — lossy
  read-side clamps pass a default local build and fail CI
  deterministically. Reproduce with that flag or the ci-asan preset.
- Narrow wire types alias: an int8 guard like `>= 256` is tautological
  (clang -Werror in the TSan lane catches it; GCC ci-test does not), and
  bytes 128–255 read negative. Sweep new guard code on wire paths with
  clang syntax-only.
- Team/identity bytes above the classic 0–3 range (e.g. the FFA band
  16–31) break hidden assumptions: revive paths that only restore teams
  <4, packed-slot decodes that overflow, palette lookups. Grep for `< 4`
  and `NUM_TEAMS`-shaped literals when widening any identity byte.
