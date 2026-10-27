-- The Castle of Gold (scen 2) — the Imaginations dream-log's level script,
-- shipped inside builtin/imaginations.glad as the embedded pack
-- imaginations.dreams (it mounts and unmounts with the campaign).
--
-- The castle is OURS and a faerie war rages around it. What the level data
-- cannot say, the script does:
--   * gold wakes ghosts: every gold bar a keeper takes from the castle's
--     strongrooms wakes six ghosts for our side at the Ghost Tent, the
--     last bar seven (the core:gold_bar on_eat override below — it keeps
--     the classic banking on every level),
--   * the twelve keys: the gate opens for no faerie until the faeries
--     still alive carry all twelve shore keys between them; a bearer's
--     death takes its keys out of the count (the core:key on_eat override
--     plus a tick safety net),
--   * nobody teleports into the castle while the gate stands: a faerie
--     that blinks or swaps inside is thrown out at the gate, and a mage
--     of ours that teleports inside is put back at the gate too,
--   * a faerie grinding at a wall stands down onto a post; the engine's
--     own sight test re-wakes it.
--
-- Determinism cookbook (docs/lua-classpacks-design.md §3): no mutable sim
-- state in locals or upvalues (R6) — every answer re-derives from the
-- world on each dispatch. No randomness is drawn.

local C = og.C
local posts = og.use("dream_posts")

local LEVEL = 2

local FAM_GHOST = assert(og.family_id("living", "core:ghost"))
local FAM_DOOR = assert(og.family_id("weapon", "core:door"))
local FAM_GOLD = assert(og.family_id("treasure", "core:gold_bar"))
local FX_FLASH = assert(og.family_id("fx", "core:flash"))

-- The castle interior in pixels: the court and its four strongrooms, plus
-- the gate tunnel inside the gate doors (tiles x 20..43, y 17..42).
local CASTLE_X0 = 20 * C.GRID_SIZE
local CASTLE_Y0 = 17 * C.GRID_SIZE
local CASTLE_X1 = 43 * C.GRID_SIZE + 15
local CASTLE_Y1 = 42 * C.GRID_SIZE + 15

-- The castle ring and the counting house, widened by two tiles: the only
-- walls on the map, so the only places a flying faerie can jam.
local RING_X0 = 15 * C.GRID_SIZE
local RING_Y0 = 12 * C.GRID_SIZE
local RING_X1 = 48 * C.GRID_SIZE + 15
local RING_Y1 = 45 * C.GRID_SIZE + 15
local HOUSE_X0 = 52 * C.GRID_SIZE
local HOUSE_Y0 = 6 * C.GRID_SIZE
local HOUSE_X1 = 62 * C.GRID_SIZE + 15
local HOUSE_Y1 = 15 * C.GRID_SIZE + 15

-- The gate: three doors on tile row 43, x 30..32.
local GATE_Y = 43 * C.GRID_SIZE
local GATE_X0 = 30 * C.GRID_SIZE
local GATE_X1 = 32 * C.GRID_SIZE

-- The Ghost Tent's pad, where the ghosts are born (the tent itself stands
-- one row north; its own spawns are stillborn, see isle.lua).
local TENT_X = 52 * C.GRID_SIZE
local TENT_Y = 33 * C.GRID_SIZE

-- Where the castle puts anyone it throws out: on the causeway, south of
-- the gate. First clear candidate wins.
local LANDINGS = {
  { 31, 45 }, { 29, 46 }, { 33, 46 }, { 31, 48 },
}

local SHORE_KEYS = 0x1FFE  -- key bits 1..12
local KEYS_NEEDED = 12
local CASTLE_KEY = 13      -- the keeper's strongroom key, not one of the twelve
local GHOST_CAP = 110      -- og.living_count() ceiling for new ghosts

local function in_rect(ob, x0, y0, x1, y1)
  local x = ob:xpos()
  local y = ob:ypos()
  if x < x0 or x > x1 then
    return false
  end
  return y >= y0 and y <= y1
end

local function in_castle(ob)
  return in_rect(ob, CASTLE_X0, CASTLE_Y0, CASTLE_X1, CASTLE_Y1)
end

local function by_a_wall(ob)
  if in_rect(ob, RING_X0, RING_Y0, RING_X1, RING_Y1) then
    return true
  end
  return in_rect(ob, HOUSE_X0, HOUSE_Y0, HOUSE_X1, HOUSE_Y1)
end

-- The gate doors still standing, from the weapon list.
local function gate_doors()
  local doors = {}
  local weaps = og.weaplist()
  for i = 1, #weaps do
    local d = weaps[i]
    if d:family() == FAM_DOOR
        and d:dead() == 0
        and d:ypos() == GATE_Y
        and d:xpos() >= GATE_X0
        and d:xpos() <= GATE_X1 then
      doors[#doors + 1] = d
    end
  end
  return doors
end

local function gate_standing()
  return #gate_doors() > 0
end

-- The shore keys the living faeries carry between them.
local function faerie_key_union()
  local union = 0
  local obs = og.oblist()
  for i = 1, #obs do
    local ob = obs[i]
    if ob:order() == C.ORDER_LIVING
        and ob:team_num() ~= 0
        and ob:dead() == 0 then
      union = union | og.trunc(ob:keys())
    end
  end
  return union & SHORE_KEYS
end

local function popcount12(mask)
  local count = 0
  for i = 1, KEYS_NEEDED do
    if (mask & (1 << i)) ~= 0 then
      count = count + 1
    end
  end
  return count
end

-- Gold bars still lying in the castle, not counting `except`.
local function castle_gold_left(except)
  local skip = og.entity_id(except)
  local left = 0
  local fx = og.fxlist()
  for i = 1, #fx do
    local ob = fx[i]
    if ob:order() == C.ORDER_TREASURE
        and ob:family() == FAM_GOLD
        and ob:dead() == 0
        and og.entity_id(ob) ~= skip
        and in_castle(ob) then
      left = left + 1
    end
  end
  return left
end

local function flash_at(x, y)
  local f = og.add_ob("fx", FX_FLASH)
  if f == nil then
    return
  end
  f.ani_type = C.ANI_EXPAND_8
  f:set_floor(0)
  f:setxy(x, y)
end

local function landing_for(ent)
  for i = 1, #LANDINGS do
    local px = LANDINGS[i][1] * C.GRID_SIZE
    local py = LANDINGS[i][2] * C.GRID_SIZE
    if og.spawn_spot_clear(ent, px, py, 0) then
      return px, py
    end
  end
  return LANDINGS[1][1] * C.GRID_SIZE, LANDINGS[1][2] * C.GRID_SIZE
end

-- One allied ghost at the tent: no lifetime and no owner (an owned ghost
-- dies with its owner; an ownerless one with a lifetime dies at once).
-- Ghosts are ethereal flyers, so they float out through the walls into
-- the war; they stay on the hunt and are never re-posted.
local function spawn_ghost(level, name)
  if og.living_count() >= GHOST_CAP then
    return nil
  end
  local g = og.add_ob("living", FAM_GHOST)
  if g == nil then
    return nil
  end
  g:set_team_num(0)
  g:set_real_team_num(0)
  g:s_set_level(level)
  g:set_difficulty(level)
  g:s_set_name(name)
  g:set_floor(0)
  for dy = -2, 2 do
    for dx = -2, 2 do
      local px = TENT_X + dx * C.GRID_SIZE
      local py = TENT_Y + dy * C.GRID_SIZE
      if og.spawn_spot_clear(g, px, py, 0) then
        g:setxy(px, py)
        return g
      end
    end
  end
  g:setxy(TENT_X, TENT_Y)
  return g
end

local function burst(n, level, name)
  for i = 1, n do
    spawn_ghost(level, name)
  end
  flash_at(TENT_X, TENT_Y)
  og.emit_sound(C.SOUND_SPARKLE)
end

-- A keeper is one of the crew's own characters (a saved hero or a
-- player-driven walker) — never an allied ghost, so ghosts cannot wake
-- more ghosts.
local function is_keeper(eater)
  if eater:has_guy() then
    return true
  end
  return eater:act_type() == C.ACT_CONTROL
end

-- core:gold_bar on_eat, campaign-wide while this pack is mounted. The
-- classic body (packs/core/lib/treasure_valuables.lua) banks for team 0
-- or any roster character on every level; scen 2's castle bars belong to
-- the keepers alone and wake the ghosts.
local function gold_bar_on_eat(self, eater)
  local banks = eater.team == 0
  if not banks then
    banks = eater:has_guy()
  end
  if not banks then
    return true
  end
  local castle_bar = false
  if og.level_id() == LEVEL then
    castle_bar = in_castle(self)
  end
  if castle_bar and not is_keeper(eater) then
    return true  -- an allied ghost floats over the gold and leaves it
  end
  og.award_score(eater.team, og.tuning(self).score_per_level * self.level)
  self.dead = 1
  og.emit_sound(C.SOUND_MONEY)
  if not castle_bar then
    return true  -- the counting house, and every other level, bank classic
  end
  flash_at(self:xpos(), self:ypos())
  local left = castle_gold_left(self)
  if left == 0 then
    burst(7, 2, "Crown Ghost")
    og.emit_notification("The last gold! SEVEN ghosts rise.", 100)
    og.emit_notification("The castle is ours. Finish them!", 100)
    og.emit_sound(C.SOUND_ROAR)
    return true
  end
  burst(6, 1, "Gold Ghost")
  og.emit_notification(string.format("Six ghosts rise! %d gold left.", left),
                       80)
  return true
end

-- The gate falls when the living faeries carry all twelve shore keys.
local function drop_gate_if_twelve(held)
  if held < KEYS_NEEDED then
    return
  end
  local doors = gate_doors()
  if #doors == 0 then
    return
  end
  for i = 1, #doors do
    local d = doors[i]
    d:set_dead(1)
    d:death()
  end
  og.emit_sound(C.SOUND_EXPLODE)
  og.emit_notification("Twelve keys! The gate falls!", 100)
  og.emit_notification("Hold the hall. Ghosts, to me!", 100)
end

-- The crew's pickup line: on scen 2 the castle key is named, so the HUD
-- never counts a thirteenth key while the dream counts twelve; every other
-- key keeps the classic line.
local function key_pickup_line(who, level)
  if og.level_id() ~= LEVEL then
    return string.format("%s picks up key %d", who, level)
  end
  if level ~= CASTLE_KEY then
    return string.format("%s picks up key %d", who, level)
  end
  return string.format("%s takes the castle key.", who)
end

-- core:key on_eat, campaign-wide while this pack is mounted: the classic
-- body (one key bit in the eater's own mask, the team-0 broadcast, here
-- for the crew's own characters only), then scen 2's count of the keys the
-- faeries carry.
local function key_on_eat(self, eater)
  local level = self.level
  local shift = og.clamp(level, 0, 30)
  local key_mask = 1 << shift
  if (eater:keys() & key_mask) ~= 0 then
    return true  -- re-touching a key you hold is silent
  end
  eater:set_keys(eater:keys() | key_mask)
  local who
  if eater:has_guy() then
    who = eater:g_name()
  else
    who = eater:s_name()
  end
  -- The classic pickup line is for the crew's own characters: an allied
  -- ghost sweeping the shore holds the key (so no faerie can carry it)
  -- without a broadcast.
  local announce = false
  if eater.team == 0 then
    announce = is_keeper(eater)
  end
  if announce then
    og.emit_notification(key_pickup_line(who, level))
    og.emit_sound(C.SOUND_MONEY)
  end
  if og.level_id() ~= LEVEL then
    return true
  end
  if eater:team_num() == 0 then
    return true
  end
  local held = popcount12(faerie_key_union())
  og.emit_notification(string.format("The faeries hold %d of 12 keys!",
                                     held), 80)
  drop_gate_if_twelve(held)
  return true
end

-- Throw one walker out of the castle, onto the causeway.
local function throw_out(ob, message)
  flash_at(ob:xpos(), ob:ypos())
  local px, py = landing_for(ob)
  ob:setxy(px, py)
  flash_at(px, py)
  og.emit_sound(C.SOUND_TELEPORT)
  og.emit_notification(message, 60)
end

-- Nobody teleports into the castle while the gate stands. A hostile in
-- the court got there by blink, swap or pad (the walls and the gate stop
-- everything else); one of ours mid-teleport-landing came by spell.
local function guard_the_castle()
  local obs = og.oblist()
  for i = 1, #obs do
    local ob = obs[i]
    if ob:order() == C.ORDER_LIVING
        and ob:dead() == 0
        and not ob:dormant()
        and in_castle(ob) then
      if ob:team_num() ~= 0 then
        throw_out(ob, "The castle throws a faerie out!")
      elseif ob:ani_type() == C.ANI_TELE_IN then
        throw_out(ob, "The castle allows no spell-doors.")
      end
    end
  end
end

-- A faerie that hunts something on the far side of a wall grinds against
-- it forever (the hunt AI beelines, it does not path). By a wall, every
-- hostile hunter stands down onto a post; act_guard re-wakes it at the
-- next clear sighting, so a faerie in a real fight loses nothing. Saved
-- characters (has_guy) are never posted.
local function walls_jam(ob)
  if ob:team_num() == 0 then
    return false
  end
  return by_a_wall(ob)
end

local function hostiles_left()
  local obs = og.oblist()
  for i = 1, #obs do
    local ob = obs[i]
    if ob:order() == C.ORDER_LIVING
        and ob:team_num() ~= 0
        and ob:dead() == 0 then
      return true
    end
  end
  return false
end

local function narrate(tick)
  if tick == 60 then
    og.emit_notification("The dream-log says: the castle is", 90)
    og.emit_notification("ours. The faeries want in. Keep", 90)
    og.emit_notification("the twelve keys from them!", 90)
  end
  if tick == 240 then
    og.emit_notification("Every gold bar in the rooms wakes", 90)
    og.emit_notification("six ghosts for our side.", 90)
  end
  if tick == 2150 then
    og.emit_notification("The Fae Queen is coming.", 90)
    og.emit_sound(C.SOUND_ROAR)
  end
  if tick == 0 then
    return
  end
  if og.mod(tick, 600) ~= 0 then
    return
  end
  local held = popcount12(faerie_key_union())
  if held > 0 then
    og.emit_notification(string.format("The faeries hold %d of 12 keys.",
                                       held), 80)
  end
  if not gate_standing() and hostiles_left() then
    og.emit_notification("The gate is down! The ghosts hold.", 80)
  end
end

local function on_load(level)
  if gate_standing() then
    og.emit_notification("The gate knows us. It opens for", 120)
    og.emit_notification("no faerie without twelve keys.", 120)
  end
end

local function on_tick(level, tick)
  narrate(tick)
  if og.mod(tick, 100) == 0 then
    drop_gate_if_twelve(popcount12(faerie_key_union()))
  end
  if gate_standing() then
    guard_the_castle()
  end
  if og.mod(tick, 50) == 0 then
    posts.stand_down(walls_jam)
  end
end

local function on_entity_death(ent)
  if ent:order() ~= C.ORDER_LIVING then
    return
  end
  if ent:team_num() == 0 then
    return
  end
  if (og.trunc(ent:keys()) & SHORE_KEYS) ~= 0 then
    og.emit_notification("A key-bearer falls! Its keys are", 80)
    og.emit_notification("lost to the faeries.", 80)
    og.emit_sound(C.SOUND_CLANG)
  end
  if ent:s_name() == "Fae Queen" then
    og.emit_notification("The Fae Queen pops!", 100)
    og.emit_notification("Dream won. Walk to the throne!", 100)
    og.emit_sound(C.SOUND_SPARKLE)
    og.emit_sound(C.SOUND_MONEY)
  end
end

og.register_level_hooks(LEVEL, {
  on_load = on_load,
  on_tick = on_tick,
  on_entity_death = on_entity_death,
})

-- Declared-family overrides (one chunk per family, campaign-wide while the
-- pack is mounted; both keep the classic behavior off scen 2).
og.register_hooks("treasure", "core:gold_bar", { on_eat = gold_bar_on_eat })
og.register_hooks("treasure", "core:key", { on_eat = key_on_eat })
