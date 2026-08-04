-- modes lib: mode_items — deterministic respawning pickups: 30-tick cadence over the manifest item_pads, per-level interval, mode-var rotation cursor, ONE spawn per firing (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
--
-- run(row, cursor_slot, last_slot, default_interval) is called from an
-- adopting impl's on_mode_tick (post-death-scan, pre-HUD) with the level's
-- manifest row. All state lives in the caller's two mode vars (R6: no
-- module state): the pad rotation cursor and the last-spawn tick (seeded
-- to the init tick in on_mode_init). Census, occupancy and blocking
-- re-derive from the world every firing, so the pass is a pure function
-- of replicated sim state — no RNG, mirrors receive spawned items via
-- snapshot. Only the four respawnable families ever spawn (food + the
-- three shipped potions; the mapgen emitter refuses anything else on a
-- pad), and a pad naming an unknown family is inert, never a fallback.

local C = og.C

local CADENCE = 30 -- scan every 2.5 s once the interval has elapsed

-- The respawnable families, index-aligned 1..4 with the manifest's family
-- strings; BYTE_INDEX maps wire bytes back for the census walk.
local BYTES = {
  assert(og.family_id("treasure", "core:drumstick")),
  assert(og.family_id("treasure", "core:magic_potion")),
  assert(og.family_id("treasure", "core:invis_potion")),
  assert(og.family_id("treasure", "core:speed_potion")),
}
local NAME_INDEX = {
  drumstick = 1,
  magic_potion = 2,
  invis_potion = 3,
  speed_potion = 4,
}
local BYTE_INDEX = {}
BYTE_INDEX[BYTES[1]] = 1
BYTE_INDEX[BYTES[2]] = 2
BYTE_INDEX[BYTES[3]] = 3
BYTE_INDEX[BYTES[4]] = 4

-- Tile key of a pixel position (pads use their center; items their
-- top-left — both land on the owning tile, the same convention the
-- mapgen self-check pins the pad multiset with).
local function tile_key(x, y)
  return og.div(x, 16) * 4096 + og.div(y, 16)
end

local function tile_occupied(occupied, key)
  for k = 1, #occupied do
    if occupied[k] == key then
      return true
    end
  end
  return false
end

-- The spawn-camp deny: a live living or generator overlapping the pad's
-- 16x16 item box blocks the spawn. Same rectangle test as the engine's
-- spawn_spot_blocked_by (respawn.cpp) — og.spawn_spot_clear needs the
-- walker being placed, which does not exist until a pad picks its
-- family, so the test is applied here over the oblist instead.
local function pad_blocked(blockers, bx, by)
  for k = 1, #blockers do
    local w = blockers[k]
    if w:xpos() + w:sizex() > bx then
      if w:xpos() < bx + 16 then
        if w:ypos() + w:sizey() > by then
          if w:ypos() < by + 16 then
            return true
          end
        end
      end
    end
  end
  return false
end

-- One respawner pass. Answers true when an item spawned (at most one per
-- firing; the interval clock restarts only then).
local function run(row, cursor_slot, last_slot, default_interval)
  if row == nil then
    return false
  end
  local pads = row.item_pads
  if pads == nil then
    return false
  end
  local count = #pads
  if count == 0 then
    return false
  end
  if og.mod(og.world_tick(), CADENCE) ~= 0 then
    return false
  end
  local interval = row.item_interval
  if interval == nil then
    interval = default_interval
  end
  if interval <= 0 then
    interval = default_interval
  end
  if og.world_tick() - og.mode_get(last_slot) < interval then
    return false
  end

  -- Census: live respawnable treasures per family, plus their tiles for
  -- the pad-occupancy check.
  local live = { 0, 0, 0, 0 }
  local occupied = {}
  local fxlist = og.fxlist()
  for k = 1, #fxlist do
    local e = fxlist[k]
    if e:dead() == 0 then
      if e:order() == C.ORDER_TREASURE then
        local idx = BYTE_INDEX[e:family()]
        if idx ~= nil then
          live[idx] = live[idx] + 1
          occupied[#occupied + 1] = tile_key(e:xpos(), e:ypos())
        end
      end
    end
  end
  -- Authored target per family: a pure function of the manifest row.
  local target = { 0, 0, 0, 0 }
  for k = 1, count do
    local idx = NAME_INDEX[pads[k].family]
    if idx ~= nil then
      target[idx] = target[idx] + 1
    end
  end
  local deficit = false
  for i = 1, 4 do
    if live[i] < target[i] then
      deficit = true
    end
  end
  if not deficit then
    return false
  end

  local blockers = {}
  local obs = og.oblist()
  for k = 1, #obs do
    local w = obs[k]
    if w:dead() == 0 then
      local order = w:order()
      local blocking = order == C.ORDER_LIVING
      if not blocking then
        blocking = order == C.ORDER_GENERATOR
      end
      if blocking then
        blockers[#blockers + 1] = w
      end
    end
  end

  -- Walk the pad list at most once around from the rotation cursor; the
  -- first eligible pad takes ONE item and the cursor advances past it.
  local cursor = og.mode_get(cursor_slot)
  for k = 0, count - 1 do
    local offset = og.mod(cursor + k, count)
    local pad = pads[offset + 1]
    local idx = NAME_INDEX[pad.family]
    local eligible = idx ~= nil
    if eligible then
      eligible = live[idx] < target[idx]
    end
    if eligible then
      eligible = not tile_occupied(occupied, tile_key(pad.x, pad.y))
    end
    if eligible then
      eligible = not pad_blocked(blockers, pad.x - 8, pad.y - 8)
    end
    if eligible then
      -- add_fx_ob, NOT add_ob: treasures never act, and the fx list is
      -- where authored treasures live and where the engine walk-on eat
      -- finds them (matches the mapgen's place_at).
      local item = og.add_fx_ob("treasure", BYTES[idx])
      if item ~= nil then
        item:setxy(pad.x - 8, pad.y - 8)
        item:set_team_num(0)
        item:set_real_team_num(0)
      end
      og.mode_set(cursor_slot, og.mod(offset + 1, count))
      og.mode_set(last_slot, og.world_tick())
      return true
    end
  end
  return false
end

return {
  CADENCE = CADENCE,
  run = run,
}
