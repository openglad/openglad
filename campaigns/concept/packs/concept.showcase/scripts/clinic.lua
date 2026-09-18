-- The Low-Magic Clinic (scen 600) — a campaign-var-fenced capture rig that
-- puts one cleric heal on camera, shipped INSIDE builtin-dev/concept.glad as
-- part of the embedded pack concept.showcase.
--
-- WHY A RIG LIVES IN A CAMPAIGN: the heal the engine's mp_cost gate approves
-- must LAND even when the pool-scaled surcharge prices at zero
-- (packs/core/families/living-05-cleric.lua, heal_or_mace). Proving that on
-- camera needs a cleric holding 2..7 MP next to a hurt friend, and neither
-- openglad_demo's seeded roster nor its scenario list can ask for one. The
-- campaign decision store can: OPENGLAD_DEMO_CAMPAIGN_STATE=clinic=1 stages
-- the scene, and scripts/media/capture_pr292_cleric_clinic.sh films it.
--
-- THE FENCE: every hook returns immediately unless og.campaign_var("clinic")
-- reads 1. A campaign var defaults to 0, so Stairs plays exactly as
-- tools/concept_mapgen authored it for every player, every test that does not
-- ask for the clinic, and every tool that walks the campaign.
--
-- Determinism cookbook (docs/lua-classpacks-design.md §3) applies: no chunk-
-- level mutable state (R6 — the rig is re-derived from the world by name on
-- every dispatch), no randomness (R4 — the thin pool is chosen so the heal
-- formula never draws), og.mod for integer % (R1).

local C = og.C

local CLINIC_LEVEL = 600

-- The campaign decision that stages the scene. 0 (unset) is stock Stairs.
local CLINIC_VAR = "clinic"

local FAM_CLERIC = assert(og.family_id("living", "core:cleric"))
local FAM_SOLDIER = assert(og.family_id("living", "core:soldier"))

-- Names are the rig's whole ledger: on_load's idempotence census and
-- on_tick's lookup both read them back off the world (R6).
local CLERIC_NAME = "Clinic"
local PATIENT_NAME = "Patient"

-- Floor 0 of Stairs is 24x18 grass with one Z-stair at tile (12, 9). The pair
-- stands two rows north of it: clear of the stair tile (neither of them ever
-- takes the stairs), horizontally centred under the FOCUS=center camera, and
-- high enough on screen to sit just below the notification lines, which is
-- what lets one capture frame hold the toast and the HP bar together.
local CLERIC_X = 11 * C.GRID_SIZE
local CLERIC_Y = 7 * C.GRID_SIZE
local PATIENT_DX = 20

-- A pool of exactly 3 is the whole point: it clears HEAL's declared mp_cost
-- of 2, so walker::special dispatches, and it is under 4, so
-- compute_heal_amount's base = trunc(mp)/4 is 0 — the surcharge prices at
-- zero and the amount is level*5 with no rng draw at all (R4).
local THIN_POOL = 3.0
local CLERIC_LEVEL = 4

-- 20 of 120: five heals of level*5 = 20 fill the bar exactly, and the sixth
-- cadence slot finds everyone healthy and refuses. That arc is the GIF.
local PATIENT_HP = 20.0
local PATIENT_MAX_HP = 120.0

-- The wound has to stay open: a living regenerates +heal_per_round every tick
-- and +1 every max_heal_delay ticks, which would walk the bar up on its own
-- and make the "no heal ever lands" cut unreadable. Zero per round and a
-- delay far past any capture length leave the cleric as the ONLY thing that
-- can move the Patient's hitpoints.
local PATIENT_HEAL_PER_ROUND = 0.0
local PATIENT_HEAL_DELAY = 1000000

-- Casts land on ticks 30, 90, 150, ... — far enough apart that each toast and
-- each step of the HP bar is legible at 12 fps.
local CAST_PERIOD = 60
local CAST_PHASE = 30

-- The HEAL slot (living-05-cleric.lua's specials[1]).
local SPECIAL_HEAL = 1

-- The live living named `name`, or nil. The census is the rig's only
-- state: the rig remembers nothing between dispatches (R6).
local function find_named(name)
  local obs = og.oblist()
  for i = 1, #obs do
    local ob = obs[i]
    if ob:order() == C.ORDER_LIVING
        and ob:dead() == 0
        and ob:s_name() == name then
      return ob
    end
  end
  return nil
end

-- Stairs seeds one orc upstairs. It walks the Z-stair down inside 150 ticks
-- and puts the Patient's bar somewhere the cleric cannot be blamed for, which
-- is exactly the measurement the scene exists to make -- so the clinic clears
-- the level of hostiles as it opens. Stairs keeps its exit, and an exit-
-- bearing level never auto-completes for want of foes (GameWorld::tick's
-- level_done ladder), so the scene runs as long as the capture wants it.
local function clear_the_ward()
  local obs = og.oblist()
  for i = 1, #obs do
    local ob = obs[i]
    if ob:order() == C.ORDER_LIVING
        and ob:team_num() ~= 0 then
      ob.dead = 1
    end
  end
end

-- One team-0 living, parked. set_floor precedes setxy (the obmap rule).
local function place_sitter(family, name, x, y)
  local ob = assert(og.add_ob("living", family))
  ob:set_team_num(0)
  ob:set_real_team_num(0)
  ob:s_set_name(name)
  ob:set_act_type(C.ACT_SIT)
  ob:set_floor(0)
  ob:setxy(x, y)
  return ob
end

-- on_load fires on each peer's first tick of the level, so it must be
-- idempotent: a "Clinic" already standing means the rig is already staged.
local function on_load(level)
  if og.campaign_var(CLINIC_VAR) ~= 1 then
    return
  end
  if find_named(CLERIC_NAME) ~= nil then
    return
  end
  clear_the_ward()
  local cleric = place_sitter(FAM_CLERIC, CLERIC_NAME, CLERIC_X, CLERIC_Y)
  cleric.level = CLERIC_LEVEL
  cleric.current_special = SPECIAL_HEAL
  local patient = place_sitter(FAM_SOLDIER, PATIENT_NAME,
                               CLERIC_X + PATIENT_DX, CLERIC_Y)
  patient:s_set_heal_per_round(PATIENT_HEAL_PER_ROUND)
  patient:s_set_max_heal_delay(PATIENT_HEAL_DELAY)
  patient.max_hp = PATIENT_MAX_HP
  patient.hp = PATIENT_HP
  og.emit_notification("The clinic opens.", 80)
end

-- on_tick re-pins the pool (stat regen would drift it out of the 2..7 window
-- the scene exists to film) and issues the cast through the real
-- walker::special gate. The Patient is never touched after on_load: its
-- hitpoints are the observable.
local function on_tick(level, tick)
  if og.campaign_var(CLINIC_VAR) ~= 1 then
    return
  end
  local cleric = find_named(CLERIC_NAME)
  if cleric == nil then
    return
  end
  cleric.magicpoints = THIN_POOL
  if og.mod(tick, CAST_PERIOD) ~= CAST_PHASE then
    return
  end
  cleric:set_current_special(SPECIAL_HEAL)
  cleric:special()
end

-- og.register_campaign_hooks is what puts "clinic" on the campaign's
-- REGISTERED var list, and only registered names survive the filter that
-- copies a save's decision book into the sim world (screen.cpp's
-- sync_world_from_save_data and its headless twin). Without this, the fence
-- above would read 0 in every real session and
-- OPENGLAD_DEMO_CAMPAIGN_STATE=clinic=1 would stage nothing.
--
-- The registration must carry at least one picker hook. The Concept
-- Playground has no Base Camp book and should not grow one for a capture
-- rig, so this one composes no page at all: returning nil for every page id
-- (the root "" included) is what keeps the book door off the camp screen.
local function picker_menu(page_id)
  return nil
end

og.register_campaign_hooks({
  vars = { "clinic" },
  picker_menu = picker_menu,
})

og.register_level_hooks(CLINIC_LEVEL, {
  on_load = on_load,
  on_tick = on_tick,
})
