-- The Asteroid Castle (scen 3) — the Imaginations dream-log's level script,
-- shipped inside builtin/imaginations.glad as the embedded pack
-- imaginations.dreams (it mounts and unmounts with the campaign).
--
-- Our castle stands on an asteroid in black space, spikes all around its
-- walls. What the level data cannot say, the script does:
--   * the spikes know their own: the jagged band around the walls is too
--     sharp to walk or fly over, and every hostile that comes up against
--     it (its centre within a tile of a spike) is hurt every ten ticks,
--     with a flash and a clang; the crew is never touched. The crown of
--     spikes in the court bites the same way,
--   * the Big One on the Balloon Rock never throws while he stands on the
--     rock (his sight is held at 0, so the hunt AI only walks), and wakes
--     when the crew comes within nine tiles of him,
--   * walking at the crew takes him off the deck onto the sky, and he
--     falls one floor onto the asteroid: hurt and slow, the Fallen One
--     (coming down the stairs instead makes him the Stomper),
--   * the raider waves and the key are narrated as they come, and the last
--     bad guy's death sets the rainbow by the big door sparkling: flashes
--     roll across the whole arc, band by band, outer to inner, for three
--     seconds.
--
-- Determinism cookbook (docs/lua-classpacks-design.md §3): no mutable sim
-- state in locals or upvalues (R6) — every answer re-derives from the
-- world on each dispatch, the fall beat carries its state in the
-- golem's name, and the rainbow's countdown rides in the exit pad's cycle
-- field (the pad is on the fx list and never acts, so nothing else reads
-- it). No randomness is drawn.

local C = og.C
local posts = og.use("dream_posts")

local LEVEL = 3

local FAM_GOLEM = assert(og.family_id("living", "core:#18"))
local FX_FLASH = assert(og.family_id("fx", "core:flash"))
local FAM_EXIT = assert(og.family_id("treasure", "core:exit"))

-- The isle's mist rule: hostile sight caps at seven tiles.
local SIGHT_CAP = 7

-- The spikes: damage per bite, every SPIKE_PERIOD ticks.
local SPIKE_PERIOD = 10
local SPIKE_DMG = 12

-- Tile rects (inclusive). The spike band is BAND minus the castle block
-- minus the gate apron; its jagged ground blocks walkers and flyers.
local BAND = { x0 = 6, y0 = 13, x1 = 25, y1 = 48 }
local CASTLE = { x0 = 8, y0 = 15, x1 = 23, y1 = 46 }
local APRON = { x0 = 24, y0 = 29, x1 = 25, y1 = 31 }
-- The crown of spikes in the court: a jagged V from (11,17) and (20,17)
-- down to (15,21) and (16,21), one cell per arm per row.
local CROWN_Y0 = 17
local CROWN_Y1 = 21
local CROWN_LEFT = 11
local CROWN_RIGHT = 20

-- The Big One wakes when any of the crew comes this close (tiles).
local WAKE_TILES = 9

-- The golem's own step (packs/core: the beast row's combat.stepsize).
local GOLEM_STEP = 8

-- Fallen off the rock: hurt and slow.
local FALL_HP = 90
local FALL_DAMAGE = 24
local FALL_STEP = 4
-- Down the stairs instead: angry, still slower than on the rock.
local STAIR_HP = 180
local STAIR_DAMAGE = 40
local STAIR_STEP = 6

-- Flashes from the deck edge toward the gate when he falls.
local FALL_ARC = {
  { 44, 15 }, { 41, 17 }, { 38, 19 }, { 35, 21 }, { 32, 22 }, { 29, 23 },
  { 26, 24 },
}

-- The rainbow (the mapgen's asteroid_rainbow_ring): rings 3..7 round the
-- big door's middle (tile 15, row 46), rows 49 to 55, x 7 to 23. When the
-- last bad guy dies the rings flash one at a time, outer (7) to inner
-- (3) and round again, one ring every RAINBOW_PERIOD ticks for
-- RAINBOW_SWEEPS sweeps (180 ticks).
local RAINBOW_CX = 15
local RAINBOW_CY = 46
local RAINBOW_X0 = 7
local RAINBOW_X1 = 23
local RAINBOW_Y0 = 49
local RAINBOW_Y1 = 55
local RAINBOW_OUTER = 7
local RAINBOW_INNER = 3
local RAINBOW_PERIOD = 10
local RAINBOW_SWEEPS = 18

-- The centre tile of an entity.
local function tile_of(ob)
  local x = og.trunc(ob:xpos()) + og.div(og.trunc(ob:sizex()), 2)
  local y = og.trunc(ob:ypos()) + og.div(og.trunc(ob:sizey()), 2)
  return og.div(x, C.GRID_SIZE), og.div(y, C.GRID_SIZE)
end

local function in_rect(r, tx, ty)
  if tx < r.x0 or tx > r.x1 then
    return false
  end
  return ty >= r.y0 and ty <= r.y1
end

local function is_crown(tx, ty)
  if ty < CROWN_Y0 or ty > CROWN_Y1 then
    return false
  end
  local k = ty - CROWN_Y0
  if tx == CROWN_LEFT + k then
    return true
  end
  return tx == CROWN_RIGHT - k
end

local function is_spike(tx, ty)
  if is_crown(tx, ty) then
    return true
  end
  if not in_rect(BAND, tx, ty) then
    return false
  end
  if in_rect(CASTLE, tx, ty) then
    return false
  end
  return not in_rect(APRON, tx, ty)
end

-- Up against the spikes: a spike cell under or beside the centre tile.
local function by_spikes(tx, ty)
  for dy = -1, 1 do
    for dx = -1, 1 do
      if is_spike(tx + dx, ty + dy) then
        return true
      end
    end
  end
  return false
end

local function is_live_hostile(ob)
  if ob:order() ~= C.ORDER_LIVING then
    return false
  end
  if ob:team_num() == 0 then
    return false
  end
  return ob:dead() == 0
end

-- The Big One (or the Fallen One, or the Stomper): the live, awake
-- hostile golem.
local function find_big_one()
  local obs = og.oblist()
  for i = 1, #obs do
    local ob = obs[i]
    if is_live_hostile(ob)
        and ob:family() == FAM_GOLEM
        and not ob:dormant() then
      return ob
    end
  end
  return nil
end

-- Any of the crew (team 0, alive, awake) within `tiles` of ob, by
-- Chebyshev tile distance on the map (any floor).
local function crew_near(ob, tiles)
  local gx, gy = tile_of(ob)
  local obs = og.oblist()
  for i = 1, #obs do
    local o = obs[i]
    if o:order() == C.ORDER_LIVING
        and o:team_num() == 0
        and o:dead() == 0
        and not o:dormant() then
      local tx, ty = tile_of(o)
      local dx = math.abs(tx - gx)
      local dy = math.abs(ty - gy)
      if og.max(dx, dy) <= tiles then
        return true
      end
    end
  end
  return false
end

local function new_flash(floor)
  local f = og.add_ob("fx", FX_FLASH)
  if f == nil then
    return nil
  end
  f.ani_type = C.ANI_EXPAND_8
  f:set_floor(floor)
  return f
end

local function flash_at(tx, ty, floor)
  local f = new_flash(floor)
  if f == nil then
    return
  end
  f:setxy(tx * C.GRID_SIZE, ty * C.GRID_SIZE)
end

local function flash_on(ob)
  local f = new_flash(ob:floor())
  if f == nil then
    return
  end
  f:center_on(ob)
end

local function flash_arc(arc)
  for i = 1, #arc do
    flash_at(arc[i][1], arc[i][2], 0)
  end
end

-- The rainbow ring (3..7) a tile lies on, or 0: its distance from the
-- big door's middle rounds to the ring, (2r-1)^2 <= 4 d^2 < (2r+1)^2.
local function rainbow_ring(tx, ty)
  if ty < RAINBOW_Y0 then
    return 0
  end
  local dx = tx - RAINBOW_CX
  local dy = ty - RAINBOW_CY
  local d2x4 = 4 * (dx * dx + dy * dy)
  for r = RAINBOW_INNER, RAINBOW_OUTER do
    local lo = (2 * r - 1) * (2 * r - 1)
    local hi = (2 * r + 1) * (2 * r + 1)
    if lo <= d2x4 then
      if d2x4 < hi then
        return r
      end
    end
  end
  return 0
end

-- A flash on every tile of one rainbow ring.
local function flash_ring(r)
  for ty = RAINBOW_Y0, RAINBOW_Y1 do
    for tx = RAINBOW_X0, RAINBOW_X1 do
      if rainbow_ring(tx, ty) == r then
        flash_at(tx, ty, 0)
      end
    end
  end
end

-- The exit pad (the big door's vestibule), whose cycle field counts the
-- rainbow's sweeps still to run.
local function exit_pad()
  local fx = og.fxlist()
  for i = 1, #fx do
    local e = fx[i]
    if e:order() == C.ORDER_TREASURE then
      if e:family() == FAM_EXIT then
        return e
      end
    end
  end
  return nil
end

-- One sweep of the rainbow show: the next ring flashes and the count
-- drops. The rings roll outer to inner, then start again at the outer.
local function rainbow_sweep(pad)
  local left = pad:cycle()
  local done = RAINBOW_SWEEPS - left
  local span = RAINBOW_OUTER - RAINBOW_INNER + 1
  flash_ring(RAINBOW_OUTER - og.mod(done, span))
  pad:set_cycle(left - 1)
end

local function rainbow_show(tick)
  if og.mod(tick, RAINBOW_PERIOD) ~= 0 then
    return
  end
  local pad = exit_pad()
  if pad == nil then
    return
  end
  if pad:cycle() > 0 then
    rainbow_sweep(pad)
  end
end

-- Every hostile live living still in the dream, dormant waves included.
local function hostiles_left()
  local count = 0
  local obs = og.oblist()
  for i = 1, #obs do
    if is_live_hostile(obs[i]) then
      count = count + 1
    end
  end
  return count
end

-- The isle's mist: every hostile, dormant waves included, sees 7 tiles.
local function mist_cap_sight()
  local obs = og.oblist()
  for i = 1, #obs do
    local ob = obs[i]
    if is_live_hostile(ob)
        and ob:lineofsight() > SIGHT_CAP then
      ob:set_lineofsight(SIGHT_CAP)
    end
  end
end

-- The spikes bite every hostile up against the band (floor 0, awake).
local function spikes_bite()
  local obs = og.oblist()
  for i = 1, #obs do
    local ob = obs[i]
    if is_live_hostile(ob)
        and not ob:dormant()
        and ob:floor() == 0 then
      local tx, ty = tile_of(ob)
      if by_spikes(tx, ty) then
        ob.hp = ob.hp - SPIKE_DMG
        flash_on(ob)
        og.emit_positional_sound(ob, C.SOUND_CLANG)
        if ob.hp <= 0 then
          ob:set_dead(1)
          ob:death()
        end
      end
    end
  end
end

-- The nearest of the crew (team 0, alive, awake) to ob on the map, any
-- floor, by tile distance; nil when there is none.
local function nearest_crew(ob)
  local gx, gy = tile_of(ob)
  local best = nil
  local best_d = 0
  local obs = og.oblist()
  for i = 1, #obs do
    local o = obs[i]
    if o:order() == C.ORDER_LIVING
        and o:team_num() == 0
        and o:dead() == 0
        and not o:dormant() then
      local tx, ty = tile_of(o)
      local d = math.abs(tx - gx) + math.abs(ty - gy)
      if best == nil or d < best_d then
        best = o
        best_d = d
      end
    end
  end
  return best
end

-- On the rock he never throws (sight 0), and the crew coming near wakes
-- him. Awake, he walks STRAIGHT at the nearest of the crew: every tick
-- his orders are replaced by one step toward them, so the hunt AI's path
-- search (which knows the sky is no floor and would lead him round to the
-- stairs) never runs up here, and the straight walk carries him off the
-- deck onto the sky. Every foe he can want is south or west of the rock:
-- beyond it is space.
local function big_one_on_the_rock(g)
  if g:lineofsight() ~= 0 then
    g:set_lineofsight(0)
  end
  if g:act_type() ~= C.ACT_RANDOM then
    if not crew_near(g, WAKE_TILES) then
      return
    end
    g:set_act_type(C.ACT_RANDOM)
    og.emit_notification("The Big One wakes up!", 90)
    og.emit_sound(C.SOUND_ROAR)
  end
  g:s_clear_command()
  local prey = nearest_crew(g)
  if prey == nil then
    return
  end
  local gx, gy = tile_of(g)
  local tx, ty = tile_of(prey)
  g:s_set_command(C.COMMAND_WALK, 1, og.sign(tx - gx), og.sign(ty - gy))
end

-- He has come down to the asteroid: by falling (sky above him) or by the
-- stairs. Either way the name changes, so this runs once.
local function big_one_lands(g)
  local tx, ty = tile_of(g)
  if og.query_genre(tx, ty, 1) == C.TYPE_AIR then
    g:s_set_name("Fallen One")
    g.hp = FALL_HP
    g:set_damage(FALL_DAMAGE)
    g:set_lineofsight(SIGHT_CAP)
    og.emit_notification("The Big One falls off the rock!", 90)
    og.emit_notification("He is hurt and slow. Get him!", 90)
    og.emit_sound(C.SOUND_EXPLODE)
    flash_arc(FALL_ARC)
    return
  end
  g:s_set_name("Stomper")
  g.hp = STAIR_HP
  g:set_damage(STAIR_DAMAGE)
  g:set_lineofsight(SIGHT_CAP)
  og.emit_notification("The Big One stomps down!", 90)
  og.emit_sound(C.SOUND_ROAR)
end

-- Down on the asteroid he stays slow: his step is held at the landing
-- branch's pace by a speed bonus re-armed every tick (the engine resets
-- the step to the family's own each act, and adds the bonus while it
-- lasts).
local function hold_pace(g, step)
  g:set_speed_bonus(step - GOLEM_STEP)
  g:set_speed_bonus_left(2)
end

local function the_big_one()
  local g = find_big_one()
  if g == nil then
    return
  end
  if g:floor() == 1 then
    big_one_on_the_rock(g)
    return
  end
  local name = g:s_name()
  if name == "Big One" then
    big_one_lands(g)
    name = g:s_name()
  end
  if name == "Fallen One" then
    hold_pace(g, FALL_STEP)
  end
  if name == "Stomper" then
    hold_pace(g, STAIR_STEP)
  end
end

-- The stand-down rule (lib/dream_posts.lua) takes every hostile hunter,
-- except the Big One while he is on the rock: there the script drives him.
local function asteroid_reposts(ob)
  if ob:team_num() == 0 then
    return false
  end
  if ob:family() ~= FAM_GOLEM then
    return true
  end
  return ob:floor() ~= 1
end

local function narrate(tick)
  if tick == 80 then
    og.emit_notification("The dream-log says: our castle", 90)
    og.emit_notification("has spikes. The bad guys think", 90)
    og.emit_notification("it is theirs and come too close.", 90)
  end
  if tick == 260 then
    og.emit_notification("The Big One sits on the Balloon", 90)
    og.emit_notification("Rock. The key hangs under it.", 90)
  end
  if tick == 301 then
    og.emit_notification("Raiders ride the space wind!", 90)
  end
  if tick == 801 then
    og.emit_notification("Falling stars from the south!", 90)
  end
  if tick == 1401 then
    og.emit_notification("Black Dash brings his band!", 90)
    og.emit_sound(C.SOUND_ROAR)
  end
  if tick == 1500 then
    og.emit_notification("Key 3 opens the big door.", 90)
  end
end

local function on_load(level)
  mist_cap_sight()
  og.emit_notification("The spikes know their own.", 120)
  og.emit_notification("Bad guys who come close get caught.", 120)
end

local function on_tick(level, tick)
  if og.mod(tick, SPIKE_PERIOD) == 0 then
    spikes_bite()
  end
  the_big_one()
  if og.mod(tick, 50) == 0 then
    posts.stand_down(asteroid_reposts)
  end
  narrate(tick)
  rainbow_show(tick)
end

local function on_entity_death(ent)
  if ent:order() ~= C.ORDER_LIVING then
    return
  end
  if ent:team_num() == 0 then
    return
  end
  if ent:family() == FAM_GOLEM then
    og.emit_notification("The Big One is beaten!", 90)
    og.emit_notification("Up the Balloon Rock: the key.", 90)
    og.emit_sound(C.SOUND_SPARKLE)
  end
  -- The dying one is already flagged dead, so it is out of this count.
  if hostiles_left() > 0 then
    return
  end
  og.emit_notification("All the bad guys are gone!", 100)
  og.emit_notification("A rainbow lands on the big door.", 100)
  og.emit_notification("Key 3 opens it. Dream won!", 100)
  og.emit_sound(C.SOUND_MONEY)
  local pad = exit_pad()
  if pad == nil then
    return
  end
  pad:set_cycle(RAINBOW_SWEEPS)
  rainbow_sweep(pad)
end

og.register_level_hooks(LEVEL, {
  on_load = on_load,
  on_tick = on_tick,
  on_entity_death = on_entity_death,
})
