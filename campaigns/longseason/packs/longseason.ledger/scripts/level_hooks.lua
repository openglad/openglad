-- longseason.ledger: level_hooks — the book's sim consequences: addressed crates, the fair round, the Collectors, the Carried, the door coin (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
--
-- Every consequence function's FIRST statement is its var == 0 early
-- return, before any world read, spawn, or RNG draw — with an empty book
-- each level is byte-identical to the shipped campaign (the calibration
-- floors, battle smokes and parity goldens all run at var 0). No og.rand
-- anywhere in this pack: every spawn tile is fixed data in ledger_data.

local C = og.C
local D = og.use("ledger_data")

-- Core families always resolve; a nil would be a load bug and the add
-- erroring loudly is the right failure (the court.lua precedent).
local FAM_SOLDIER = og.family_id("living", "core:soldier") --[[@as integer]]
local FAM_DRUMSTICK = og.family_id("treasure", "core:drumstick") --[[@as integer]]
local FAM_SILVER_BAR = og.family_id("treasure", "core:silver_bar") --[[@as integer]]

local FAIR_NAMES = { "Stallkeeper", "Potboy" }
local CARRIED_NAMES = { "The Carried" }

-- One leveled soldier, the classic-fire spawn recipe (level, then the
-- family set_difficulty scaling, then heal to the scaled maximum).
local function spawn_soldier(team, level, name, floor, tx, ty)
  local w = og.add_ob("living", FAM_SOLDIER)
  if w == nil then
    return nil
  end
  w:set_team_num(team)
  w:set_real_team_num(team)
  w.level = level
  w:set_difficulty(level)
  w:s_set_hitpoints(w:s_max_hitpoints())
  w:s_set_name(name)
  w:s_set_bit_flags(C.BIT_NAMED, 1)
  w:set_floor(floor)
  w:setxy(tx * C.GRID_SIZE, ty * C.GRID_SIZE)
  return w
end

local function spawn_treasure(fam, floor, tx, ty)
  local t = og.add_fx_ob("treasure", fam)
  if t == nil then
    return nil
  end
  t:set_team_num(0)
  t:set_real_team_num(0)
  t:set_floor(floor)
  t:setxy(tx * C.GRID_SIZE, ty * C.GRID_SIZE)
  return t
end

-- Scripted hold-post. npc_flags bit 1 (the loader's hold-post policy) is
-- not writable from a script spawn, and a plain allied ACT_GUARD wakes to
-- ACT_RANDOM on genuine sighting (walker::act_guard) and wanders. Re-pin
-- ACT_GUARD every tick BETWEEN the walker's own acts: the helper then acts
-- as a guard on every tick it takes, which is exactly the stationary
-- sentry — act_guard never moves the walker and the wake tick's own
-- face-and-fire (and its rng(30) draw) are the same either way.
local function repin_hold_post(names)
  local obs = og.oblist()
  for i = 1, #obs do
    local ob = obs[i]
    if ob:order() == C.ORDER_LIVING
        and ob:team_num() == 0
        and ob:dead() == 0
        and ob:user() == -1
        and ob:act_type() ~= C.ACT_CONTROL
        and ob:act_type() ~= C.ACT_GUARD then
      for j = 1, #names do
        if ob:s_name() == names[j] then
          ob:set_act_type(C.ACT_GUARD)
        end
      end
    end
  end
end

-- The addressed crate: meals (and the strong crate's two silver) at the
-- fixed spots ringing the lead start marker — but only on the level the
-- crate was addressed to. provisions encodes kind + 8 * level.
local function crate_delivery(lvl)
  if og.campaign_var("provisions") == 0 then
    return
  end
  local provisions = og.campaign_var("provisions")
  if og.div(provisions, 8) ~= lvl then
    return
  end
  local kind = og.mod(provisions, 8)
  if kind < 1 or kind > 3 then
    return
  end
  local spots = D.SUPPLY[lvl]
  local meals = 4
  if kind >= 2 then
    meals = 8
  end
  for i = 1, meals do
    local spot = spots.tiles[i]
    spawn_treasure(FAM_DRUMSTICK, spots.floor, spot[1], spot[2])
  end
  if kind == 3 then
    for i = 9, 10 do
      local spot = spots.tiles[i]
      spawn_treasure(FAM_SILVER_BAR, spots.floor, spot[1], spot[2])
    end
  end
end

-- Ashfall Fair: the crew's round comes back as hands on the strongroom
-- door — two hold-post townsfolk and two meals inside.
local function fair_round_help()
  if og.campaign_var("fair_round") == 0 then
    return
  end
  for i = 1, #D.FAIR.helpers do
    local h = D.FAIR.helpers[i]
    local w = spawn_soldier(0, D.FAIR.helper_level, h[3], D.FAIR.floor,
                            h[1], h[2])
    if w ~= nil then
      w:set_act_type(C.ACT_GUARD)
    end
  end
  for i = 1, #D.FAIR.food do
    local spot = D.FAIR.food[i]
    spawn_treasure(FAM_DRUMSTICK, D.FAIR.floor, spot[1], spot[2])
  end
end

-- The Long Toll: the season collects in bodies while the book still shows
-- nine hundred. Hunt AI (the spawn default) walks the toll road east.
local function collectors_call()
  if og.campaign_var("advance_debt") == 0 then
    return
  end
  for i = 1, #D.COLLECTORS.tiles do
    local spot = D.COLLECTORS.tiles[i]
    spawn_soldier(D.COLLECTORS.team, D.COLLECTORS.level, D.COLLECTORS.name,
                  D.COLLECTORS.floor, spot[1], spot[2])
  end
end

-- The Warm Mint: the bones remember being carried, not spent — one ally
-- per four kept coins, capped, anchored in the Kettle pocket.
local function the_carried()
  if og.campaign_var("coin_kept") == 0 then
    return
  end
  local kept = D.popcount_coins(og.campaign_var("coin_kept"))
  local count = og.min(og.div(kept, D.CARRIED_PER_KEPT), D.CARRIED_CAP)
  if count == 0 then
    return
  end
  for i = 1, count do
    local spot = D.CARRIED.tiles[i]
    local w = spawn_soldier(0, D.CARRIED.level, D.CARRIED.name,
                            D.CARRIED.floor, spot[1], spot[2])
    if w ~= nil then
      w:set_act_type(C.ACT_GUARD)
    end
  end
end

-- Settlement Day: one silver bar nailed over the winter-quarters door,
-- pried loose by walking over it.
local function door_coin()
  if og.campaign_var("coin_kept") == 0 then
    return
  end
  spawn_treasure(FAM_SILVER_BAR, D.DOOR_COIN.floor, D.DOOR_COIN.tile[1],
                 D.DOOR_COIN.tile[2])
end

local function make_on_load(lvl)
  return function(level)
    crate_delivery(lvl)
    if lvl == D.FAIR_LEVEL then
      fair_round_help()
    end
    if lvl == D.TOLL_LEVEL then
      collectors_call()
    end
    if lvl == D.MINT_LEVEL then
      the_carried()
    end
    if lvl == D.SETTLEMENT_LEVEL then
      door_coin()
    end
  end
end

local function fair_on_tick(level, tick)
  if og.campaign_var("fair_round") == 0 then
    return
  end
  repin_hold_post(FAIR_NAMES)
end

local function mint_on_tick(level, tick)
  if og.campaign_var("coin_kept") == 0 then
    return
  end
  repin_hold_post(CARRIED_NAMES)
end

for lvl = 1, D.LEVEL_COUNT do
  local hooks = { on_load = make_on_load(lvl) }
  if lvl == D.FAIR_LEVEL then
    hooks.on_tick = fair_on_tick
  end
  if lvl == D.MINT_LEVEL then
    hooks.on_tick = mint_on_tick
  end
  og.register_level_hooks(lvl, hooks)
end
