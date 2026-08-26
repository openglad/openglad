-- modes lib: mode_fighters — the fighter band (team bytes 16-31): bound-first enumeration, Fisher-Yates byte shuffle, single-bot fill, rotated 4-pool placement, mid-join adoption, charm renormalize, band-aware director eligibility (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
--
-- Rule spec: docs/ffa-design.md §2-§3 and §8 — one implementation of the
-- fighter machinery, shared by FFA now and the Mutant conversion (D13).
-- All state lives in caller-designated mode vars (R6): an entity-id slot
-- range (id_base + color index c, 0 = free), a frag slot range and a
-- 16-bit assignment bitmap. Color index c wears team byte 16 + c.

local C = og.C
local core = og.use("mode_core")
local ai = og.use("mode_ai")
local match = og.use("mode_match")
local strip = og.use("mode_strip")

-- Bit weights for the 16-slot assignment bitmap (integer arithmetic only —
-- the sandbox has no bitwise operators; mode_core.TEAM_BIT's shape).
local SLOT_BIT = { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096,
                   8192, 16384, 32768 }

local function band_has(bitmap, c)
  return og.mod(og.div(bitmap, SLOT_BIT[c + 1]), 2) == 1
end

local function band_count(bitmap)
  local count = 0
  for c = 0, C.FFA_TEAM_COUNT - 1 do
    if band_has(bitmap, c) then
      count = count + 1
    end
  end
  return count
end

-- The band byte of color index c.
local function band_byte(c)
  return C.FFA_TEAM_BASE + c
end

-- The color index a registered entity id occupies, or nil.
local function slot_of(id, id_base)
  for c = 0, C.FFA_TEAM_COUNT - 1 do
    if og.mode_get(id_base + c) == id then
      return c
    end
  end
  return nil
end

-- Deployed-fighter enumeration (D3): live has_guy Livings, BOUND walkers
-- first (user() >= 0 — every seat's controlled hero is guaranteed a slot),
-- then unbound, oblist order within each pass. Pure — the band modes'
-- decide fold counts this list BEFORE any world write, so a refusal
-- leaves the staged world exactly as authored.
local function enumerate(obs)
  local fighters = {}
  for k = 1, #obs do
    local w = obs[k]
    if w:dead() == 0 then
      if w:order() == C.ORDER_LIVING then
        if w:has_guy() then
          if w:user() >= 0 then
            fighters[#fighters + 1] = w
          end
        end
      end
    end
  end
  for k = 1, #obs do
    local w = obs[k]
    if w:dead() == 0 then
      if w:order() == C.ORDER_LIVING then
        if w:has_guy() then
          if w:user() < 0 then
            fighters[#fighters + 1] = w
          end
        end
      end
    end
  end
  return fighters
end

-- The deployed list, capped: extras past the cap are retired
-- (mode_strip.retire: off the score range, then dead) with a toast.
local function deploy(fighters, cap)
  if #fighters <= cap then
    return fighters
  end
  local extras = #fighters - cap
  for j = cap + 1, #fighters do
    strip.retire(fighters[j])
  end
  core.announce("BAND FULL: " .. extras .. " RETIRED", C.SOUND_TELEPORT)
  local kept = {}
  for j = 1, cap do
    kept[j] = fighters[j]
  end
  return kept
end

-- Byte assignment: a Fisher-Yates shuffle of all 16 band bytes with og.rand
-- (sim RNG — mirrors receive the bytes via snapshots and never run init),
-- fighter j takes the j-th shuffled byte. Records entity ids and the
-- assignment bitmap in the caller's vars.
local function assign(fighters, id_base, bitmap_slot)
  local order = {}
  for c = 1, C.FFA_TEAM_COUNT do
    order[c] = c - 1
  end
  for i = C.FFA_TEAM_COUNT, 2, -1 do
    local j = og.rand(i) + 1
    local hold = order[i]
    order[i] = order[j]
    order[j] = hold
  end
  local bitmap = 0
  for k = 1, #fighters do
    local c = order[k]
    local w = fighters[k]
    w:set_team_num(band_byte(c))
    og.mode_set(id_base + c, og.entity_id(w))
    bitmap = bitmap + SLOT_BIT[c + 1]
  end
  og.mode_set(bitmap_slot, bitmap)
end

-- Rotated placement over the four anchor POOLS (docs/ffa-design.md §6:
-- position pools, not identities): pool = cursor mod 4, then mode_match's
-- anchor rotation + ring fallback, which advance the same cursor. A pool
-- with no anchors advances the cursor and yields to the next, so a map
-- authoring fewer than four clusters cannot stall the rotation.
local function place_rotated(w, cursor_slot, allow_teleport)
  for _ = 1, C.SCORE_TEAM_COUNT do
    local pool = og.mod(og.mode_get(cursor_slot), C.SCORE_TEAM_COUNT)
    if og.respawn_anchor_count(pool) > 0 then
      return match.place_at_anchor(w, pool, cursor_slot, allow_teleport)
    end
    og.mode_set(cursor_slot, og.mode_get(cursor_slot) + 1)
  end
  if allow_teleport then
    return w:teleport()
  end
  return false
end

-- Bot fill to the target fighter count: mutant-style SINGLES from the
-- caller's roster (family cycles over the free color indices), one per
-- free band slot, ascending. mode_match.spawn_bots is not reusable here —
-- its matched-plan decode indexes PLAN_BASE by team byte, which band
-- bytes overflow — so the single-bot spawn is spelled out, each single
-- solved on its own (below). Returns the new fighter count.
--
-- The FILL wheel (amendment B2/B3) reaches the band through TEAM 1's
-- knob — the band is ONE fighter population, so the first team's wheel
-- governs it: NONE fields nothing, and every other value fills singles
-- at the SOLVED level — each bot's level is the D22 argmin of its own
-- measured base against reference × multiplier, where the reference is
-- the weakest DEPLOYED FIGHTER's f (every human team in a band is one
-- fighter, so the weakest human team's f-sum is the weakest fighter's
-- f). The reference always exists at fill time: the fold refused below
-- two fighters, and a live has_guy fighter always prices above zero
-- (the +60 offense floor). PLAN_BASE stays untouched
-- (band bytes overflow its decode) and nothing is banked in the shared
-- facts slot: the staged report renders a band by its fighters, not by
-- team squad rows.
--
-- The pairs of teams 2-4 are DEAD in a band mode: every fighter wears a
-- band byte, no score team ever fields a squad, so fill_2..4 and
-- map_units_2..4 are read by nobody here. The fact a menu needs to dim
-- them is the mode name the staged world already carries (FFA / MUTANT
-- via og.set_mode_name — the staged report's mode_name); no extra
-- variable is banked for it (docs/lineup-design.md §3.2).
local function band_knob()
  return og.match_setting("fill_1")
end

-- The weakest deployed fighter's f (B3's reference, spelled for a band):
-- minimum walker_power over the live has_guy Livings, whatever byte they
-- wear (at fill time the fighters already wear band bytes). 0 = none.
local function weakest_fighter_power(obs)
  local reference = 0
  for k = 1, #obs do
    local w = obs[k]
    if w:dead() == 0 then
      if w:order() == C.ORDER_LIVING then
        if w:has_guy() then
          local p = match.walker_power(w)
          if reference == 0 then
            reference = p
          elseif p < reference then
            reference = p
          end
        end
      end
    end
  end
  return reference
end

-- The fighter count the fill below will reach, decided from the census
-- alone (the band modes' decide fold): NONE keeps the deployed count,
-- anything else fills the free slots up to the row's target. The band
-- modes refuse on this number BEFORE touching the world, so no knob shape
-- can reach an error() from a half-applied init.
local function planned_count(deployed_count, target)
  if match.squad_off(band_knob()) then
    return deployed_count
  end
  return og.max(deployed_count, target)
end

local function fill_bots(count, target, id_base, bitmap_slot, cursor_slot,
                         roster)
  local knob = band_knob()
  if match.squad_off(knob) then
    return count
  end
  -- The band's solve inputs (B3): the weakest fighter's f times the
  -- wheel, read once — every single solves against the same target. The
  -- fold refused below two fighters already, and a live has_guy fighter
  -- always prices above zero (the +60 offense floor), so the reference
  -- always exists here.
  local reference = weakest_fighter_power(og.oblist())
  local target_power = og.div(reference * match.fill_percent(knob), 100)
  local bitmap = og.mode_get(bitmap_slot)
  for c = 0, C.FFA_TEAM_COUNT - 1 do
    if count < target then
      if not band_has(bitmap, c) then
        local name = roster[og.mod(c, #roster) + 1]
        local fam = og.family_id("living", name) --[[@as integer]] -- caller rosters name core families; a nil would be a load bug and add_ob erroring loudly is the right failure
        local w = og.add_ob("living", fam)
        if w ~= nil then
          w:set_team_num(band_byte(c))
          w:set_real_team_num(255)
          -- One single, solved on its own measured base (D24's
          -- measure-and-solve, squad size one).
          local bases = { { family = name, stats = match.measured_base(w) } }
          local level = match.solve_matched_levels(target_power, bases)
          w:s_set_level(level)
          w:set_difficulty(level)
          place_rotated(w, cursor_slot, true)
          og.mode_set(id_base + c, og.entity_id(w))
          bitmap = bitmap + SLOT_BIT[c + 1]
          count = count + 1
        end
      end
    end
  end
  og.mode_set(bitmap_slot, bitmap)
  return count
end

-- Mid-join adoption of one walker (D19). A free slot adopts directly; a
-- full band retires the lowest-frag BOT (ascending color-index tiebreak),
-- zeroes that slot's frags and adopts into it; a full band with no bots
-- leaves the joiner on its seat team (it still fights everyone — the
-- documented cap). Answers whether the walker was adopted.
local function adopt_one(w, id, id_base, frag_base, bitmap_slot)
  local bitmap = og.mode_get(bitmap_slot)
  for c = 0, C.FFA_TEAM_COUNT - 1 do
    if not band_has(bitmap, c) then
      og.mode_set(id_base + c, id)
      og.mode_set(frag_base + c, 0)
      og.mode_set(bitmap_slot, bitmap + SLOT_BIT[c + 1])
      w:set_team_num(band_byte(c))
      return true
    end
  end
  local worst = nil
  local worst_frags = 0
  for c = 0, C.FFA_TEAM_COUNT - 1 do
    local holder = og.find_by_id(og.mode_get(id_base + c))
    if holder ~= nil then
      if not holder:has_guy() then
        local frags = og.mode_get(frag_base + c)
        local lower = worst == nil
        if not lower then
          lower = frags < worst_frags
        end
        if lower then
          worst = c
          worst_frags = frags
        end
      end
    end
  end
  if worst == nil then
    return false
  end
  local bot = og.find_by_id(og.mode_get(id_base + worst))
  if bot ~= nil then
    strip.retire(bot)
  end
  og.mode_set(id_base + worst, id)
  og.mode_set(frag_base + worst, 0)
  w:set_team_num(band_byte(worst))
  return true
end

-- The adoption sweep: any live has_guy Living wearing a byte below the
-- band and not yet registered is a joiner. Registered fighters parked on
-- low bytes by a berserk charm are skipped by the id check; the cadence
-- renormalize heals them instead. Idempotent — callers run it from
-- on_entity_spawn AND every tick (the engine attaches a joiner's guy
-- record after add_ob returns, so the spawn dispatch itself cannot see
-- has_guy yet and the per-tick sweep is the arm that lands).
local function adopt_new(obs, id_base, frag_base, bitmap_slot)
  for k = 1, #obs do
    local w = obs[k]
    if w:dead() == 0 then
      if w:order() == C.ORDER_LIVING then
        if w:has_guy() then
          if w:team_num() < C.FFA_TEAM_BASE then
            local id = og.entity_id(w)
            if slot_of(id, id_base) == nil then
              adopt_one(w, id, id_base, frag_base, bitmap_slot)
            end
          end
        end
      end
    end
  end
end

-- Charm renormalize (D12): re-assert the assigned byte on registered
-- fighters whose real_team_num is the 255 sentinel but whose team_num
-- drifted (berserk-charm residue, the revive_player_walker band-byte
-- hole). Charmed fighters (real_team_num ~= 255) are left alone — the
-- charmer's byte is how charm frags credit correctly.
local function renormalize(id_base, bitmap_slot)
  local bitmap = og.mode_get(bitmap_slot)
  for c = 0, C.FFA_TEAM_COUNT - 1 do
    if band_has(bitmap, c) then
      local w = og.find_by_id(og.mode_get(id_base + c))
      if w ~= nil then
        if w:dead() == 0 then
          if w:real_team_num() == 255 then
            if w:team_num() ~= band_byte(c) then
              w:set_team_num(band_byte(c))
            end
          end
        end
      end
    end
  end
end

-- The per-tick backstop behind the synchronous on_entity_death schedule:
-- every dead registered fighter not already queued is scheduled, so a
-- silent set_dead corpse (no walker::death, no hook) still comes back.
-- Runs pre-sweep (on_mode_tick precedes the engine corpse sweep), the
-- same timing mode_match.schedule_dead relies on.
local function schedule_dead(id_base, bitmap_slot, delay)
  local bitmap = og.mode_get(bitmap_slot)
  for c = 0, C.FFA_TEAM_COUNT - 1 do
    if band_has(bitmap, c) then
      local w = og.find_by_id(og.mode_get(id_base + c))
      if w ~= nil then
        if w:dead() ~= 0 then
          if w:order() == C.ORDER_LIVING then
            if not og.respawn_pending(w) then
              og.respawn_schedule(w, delay)
            end
          end
        end
      end
    end
  end
end

-- A live walker's eligibility as a director target: an ASSIGNED band byte
-- other than the hunter's own. The shared mode_match.foe_scores filters
-- team >= 4 as wildlife, so the band modes carry their own predicate
-- rather than editing the shared one (D13 — existing modes provably
-- unaffected). A charmed fighter wearing the hunter's own byte reads as
-- friendly, exactly like the engine's is_friendly rule.
local function eligible_foe(w, own_team, bitmap)
  local team = w:team_num()
  if team == own_team then
    return false
  end
  if team < C.FFA_TEAM_BASE then
    return false
  end
  if team >= C.FFA_TEAM_BASE + C.FFA_TEAM_COUNT then
    return false
  end
  return band_has(bitmap, team - C.FFA_TEAM_BASE)
end

-- A foe worth keeping (the director repair predicate — nil, dead and
-- non-band foes from the engine backstop all read as broken).
local function foe_is_fighter(foe, own_team, bitmap)
  if foe == nil then
    return false
  end
  if foe:dead() ~= 0 then
    return false
  end
  return eligible_foe(foe, own_team, bitmap)
end

-- Nearest eligible fighter by Manhattan distance, ties to the earlier
-- oblist slot (mode_match.nearest_enemy's shape over band eligibility).
local function nearest_fighter(livings, own_team, bitmap, x, y)
  local best = nil
  local best_distance = 0
  for k = 1, #livings do
    local w = livings[k]
    if eligible_foe(w, own_team, bitmap) then
      local distance = ai.dist_to(w, x, y)
      if best == nil then
        best = w
        best_distance = distance
      elseif distance < best_distance then
        best = w
        best_distance = distance
      end
    end
  end
  return best
end

return {
  band_has = band_has,
  band_count = band_count,
  band_byte = band_byte,
  slot_of = slot_of,
  enumerate = enumerate,
  deploy = deploy,
  assign = assign,
  place_rotated = place_rotated,
  planned_count = planned_count,
  fill_bots = fill_bots,
  adopt_new = adopt_new,
  renormalize = renormalize,
  schedule_dead = schedule_dead,
  foe_is_fighter = foe_is_fighter,
  nearest_fighter = nearest_fighter,
}
