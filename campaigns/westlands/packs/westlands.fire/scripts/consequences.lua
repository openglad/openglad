-- westlands.fire — decision consequences: watch allies (15), gold-wraiths (11), provision drops (13-17, 19-23), the softened knife (21), The Pilgrim (26) (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
--
-- Every consequence helper's FIRST statement is its var == 0 early return,
-- before any world read, spawn, or notification — with the campaign vars
-- unset (every fresh save, every calibration floor, every battle smoke,
-- every parity golden, every dedicated server) each hook returns having
-- made zero world calls and drawn zero randomness, so the shipped levels
-- stay byte-identical. This pack draws no randomness on ANY path: every
-- spawn tile is fixed data in lib/spawns.lua.
--
-- Spawned allies keep the walker default ACT_RANDOM (the allowed choice:
-- the map's allied hold-post rule binds ACT_GUARD posts; these three are
-- relief that marches, not a post that holds).

local C = og.C
local spawns = og.use("spawns")

local FAM_SOLDIER = og.family_id("living", "core:soldier")
local FAM_ARCHMAGE = og.family_id("living", "core:archmage")
local FAM_GHOST = og.family_id("living", "core:ghost")
local FAM_DRUMSTICK = og.family_id("treasure", "core:drumstick")
local FAM_MAGIC_POTION = og.family_id("treasure", "core:magic_potion")

-- One spawned living, mirroring the mode packs' bot recipe (level via
-- s_set_level + set_difficulty) and the mapgen placement order (floor
-- before setxy — the obmap keying rule).
local function spawn_living(family, team, level, name, floor, x, y)
  local ally = og.add_ob("living", family)
  if ally == nil then
    return nil
  end
  ally:set_team_num(team)
  ally:set_real_team_num(team)
  ally:s_set_level(level)
  ally:set_difficulty(level)
  ally:s_set_name(name)
  ally:set_floor(floor)
  ally:setxy(x * C.GRID_SIZE, y * C.GRID_SIZE)
  return ally
end

-- One dropped treasure (fx list, like every placed bar and drumstick).
local function drop_treasure(family, floor, x, y)
  local item = og.add_fx_ob("treasure", family)
  if item == nil then
    return nil
  end
  item:set_team_num(0)
  item:set_real_team_num(0)
  item:set_floor(floor)
  item:setxy(x * C.GRID_SIZE, y * C.GRID_SIZE)
  return item
end

-- Level 15: the paid Watch sends three swords to the gate.
local function watch_allies()
  if og.campaign_var("watch_paid") == 0 then
    return
  end
  local posts = spawns.WATCH
  for i = 1, #posts do
    local post = posts[i]
    spawn_living(FAM_SOLDIER, 0, post.level, post.name, post.floor, post.x,
                 post.y)
  end
  og.emit_notification("The Watch remembers its wages.", 80)
end

-- Level 11: the counted hoard's debt follows on the water.
local function wraith_pursuit()
  if og.campaign_var("delve_counted") == 0 then
    return
  end
  local posts = spawns.WRAITHS
  for i = 1, #posts do
    local post = posts[i]
    spawn_living(FAM_GHOST, 2, spawns.WRAITH_LEVEL, spawns.WRAITH_NAME,
                 post.floor, post.x, post.y)
  end
  og.emit_notification("Something pale keeps pace on the water.", 80)
end

-- Levels 13-17 and 19-23: the pack-train delivers beside the lead start
-- marker — one drumstick per tier, the physic potion from tier two.
local function provisions_drop(level)
  if og.campaign_var("provisions") == 0 then
    return
  end
  local tier = og.min(og.campaign_var("provisions"), 3)
  local spot = spawns.PROVISIONS[level]
  for i = 1, tier do
    drop_treasure(FAM_DRUMSTICK, spot.floor, spot.tiles[i][1],
                  spot.tiles[i][2])
  end
  if tier >= 2 then
    drop_treasure(FAM_MAGIC_POTION, spot.floor, spot.tiles[4][1],
                  spot.tiles[4][2])
  end
end

-- Level 21: the knife remembers the bread. The betrayal stands — Sneak
-- keeps his post and his team — but the placed lvl-8 guide waits at
-- lvl 5 on half his strength (walker property writes; the adjudicated
-- mechanism, not the Oath-Keeper fallback).
local function soften_sneak()
  if og.campaign_var("sneak_bread") == 0 then
    return
  end
  local obs = og.oblist()
  local sneak = nil
  for i = 1, #obs do
    local ob = obs[i]
    if ob:order() == C.ORDER_LIVING then
      if ob:team_num() == 2 then
        if ob:s_name() == "Sneak" then
          sneak = ob
        end
      end
    end
  end
  if sneak == nil then
    return
  end
  sneak.level = 5
  -- hitpoints are a C++ float: one og.f* call per float op.
  sneak.hp = og.fdiv(sneak.hp, 2.0)
  og.emit_notification("For a breath, the knife hesitates.", 80)
end

-- Level 26: the finale's earned surprise — generosity on all three
-- counts (wages paid, bread shared, hoard refused) is answered on the
-- quay. Three guards in sequence keep the first-statement rule: with
-- every var at zero the first returns before any world call.
local function pilgrim_on_quay()
  if og.campaign_var("watch_paid") == 0 then
    return
  end
  if og.campaign_var("sneak_bread") == 0 then
    return
  end
  if og.campaign_var("delve_counted") ~= 0 then
    return
  end
  local post = spawns.PILGRIM
  spawn_living(FAM_ARCHMAGE, 0, spawns.PILGRIM_LEVEL, spawns.PILGRIM_NAME,
               post.floor, post.x, post.y)
  og.emit_notification("A grey figure waits upon the quay.", 80)
end

-- ---------------------------------------------------------------------------
-- Registration: explicit per-id hooks, never id -1. Level-hook slots are
-- last-wins per (level, hook), so 15 and 21 merge their two consequences
-- into one on_load each; every helper re-guards its own var.
-- ---------------------------------------------------------------------------

local function on_load_15(level)
  watch_allies()
  provisions_drop(level)
end

local function on_load_21(level)
  soften_sneak()
  provisions_drop(level)
end

og.register_level_hooks(11, { on_load = wraith_pursuit })
og.register_level_hooks(15, { on_load = on_load_15 })
og.register_level_hooks(21, { on_load = on_load_21 })
og.register_level_hooks(26, { on_load = pilgrim_on_quay })

local PROVISION_LEVELS = { 13, 14, 16, 17, 19, 20, 22, 23 }
for i = 1, #PROVISION_LEVELS do
  og.register_level_hooks(PROVISION_LEVELS[i],
                          { on_load = provisions_drop })
end
